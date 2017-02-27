/*******************************************************************************
 * utest-app.c
 *
 * IMR unit test application
 *
 * Copyright (c) 2016 Cogent Embedded Inc. ALL RIGHTS RESERVED.
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

#define MODULE_TAG                      APP

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-app.h"
#include "utest-vsink.h"
#include "utest-imr.h"
#include "utest-mesh.h"
#include "utest-compositor.h"
#include "utest-png.h"
#include "utest-math.h"
#include <linux/videodev2.h>
#include <pango/pangocairo.h>
#include <math.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/* ...external declarations - tbd */
typedef struct car_renderer car_renderer_t;
extern car_renderer_t * car_renderer_init(char *file, int w, int h);
extern int car_render(car_renderer_t *car, texture_data_t *texture, const __mat4x4 P, const __mat4x4 V, const __mat4x4 M);

/*******************************************************************************
 * Local constants definitions
 ******************************************************************************/

/* ...IMR engines identifiers */
#define IMR_CAMERA_LEFT                 0
#define IMR_CAMERA_RIGHT                1
#define IMR_CAMERA_FRONT                2
#define IMR_CAMERA_REAR                 3
#define IMR_ALPHA                       4
#define IMR_NUMBER                      5

/* ...VSP buffers indices */
#define VSP_CAMERA_LEFT                 0
#define VSP_CAMERA_RIGHT                1
#define VSP_CAMERA_FRONT                2
#define VSP_CAMERA_REAR                 3
#define VSP_ALPHA                       4
#define VSP_CAR                         5
#define VSP_OUTPUT                      6
#define VSP_NUMBER                      7

/* ...size of compositor buffers pool */
#define VSP_POOL_SIZE                   2

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

struct app_data
{
    /* ...main window handle */
    window_data_t      *window;

    /* ...auxiliary window handle */
    window_data_t      *window_aux;

    /* ...window viewports */
    texture_view_t      view_aux[CAMERAS_NUMBER + 2 + 2 + 1];
    
    /* ...main execution loop */
    GMainLoop          *loop;

    /* ...GStreamer pipeline */
    GstElement         *pipe;

    /* ...camera-set container */
    GstElement         *camera;

    /* ...input stream dimensions */
    int                 width, height;

    /* ...miscellaneous control flags */
    u32                 flags, imr_flags;

    /* ...pending input buffers (waiting for IMR processing start) */
    GQueue              input[CAMERAS_NUMBER];

    /* ...rendering queue for main window */
    GQueue              render;
    
    /* ...rendering queue for raw frames plus final composited frame */
    GQueue              render_aux[CAMERAS_NUMBER + VSP_NUMBER];

    /* ...pending VSP buffers queue (waiting for compositing) */
    GQueue              vsp_pending[VSP_NUMBER + CAMERAS_NUMBER];

    /* ...currently processed VSP buffers */
    GstBuffer          *vsp_buffers[VSP_NUMBER + CAMERAS_NUMBER];

    /* ...VSP queues access lock */
    pthread_mutex_t     vsp_lock;
    
    /* ...data access lock */
    pthread_mutex_t     lock;

    /* ...IMR engine handle */
    imr_data_t         *imr;

    /* ...pending IMR engines configurations */
    imr_cfg_t          *imr_cfg[IMR_NUMBER];

    /* ...VSP compositor data */
    vsp_compositor_t   *vsp;

    /* ...input frames sequence number */
    u32                 sequence, sequence_out, sequence_imr[IMR_NUMBER];

    /* ...last update sequence number */
    u32                 last_update;

    /* ...IMR output buffers (inputs to the compositor) */
    vsp_mem_t          *camera_plane[2][VSP_POOL_SIZE];

    /* ...alpha plane, car model buffers - tbd */
    vsp_mem_t          *alpha_input[1], *alpha_plane[2], *car_plane[2];

    /* ...VSP output buffers (compositor output) */
    vsp_mem_t          *output[VSP_POOL_SIZE];

    /* ...alpha-plane input buffer */
    GstBuffer          *alpha_buffer;

    /* ...active alpha-plane output buffer */
    GstBuffer          *alpha_active;
    
    /* ...car image buffers */
    GstBuffer          *car_buffer[2];

    /* ...active car-model buffer */
    GstBuffer          *car_active;

    /* ...mesh configuration update thread handle */
    pthread_t           mesh_thread;

    /* ...car model update thread handle */
    pthread_t           car_thread;

    /* ...conditional variable for update sequence */
    pthread_cond_t      update;
    
    /* ...output buffers pool */
    GstBuffer          *buffer[VSP_POOL_SIZE];

    /* ...input (camera) buffers readiness flag */
    u32                 input_ready;
    
    /* ...VSP buffers readiness flag */
    u32                 vsp_ready;

    /* ...frame number */
    u32                 frame_num;

    /* ...cameras mesh data */
    mesh_data_t        *mesh[IMR_NUMBER];

    /* ...car image renderer */
    car_renderer_t     *car;

    /* ...projection/view matrix */
    __mat4x4            pv_matrix;
    
    /* ...model matrix */
    __mat4x4            model_matrix;

    /* ...latched project/view/model matrix - tbd - do I need that? */
    __mat4x4            pvm_matrix;

    /* ...rotation matrix accumulators */
    __vec3              rot_acc;

    /* ...scaling factor */
    __scalar            scl_acc;

    /* ...number of milliseconds since last update */
    u32                 spnav_delta;

    /* ...touchscreen state processing */
    u32                 ts_flags;

    /* ...touchscreen latched positions */
    int                 ts_pos[2][2];

    /* ...distance between touch points */
    int                 ts_dist;
};

/*******************************************************************************
 * Operation control flags
 ******************************************************************************/

/* ...output debugging info */
#define APP_FLAG_DEBUG                  (1 << 0)

/* ...renderer flushing condition */
#define APP_FLAG_EOS                    (1 << 1)

/* ...switching to next track */
#define APP_FLAG_NEXT                   (1 << 2)

/* ...switching to previous track */
#define APP_FLAG_PREV                   (1 << 3)

/* ...application termination request */
#define APP_FLAG_EXIT                   (1 << 4)

/* ...output alpha-plane mesh */
#define APP_FLAG_DEBUG_ALPHA_MESH       (1 << 5)

/* ...output camera-planes meshes */
#define APP_FLAG_DEBUG_CAMERA_MESH      (1 << 6)

/* ...active set of alpha/car images */
#define APP_FLAG_SET_INDEX              (1 << 10)

/* ...on-going update sequence status */
#define APP_FLAG_UPDATE                 (1 << 12)

/* ...mesh configuration update condition */
#define APP_FLAG_MAP_UPDATE             (1 << 13)

/* ...car model update condition  */
#define APP_FLAG_CAR_UPDATE             (1 << 14)

/* ...buffer clearing mask */
#define APP_FLAG_CLEAR_BUFFER           (1 << 16)

/*******************************************************************************
 * Mesh processing
 ******************************************************************************/

/* ...default projection matrix */
static const __mat4x4 __p_matrix = {
    __MATH_FLOAT(1.0083325),    __MATH_FLOAT(0),        __MATH_FLOAT(0),            __MATH_FLOAT(0),
    __MATH_FLOAT(0),            __MATH_FLOAT(1.792591), __MATH_FLOAT(0),            __MATH_FLOAT(0),
    __MATH_FLOAT(0),            __MATH_FLOAT(0),        __MATH_FLOAT(-1.020202),    __MATH_FLOAT(-1),
    __MATH_FLOAT(0),            __MATH_FLOAT(0),        __MATH_FLOAT(-0.20202021),  __MATH_FLOAT(0),
};

/* ...default view matrix */
static const __mat4x4 __v_matrix = {
    __MATH_FLOAT(1),    __MATH_FLOAT(0),    __MATH_FLOAT(0),    __MATH_FLOAT(0),
    __MATH_FLOAT(0),    __MATH_FLOAT(1),    __MATH_FLOAT(0),    __MATH_FLOAT(0),
    __MATH_FLOAT(0),    __MATH_FLOAT(0),    __MATH_FLOAT(1),    __MATH_FLOAT(0),
    __MATH_FLOAT(0),    __MATH_FLOAT(0),    __MATH_FLOAT(-1),   __MATH_FLOAT(1),
};


/*******************************************************************************
 * Compositor interface
 ******************************************************************************/

