/*******************************************************************************
 * utest-vin.c
 *
 * ADAS unit-test. VIN LVDS cameras backend
 *
 * Copyright (c) 2015 Cogent Embedded Inc. ALL RIGHTS RESERVED.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************/

#define MODULE_TAG                      VIN

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"
#include "utest-camera.h"
#include "utest-vsink.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <linux/version.h>
#include <linux/videodev2.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * Local constants definitions
 ******************************************************************************/

/* ...individual camera buffer pool size */
#define VIN_BUFFER_POOL_SIZE            5

/* ...external VIN device names */
extern char * vin_devices[CAMERAS_NUMBER];

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

/* ...buffer description */
typedef struct vin_buffer
{
    /* ...data pointer */
    void               *data;
    
    /* ...memory offset */
    u32                 offset;

    /* ...buffer length */
    u32                 length;

    /* ...associated GStreamer buffer */
    GstBuffer          *buffer;
    
}   vin_buffer_t;

/* ...particular video device */
typedef struct vin_device
{
    /* ...V4L2 device descriptor */
    int                     vfd;

    /* ...output buffers pool length */
    int                     size;
    
    /* ...buffers pool */
    vin_buffer_t           *pool;

    /* ...streaming status */
    int                     active;

    /* ...number of submitted buffers */
    int                     submitted;

    /* ...number of busy buffers (owned by application) */
    int                     busy;

    /* ...conditional variable for busy buffers collection */
    pthread_cond_t          wait;

}   vin_device_t;
    
/* ...decoder data structure */   
typedef struct vin_data
{
    /* ...GStreamer bin element for pipeline handling */
    GstElement                 *bin;

    /* ...single epoll-descriptor */
    int                         efd;

    /* ...number of devices connected */
    int                         num;
    
    /* ...device-specific data */
    vin_device_t               *dev;

    /* ...number of output buffers queued to VIN */
    int                         output_count;

    /* ...number of output buffers submitted to the application */
    int                         output_busy;

    /* ...decoder activity state */
    int                         active;

    /* ...queue access lock */
    pthread_mutex_t             lock;

    /* ...decoding thread - tbd - make it a data source for GMainLoop? */
    pthread_t                   thread;

    /* ...decoding thread conditional variable */
    pthread_cond_t              wait;

    /* ...input buffer waiting conditional */
    pthread_cond_t              wait_input[CAMERAS_NUMBER];
    
    /* ...output buffers flushing conditional */
    pthread_cond_t              flush_wait;
    
    /* ...application-provided callback */
    const camera_callback_t    *cb;

    /* ...application callback data */
    void                       *cdata;

}   vin_data_t;

/*******************************************************************************
 * Custom buffer metadata implementation
 ******************************************************************************/

/* ...metadata structure */
typedef struct vin_meta
{
    GstMeta             meta;

    /* ...camera identifier */
    int                 camera_id;

    /* ...buffer index in the camera pool */
    int                 index;

}   vin_meta_t;

/* ...metadata API type accessor */
extern GType vin_meta_api_get_type(void);
#define VIN_META_API_TYPE               (vin_meta_api_get_type())

/* ...metadata information handle accessor */
extern const GstMetaInfo *vin_meta_get_info(void);
#define VIN_META_INFO                   (vin_meta_get_info())

/* ...get access to the buffer metadata */
#define gst_buffer_get_vin_meta(b)      \
    ((vin_meta_t *)gst_buffer_get_meta((b), VIN_META_API_TYPE))

/* ...attach metadata to the buffer */
#define gst_buffer_add_vin_meta(b)    \
    ((vin_meta_t *)gst_buffer_add_meta((b), VIN_META_INFO, NULL))

/* ...metadata type registration */
GType vin_meta_api_get_type(void)
{
    static volatile GType type;
    static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, GST_META_TAG_MEMORY_STR, NULL };

    if (g_once_init_enter(&type))
    {
        GType _type = gst_meta_api_type_register("VinDecMetaAPI", tags);
        g_once_init_leave(&type, _type);
    }

    return type;
}

