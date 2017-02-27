/*******************************************************************************
 * utest-vsink.c
 *
 * Gstreamer video sink for rendering via EGL
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

#define MODULE_TAG                      VSINK

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"
#include "utest-display.h"
#include "utest-vsink.h"
#include <gst/app/gstappsink.h>
#include <gst/video/video-info.h>
#include <gst/allocators/gstdmabuf.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

/* ...custom video sink node */
struct video_sink
{
    /* ...application sink node */
    GstAppSink                 *appsink;

    /* ...buffer pool */
    GstBufferPool              *pool;

    /* ...user-provided callbacks */
    const vsink_callback_t     *cb;

    /* ...processing function custom data */
    void                       *cdata;
};

/*******************************************************************************
 * Custom buffer metadata implementation
 ******************************************************************************/

/* ...metadata type registration */
GType vsink_meta_api_get_type(void)
{
    static volatile GType type;
    static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, GST_META_TAG_MEMORY_STR, NULL };

    if (g_once_init_enter(&type))
    {
        GType _type = gst_meta_api_type_register("VideoSinkMetaAPI", tags);
        g_once_init_leave(&type, _type);
    }

    return type;
}

/* ...low-level interface */
static gboolean vsink_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
    vsink_meta_t   *_meta = (vsink_meta_t *) meta;
    
    /* ...reset fields */
    memset(meta + 1, 0, sizeof(*_meta) - sizeof(*meta));

    return TRUE;
}

/* ...metadata transformation */
static gboolean vsink_meta_transform(GstBuffer *transbuf, GstMeta *meta,
        GstBuffer *buffer, GQuark type, gpointer data)
{
    vsink_meta_t   *_meta = (vsink_meta_t *) meta, *_tmeta;

    /* ...just copy data regardless of transform type? */
    _tmeta = gst_buffer_add_vsink_meta(transbuf);

    C_UNUSED(_tmeta), C_UNUSED(_meta);
    
    return TRUE;
}

/* ...metadata release */
static void vsink_meta_free(GstMeta *meta, GstBuffer *buffer)
{
    vsink_meta_t   *_meta = (vsink_meta_t *) meta;
    //video_sink_t   *sink = _meta->sink;
    
    TRACE(1, _b("free metadata %p"), _meta);

    /* ...notify sink about buffer destruction */
    //sink->cb->destroy(sink, buffer, sink->cdata);
}

/* ...register metadata implementation */
const GstMetaInfo * vsink_meta_get_info(void)
{
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info))
    {
        const GstMetaInfo *mi = gst_meta_register(
            VSINK_META_API_TYPE,
            "VideoSinkMeta",
            sizeof(vsink_meta_t),
            vsink_meta_init,
            vsink_meta_free,
            vsink_meta_transform);
        
        g_once_init_leave (&meta_info, mi);
    }

    return meta_info;
}

/*******************************************************************************
 * Buffer allocation
 ******************************************************************************/