/* ...trigger surround-view scene composition if possible (called with VSP lock held) */
static int __vsp_compose(app_data_t *app)
{
    vsp_mem_t      *mem[VSP_NUMBER];
    GstBuffer     **buf = app->vsp_buffers;
    int             i;

    /* ...all buffers must be available */
    BUG(app->vsp_ready != 0, _x("invalid state: %x"), app->vsp_ready);
    
    /* ...collect memory descriptors */
    for (i = 0; i < VSP_NUMBER; i++)
    {
        GQueue         *queue = &app->vsp_pending[i];
        GstBuffer      *buffer = g_queue_pop_head(queue);
        imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);

        /* ...save buffer in the VSP processing array (keep ownership) */
        buf[i] = buffer, mem[i] = meta->priv;

        /* ...check if queue gets empty */
        (g_queue_is_empty(queue) ? app->vsp_ready |= 1 << i : 0);
    }

    /* ...make sure memory buffers are same for cameras */
    BUG(mem[0] != mem[1], _x("left/right buffers mismatch: %p != %p"), mem[0], mem[1]);
    BUG(mem[2] != mem[3], _x("front/rear buffers mismatch: %p != %p"), mem[2], mem[3]);

    /* ...place associated input buffers into active buffers queue */
    if (app->window_aux)
    {
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            /* ...associated input buffers are saved in VSP queues */
            buf[VSP_NUMBER + i] = g_queue_pop_head(&app->vsp_pending[VSP_NUMBER + i]);
        }
    }

    /* ...mark buffer processing is ongoing */
    app->vsp_ready |= 1 << VSP_NUMBER;

    /* ...submit a job to compositor */
    CHK_ERR(vsp_job_submit(app->vsp, mem, mem[VSP_OUTPUT]) == 0, -(errno = EBADFD));

    TRACE(DEBUG, _b("job submitted..."));

    return 0;
}

/* ...submit particular buffer to a compositor (called with a VSP lock held) */
static int __vsp_submit_buffer(app_data_t *app, int i, GstBuffer *buffer)
{
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);

    /* ...place buffer into pending IMR queue */
    g_queue_push_tail(&app->vsp_pending[i], gst_buffer_ref(buffer));

    //BUG(g_queue_get_length(&app->vsp_pending[i]) > 1, _x("vsp-%d: bad length: %d"), i, g_queue_get_length(&app->vsp_pending[i]));

    /* ...submit processing task if all buffers are available */
    if ((app->vsp_ready &= ~(1 << i)) == 0)
    {
        CHK_API(__vsp_compose(app));
    }
    else
    {
        TRACE(DEBUG, _b("buffer #<%d,%d> submitted: render-mask: %X"), i, meta->index, app->vsp_ready);
    }

    return 0;
}

/* ...compositor processing callback */
static void vsp_callback(void *data, int result)
{
    app_data_t     *app = data;
    GstBuffer     **buf = app->vsp_buffers;
    u32             sequence = app->sequence_out;
    int             i;
    
    /* ...lock application state */
    pthread_mutex_lock(&app->lock);

    /* ...update output buffers sequence counter */
    app->sequence_out = sequence + 1;
    
    /* ...test if we need to update current alpha- and car-model buffers */
    if ((app->flags & APP_FLAG_UPDATE) && (sequence == app->last_update))
    {
        /* ...latch new buffers for subsequent jobs */ 
        app->alpha_active = gst_buffer_ref(buf[VSP_ALPHA]);
        app->car_active = gst_buffer_ref(buf[VSP_CAR]);

        /* ...we might have submitted some number of jobs already */
        pthread_mutex_lock(&app->vsp_lock);

        /* ...submit remaining number of jobs (compositing is now disabled) */
        while (++sequence != app->sequence)
        {
            __vsp_submit_buffer(app, VSP_ALPHA, app->alpha_active);
            __vsp_submit_buffer(app, VSP_CAR, app->car_active);
        }

        /* ...unlock VSP queues */
        pthread_mutex_unlock(&app->vsp_lock);

        /* ...clear update sequence flag */
        app->flags &= ~APP_FLAG_UPDATE;
    }

    /* ...place resulting output buffer into dedicated rendering queue */
    if (app->window)
    {
        /* ...place buffer into rendering queue (take extra reference) */
        g_queue_push_tail(&app->render, gst_buffer_ref(buf[VSP_OUTPUT]));
    }

    /* ...schedule auxiliary window rendering */
    if (app->window_aux)
    {
        /* ...move input buffers to the queue */
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            g_queue_push_tail(&app->render_aux[i], buf[VSP_NUMBER + i]);
        }
        
        /* ...move buffers into auxiliary rendering queue (retain ownership) */
        for (i = 0; i < VSP_NUMBER; i++)
        {
            g_queue_push_tail(&app->render_aux[CAMERAS_NUMBER + i], buf[i]);
        }
    }
    else
    {
        /* ...no auxiliary window; release all processed buffers */
        for (i = 0; i < VSP_NUMBER; i++)
        {
            gst_buffer_unref(buf[i]);
        }
    }

    /* ...unlock application data */
    pthread_mutex_unlock(&app->lock);

    /* ...lock VSP data access */
    pthread_mutex_lock(&app->vsp_lock);

    /* ...clear compositing flag and submit another job if possible */
    if ((app->vsp_ready &= ~(1 << VSP_NUMBER)) == 0)
    {
        __vsp_compose(app);
    }

    /* ...unlock VSP data access */
    pthread_mutex_unlock(&app->vsp_lock);

    /* ...schedule main/auxiliary windows rendering */
    (app->window ? window_schedule_redraw(app->window) : 0);
    (app->window_aux ? window_schedule_redraw(app->window_aux) : 0);
}

/*******************************************************************************
 * Input job processing interface
 ******************************************************************************/

/* ...setup IMR engines for a processing (PVM matrix access is locked) */
static int app_map_setup(app_data_t *app)
{
    int     i;

    /* ...calculate projection transformations of the points (single-threaded?) */
    for (i = 0; i < IMR_NUMBER; i++)
    {
        float  *uv, *xy;
        int     n;

        /* ...create transformation */
        n = mesh_translate(app->mesh[i], &uv, &xy, app->pvm_matrix, 1.0);

        TRACE(INFO, _b("engine-%d mesh setup: n = %d"), i, n);

        /* ...create new configuration */
        CHK_ERR(app->imr_cfg[i] = imr_cfg_create(app->imr, i, uv, xy, n), -errno);
    }

    return 0;
}

/* ...submit new input job to IMR engines (function called with a lock held) */
static int __app_job_submit(app_data_t *app)
{
    u32         sequence = app->sequence;
    GstBuffer  *buffer[CAMERAS_NUMBER];
    int         i;

    /* ...all buffers must be available */
    BUG(app->input_ready != 0, _x("invalid state: %x"), app->input_ready);

    /* ...submit the buffers to the engines */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        /* ...get buffer from the head of the pending input queue */
        buffer[i] = g_queue_pop_head(&app->input[i]);

        /* ...submit to an engine for processing (increases refcount) */
        CHK_API(imr_engine_push_buffer(app->imr, i, buffer[i], sequence));
        
        /* ...check if queue gets empty */
        (g_queue_is_empty(&app->input[i]) ? app->input_ready |= 1 << i : 0);
    }

    /* ...increment sequence number */
    app->sequence = sequence + 1;

    /* ...select alpha-plane and car-model buffers for a given job */
    pthread_mutex_lock(&app->vsp_lock);

#if 0
    /* ...put alpha-plane buffer if available */
    if (app->alpha_active)
    {
        /* ...place buffer into pending queue */
        __vsp_submit_buffer(app, VSP_ALPHA, app->alpha_active);
    }

    /* ...put car-model buffer if available */
    if (app->car_active)
    {
        __vsp_submit_buffer(app, VSP_CAR, app->car_active);
    }
#else
    if (app->alpha_active && app->car_active)
    {
        __vsp_submit_buffer(app, VSP_ALPHA, app->alpha_active);
        __vsp_submit_buffer(app, VSP_CAR, app->car_active);
    }
#endif
    
    /* ...put buffers into auxiliary window if needed */
    if (app->window_aux)
    {
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            g_queue_push_tail(&app->vsp_pending[VSP_NUMBER + i], buffer[i]);
        }
    }
    else
    {
        /* ...input buffer is not needed anymore; release ownership */
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            gst_buffer_unref(buffer[i]);
        }
    }

    /* ...unlock VSP queues */
    pthread_mutex_unlock(&app->vsp_lock);
    
    TRACE(DEBUG, _b("job submitted: sequence=%u"), sequence);

    return 0;
}

/* ...trigger update of alpha-plane (called with a lock held) */
static inline int __app_alpha_update(app_data_t *app)
{
    /* ...push alpha-plane input buffer (use sequence id of next job to submit) */
    CHK_API(imr_engine_push_buffer(app->imr, IMR_ALPHA, app->alpha_buffer, app->sequence));

    return 0;
}

/*******************************************************************************
 * Mesh update (hmm; not well-positioned)
 ******************************************************************************/

/* ...rotate/scale model matrix (called with a lock held) */
static void __app_matrix_update(app_data_t *app, int rx, int ry, int rz, int ts)
{
    static const float  speed = 1.0 / 360;

    /* ...update accumulators */
    app->rot_acc[0] -= speed * rx;
    app->rot_acc[1] += 0 * speed * ry;
    app->rot_acc[2] += speed * rz;
    app->scl_acc -= 4 * speed * speed * ts;

    /* ...clamp components */
    (app->rot_acc[0] > 0 ? app->rot_acc[0] = 0 : (app->rot_acc[0] < -80 ? app->rot_acc[0] = -80 : 0));
    app->rot_acc[1] = fmodf(app->rot_acc[1], 360);
    app->rot_acc[2] = fmodf(app->rot_acc[2], 360);
    app->scl_acc = (app->scl_acc < 0.1 ? 0.1 : (app->scl_acc > 0.5 ? 0.5 : app->scl_acc));
}