/* ...low-level interface */
static gboolean vin_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
    vin_meta_t     *_meta = (vin_meta_t *) meta;

    /* ...reset fields */
    memset(&_meta->meta + 1, 0, sizeof(vin_meta_t) - sizeof(GstMeta));

    return TRUE;
}

/* ...metadata transformation */
static gboolean vin_meta_transform(GstBuffer *transbuf, GstMeta *meta,
        GstBuffer *buffer, GQuark type, gpointer data)
{
    vin_meta_t     *_meta = (vin_meta_t *) meta, *_tmeta;

    /* ...add JPU metadata for a transformed buffer */
    _tmeta = gst_buffer_add_vin_meta(transbuf);

    /* ...just copy data regardless of transform type? */
    memcpy(&_tmeta->meta + 1, &_meta->meta + 1, sizeof(vin_meta_t) - sizeof(GstMeta));

    return TRUE;
}

/* ...metadata release */
static void vin_meta_free(GstMeta *meta, GstBuffer *buffer)
{
    vin_meta_t     *_meta = (vin_meta_t *) meta;

    /* ...anything to destroy? - tbd */
    TRACE(1, _b("free metadata %p"), _meta);
}

/* ...register metadata implementation */
const GstMetaInfo * vin_meta_get_info(void)
{
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info))
    {
        const GstMetaInfo *mi = gst_meta_register(
            VIN_META_API_TYPE,
            "VinDecMeta",
            sizeof(vin_meta_t),
            vin_meta_init,
            vin_meta_free,
            vin_meta_transform);

        g_once_init_leave (&meta_info, mi);
    }

    return meta_info;
}

/*******************************************************************************
 * V4L2 VIN interface helpers
 ******************************************************************************/

/* ...check video device capabilities */
static inline int __vin_check_caps(int vfd)
{
	struct v4l2_capability  cap;
    u32                     caps;
    
    /* ...query device capabilities */
    CHK_API(ioctl(vfd, VIDIOC_QUERYCAP, &cap));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    caps = cap.device_caps;
#else
    caps = cap.capabilities;
#endif

    /* ...check for a required capabilities */
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE))
    {
        TRACE(ERROR, _x("single-planar output expected: %X"), caps);
        return -1;
    }
    else if (!(caps & V4L2_CAP_STREAMING))
    {
        TRACE(ERROR, _x("streaming I/O is expected: %X"), caps);
        return -1;
    }

    /* ...all good */
    return 0;
}

/* ...prepare VIN module for operation */
static inline int vin_set_formats(int vfd, int width, int height, u32 format)
{
	struct v4l2_format  fmt;

    /* ...set output format (single-plane NV12/NV16/UYVY? - tbd) */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.pixelformat = format;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    CHK_API(ioctl(vfd, VIDIOC_S_FMT, &fmt));

    return 0;
}

/* ...start/stop streaming on specific V4L2 device */
static inline int vin_streaming_enable(int vfd, int enable)
{
    int     type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    return CHK_API(ioctl(vfd, (enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF), &type));
}

/* ...allocate buffer pool */
static inline int vin_allocate_buffers(int vfd, vin_buffer_t *pool, int num)
{
    struct v4l2_requestbuffers  reqbuf;
    struct v4l2_buffer          buf;
    int                         j;
    
    /* ...all buffers are allocated by kernel */
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = num;
    CHK_API(ioctl(vfd, VIDIOC_REQBUFS, &reqbuf));
    CHK_ERR(reqbuf.count == (u32)num, -(errno = ENOMEM));

    /* ...prepare query data */
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    for (j = 0; j < num; j++)
    {
        vin_buffer_t   *_buf = &pool[j];
        
        /* ...query buffer */
        buf.index = j;
        CHK_API(ioctl(vfd, VIDIOC_QUERYBUF, &buf));
        _buf->length = buf.length;
        _buf->offset = buf.m.offset;
        _buf->data = mmap(NULL, _buf->length, PROT_READ | PROT_WRITE, MAP_SHARED, vfd, _buf->offset);
        CHK_ERR(_buf->data != MAP_FAILED, -errno);

        TRACE(DEBUG, _b("output-buffer-%d mapped: %p[%08X] (%u bytes)"), j, _buf->data, _buf->offset, _buf->length);
    }

    /* ...start streaming as soon as we allocated buffers */
    CHK_API(vin_streaming_enable(vfd, 1));
    
    TRACE(INFO, _b("buffer-pool allocated (%u buffers)"), num);

    return 0;
}