/* ...allocate video-buffer (custom query from OMX component) */
static GstBuffer * vsink_buffer_create(video_sink_t *sink, gint *dmabuf,
        GstAllocator *allocator, gint width, gint height, gint *stride, gpointer *planebuf,
        GstVideoFormat format, int n_planes)
{
    GstBuffer          *buffer = gst_buffer_new();
    vsink_meta_t       *meta = gst_buffer_add_vsink_meta(buffer);
    //gsize               offset[GST_VIDEO_MAX_PLANES] = { 0 };
    GstMemory          *m;
    int                 i;

    /* ...save meta-format */
    meta->width = width;
    meta->height = height;
    meta->format = format;
    meta->sink = sink;

    /* ...avoid detaching of metadata when buffer is returned to a pool */
    GST_META_FLAG_SET(meta, GST_META_FLAG_POOLED);

    /* ...buffer meta-data creation - tbd */
    switch (format)
    {
    case GST_VIDEO_FORMAT_NV12:
        /* ...make sure we have two planes */
        if (n_planes != 2)      goto error_planes;
        
        TRACE(INFO, _b("allocate NV12 %u*%u texture (buffer=%p, meta=%p)"), width, height, buffer, meta);

        /* ...add buffer metadata */
        meta->plane[0] = planebuf[0];
        meta->plane[1] = planebuf[1];
        meta->dmafd[0] = dmabuf[0];
        meta->dmafd[1] = dmabuf[1];

        /* ...invoke user-supplied allocation callback */
        if (sink->cb->allocate(sink, buffer, sink->cdata))   goto error_user;

        TRACE(INFO, _b("allocated %u*%u NV12 buffer: %p (dmafd=%d,%d)"), width, height, buffer, dmabuf[0], dmabuf[1]);

        break;
    
    default:
        /* ...unrecognized format; give up */
        TRACE(ERROR, _b("unsupported buffer format: %s"), gst_video_format_to_string(format));
        goto error;
    }

    /* ...add fake memory to the buffer */
    for (i = 0; i < n_planes; i++)
    {
        /* ...the memory allocated in that way cannot be used by GPU */
        m = gst_dmabuf_allocator_alloc(allocator, dmabuf[i], 0);

        /* ...but we still need to put some memory to the buffer to mark buffer is allocated */
        gst_buffer_append_memory(buffer, m);
    }

#if 0    
    /* ...add video metadata (not needed really - then why?) */
    gst_buffer_add_video_meta_full(buffer, GST_VIDEO_FRAME_FLAG_NONE, format,
                                   width, height, n_planes, offset, stride);
#endif

    return buffer;

error_user:
    TRACE(ERROR, _b("buffer creation rejected by user"));
    goto error;

error_planes:
    TRACE(ERROR, _b("invalid number of planes for format '%s': %d"), gst_video_format_to_string(format), n_planes);
    goto error;

error:
    /* ...release buffer */
    gst_object_unref(buffer);
    return NULL;
}