/* ...reset application matrices (called wid a lock held) */
static void __app_matrix_reset(app_data_t *app)
{
    /* ...reset accumulators */
    memset(app->rot_acc, 0, sizeof(app->rot_acc));

    /* ...set initial scale */
    app->scl_acc = 0.25;
}

/* ...mesh update thread */
static void * mesh_update_thread(void *arg)
{
    app_data_t     *app = arg;

    /* ...protect intenal app data */
    pthread_mutex_lock(&app->lock);

    /* ...wait for an update event */
    while (1)
    {
        /* ...wait for a mesh update flag */
        while ((app->flags & (APP_FLAG_MAP_UPDATE | APP_FLAG_EOS)) == 0)
        {
            pthread_cond_wait(&app->update, &app->lock);
        }

        /* ...process termination request */
        if (app->flags & APP_FLAG_EOS)
        {
            TRACE(INIT, _b("termination request received"));
            goto out;
        }
        
        /* ...disable input path */
        //app->input_ready |= 1 << CAMERAS_NUMBER;

        /* ...release application lock */
        //pthread_mutex_unlock(&app->lock);

        /* ...update IMR mappings */
        if (app_map_setup(app) != 0)
        {
            TRACE(ERROR, _x("maps update failed: %m"));
        }

        /* ...reacquire application lock */
        //pthread_mutex_lock(&app->lock);

        /* ...clear mesh update command condition */
        app->flags &= ~APP_FLAG_MAP_UPDATE;

        BUG(app->alpha_active, _x("alpha should not be ready yet"));    

        /* ...start alpha-plane processing */
        __app_alpha_update(app);

        /* ...re-enable input */
        if ((app->input_ready &= ~(1 << CAMERAS_NUMBER)) == 0)
        {
            /* ...force job submission */
            if (__app_job_submit(app) != 0)
            {
                TRACE(ERROR, _b("job submission failed: %m"));
                goto out;
            }
        }
    }

out:
    /* ...release application lock */
    pthread_mutex_unlock(&app->lock);

    TRACE(INIT, _b("map update thread terminated"));

    return NULL;
}

/* ...process mesh rotation (called with a lock held) */
static int __app_map_update(app_data_t *app)
{
    /* ...ignore update request if one is started */
    if (app->flags & APP_FLAG_UPDATE)       return 0;

    /* ...calculate M matrix */
    __mat4x4_rotation(app->model_matrix, app->rot_acc, app->scl_acc);

    /* ...multiply projection/view matrix by model matrix (create PVM matrix copy) */
    __mat4x4_mul(app->pv_matrix, app->model_matrix, app->pvm_matrix);

    /* ...initiate point-of-view update sequence */
    app->flags ^= APP_FLAG_UPDATE | APP_FLAG_MAP_UPDATE | APP_FLAG_CAR_UPDATE;

    /* ...latch sequence number of next job that will have updated configuration */
    app->last_update = app->sequence;

    /* ...update alpha-plane on next invocation */
    app->sequence_imr[IMR_ALPHA] = app->last_update;
    
    /* ...disable input path */
    app->input_ready |= 1 << CAMERAS_NUMBER;

    /* ...drop alpha- and car-model buffers as needed */
    (app->alpha_active ? gst_buffer_unref(app->alpha_active), app->alpha_active = NULL : 0);
    (app->car_active ? gst_buffer_unref(app->car_active), app->car_active = NULL : 0);
    
    TRACE(DEBUG, _b("trigger update sequence"));

    /* ...kick update threads */
    pthread_cond_broadcast(&app->update);

    return 0;
}

/* ...initialize mesh update thread */
static inline int app_map_init(app_data_t *app)
{
    pthread_attr_t  attr;
    int             r;

    /* ...reset initial matrices */
    __app_matrix_reset(app);
   
    /* ...calculate PV matrix (which is constant for now) */
    __mat4x4_mul(__p_matrix, __v_matrix, app->pv_matrix);

    /* ...initialize thread attributes (joinable, 128KB stack) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr, 128 << 10);

    /* ...create car model update thread */
    r = pthread_create(&app->mesh_thread, &attr, mesh_update_thread, app);
    pthread_attr_destroy(&attr);
    CHK_API(r);

    return 0;
}

/*******************************************************************************
 * Distortion correction engine interface (all functions are interlocked)
 ******************************************************************************/

/* ...deallocate texture data */
static void __destroy_imr_buffer(gpointer data, GstMiniObject *obj)
{
    GstBuffer      *buffer = (GstBuffer *)obj;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);

    /* ...destroy texture data */
    texture_destroy(meta->priv2);

    TRACE(DEBUG, _b("destroy IMR buffer <%d:%d>"), meta->id, meta->index);
}

/* ...buffer allocation callback */
static int imr_buffer_allocate(void *cdata, GstBuffer *buffer)
{
    app_data_t     *app = cdata;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    int             w = meta->width, h = meta->height, format = meta->format;
    int             i = meta->id;
    int             j = meta->index;
    void           *planes[3] = { NULL, };

    if (i < IMR_ALPHA)
    {
        /* ...camera plane */
        BUG((u32)j >= VSP_POOL_SIZE, _x("invalid buffer: <%d,%d>"), i, j);

        /* ...save pointer to the memory buffer */
        meta->priv = app->camera_plane[i >> 1][j];
    }
    else
    {
        /* ...alpha plane */
        BUG(i != IMR_ALPHA || (u32)j >= 2, _x("invalid buffer: <%d,%d>"), i, j);

        /* ...save pointer to the memory buffer */
        meta->priv = app->alpha_plane[j];
    }

    /* ...assign camera buffer pointer */
    planes[0] = meta->buf->data = vsp_mem_ptr(meta->priv);

    /* ...create external texture (for debugging purposes only? - tbd) */
    CHK_ERR(meta->priv2 = texture_create(w, h, planes, format), -errno);

    /* ...add custom buffer destructor */
    gst_mini_object_weak_ref(GST_MINI_OBJECT(buffer), __destroy_imr_buffer, app);

    TRACE(INFO, _b("imr-buffer <%d:%d> allocated: %u*%u (format=%u, data=%p)"), i, j, w, h, meta->format, meta->buf->data);
    
    return 0;
}

/* ...buffer preparation callback */
static int imr_buffer_prepare(void *cdata, int i, GstBuffer *buffer)
{
    app_data_t     *app = cdata;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    vsp_mem_t      *mem = meta->priv;
    u32             sequence = app->sequence_imr[i];
    
    /* ...protect internal data */
    pthread_mutex_lock(&app->vsp_lock);

    /* ...setup engine if required */
    if ((app->flags & APP_FLAG_UPDATE) && (sequence == app->last_update))
    {
        imr_cfg_t  *cfg = app->imr_cfg[i];
        
        /* ...configuration must be available */
        BUG(cfg == NULL, _x("imr-%d: no active configuration"), i);
            
        /* ...set new configuration (and release it) */
        imr_cfg_apply(app->imr, i, cfg);
        imr_cfg_destroy(cfg);
        app->imr_cfg[i] = NULL;
    }

    /* ...update sequence number */
    app->sequence_imr[i] = sequence + 1;    

    /* ...unlock internal data */
    pthread_mutex_unlock(&app->vsp_lock);

    /* ...buffer clearing - tbd - probably should use different alpha masks */
    if (i < VSP_ALPHA)
    {
        int     j = meta->index;
        u32     mask = (APP_FLAG_CLEAR_BUFFER << ((i >> 1) + j * 2));

        /* ...camera-buffer preparation; reset memory if we didn't do that already */
        if (((app->imr_flags ^= mask) & mask) != 0)
        {
            memset(vsp_mem_ptr(mem), 0, vsp_mem_size(mem));
            TRACE(0, _b("<%d,%d>: clear done (size=%u, addr=%p)"), i, j, vsp_mem_size(mem), vsp_mem_ptr(mem));
        }
        else
        {
            /* ...second buffer submitted; do not clear anything */
            TRACE(0, _b("<%d,%d>: no clear"), i, j);
        }
    }
    else
    {
        /* ...alpha-buffer; reset memory always (tbd - now it's a single channel setting) */
        memset(vsp_mem_ptr(mem), 0, vsp_mem_size(mem));
        TRACE(0, _b("reset alpha plane: %u"), vsp_mem_size(mem));
    }

    return 0;
}

/* ...output buffer callback */
static int imr_buffer_process(void *cdata, int i, GstBuffer *buffer)
{
    app_data_t     *app = cdata;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    int             j = meta->index;
    int             r;
    
    TRACE(DEBUG, _b("imr-buffer <%d:%d> ready: %p (refcount=%d)"), i, j, buffer, GST_MINI_OBJECT_REFCOUNT(buffer));

    /* ...lock VSP data access */
    pthread_mutex_lock(&app->vsp_lock);

    /* ...submit buffer to a compositor (takes buffer ownership) */
    r = __vsp_submit_buffer(app, i, buffer);

    /* ...release VSP data access lock */
    pthread_mutex_unlock(&app->vsp_lock);

    return CHK_API(r);
}