/* ...allocate output/capture buffer pool */
static inline int vin_destroy_buffers(int vfd, vin_buffer_t *pool, int num)
{
    struct v4l2_requestbuffers  reqbuf;
    int                         j;

    /* ...stop streaming before doing anything */
    CHK_API(vin_streaming_enable(vfd, 0));

    /* ...unmap all buffers */
    for (j = 0; j < num; j++)
    {
        munmap(pool[j].data, pool[j].length);
    }
    
    /* ...release kernel-allocated buffers */
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 0;
    CHK_API(ioctl(vfd, VIDIOC_REQBUFS, &reqbuf));

    TRACE(INFO, _b("buffer-pool destroyed (%d buffers)"), num);

    return 0;
}

/* ...enqueue output buffer */
static inline int vin_output_buffer_enqueue(int vfd, int j)
{
    struct v4l2_buffer  buf;

    /* ...set buffer parameters */
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = j;
    CHK_API(ioctl(vfd, VIDIOC_QBUF, &buf));

    return 0;
}

/* ...dequeue input buffer */
static inline int vin_output_buffer_dequeue(int vfd, u64 *ts, u32 *seq)
{
    struct v4l2_buffer  buf;

    /* ...set buffer parameters */
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    CHK_API(ioctl(vfd, VIDIOC_DQBUF, &buf));
    (ts ? *ts = buf.timestamp.tv_sec * 1000000ULL + buf.timestamp.tv_usec : 0);
    (seq ? *seq = buf.sequence : 0);
    
    return buf.index;
}


/*******************************************************************************
 * V4L2 decoder thread
 ******************************************************************************/

/* ...add device to the poll sources */
static inline int __register_poll(vin_data_t *vin, int i, int add)
{
    vin_device_t       *dev = &vin->dev[i];
    struct epoll_event  event;

    /* ...specify waiting flags */
    event.events = EPOLLIN, event.data.u32 = (u32)i;

    /* ...add/remove source */
    CHK_API(epoll_ctl(vin->efd, (add ? EPOLL_CTL_ADD : EPOLL_CTL_DEL), dev->vfd, &event));

    TRACE(DEBUG, _b("#%d: poll source %s"), i, (add ? "added" : "removed"));

    return 0;
}

/* ...submit buffer to the device (called with a decoder lock held) */
static inline int __submit_buffer(vin_data_t *vin, int i, int j)
{
    vin_device_t   *dev = &vin->dev[i];

    /* ...submit a buffer */
    CHK_API(vin_output_buffer_enqueue(dev->vfd, j));

    /* ...prepare output buffer if needed */
    (vin->cb->prepare ? vin->cb->prepare(vin->cdata, i, dev->pool[i].buffer) : 0);

    /* ...register poll if needed */
    CHK_API(dev->submitted++ == 0 ? __register_poll(vin, i, 1) : 0);

    TRACE(DEBUG, _b("enqueue buffer #<%d,%d>"), i, j);    

    return 0;
}