/* ...buffer probing callback */
static GstPadProbeReturn vsink_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    video_sink_t   *sink = user_data;
    
    TRACE(0, _b("video-sink[%p]: probe <%X, %lu, %p, %zX, %u>"), sink, info->type, info->id, info->data, info->offset, info->size);

    if (info->type & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM)
    {
        GstQuery        *query = GST_PAD_PROBE_INFO_QUERY(info);

        if (GST_QUERY_TYPE(query) == GST_QUERY_ALLOCATION)
        {
            GstBufferPool          *pool = sink->pool;
            GstAllocator           *allocator = NULL;
            GstCaps                *caps;
            GstAllocationParams     params;
            guint                   size, min = 4, max = 4;
            GstVideoInfo            vinfo;
            GstStructure           *config;
            gboolean                need_pool;
            gchar                  *str;
            
            /* ...parse allocation parameters from the query */
            gst_allocation_params_init(&params);
            gst_query_parse_allocation(query, &caps, &need_pool);
            gst_video_info_from_caps(&vinfo, caps);

            str = gst_caps_to_string(caps);
            TRACE(DEBUG, _b("caps={%s}; need-pool=%d, writable:%d"), str, need_pool, gst_query_is_writable(query));
            free(str);

            /* ...check if we already have a pool allocated */
            if (pool)
            {
                GstCaps    *_caps;
                
                config = gst_buffer_pool_get_config(pool);
                gst_buffer_pool_config_get_params(config, &_caps, &size, NULL, NULL);
                
                if (!gst_caps_is_equal(caps, _caps))
                {
                    TRACE(INFO, _b("caps are different; destroy pool"));
                    gst_object_unref(pool);
                    sink->pool = pool = NULL;
                }
                else
                {
                    TRACE(DEBUG, _b("caps are same"));
                }

                gst_structure_free(config);
            }
            
            /* ...allocate pool if needed */
            if (pool == NULL && need_pool)
            {
                /* ...create new buffer pool */
                sink->pool = pool = gst_buffer_pool_new();
                min = max = 4;
                size = vinfo.size;

                TRACE(DEBUG, _b("pool allocated: %u/%u/%u"), size, min, max);

                /* ...configure buffer pool */
                config = gst_buffer_pool_get_config(pool);
                gst_buffer_pool_config_set_params(config, caps, size, min, max);
                gst_structure_set(config, "videosink_buffer_creation_request_supported", G_TYPE_BOOLEAN, TRUE, NULL);
                gst_buffer_pool_config_set_allocator(config, NULL, &params);
                gst_buffer_pool_set_config(pool, config);
            }

            /* ...add allocation pool description */
            if (pool)
            {
                /* ...add allocation pool */
                gst_query_add_allocation_pool(query, pool, size, min, max);

                /* ...set allocator parameters */
                gst_query_add_allocation_param(query, gst_allocator_find(NULL), &params);

                TRACE(1, _b("query: %p added pool %p, allocator: %p"), query, pool, gst_allocator_find(NULL));
                
                /* ...create DMA allocator as well (hmm? doesn't look great) */
                allocator = gst_dmabuf_allocator_new();
                //gst_query_add_allocation_param(query, allocator, &params);
                gst_object_unref(allocator);
            }

            /* ...output query parameters */
            TRACE(1, _b("query[%p]: alloc: %d, pools: %d"), query, gst_query_get_n_allocation_params(query), gst_query_get_n_allocation_pools(query));
            
            /* ...do not pass allocation request to the component */
            return GST_PAD_PROBE_DROP;
        }
        else if (GST_QUERY_TYPE(query) == GST_QUERY_CUSTOM)
        {
            const GstStructure  *structure = gst_query_get_structure(query);
            GstStructure    *str_writable = gst_query_writable_structure(query);
            gint dmabuf[GST_VIDEO_MAX_PLANES] = { 0 };
            GstAllocator *allocator;
            gint width, height;
            gint stride[GST_VIDEO_MAX_PLANES] = { 0 };
            gpointer planebuf[GST_VIDEO_MAX_PLANES] = { 0 };
            const gchar *str;
            const GValue *p_val;
            GValue val = { 0, };
            GstVideoFormat format;
            GstBuffer *buffer;
            GArray *dmabuf_array;
            GArray *stride_array;
            GArray *planebuf_array;
            gint n_planes;
            gint i;
            
            if (!structure || !gst_structure_has_name (structure, "videosink_buffer_creation_request"))
            {
                TRACE(DEBUG, _b("unknown query"));
                return GST_PAD_PROBE_DROP;
            }

            /* ...retrieve allocation paameters from the structure */
            gst_structure_get (structure, 
                               "width", G_TYPE_INT, &width,
                               "height", G_TYPE_INT, &height, 
                               "stride", G_TYPE_ARRAY, &stride_array,
                               "dmabuf", G_TYPE_ARRAY, &dmabuf_array,
                               "planebuf", G_TYPE_ARRAY, &planebuf_array,
                               "n_planes", G_TYPE_INT, &n_planes,
                               "allocator", G_TYPE_POINTER, &p_val,
                               "format", G_TYPE_STRING, &str, NULL);

            allocator = (GstAllocator *) g_value_get_pointer(p_val);
            g_assert(allocator);

            format = gst_video_format_from_string(str);
            g_assert(format != GST_VIDEO_FORMAT_UNKNOWN);

            for (i = 0; i < n_planes; i++)
            {
                dmabuf[i] = g_array_index (dmabuf_array, gint, i);
                stride[i] = g_array_index (stride_array, gint, i);
                planebuf[i] = g_array_index (planebuf_array, gpointer, i);
                TRACE(DEBUG, _b("plane-%d: dmabuf=%d, stride=%d, planebuf=%p"), i, dmabuf[i], stride[i], planebuf[i]);
            }

            buffer = vsink_buffer_create(sink, dmabuf, allocator, width, height, stride, planebuf, format, n_planes);
            g_value_init(&val, GST_TYPE_BUFFER);
            gst_value_set_buffer(&val, buffer);
            gst_buffer_unref(buffer);

            //str_writable = gst_query_writable_structure(query);
            gst_structure_set_value (str_writable, "buffer", &val);

            /* ...do not pass allocation request to the component */
            return GST_PAD_PROBE_DROP;
        }
    }
    else if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
    {
        TRACE(0, _b("event-dowstream"));
    }

    /* ...everything else is passed to the sink? */
    return GST_PAD_PROBE_OK;
}

/*******************************************************************************
 * Video sink implementation
 ******************************************************************************/

/* ...end-of-stream submission */
static void vsink_eos(GstAppSink *appsink, gpointer user_data)
{
    video_sink_t   *sink = user_data;

    TRACE(DEBUG, _b("video-sink[%p]::eos called"), sink);
}