/* ...engine callback structure */
static camera_callback_t   imr_cb = {
    .allocate = imr_buffer_allocate,
    .prepare = imr_buffer_prepare,
    .process = imr_buffer_process,
};    

/*******************************************************************************
 * Interface exposed to the camera backend
 ******************************************************************************/

/* ...deallocate texture data */
static void __destroy_vsink_texture(gpointer data, GstMiniObject *obj)
{
    GstBuffer      *buffer = (GstBuffer *)obj;
    vsink_meta_t   *meta = gst_buffer_get_vsink_meta(buffer);

    TRACE(DEBUG, _b("destroy texture referenced by meta: %p:%p"), meta, meta->priv);

    /* ...destroy texture */
    texture_destroy(meta->priv);
}

/* ...input buffer allocation */
static int app_input_alloc(void *data, GstBuffer *buffer)
{
    app_data_t     *app = data;
    vsink_meta_t   *vmeta = gst_buffer_get_vsink_meta(buffer);
    int             w = vmeta->width, h = vmeta->height;

    if (app->width)
    {
        /* ...verify buffer dimensions are valid */
        CHK_ERR(w == app->width && h == app->height, -EINVAL);
    }
    else
    {
        int     W, H;
        int     i;    

        /* ...check dimensions are valid */
        CHK_ERR(w && h, -EINVAL);
        
        /* ...set buffer dimensions */
        app->width = w, app->height = h;

        /* ...determine output buffers dimensions */
        if (app->window)
        {
            W = window_get_width(app->window), H = window_get_height(app->window);
        }
        else
        {
            W = window_get_width(app->window_aux), H = window_get_height(app->window_aux);
        }

        /* ...initialize IMR engines */
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            /* ...setup camera engine */
            CHK_API(imr_setup(app->imr, i, w, h, W, H, vmeta->format, GST_VIDEO_FORMAT_UYVY, VSP_POOL_SIZE));
        }

        /* ...start engine */
        CHK_API(imr_start(app->imr));        

        /* ...set initial map */
        CHK_API(__app_map_update(app));

        /* ...initialize auxiliary window if present */
        if (app->window_aux)
        {
            /* ...calculate viewport for the auxiliary window; get its size */
            W = window_get_width(app->window_aux), H = window_get_height(app->window_aux);

            /* ...prepare view-ports for raw cameras data */
            for (i = 0; i < CAMERAS_NUMBER; i++)
            {
                /* ...position raw camera images in upper left quadrant of the window */
                texture_set_view(&app->view_aux[i], (i & 1 ? 0.25 : 0), (i & 2 ? 0.75 : 0.5), (i & 1 ? 0.5 : 0.25), (i & 2 ? 1.0 : 0.75));
            }

            /* ...prepare view-ports for IMR output buffers; left/right camera set */
            texture_set_view(&app->view_aux[CAMERAS_NUMBER + 0], 0.5, 0.5, 0.75, 0.75);

            /* ...front/rear camera set */
            texture_set_view(&app->view_aux[CAMERAS_NUMBER + 1], 0.75, 0.75, 1, 1);

            /* ...alpha-plane view-port */
            texture_set_view(&app->view_aux[CAMERAS_NUMBER + 2], 0.75, 0.5, 1, 0.75);
            
            /* ...car image view-port */
            texture_set_view(&app->view_aux[CAMERAS_NUMBER + 3], 0.5, 0, 1, 0.5);

            /* ...output window view-port */
            texture_set_view(&app->view_aux[CAMERAS_NUMBER + 4], 0, 0, 0.5, 0.5);
        }
    }

    /* ...allocate texture to wrap the buffer */
    CHK_ERR(vmeta->priv = texture_create(w, h, vmeta->plane, vmeta->format), -errno);

    /* ...add custom destructor to the buffer */
    gst_mini_object_weak_ref(GST_MINI_OBJECT(buffer), __destroy_vsink_texture, app);

    /* ...do not need to do anything with the buffer allocation? */
    TRACE(INFO, _b("input buffer %p allocated"), buffer);

    return 0;
}

/* ...process new input buffer submitted from camera */
static int app_input_process(void *data, int i, GstBuffer *buffer)
{
    app_data_t     *app = data;
    vsink_meta_t   *vmeta = gst_buffer_get_vsink_meta(buffer);

    TRACE(DEBUG, _b("camera-%d: input buffer received"), i);

    /* ...make sure camera index is valid */
    CHK_ERR(i >= 0 && i < CAMERAS_NUMBER, -EINVAL);

    /* ...make sure buffer dimensions are valid */
    CHK_ERR(vmeta && vmeta->width == app->width && vmeta->height == app->height, -EINVAL);

    /* ...lock access to the internal queue */
    pthread_mutex_lock(&app->lock);
    
    /* ...collect buffers in a pending input queue */
    g_queue_push_tail(&app->input[i], gst_buffer_ref(buffer));

    /* ...submit a job only when all buffers have been collected */
    if ((app->input_ready &= ~(1 << i)) == 0)
    {
        if (__app_job_submit(app) != 0)
        {
            TRACE(ERROR, _x("job submission failed: %m"));
        }
    }
    else
    {
        TRACE(DEBUG, _b("buffer queued: %X"), app->input_ready);
    }

    /* ...unlock internal data */
    pthread_mutex_unlock(&app->lock);

    return 0;
}

/* ...callbacks for camera back-end */
static const camera_callback_t camera_cb = {
    .allocate = app_input_alloc,
    .process = app_input_process,
};

/*******************************************************************************
 * Drawing functions
 ******************************************************************************/

__attribute__((format (printf, 5, 6), unused))
static void draw_text(cairo_t *cr, const char *font, int x, int y, const char *fmt, ...)
{
    PangoLayout            *layout;
    PangoLayoutLine        *line;
    PangoFontDescription   *desc;
    cairo_font_options_t   *font_options;
	char                    buffer[256];
	va_list                 argp;

	va_start(argp, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, argp);
	va_end(argp);

    font_options = cairo_font_options_create();

    cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics(font_options, CAIRO_HINT_METRICS_OFF);

    cairo_set_font_options(cr, font_options);
    cairo_font_options_destroy(font_options);

    layout = pango_cairo_create_layout(cr);

    desc = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    pango_layout_set_text(layout, buffer, -1);

    /* Use pango_layout_get_line() instead of pango_layout_get_line_readonly()
     * for older versions of pango
     */
    line = pango_layout_get_line_readonly(layout, 0);

    cairo_move_to(cr, x, y);

    /* ...draw line of text */
    pango_cairo_layout_line_path(cr, line);
    pango_cairo_show_layout (cr, layout);

    g_object_unref(layout);
}