/* ...buffer processing function */
static inline int __process_buffer(vin_data_t *vin, int i)
{
    vin_device_t   *dev = &vin->dev[i];
    GstBuffer      *buffer;
    int             j;
    u64             ts;
    u32             seq;
    
    /* ...if streaming is disabled already, bail out (hmm - tbd) */
    BUG(!dev->active, _x("vin-%d: invalid state"), i);

    /* ...get buffer from a device */
    CHK_API(j = vin_output_buffer_dequeue(dev->vfd, &ts, &seq));

    /* ...remove poll-source if last buffer is dequeued */
    (--dev->submitted == 0 ? __register_poll(vin, i, 0) : 0);

    /* ...get buffer descriptor */
    buffer = dev->pool[j].buffer;
    
    /* ...set decoding/presentation timestamp (in nanoseconds) */
    GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer) = ts * 1000;

    TRACE(DEBUG, _b("dequeued buffer #<%d,%d>, ts=%zu, seq=%u, submitted=%d"), i, j, ts, seq, dev->submitted);

    /* ...advance number of busy buffers */
    dev->busy++;

    /* ...release lock before passing buffer to the application */
    pthread_mutex_unlock(&vin->lock);

    /* ...pass output buffer to application */
    CHK_API(vin->cb->process(vin->cdata, i, buffer));

    /* ...drop the reference (buffer is now owned by application) */
    gst_buffer_unref(buffer);

    /* ...reacquire data access lock */
    pthread_mutex_lock(&vin->lock);
    
    return 0;
}

/* ...decoding thread */
static void * vin_thread(void *arg)
{
    vin_data_t         *vin = arg;
    struct epoll_event  event[vin->num];

    /* ...lock internal data access */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_mutex_lock(&vin->lock);

    /* ...start processing loop */
    while (1)
    {
        int     r, k;
        
        /* ...release the lock before going to waiting state */
        pthread_mutex_unlock(&vin->lock);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

        TRACE(DEBUG, _b("start waiting..."));
        
        /* ...wait for event (infinite timeout) */
        r = epoll_wait(vin->efd, event, vin->num, -1);

        TRACE(DEBUG, _b("done waiting: %d"), r);

        /* ...reacquire the lock */
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        pthread_mutex_lock(&vin->lock);

        /* ...check operation result */
        if (r < 0)
        {
            /* ...ignore soft interruptions (e.g. from debugger) */
            if (errno == EINTR)     continue;
            TRACE(ERROR, _x("poll failed: %m"));
            goto out;
        }

        /* ...process all signalled descriptors */
        for (k = 0; k < r; k++)
        {
            int     i = (int)event[k].data.u32;

            /* ...process output buffers */
            if (event[k].events & EPOLLIN)
            {
                if (__process_buffer(vin, i) < 0)
                {
                    TRACE(ERROR, _x("processing failed: %m"));
                    goto out;
                }
            }
            else
            {
                BUG(1, _x("invalid poll events: i=%d, event=%X"), i, event[k].events);
            }
        }
    }

out:
    /* ...release access lock */
    pthread_mutex_unlock(&vin->lock);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    TRACE(INIT, _b("thread exits: %m"));

    return (void *)(intptr_t)-errno;
}


/* ...start module operation */
int vin_start(vin_data_t *vin)
{
    pthread_attr_t  attr;
    int             r;
    
    /* ...mark module is active */
    vin->active = 1;

    /* ...initialize thread attributes (joinable, 128KB stack) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr, 128 << 10);

    /* ...create V4L2 thread to asynchronously process input buffers */
    r = pthread_create(&vin->thread, &attr, vin_thread, vin);
    pthread_attr_destroy(&attr);

    return CHK_API(r);
}

/*******************************************************************************
 * Buffer pool handling
 ******************************************************************************/