/* ...new preroll sample is available */
static GstFlowReturn vsink_new_preroll(GstAppSink *appsink, gpointer user_data)
{
    video_sink_t   *sink = user_data;
    GstSample      *sample;
    GstBuffer      *buffer;
    int             r;

    TRACE(DEBUG, _b("video-sink[%p]::new-preroll called"), sink);

    /* ...retrieve new sample from the pipe */
    sample = gst_app_sink_pull_preroll(sink->appsink);
    buffer = gst_sample_get_buffer(sample);
    
    TRACE(0, _b("buffer: %p, timestamp: %zu"), buffer, GST_BUFFER_PTS(buffer));
 
    /* ...process frame; invoke user-provided callback - hmm; think about that a bit...*/
    //r = sink->cb->process(sink, buffer, sink->cdata);
    r = 0;

    /* ...release the sample (and buffer automatically unless user adds a reference) */
    gst_sample_unref(sample);

    return (r < 0 ? GST_FLOW_ERROR : GST_FLOW_OK);
}

/* ...new sample is available */
static GstFlowReturn vsink_new_sample(GstAppSink *appsink, gpointer user_data)
{
    video_sink_t   *sink = user_data;
    GstSample      *sample;
    GstBuffer      *buffer;
    int             r;
    
    TRACE(DEBUG, _b("video-sink[%p]::new-samples called"), sink);

    /* ...retrieve new sample from the pipe */
    sample = gst_app_sink_pull_sample(sink->appsink);
    buffer = gst_sample_get_buffer(sample);
    
    TRACE(0, _b("buffer: %p, timestamp: %zu"), buffer, GST_BUFFER_PTS(buffer));
 
    /* ...process frame; invoke user-provided callback */
    r = sink->cb->process(sink, buffer, sink->cdata);

    /* ...release the sample (and buffer automatically unless user adds a reference) */
    gst_sample_unref(sample);

    return (r < 0 ? GST_FLOW_ERROR : GST_FLOW_OK);
}

/* ...element destruction notification */
static void vsink_destroy(gpointer data)
{
    video_sink_t   *sink = data;

    TRACE(INIT, _b("video-sink[%p] destroy notification"), sink);

    /* ...destroy buffer pool if allocated (doesn't look great - memleaks - tbd) */
    (sink->pool ? gst_object_unref(sink->pool) : 0);

    TRACE(INIT, _b("video-sink[%p] deallocate"), sink);

    free(sink);

    TRACE(INIT, _b("video-sink[%p] destroyed"), sink);
}

/* ...sink node callbacks */
static GstAppSinkCallbacks  vsink_callbacks = 
{
    .eos = vsink_eos,
    .new_preroll = vsink_new_preroll,
    .new_sample = vsink_new_sample,
};

/*******************************************************************************
 * Public API
 ******************************************************************************/

/* ...create custom video sink node */
video_sink_t * video_sink_create(GstCaps *caps, const vsink_callback_t *cb, void *cdata)
{
    video_sink_t   *sink;
    GstPad         *pad;
    
    /* ...allocate data */
    CHK_ERR(sink = malloc(sizeof(*sink)), NULL);

    /* ...create application source item */
    if ((sink->appsink = (GstAppSink *)gst_element_factory_make("appsink", NULL)) == NULL)
    {
        TRACE(ERROR, _x("element creation failed"));
        goto error;
    }

    /* ...by default, consume buffers as fast as they arrive */
    g_object_set(G_OBJECT(sink->appsink), "sync", FALSE, NULL);

    /* ...reset pool handle */
    sink->pool = NULL;

    /* ...set processing function */
    sink->cb = cb, sink->cdata = cdata;

    /* ...bind control interface */
    gst_app_sink_set_callbacks(sink->appsink, &vsink_callbacks, sink, vsink_destroy);

    /* ...set stream capabilities */
    gst_app_sink_set_caps(sink->appsink, caps);

    /* ...set probing function for output pad to intercept allocations */
    pad = gst_element_get_static_pad(GST_ELEMENT(sink->appsink), "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_ALL_BOTH, vsink_probe, sink, NULL);
    gst_object_unref(pad);

    TRACE(INIT, _b("video-sink[%p] created"), sink);

    return sink;

error:
    /* ...destroy data allocated */
    free(sink); 
    return NULL;
}

/* ...retrieve GStreamer element node */
GstElement * video_sink_element(video_sink_t *sink)
{
    return (GstElement *)sink->appsink;
}