/* ...redraw main application window */
static void app_redraw(display_data_t *display, void *data)
{
    app_data_t     *app = data;
    window_data_t  *window = app->window;

    /* ...lock internal data */
    pthread_mutex_lock(&app->lock);
    
    /* ...retrieve pending buffers from rendering queue */
    while (!g_queue_is_empty(&app->render))
    {
        float           fps = window_frame_rate_update(window);
        GstBuffer      *buffer = g_queue_pop_head(&app->render);
        imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
        cairo_t        *cr;

        /* ...release data access lock */
        pthread_mutex_unlock(&app->lock);

        /* ...add some performance monitors here - tbd */
        TRACE(INFO, _b("redraw frame: %u"), app->frame_num++);

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClearDepthf(1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        /* ...output texture on the screen (stretch to entire viewable area) */
        texture_draw(meta->priv2, NULL, NULL, 1.0);

        /* ...open drawing context */
        cr = window_get_cairo(window);

        if (LOG_LEVEL > 0)
        {
            /* ...output frame-rate in the upper-left corner */
            cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
            draw_text(cr, "sans 18", 40, 80, "%.1f FPS", fps);
        }
        else
        {
            TRACE(DEBUG, _b("fps: %.2f"), fps);
        }

        /* ...release cairo drawing context */
        window_put_cairo(window, cr);

        /* ...submit window to display renderer */
        window_draw(window);

        /* ...make sure the processing is complete before we release output buffer (may be started instantly) */
        glFinish();
        
        /* ...return output buffer to the pool */
        gst_buffer_unref(buffer);

        /* ...reacquire data access lock */
        pthread_mutex_lock(&app->lock);
    }

    /* ...release processing lock */
    pthread_mutex_unlock(&app->lock);    

    TRACE(DEBUG, _b("drawing complete.."));
}

/* ...redraw auxiliary window */
static void app_redraw_aux(display_data_t *display, void *data)
{
    app_data_t     *app = data;
    window_data_t  *window = app->window_aux;

    /* ...lock internal data */
    pthread_mutex_lock(&app->lock);
    
    /* ...process all buffers queued thus far */
    while (!g_queue_is_empty(&app->render_aux[CAMERAS_NUMBER + VSP_OUTPUT]))
    {
        float       fps = window_frame_rate_update(window);
        GstBuffer  *buffer[CAMERAS_NUMBER + VSP_NUMBER];
        cairo_t    *cr;
        int         i;

        /* ...get all buffers from the head of the rendering queue */
        for (i = 0; i < CAMERAS_NUMBER + VSP_NUMBER; i++)
        {
            /* ...there must be a buffer queued */
            BUG(g_queue_is_empty(&app->render_aux[i]), _x("queue-%d is empty"), i);
            
            /* ...take the head of the queue (buffer must exist) */
            buffer[i] = g_queue_pop_head(&app->render_aux[i]);
        }

        /* ...release data access lock */
        pthread_mutex_unlock(&app->lock);

        /* ...clear frame */
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClearDepthf(1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        /* ...output raw cameras images */
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            vsink_meta_t       *vmeta = gst_buffer_get_vsink_meta(buffer[i]);

            texture_draw(vmeta->priv, &app->view_aux[i], NULL, 1.0);
        }

        /* ...output IMR outputs for left/right cameras */
        if (1)
        {
            imr_meta_t     *meta1 = gst_buffer_get_imr_meta(buffer[CAMERAS_NUMBER + VSP_CAMERA_LEFT]);
            imr_meta_t     *meta2 = gst_buffer_get_imr_meta(buffer[CAMERAS_NUMBER + VSP_CAMERA_RIGHT]);

            /* ...make sure indices are same */
            BUG(meta1->index != meta2->index, _x("invalid state"));
            
            texture_draw(meta1->priv2, &app->view_aux[CAMERAS_NUMBER + 0], NULL, 1.0);
        }

        /* ...output IMR outputs for front/rear buffer */
        if (1)
        {
            imr_meta_t     *meta1 = gst_buffer_get_imr_meta(buffer[CAMERAS_NUMBER + VSP_CAMERA_FRONT]);
            imr_meta_t     *meta2 = gst_buffer_get_imr_meta(buffer[CAMERAS_NUMBER + VSP_CAMERA_REAR]);

            /* ...make sure indices are same */
            BUG(meta1->index != meta2->index, _x("invalid state"));

            texture_draw(meta1->priv2, &app->view_aux[CAMERAS_NUMBER + 1], NULL, 1.0);
        }
        
        /* ...output alpha-plane */
        if (1)
        {
            imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer[CAMERAS_NUMBER + VSP_ALPHA]);

            texture_draw(meta->priv2, &app->view_aux[CAMERAS_NUMBER + 2], NULL, 1.0);
        }

        /* ...output car-plane */
        if (1)
        {
            imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer[CAMERAS_NUMBER + VSP_CAR]);

            texture_draw(meta->priv2, &app->view_aux[CAMERAS_NUMBER + 3], NULL, 1.0);
        }

        /* ...output resulting window */
        if (1)
        {
            imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer[CAMERAS_NUMBER + VSP_OUTPUT]);

            texture_draw(meta->priv2, &app->view_aux[CAMERAS_NUMBER + 4], NULL, 1.0);
        }
        
        /* ...output camera meshes */
        if (0 && (app->flags & APP_FLAG_DEBUG_CAMERA_MESH))
        {
            static u32      colors[CAMERAS_NUMBER] = {
                0xFF000000, 0x00FF0000, 0x0000FF00, 0xFFFFFF00,
            };

            /* ...acquire mutex to prevent mesh update while drawing is in progress */
            pthread_mutex_lock(&app->lock);
            
            for (i = 0; i < CAMERAS_NUMBER; i++)
            {
                mesh_draw(app->mesh[i], &app->view_aux[CAMERAS_NUMBER + 3], app->pv_matrix, app->model_matrix, colors[i]);
            }

            /* ...is that correct at all? should we wait for a GPU to finish? - tbd */
            pthread_mutex_unlock(&app->lock);
        }

        /* ...output alpha-plane meshes */
        if (0 && (app->flags & APP_FLAG_DEBUG_ALPHA_MESH))
        {
            /* ...acquire mutex to prevent mesh update */
            pthread_mutex_lock(&app->lock);

            /* ...a bit unclear now how we should act - GPU is running in a background */
            mesh_draw(app->mesh[IMR_ALPHA], &app->view_aux[CAMERAS_NUMBER + 3], app->pv_matrix, app->model_matrix, 0xFFFF0000);

            pthread_mutex_unlock(&app->lock);
        }

        /* ...low-level debugging */
        if (0)
        {
            extern void mesh_draw_coords(vbo_data_t *vbo, int n);
            extern vbo_data_t *__alpha_vbo;
            extern int  __alpha_vbo_n;

            /* ...acquire mutex to prevent mesh update */
            pthread_mutex_lock(&app->lock);
            
            if (__alpha_vbo)
            {
                mesh_draw_coords(__alpha_vbo, __alpha_vbo_n);
            }

            pthread_mutex_unlock(&app->lock);
        }

        /* ...output textual information */
        if (1)
        {
            int     w = window_get_width(window);
            int     h = window_get_height(window);

            /* ...get drawing context */
            cr = window_get_cairo(window);

            cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
            
            /* ...output rotation/scaling parameters in lower left corner */
            draw_text(cr, "sans 12", w - 250, h - 32 - 16, "RX:%d, RY:%d, RZ:%d, S:%.2f",
                      (int)app->rot_acc[0], (int)app->rot_acc[1], (int)app->rot_acc[2],
                      app->scl_acc);

            /* ...output per-buffer processing times */
            draw_text(cr, "sans 12", w / 2 + 16, 16 + 0 * 24, "front: %u", imr_engine_avg_time(app->imr, IMR_CAMERA_FRONT));
            draw_text(cr, "sans 12", w / 2 + 16, 16 + 1 * 24, "rear:  %u", imr_engine_avg_time(app->imr, IMR_CAMERA_REAR));
            draw_text(cr, "sans 12", w / 2 + 16, 16 + 2 * 24, "left:  %u", imr_engine_avg_time(app->imr, IMR_CAMERA_LEFT));
            draw_text(cr, "sans 12", w / 2 + 16, 16 + 3 * 24, "right: %u", imr_engine_avg_time(app->imr, IMR_CAMERA_RIGHT));
            draw_text(cr, "sans 12", w / 2 + 16, 16 + 4 * 24, "alpha: %u", imr_engine_avg_time(app->imr, IMR_ALPHA));

            /* ...output FPS in upper left corner */
            draw_text(cr, "sans 12", 16, 16, "FPS: %.1f", fps);

            /* ...release drawing context */
            window_put_cairo(window, cr);
        }
        
        TRACE(DEBUG, _b("aux-window fps: %.2f"), fps);

        /* ...submit window to composer */
        window_draw(window);

        /* ...make sure the pipeline is processed fully before dropping the buffers */
        glFinish();

        /* ...drop all buffers once GPU processing is over */
        for (i = 0; i < CAMERAS_NUMBER + VSP_NUMBER; i++)
        {
            gst_buffer_unref(buffer[i]);
        }

        /* ...regain access lock */
        pthread_mutex_lock(&app->lock);
    }

    /* ...unlock internal data */
    pthread_mutex_unlock(&app->lock);

    TRACE(DEBUG, _b("aux-drawing complete.."));
}

/* ...stop rendering */
static inline void app_stop(app_data_t *app)
{
    /* ...set end-of-stream flag */
    app->flags |= APP_FLAG_EOS;
    
    /* ...stop IMR engine */
    if (app->imr)
    {
        /* ...workaround - release application lock to allow IMR to finish */
        pthread_mutex_unlock(&app->lock);

        imr_engine_close(app->imr), app->imr = NULL;

        /* ...workaround - regain application lock */
        pthread_mutex_lock(&app->lock);
    }
    
    /* ...wait for renderers completion */
    (app->window_aux ? window_schedule_redraw(app->window_aux) : 0);    
    (app->window ? window_schedule_redraw(app->window) : 0);

    /* ...clear flag eventually */
    app->flags &= ~APP_FLAG_EOS;

    TRACE(INIT, _b("rendering stopped"));
}

/*******************************************************************************
 * Compositor buffers pool
 ******************************************************************************/

/* ...output buffer dispose function */
static gboolean __vsp_buffer_dispose(GstMiniObject *obj)
{
    GstBuffer      *buffer = GST_BUFFER(obj);
    app_data_t     *app = (app_data_t *)buffer->pool;
    gboolean        destroy = FALSE;

    /* ...lock VSP data access */
    pthread_mutex_lock(&app->vsp_lock);
    
    /* ...submit output buffer to the compositor (adds ref) - tbd - graceful termination */
    __vsp_submit_buffer(app, VSP_OUTPUT, buffer);

    /* ...unlock internal data */
    pthread_mutex_unlock(&app->vsp_lock);

    return destroy;
}

/*******************************************************************************
 * Runtime initialization
 ******************************************************************************/