/* ...output buffer dispose function (called in response to "gst_buffer_unref") */
static gboolean __output_buffer_dispose(GstMiniObject *obj)
{
    GstBuffer      *buffer = GST_BUFFER(obj);
    vin_data_t     *vin = (vin_data_t *)buffer->pool;
    vin_meta_t     *meta = gst_buffer_get_vin_meta(buffer);
    int             i = meta->camera_id, j = meta->index;
    vin_device_t   *dev = &vin->dev[i];
    gboolean        destroy;

    /* ...verify buffer validity */
    BUG((u32)i >= (u32)vin->num || (u32)j >= (u32)dev->size, _x("invalid buffer: <%d,%d>"), i, j);

    /* ...lock internal data access */
    pthread_mutex_lock(&vin->lock);

    /* ...decrement amount of outstanding buffers */
    dev->busy--;

    TRACE(DEBUG, _b("output buffer #<%d:%d> (%p) returned to pool (busy: %d)"), i, j, buffer, dev->busy);

    /* ...check if buffer needs to be requeued into the pool */
    if (vin->active)
    {
        /* ...increment buffer reference */
        gst_buffer_ref(buffer);

        /* ...if streaming is enabled, submit buffer */
        (dev->active ? __submit_buffer(vin, i, j) : 0);
        
        /* ...indicate the miniobject should not be freed */
        destroy = FALSE;
    }
    else
    {
        TRACE(DEBUG, _b("buffer %p is freed"), buffer);
 
        /* ...signal flushing completion operation */
        (dev->busy == 0 ? pthread_cond_signal(&vin->wait) : 0);

        /* ...reset buffer pointer to indicate it's destroyed */
        dev->pool[j].buffer = NULL;

        /* ...force destruction of the buffer miniobject */
        destroy = TRUE;
    }

    /* ...release data access lock */
    pthread_mutex_unlock(&vin->lock);
    
    return destroy;
}


/* ...runtime initialization */
int vin_device_init(vin_data_t *vin, int i, int w, int h, u32 fmt, int size)
{
    vin_device_t   *dev = &vin->dev[i];
    int         j;

    /* ...make sure we have proper index */
    CHK_ERR((u32)i < (u32)vin->num, -(errno = EINVAL));

    /* ...create buffer pool */
    CHK_ERR(dev->pool = calloc(dev->size = size, sizeof(*dev->pool)), -(errno = ENOMEM));

    /* ...set VIN format */
    CHK_API(vin_set_formats(dev->vfd, w, h, fmt));

    /* ...allocate output buffers */
    CHK_API(vin_allocate_buffers(dev->vfd, dev->pool, size));
    
    /* ...mark device is active */
    dev->active = 1;

    /* ...create gstreamer buffers */
    for (j = 0; j < size; j++)
    {
        vin_buffer_t   *buf = &dev->pool[j];
        GstBuffer      *buffer;
        vin_meta_t     *meta;
        vsink_meta_t   *vmeta;
            
        /* ...allocate empty GStreamer buffer */
        CHK_ERR(buf->buffer = buffer = gst_buffer_new(), -ENOMEM);

        /* ...add VIN metadata for decoding purposes */
        CHK_ERR(meta = gst_buffer_add_vin_meta(buffer), -ENOMEM);
        meta->camera_id = i;
        meta->index = j;
        GST_META_FLAG_SET(meta, GST_META_FLAG_POOLED);

        /* ...add vsink metadata */
        CHK_ERR(vmeta = gst_buffer_add_vsink_meta(buffer), -ENOMEM);
        vmeta->width = w;
        vmeta->height = h;
        vmeta->format = __pixfmt_v4l2_to_gst(fmt);
        vmeta->dmafd[0] = -1;
        vmeta->dmafd[1] = -1;
        vmeta->plane[0] = buf->data;
        vmeta->plane[1] = NULL;
        GST_META_FLAG_SET(vmeta, GST_META_FLAG_POOLED);

        /* ...modify buffer release callback */
        GST_MINI_OBJECT(buffer)->dispose = __output_buffer_dispose;

        /* ...use "pool" pointer as a custom data */
        buffer->pool = (void *)vin;

        /* ...notify application on output buffer allocation */
        CHK_API(vin->cb->allocate(vin->cdata, i, buffer));

        /* ...submit a buffer into device? - streaming is OFF yet? */
        __submit_buffer(vin, i, j);
    }

    TRACE(INIT, _b("vin-%d runtime initialized: %d*%d %c%c%c%c (%d)"), i, w, h, __v4l2_fmt(fmt), size);

    return 0;
}