/* ...alpha input buffer disposal hook (called from a context of IMR thread) - function not needed at all? - tbd */
static gboolean __alpha_buffer_dispose(GstMiniObject *obj)
{
    GstBuffer      *buffer = GST_BUFFER(obj);
    //app_data_t     *app = (app_data_t *)buffer->pool;
    gboolean        destroy = FALSE;

    /* ...lock internal data access */
    //pthread_mutex_lock(&app->lock);

    /* ...process graceful termination - tbd */
    gst_buffer_ref(buffer);

    TRACE(DEBUG, _b("alpha-input buffer %p returned to pool"), buffer);

    /* ...unlock internal data */
    //pthread_mutex_unlock(&app->lock);

    return destroy;
}

/* ...alpha-plane processing initialization */
static inline int app_alpha_setup(app_data_t *app, int W, int H)
{
    int             format = GST_VIDEO_FORMAT_GRAY8, w = 512, h = 512;
    void           *alpha;
    GstBuffer      *buffer;
    vsink_meta_t   *vmeta;

    /* ...load alpha-plane mesh data */
    CHK_ERR(app->mesh[IMR_ALPHA] = mesh_create("meshAlpha.obj"), -errno);

    /* ...allocate single buffer for alpha-plane transformation */
    CHK_API(vsp_allocate_buffers(w, h, V4L2_PIX_FMT_GREY, app->alpha_input, 1));

    /* ...and two output buffers */
    CHK_API(vsp_allocate_buffers(W, H, V4L2_PIX_FMT_GREY, app->alpha_plane, 2));

    /* ...load alpha-plane image */
    CHK_API(create_png("blendPlane.png", &w, &h, &format, (alpha = vsp_mem_ptr(app->alpha_input[0]), &alpha)));

    /* ...setup IMR engine (two buffers) */
    CHK_API(imr_setup(app->imr, IMR_ALPHA, w, h, W, H, GST_VIDEO_FORMAT_GRAY8, GST_VIDEO_FORMAT_GRAY8, 2));

    /* ...allocate single input buffer */
    CHK_ERR(buffer = gst_buffer_new(), -(errno = ENOMEM));

    /* ...add vsink meta-data (needed by IMR engine?) */
    vmeta = gst_buffer_add_vsink_meta(buffer);
    vmeta->plane[0] = alpha;
    vmeta->format = GST_VIDEO_FORMAT_GRAY8;
    vmeta->width = w;
    vmeta->height = h;
    GST_META_FLAG_SET(vmeta, GST_META_FLAG_POOLED);

    /* ...modify buffer release callback */
    GST_MINI_OBJECT_CAST(buffer)->dispose = __alpha_buffer_dispose;

    /* ...save buffer custom data */
    (app->alpha_buffer = buffer)->pool = (void *)app;

    TRACE(INIT, _b("alpha-plane set up"));

    return 0;
}

/*******************************************************************************
 * Car rendering
 ******************************************************************************/

/* ...car image buffer disposal function */
static gboolean __car_buffer_dispose(GstMiniObject *obj)
{
    GstBuffer      *buffer = GST_BUFFER(obj);
    app_data_t     *app = (app_data_t *)buffer->pool;
    gboolean        destroy = FALSE;

    /* ...lock internal data access */
    pthread_mutex_lock(&app->lock);

    /* ...add buffer reference (tbd - process graceful termination command) */
    gst_buffer_ref(buffer);

    TRACE(0, _b("car image updated: flags=%X"), app->flags);

    /* ...unlock internal data */
    pthread_mutex_unlock(&app->lock);

    return destroy;
}

/* ...load car buffer with image */
static int app_car_buffer_load(app_data_t *app, GstBuffer *buffer, const char *path)
{
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    texture_data_t *texture = meta->priv2;
    int             format = meta->format;
    int             w = meta->width, h = meta->height;
    void           *data = vsp_mem_ptr(meta->priv);
    static          int count = 0;
    u32             t0, t1;

    t0 = __get_time_usec();
    
    /* ...load PNG image into corresponding car plane */
    if (0)
    {
        char    buf[64];
        
        sprintf(buf, "car-render-%04d.png", count++ % 21);
        CHK_API(create_png(buf, &w, &h, &format, &data));
        
    }
    else
    {
        CHK_API(car_render(app->car, texture, __p_matrix, __v_matrix, app->model_matrix));
    }

    t1 = __get_time_usec();
    
    TRACE(INFO, _b("car-buffer ready: '%s' (flags: %X, time: %u)"), path, app->flags, (u32)(t1 - t0));

    /* ...save car image on disk */
    if (0)
    {
        static int counter = 0;
        char buffer[256];
        
        sprintf(buffer, "car-render-%04d.png", counter);
        counter = (counter + 1) % 10000;
        
        if (store_png(buffer, w, h, format, data) != 0)
        {
            BUG(1, _x("breakpoint"));
        }
    }

    /* ...submit buffer to a compositor pending queue */
    pthread_mutex_lock(&app->vsp_lock);

    /* ...submit immediately to the compositor queue */
    __vsp_submit_buffer(app, VSP_CAR, buffer);

    pthread_mutex_unlock(&app->vsp_lock);

    return 0;
}

/* ...car buffer update thread */
static void * car_update_thread(void *arg)
{
    app_data_t     *app = arg;

    /* ...protect internal data access */
    pthread_mutex_lock(&app->lock);
    
    /* ...wait for update event */
    while (1)
    {
        int     m;
        
        /* ...wait for car model update flag */
        while ((app->flags & (APP_FLAG_CAR_UPDATE | APP_FLAG_EOS)) == 0)
        {
            pthread_cond_wait(&app->update, &app->lock);
        }

        /* ...check for a termination request (tbd) */
        if (app->flags & APP_FLAG_EOS)
        {
            TRACE(INIT, _b("termination request received"));
            goto out;
        }

        /* ...get index of the buffer to load */
        m = (app->flags & APP_FLAG_SET_INDEX ? 1 : 0);
        
        /* ...toggle buffers immediately */
        app->flags ^= APP_FLAG_SET_INDEX;

        /* ...release internal data lock */
        pthread_mutex_unlock(&app->lock);

        /* ...load car model (name is pretty fake) */
        if (app_car_buffer_load(app, app->car_buffer[m], "car.png") != 0)
        {
            TRACE(ERROR, _x("car buffer loading failed: %m"));
        }

        /* ...reacquire application lock */
        pthread_mutex_lock(&app->lock);

        /* ...clear car-model update flag */
        app->flags &= ~APP_FLAG_CAR_UPDATE;
    }

out:
    /* ...release data access lock */
    pthread_mutex_unlock(&app->lock);

    TRACE(INIT, _b("car-model update thread terminated: %m"));
    
    return NULL;
}

/* ...car model initialization */
static int app_car_setup(app_data_t *app, int W, int H)
{
    pthread_attr_t  attr;
    int             j;
    int             r;

    /* ...car image plane allocation */
    CHK_API(vsp_allocate_buffers(W, H, V4L2_PIX_FMT_ARGB32, app->car_plane, 2));

    /* ...create buffers for a car image */
    for (j = 0; j < 2; j++)
    {
        GstBuffer      *buffer = gst_buffer_new();
        imr_meta_t     *meta = gst_buffer_add_imr_meta(buffer);
        void           *planes[3];
        
        /* ...set memory descriptor */
        meta->priv = app->car_plane[j];
        meta->width = W;
        meta->height = H;
        meta->format = GST_VIDEO_FORMAT_ARGB;
        meta->index = j;
        GST_META_FLAG_SET(meta, GST_META_FLAG_POOLED);

        /* ...create texture (for debugging porposes only?) */
        planes[0] = vsp_mem_ptr(meta->priv);
        CHK_ERR(meta->priv2 = texture_create(W, H, planes, meta->format), -errno);

        /* ...modify buffer release callback */
        GST_MINI_OBJECT_CAST(buffer)->dispose = __car_buffer_dispose;

        /* ...save buffer pointer */
        (app->car_buffer[j] = buffer)->pool = (void *)app;
    }

    /* ...initialize car renderer */
    CHK_ERR(app->car = car_renderer_init("Car.obj", W, H), -errno);

    /* ...initialize thread attributes (joinable, 128KB stack) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr, 128 << 10);

    /* ...create car model update thread */
    r = pthread_create(&app->car_thread, &attr, car_update_thread, app);
    pthread_attr_destroy(&attr);
    CHK_API(r);

    return 0;
}

/* ...initialize GL-processing context */
static int app_context_init(widget_data_t *widget, void *data)
{
    app_data_t     *app = data;
    window_data_t  *window = (window_data_t *)widget;
    int             w = window_get_width(window);
    int             h = window_get_height(window);
    int             i, j;

    /* ...check out if we already initialized everything */
    if (app->imr)       return 0;

    /* ...create VSP compositor */
    CHK_ERR(app->vsp = compositor_init(w, h, V4L2_PIX_FMT_UYVY, w, h, V4L2_PIX_FMT_ABGR32, vsp_callback, app), -errno);

    /* ...load camera mesh data */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        CHK_ERR(app->mesh[i] = mesh_create(mesh_file_name[i]), -errno);
    }

    /* ...create VSP memory pools for cameras and transparency planes */
    CHK_API(vsp_allocate_buffers(w, h, V4L2_PIX_FMT_UYVY, app->camera_plane[0], VSP_POOL_SIZE));
    CHK_API(vsp_allocate_buffers(w, h, V4L2_PIX_FMT_UYVY, app->camera_plane[1], VSP_POOL_SIZE));

    /* ...car image preparation */
    CHK_API(app_car_setup(app, w, h));
    
    /* ...create VSP memory pool for resulting image */
    CHK_API(vsp_allocate_buffers(w, h, V4L2_PIX_FMT_ARGB32, app->output, VSP_POOL_SIZE));

    /* ...create output buffers */
    for (j = 0; j < VSP_POOL_SIZE; j++)
    {
        GstBuffer      *buffer = gst_buffer_new();
        imr_meta_t     *meta = gst_buffer_add_imr_meta(buffer);
        void           *planes[3];

        meta->width = w;
        meta->height = h;
        meta->format = GST_VIDEO_FORMAT_ARGB;
        meta->priv = app->output[j];
        meta->index = j;
        GST_META_FLAG_SET(meta, GST_META_FLAG_POOLED);

        /* ...modify buffer release callback */
        GST_MINI_OBJECT_CAST(buffer)->dispose = __vsp_buffer_dispose;

        /* ...wrap buffer memory with a texture */
        planes[0] = vsp_mem_ptr(meta->priv);
        CHK_ERR(meta->priv2 = texture_create(w, h, planes, meta->format), -errno);

        /* ...save buffer custom data */
        (app->buffer[j] = buffer)->pool = (void *)app;

        /* ...submit buffer to the compositor */
        __vsp_submit_buffer(app, VSP_OUTPUT, buffer);

        /* ...unref buffer (ownership passed to compositor) */
        gst_buffer_unref(buffer);
    }

    /* ...create IMR engines - one per camera + alpha-plane + car shadow + car model */
    CHK_ERR(app->imr = imr_init(imr_dev_name, IMR_NUMBER, &imr_cb, app), -errno);

    /* ...alpha-plane processing setup */
    CHK_API(app_alpha_setup(app, w, h));

    TRACE(INFO, _b("run-time initialized"));

    return 0;
}

/*******************************************************************************
 * Gstreamer thread (separated from decoding)
 ******************************************************************************/

void * app_thread(void *arg)
{
    app_data_t     *app = arg;
    u32             flags;
    track_desc_t   *track = NULL;

    pthread_mutex_lock(&app->lock);

    /* ...play all tracks in a loop */
    while (((flags = app->flags) & APP_FLAG_EXIT) == 0)
    {
        /* ...process track change command */
        if (flags & APP_FLAG_NEXT)
        {
            track = (track_desc_t *)track_next();
        }
        else if (flags & APP_FLAG_PREV)
        {
            track = (track_desc_t *)track_prev();
        }

        /* ...prepare track configuration */
        TRACE(INFO, _b("playing track '%s'"), (track->info ? : "default"));

        /* ...prepare track playback files */
        if (!track_start(app, track, 1))
        {
            /* ...update flags (by default move to next track) */
            app->flags = (app->flags & ~(APP_FLAG_NEXT | APP_FLAG_PREV)) | APP_FLAG_NEXT;

            /* ...release internal data access lock */
            pthread_mutex_unlock(&app->lock);

            /* ...set pipeline to playing state (enable all cameras) */
            gst_element_set_state(app->pipe, GST_STATE_PLAYING);

            TRACE(INIT, _b("enter main loop"));

            /* ...start main application loop */
            g_main_loop_run(app->loop);

            TRACE(INIT, _b("main loop complete"));

            /* ...re-acquire internal data access lock */
            pthread_mutex_lock(&app->lock);

            /* ...stop renderer */
            app_stop(app);

            /* ...release lock */
            pthread_mutex_unlock(&app->lock);
            
            /* ...stop the pipeline */
            gst_element_set_state(app->pipe, GST_STATE_NULL);

            /* ...stop current track */
            track_start(app, track, 0);

            TRACE(DEBUG, _b("streaming stopped"));

            /* ...wait for a renderer completion */
            pthread_mutex_lock(&app->lock);
        }
        else
        {
            TRACE(ERROR, _b("failed to start track: %m"));
        }

        /* ...destroy camera bin as needed */
        (app->camera ? gst_bin_remove(GST_BIN(app->pipe), app->camera), app->camera = NULL : 0);
    }

    /* ...release internal data access lock */
    pthread_mutex_unlock(&app->lock);

    /* ...destroy pipeline and everything */
    gst_object_unref(app->pipe);
    
    return NULL;
}

/*******************************************************************************
 * Pipeline control flow callback
 ******************************************************************************/

static gboolean app_bus_callback(GstBus *bus, GstMessage *message, gpointer data)
{
    app_data_t     *app = data;
    GMainLoop      *loop = app->loop;

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
    {
        GError     *err;
        gchar      *debug;

        /* ...dump error-message reported by the GStreamer */
        gst_message_parse_error (message, &err, &debug);
        TRACE(ERROR, _b("execution failed: %s"), err->message);
        g_error_free(err);
        g_free(debug);

        /* ...and terminate the loop */
        g_main_loop_quit(loop);
        BUG(1, _x("breakpoint"));
        return FALSE;
    }

    case GST_MESSAGE_EOS:
    {
        /* ...end-of-stream encountered; break the loop */
        TRACE(INFO, _b("execution completed"));
        g_main_loop_quit(loop);
        return TRUE;
    }

    default:
        /* ...ignore message */
        TRACE(0, _b("ignore message: %s"), gst_message_type_get_name(GST_MESSAGE_TYPE(message)));

        /* ...dump the pipeline? */
        //gst_debug_bin_to_dot_file_with_ts(GST_BIN(app->pipe), GST_DEBUG_GRAPH_SHOW_ALL, "app");
        break;
    }

    /* ...remove message from the queue */
    return TRUE;
}

/*******************************************************************************
 * Interface to backend camera
 ******************************************************************************/

/* ...end-of-stream signalization (for offline playback) */
void app_eos(app_data_t *app)
{
    GstMessage     *message = gst_message_new_eos(GST_OBJECT(app->pipe));
    
    gst_element_post_message(GST_ELEMENT_CAST(app->pipe), message);;
}

/*******************************************************************************
 * Input events processing
 ******************************************************************************/

/* ...3D-joystick input processing */
static inline widget_data_t * app_spnav_event(app_data_t *app, widget_data_t *widget, widget_spnav_event_t *event)
{
    spnav_event    *e = event->e;

    if (e->type == SPNAV_EVENT_MOTION)
    {
        int     rx = e->motion.rx, ry = e->motion.ry, rz = e->motion.rz;
        int     y = e->motion.y;

        /* ...discard too fast changes (should be done differently - tbd) */
        //if ((app->spnav_delta += e->motion.period) < 50) return widget;

        /* ...clear delta */
        app->spnav_delta = 0;

        /* ...ignore slight changes */
        (rx < 100 && rx > - 100 ? rx = 0 : 0);
        (ry < 100 && ry > - 100 ? ry = 0 : 0);
        (rz < 100 && rz > - 100 ? rz = 0 : 0);
        (y < 100 && y > - 100 ? y = 0 : 0);
        if ((rx | ry | rz | y) == 0)    return widget;

        TRACE(DEBUG, _b("spnav-event-motion: <x=%d,y=%d,z=%d>, <rx=%d,ry=%d,rz=%d>, p=%d"),
              e->motion.x, e->motion.y, e->motion.z,
              e->motion.rx, e->motion.ry, e->motion.rz,
              e->motion.period);

        /* ...update all meshes */
        pthread_mutex_lock(&app->lock);

        /* ...rotate/scale model matrix */
        __app_matrix_update(app, rx, ry, rz, y);

        /* ...update meshes */
        __app_map_update(app);
        
        pthread_mutex_unlock(&app->lock);
    }
    else if (e->type == SPNAV_EVENT_BUTTON && e->button.press == 1 && e->button.bnum == 0)
    {
        pthread_mutex_lock(&app->lock);

        /* ...reset model view matrix */
        __app_matrix_reset(app);

        /* ...update all meshes */
        __app_map_update(app);

        pthread_mutex_unlock(&app->lock);
    }
    else if (e->type == SPNAV_EVENT_BUTTON && e->button.press == 1 && e->button.bnum == 1)
    {
        GstBuffer      *buffer;
        imr_meta_t     *meta;
        int             m;
        static int      counter = 0;
        char            fname[256];
        u32             t0, t1;
        
        sprintf(fname, "car-render-%04d.png", counter);
        counter = (counter + 1) % 10000;
        
        pthread_mutex_lock(&app->lock);

        t0 = __get_time_usec();
        
        /* ...get current car plane index */
        m = !!(app->flags & APP_FLAG_SET_INDEX);
        buffer = app->car_buffer[m];
        meta = gst_buffer_get_imr_meta(buffer);
        
        /* ...get current car plane buffer */
        store_png(fname, meta->width, meta->height, meta->format, vsp_mem_ptr(meta->priv));

        t1 = __get_time_usec();
        
        pthread_mutex_unlock(&app->lock);

        TRACE(INFO, _b("snapshot '%s' stored (%u usec)"), fname, (u32)(t1 - t0));
    }

    return widget;
}