/* ...close VIN device (called with lock held) */
static void __vin_device_close(vin_data_t *vin, int i)
{
    vin_device_t   *dev = &vin->dev[i];
    int             j;

    /* ...if anything has been submitted, cancel it */
    dev->active = 0;

    /* ...wait until all buffers are collected */
    while (dev->busy)
    {
        pthread_cond_wait(&dev->wait, &vin->lock);
    }

    /* ...destroy the buffers that haven't been freed yet */
    for (j = 0; j < dev->size; j++)
    {
        GstBuffer  *buffer;

        /* ...unref buffer if it hasn't yet been freed */
        ((buffer = dev->pool[j].buffer) ? gst_buffer_unref(buffer) : 0);
    }

    /* ...deallocate buffers */
    vin_destroy_buffers(dev->vfd, dev->pool, dev->size);

    /* ...close V4L2 device */
    close(dev->vfd);

    TRACE(INIT, _b("vin-%d destroyed"), i);
}

/*******************************************************************************
 * Component destructor
 ******************************************************************************/

/* ...destructor function */
void vin_destroy(gpointer data, GObject *obj)
{
    vin_data_t     *vin = data;
    int             i;

    /* ...acquire lock */
    pthread_mutex_lock(&vin->lock);
    
    /* ...clear activity flag */
    vin->active = 0;

    /* ...cancel processing thread */
    pthread_cancel(vin->thread);

    /* ...wait for all devices flushing */
    for (i = 0; i < vin->num; i++)
    {
        __vin_device_close(vin, i);
    }    

    /* ...destroy mutex */
    pthread_mutex_destroy(&vin->lock);
    
    /* ...destroy decoder structure */
    free(vin);

    TRACE(INIT, _b("vin-camera-bin destroyed"));
}

/* ...module initialization function */
vin_data_t * vin_init(char **devname, int num, camera_callback_t *cb, void *cdata)
{
    vin_data_t             *vin;
    pthread_mutexattr_t     attr;
    int                     i;

    /* ...create decoder structure */
    CHK_ERR(vin = malloc(sizeof(*vin)), (errno = ENOMEM, NULL));

    /* ...save application provided callback */
    vin->cb = cb, vin->cdata = cdata;

    /* ...allocate engine-specific data */
    if ((vin->dev = calloc(vin->num = num, sizeof(vin_device_t))) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate %zu bytes"), num * sizeof(vin_device_t));
        goto error;
    }

    /* ...create epoll descriptor */
    if ((vin->efd = epoll_create(num)) < 0)
    {
        TRACE(ERROR, _x("failed to create epoll: %m"));
        goto error;
    }    

    /* ...open V4L2 devices */
    for (i = 0; i < num; i++)
    {
        vin_device_t   *dev = &vin->dev[i];

        /* ...open VIN device */
        if ((dev->vfd = open(devname[i], O_RDWR | O_NONBLOCK)) < 0)
        {
            TRACE(ERROR, _x("failed to open device '%s'"), devname[i]);
            goto error_dev;
        }

        /* ...check capabilities */
        if (__vin_check_caps(dev->vfd) != 0)
        {
            TRACE(ERROR, _x("invalid video device '%s'"), devname[i]);
            errno = EBADFD;
            goto error_dev;
        }

	TRACE(INFO, _b("vin-%d device name '%s'"), i, devname[i]);
    }

    /* ...initialize internal queue access lock */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&vin->lock, &attr);
    pthread_mutexattr_destroy(&attr);

    TRACE(INIT, _b("VIN module initialized"));

    return vin;

error_dev:
    /* ...close all devices */
    do
    {
        (vin->dev[i].vfd >= 0 ? close(vin->dev[i].vfd) : 0);
    }
    while (i--);

    /* ...destroy devices memory */
    free(vin->dev);

error:
    /* ...close epoll file descriptor */
    close(vin->efd);

    /* ...release module memory */
    free(vin);

    return NULL;
}