/* ...touchscreen input processing */
static inline widget_data_t * app_touch_event(app_data_t *app, widget_data_t *widget, widget_touch_event_t *event)
{
    int     i;
    int     dx = 0, dy = 0;

    /* ...ignore 3-finger events */
    if ((i = event->id) >= 2)        return widget;

    /* ...lock internal data access */
    pthread_mutex_lock(&app->lock);
    
    /* ...save the touch state */
    switch (event->type)
    {
    case WIDGET_EVENT_TOUCH_MOVE:
        /* ...get difference between positions */
        dx = event->x - app->ts_pos[i][0], dy = event->y - app->ts_pos[i][1];

        /* ...and pass through */

    case WIDGET_EVENT_TOUCH_DOWN:
        /* ...update touch location */
        app->ts_pos[i][0] = event->x, app->ts_pos[i][1] = event->y;

        /* ...save touch point state */
        app->ts_flags |= (1 << i);
        break;

    case WIDGET_EVENT_TOUCH_UP:
    default:
        /* ...update touch state */
        app->ts_flags &= ~(1 << i);
    }

    /* ...process two-fingers movement */
    if (app->ts_flags == 0x3)
    {
        int     t;
        int     dist;
        
        /* ...get difference between touch-points */
        t = app->ts_pos[1][0] - app->ts_pos[0][0], dist = t * t;        
        t = app->ts_pos[1][1] - app->ts_pos[0][1], dist += t * t;

        /* ...check if the distance has been latched already */
        if (app->ts_dist && (t = app->ts_dist - dist) != 0)
        {
            /* ...update scaling coefficient only */
            __app_matrix_update(app, 0, 0, 0, 10000.0 * t / app->ts_dist);
        }

        /* ...latch current distance */
        app->ts_dist = dist;
    }
    else
    {
        /* ...reset latched distance */
        app->ts_dist = 0;

        /* ...apply rotation around X and Z axis */
        (dx || dy ? __app_matrix_update(app, -dy * 100, 0, dx * 100, 0) : 0);
    }

    /* ...update meshes */
    __app_map_update(app);

    /* ...get internal data lock access */
    pthread_mutex_unlock(&app->lock);

    /* ...single touch is used for rotation around x and z axis */
    return widget;
}

/* ...touchscreen input processing */
static inline widget_data_t * app_kbd_event(app_data_t *app, widget_data_t *widget, widget_key_event_t *event)
{
    if (event->type == WIDGET_EVENT_KEY_PRESS && event->state)
    {
        switch (event->code)
        {
        case KEY_ESC:
            /* ...terminate application (for the moment, just exit - tbd) */
            TRACE(INIT, _b("terminate application"));
            exit(0);
        }        
    }

    /* ...always keep focus */
    return widget;
}

/* ...event-processing function */
static widget_data_t * app_input_event(widget_data_t *widget, void *cdata, widget_event_t *event)
{
    app_data_t     *app = cdata;

    /* ...pass event to GUI layer first */
    switch (WIDGET_EVENT_TYPE(event->type))
    {
    case WIDGET_EVENT_SPNAV:
        return app_spnav_event(app, widget, &event->spnav);

    case WIDGET_EVENT_TOUCH:
        return app_touch_event(app, widget, &event->touch);

    case WIDGET_EVENT_KEY:
        return app_kbd_event(app, widget, &event->key);

    default:
        return NULL;
    }
}

/*******************************************************************************
 * Module initialization
 ******************************************************************************/

/* ...processing window parameters */
static window_info_t app_main_info = {
    .fullscreen = 1,
    .redraw = app_redraw,
};

/* ...main window widget paramters (input-interface + GUI?) */
static widget_info_t app_main_info2 = {
    .init = app_context_init,
    .event = app_input_event,
};

/* ...auxiliary window parameters (raw image from camera) */
static window_info_t app_aux_info = {
    .fullscreen = 1,
    .redraw = app_redraw_aux,
};

/* ...module destructor */
static void app_destroy(gpointer data, GObject *obj)
{
    app_data_t  *app = data;

    TRACE(INIT, _b("destruct application data"));

    /* ...destroy main loop */
    g_main_loop_unref(app->loop);

    /* ...destroy main application window */
    (app->window ? window_destroy(app->window) : 0);
    
    /* ...destroy auxiliary application window */
    (app->window_aux ? window_destroy(app->window_aux) : 0);

    /* ...free application data structure */
    free(app);

    TRACE(INIT, _b("module destroyed"));
}

/* ...set camera interface */
int app_camera_init(app_data_t *app, camera_init_func_t camera_init)
{
    GstElement     *bin;

    /* ...clear input stream dimensions (force engine reinitialization) */
    app->width = app->height = 0;
    
    /* ...create camera interface (it may be network camera or file on disk) */
    CHK_ERR(bin = camera_init(&camera_cb, app), -errno);

    /* ...add cameras to a pipe */
    gst_bin_add(GST_BIN(app->pipe), bin);

    /* ...synchronize state with a parent */
    gst_element_sync_state_with_parent(bin);

    /* ...save camera-set container */
    app->camera = bin;

    TRACE(INIT, _b("camera-set initialized"));

    return 0;
}

/* ...module initialization function */
app_data_t * app_init(display_data_t *display)
{
    app_data_t            *app;
    GstElement            *pipe;
    pthread_mutexattr_t    attr;

    /* ...sanity check - output devices shall be different */
    CHK_ERR(__output_main != __output_aux, (errno = EINVAL, NULL));

    /* ...create local data handle */
    CHK_ERR(app = calloc(1, sizeof(*app)), (errno = ENOMEM, NULL));

    /* ...set default flags */
    app->flags = APP_FLAG_NEXT | 0*APP_FLAG_DEBUG | APP_FLAG_DEBUG_ALPHA_MESH;

    /* ...reset input frames readiness state */
    app->input_ready = (1 << CAMERAS_NUMBER) - 1;

    /* ...reset output frames readiness state */
    app->vsp_ready = (1 << VSP_NUMBER) - 1;

    /* ...last update sequence number */
    app->last_update = ~0U;

    /* ...main window creation */
    if (__output_main >= 0)
    {
        /* ...set output device number for a main window */
        app_main_info.output = __output_main;

        /* ...create full-screen window for processing results visualization */        
        if ((app->window = window_create(display, &app_main_info, &app_main_info2, app)) == NULL)
        {
            TRACE(ERROR, _x("failed to create main window: %m"));
            goto error;
        }
    }

    /* ...auxiliary window creation */
    if (__output_aux >= 0)
    {
        /* ...set output device number */
        app_aux_info.output = __output_aux;
        
        /* ...create window for raw image visualization */
        if ((app->window_aux = window_create(display, &app_aux_info, &app_main_info2, app)) == NULL)
        {
            TRACE(ERROR, _x("failed to create auxiliary window: %m"));
            goto error;
        }
    }

    /* ...initialize decoding lock */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&app->lock, &attr);
    pthread_mutex_init(&app->vsp_lock, &attr);
    pthread_mutexattr_destroy(&attr);

    /* ...initialize conditional variable for mesh configuration update threads */
    pthread_cond_init(&app->update, NULL);

    /* ...create map update thread */
    if (app_map_init(app) != 0)
    {
        TRACE(ERROR, _x("failed to create map update thread"));
        goto error;
    } 

    /* ...create main loop object (use default context) */
    if ((app->loop = g_main_loop_new(NULL, FALSE)) == NULL)
    {
        TRACE(ERROR, _x("failed to create main loop object"));
        errno = ENOMEM;
        goto error;
    }
    else
    {
        /* ...push default thread context for all subsequent sources */
        g_main_context_push_thread_default(g_main_loop_get_context(app->loop));
    }

    /* ...create a pipeline */
    if ((app->pipe = pipe = gst_pipeline_new("app::pipe")) == NULL)
    {
        TRACE(ERROR, _x("pipeline creation failed"));
        errno = ENOMEM;
        goto error_loop;
    }
    else
    {
        GstBus  *bus = gst_pipeline_get_bus(GST_PIPELINE(pipe));
        gst_bus_add_watch(bus, app_bus_callback, app);
        gst_object_unref(bus);
    }

    /* ...add destructor to the pipe */
    g_object_weak_ref(G_OBJECT(pipe), app_destroy, app);
    
    TRACE(INIT, _b("surround-view module initialized"));

    return app;

error_loop:
    /* ...destroy main loop */
    g_main_loop_unref(app->loop);

error:
    /* ...destroy auxiliary application window */
    (app->window_aux ? window_destroy(app->window_aux) : 0);

    /* ...destroy main application window */
    (app->window ? window_destroy(app->window) : 0);

    /* ...destroy data handle */
    free(app);

    return NULL;
}
