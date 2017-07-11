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
#include "utest-vin.h"
#include "utest-imr.h"
#include "utest-mesh.h"
#include "utest-meta.h"
#include "utest-math.h"
#include "utest-compositor.h"
#include <linux/videodev2.h>
#include <pango/pangocairo.h>
#include <math.h>
#include "sv/svlib.h"
#include "objdet.h"
#include "utest-imr-sv.h"
#include "utest-gui.h"

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * Local constants definitions
 ******************************************************************************/

/* ...VIN devices identifiers */
#define VIN_SV_LEFT                     0
#define VIN_SV_RIGHT                    1
#define VIN_SV_FRONT                    2
#define VIN_SV_REAR                     3
#define VIN_DM                          4
#define VIN_SC_LEFT                     5
#define VIN_SC_RIGHT                    6
#define VIN_SC_REAR                     7
#define VIN_NUMBER                      8

/* ...IMR engines identifiers */
#define IMR_SC_LEFT                     0
#define IMR_SC_RIGHT                    1
#define IMR_SC_REAR                     2
#define IMR_DM                          3
#define IMR_NUMBER                      4

/* ...surround-view output stream */
#define OUT_SV                          0
#define OUT_DM                          1
#define OUT_SC_LEFT                     2
#define OUT_SC_RIGHT                    3
#define OUT_SC_REAR                     4
#define OUT_NUMBER                      5

/* ...buffer pool size for smart-cameras */
#define IMR_POOL_SIZE                   2

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/

/* ...joystick device name */
extern char  *joystick_dev_name;

/* ...output devices for main window */
extern int __output_main;

/* ...VIN device names */
extern char * vin_dev_name[];

/* ...IMR device names */
extern char * imr_dev_name[];

/* ...mesh data (tbd - move to track configuration) */
extern char * __mesh_file_name;

/* ...camera format */
extern u32  __vin_format;

/* ...camera dimensions */
extern int  __vin_width, __vin_height;

/* ...number of buffers to allocate */
extern int  __vin_buffers_num;

/* ...output buffer dimensions */
extern int  __vsp_width, __vsp_height;

/* ...car buffer dimensions */
extern int  __car_width, __car_height;

/* ...car shadow region */
extern __vec4   __shadow_rect;

/* ...steps for rotation parameters */
extern int  __steps[];

/* ...model file prefix */
extern char * __model;

/* ...tbd */

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

/* ...smart-camera configuration */
typedef struct sc_cfg
{
    /* ...rotation matrix accumulators */
    __vec3              rot_acc;

    /* ...scaling factor */
    __scalar            scl_acc;

    /* ...number of milliseconds since last update */
    u32                 spnav_delta;

}   sc_cfg_t;

/* ...global application data */
struct app_data
{
    /* ...main window handle */
    window_data_t      *window;

    /* ...main execution loop */
    GMainLoop          *loop;

    /* ...GStreamer pipeline */
    GstElement         *pipe;

    /* ...camera-set container */
    GstElement         *camera;

    /* ...input stream dimensions */
    int                 width, height;

    /* ...miscellaneous control flags */
    u32                 flags;

    /* ...active sview mode */
    int                 sv_gpu_mode;

    /* ...surround-view input buffers */
    GQueue              sv_input[4], sv_output;

    /* ...driver-monitor input buffers */
    GQueue              dm_input[1], dm_output[1];
    
    /* ...smart-camera input/output buffers */
    GQueue              sc_input[3], sc_output[3];

    /* ...readiness flags */
    u32                 sv_flags, dm_flags, sc_flags;

    /* ...texture output viewports */
    texture_view_t      sv_view[4], dm_view[1][2], sc_view[3][2];

    /* ...cairo matrix for 2D-graphics */
    cairo_matrix_t      dm_mat[1];

    /* ...IMR output buffers for smart-cameras and for driver-monitor (ugly - tbd) */
    vsp_mem_t          *sc_mem[3][2], *dm_mem[6];

    /* ...smart-camera configuration data */
    sc_cfg_t            sc_cfg[3];

    /* ...IMR engines configurations */
    imr_cfg_t          *imr_cfg[IMR_NUMBER];

    /* ...data access lock */
    pthread_mutex_t     lock;

    /* ...VIN engine handle */
    vin_data_t         *vin;

    /* ...IMR engine handle */
    imr_data_t         *imr;

    /* ...surround-view engine handle */
    void               *sv, *dm, *sc, *imr_sv;
    
    /* ...frame number */
    u32                 frame_num;

    /* ...cameras mesh data */
    mesh_data_t        *mesh[IMR_NUMBER];

    /* ...current focus item */
    int                 focus;

    /* ...cameras hiding state */
    u32                 focus_mask;

    /* ...fading level for an active item */
    float               alpha;

    /* ...IMR-SV carousel menu */
    carousel_t         *imr_menu;

    /* ...focus revert / long-press detector timers */
    timer_source_t     *timer, *long_press_timer;

    /* ...last button pressing time */
    int                 spnav_long_press;
};

/*******************************************************************************
 * Helpers
 ******************************************************************************/

static inline int app_camera_is_sv(int i)
{
    return i >= VIN_SV_LEFT && i <= VIN_SV_REAR;
}

static inline int app_sv_camera_index(int i)
{
    return i - VIN_SV_LEFT;
}

static inline int app_camera_is_dm(int i)
{
    return i == VIN_DM;
}

static inline int app_dm_camera_index(int i)
{
    return i - VIN_DM;
}

static inline int app_camera_is_sc(int i)
{
    return i >= VIN_SC_LEFT && i <= VIN_SC_REAR;
}

static inline int app_sc_camera_index(int i)
{
    return i - VIN_SC_LEFT;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/

static inline int app_frame_ready(app_data_t *app)
{
    if (app->sv_gpu_mode)
    {
        return (app->sv_flags | app->dm_flags | app->sc_flags) == 0;
    }
    else
    {
        return !g_queue_is_empty(&app->sv_output);
    }
}

/*******************************************************************************
 * IMR-based surround view interface
 ******************************************************************************/

/* ...buffers processing callback */
static void imr_sv_ready(void *cdata, GstBuffer **buf)
{
    app_data_t     *app = cdata;

    TRACE(DEBUG, _b("imr-sv-engine buffer ready"));

    /* ...lock internal application data */
    pthread_mutex_lock(&app->lock);

    /* ...check if we still have IMR-mode */
    if (!app->sv_gpu_mode)
    {
        GstBuffer  *buffer = imr_sview_buf_output(buf);

        /* ...put buffer into rendering queue */
        g_queue_push_tail(&app->sv_output, gst_buffer_ref(buffer));

        /* ...all other buffers are just dropped - tbd */

        /* ...rendering is available */
        window_schedule_redraw(app->window);
    }

    /* ...release application lock */
    pthread_mutex_unlock(&app->lock);
}

/* ...engine processing callback */
static imr_sview_cb_t   imr_sv_callback = {
    .ready = imr_sv_ready,
};
    
/*******************************************************************************
 * Surround view interface
 ******************************************************************************/

/* ...trigger processing of surround-view scene */
static int sv_input_process(app_data_t *app, int i, GstBuffer *buffer)
{
    /* ...collect buffers */
    g_queue_push_tail(&app->sv_input[i], gst_buffer_ref(buffer));

    /* ...check if we have sufficient amount of buffers */
    if ((app->sv_flags &= ~(1 << i)) == 0)
    {
        /* ...check if IMR- or GPU-based engine is active */
        if (app->sv_gpu_mode)
        {
            /* ...processing is done in a renderer context */
            if (app_frame_ready(app))
            {
                window_schedule_redraw(app->window);
            }
        }
        else
        {
            GstBuffer  *buf[4];
            
            /* ...collect buffers from input queue (it is a common function) */
            for (i = 0; i < 4; i++)
            {
                buf[i] = g_queue_pop_head(&app->sv_input[i]);

                /* ...update readiness flag */
                (g_queue_is_empty(&app->sv_input[i]) ? app->sv_flags |= (1 << i) : 0);
            }

            /* ...submit buffers to the engine */
            CHK_API(imr_sview_submit(app->imr_sv, buf));

            /* ...release buffers ownership */
            for (i = 0; i < 4; i++)
            {
                gst_buffer_unref(buf[i]);
            }
        }
    }

    return 0;
}

/*******************************************************************************
 * Driver monitor interface
 ******************************************************************************/

static int dm_input_process(app_data_t *app, int i, GstBuffer *buffer)
{
    vsink_meta_t       *vmeta = gst_buffer_get_vsink_meta(buffer);
    objdet_meta_t      *ometa = gst_buffer_get_objdet_meta(buffer);
    texture_data_t     *texture = (texture_data_t *)vmeta->priv;

    if (app->sv_gpu_mode)
    {
        TRACE(DEBUG, _b("dm-%d: buffer received"), i);

        /* ...submit buffer to driver-monitor engine */
        if (objdet_engine_push_buffer(app->dm, buffer, vmeta->plane[0], &ometa->info, &ometa->scene, texture->image, vmeta->format))
        {
            TRACE(ERROR, _x("dm-%d: failed to submit buffer"), i);
        }
        else
        {
            gst_buffer_ref(buffer);
        }
    }
    
    return 0;
}

/* ...object-detection library buffer processing callback */
static void dm_buffer_ready(void *cdata, void *cookie, road_scene_t *scene)
{
    app_data_t     *app = cdata;
    GstBuffer      *buffer = cookie;
    int             i = 0;
    
    TRACE(DEBUG, _b("buffer returned from engine: %p"), buffer);

    /* ...lock data access */
    pthread_mutex_lock(&app->lock);

    /* ...drop buffer if we are in the IMR-mode */
    if (app->sv_gpu_mode)
    {
        /* ...submit buffer to the ready queue */
        g_queue_push_tail(&app->dm_output[i], gst_buffer_ref(buffer));

        /* ...mark buffer queue is not empty */
        if ((app->dm_flags &= ~(1 << i)) == 0)
        {
            if (app_frame_ready(app))
            {
                window_schedule_redraw(app->window);
            }
        }
    }
    else
    {
        TRACE(DEBUG, _b("dm-%d: drop detector output"), i);
    }

    /* ...unlock data access */
    pthread_mutex_unlock(&app->lock);
}

/* ...buffer retirement hook */
static void dm_buffer_release(void *cdata, void *cookie)
{
    GstBuffer      *buffer = cookie;

    /* ...release buffer handle */
    gst_buffer_unref(buffer);
}

/* ...error processing hook (tbd - need that at all?) */
static void dm_buffer_error(void *cdata, int error)
{
    TRACE(INFO, _b("objdet-engine reported error: %d"), error);
}

/* ...object-detection engine callback */
static objdet_callback_t dm_callback = {
    .ready = dm_buffer_ready,
    .release = dm_buffer_release,
    .error = dm_buffer_error,
};

/*******************************************************************************
 * Smart camera interface
 ******************************************************************************/

static int sc_input_process(app_data_t *app, int i, GstBuffer *buffer)
{
    TRACE(DEBUG, _b("sc-%d: buffer received"), i);

    if (app->sv_gpu_mode)
    {        
        /* ...pass buffer to IMR engine */
        CHK_API(imr_engine_push_buffer(app->imr, i, buffer));
    }

    return 0;
}

/* ...deallocate texture data */
static void __destroy_imr_buffer(gpointer data, GstMiniObject *obj)
{
    GstBuffer      *buffer = (GstBuffer *)obj;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);

    /* ...destroy texture data */
    if (meta->priv2)
            texture_destroy(meta->priv2);

    TRACE(DEBUG, _b("imr-buffer <%d:%d> destroyed"), meta->id, meta->index);
}

/* ...IMR buffer allocation */
static int imr_buffer_allocate(void *cdata, int i, GstBuffer *buffer)
{
    app_data_t     *app = cdata;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    int             w = meta->width, h = meta->height, format = meta->format;
    int             j = meta->index;
    void           *planes[3] = { NULL, };

    if (i < IMR_DM)
    {
        /* ...sanity check */
        BUG((u32)j >= (u32)2, _x("imr-%d: invalid buffer index: %d"), i, j);
        
        /* ...smart-camera interface; setup buffer output memory */
        meta->priv = app->sc_mem[i][j];
    }
    else
    {
        objdet_meta_t  *ometa;

        BUG(1, _x("invalid transaction"));

        /* ...sanity check */
        BUG((u32)j >= (u32)6, _x("imr-%d: invalid buffer index: %d"), i, j);

        /* ...driver-monitor interface; allocate objdet metadata */
        CHK_ERR(ometa = gst_buffer_add_objdet_meta(buffer), -errno);
        GST_META_FLAG_SET(ometa, GST_META_FLAG_POOLED);

        /* ...set buffer output */
        meta->priv = app->dm_mem[j];
    }

    /* ...attach buffer data */
    planes[0] = meta->buf->data = vsp_mem_ptr(meta->priv);

    /* ...create external texture */
    CHK_ERR(meta->priv2 = texture_create(w, h, planes, format), -errno);
    
    /* ...add custom buffer destructor */
    gst_mini_object_weak_ref(GST_MINI_OBJECT(buffer), __destroy_imr_buffer, app);

    TRACE(INFO, _b("imr-buffer <%d:%d> allocated: %u*%u (format=%u)"), i, meta->index, w, h, format);

    return 0;
}

/* ...IMR buffer preparation callback */
static int imr_buffer_prepare(void *cdata, int i, GstBuffer *buffer)
{
    /* ...do nothing */
    return 0;
}

/* ...IMR buffer completion callback */
static int imr_buffer_process(void *cdata, int i, GstBuffer *buffer)
{
    app_data_t     *app = cdata;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);

    /* ...put buffer into corresponding queue */
    TRACE(DEBUG, _b("imr-buffer <%d:%d> ready"), i, meta->index);

    /* ...sanity check */
    CHK_ERR((u32)i < (u32)IMR_NUMBER, -(errno = EINVAL));

    /* ...lock application data */
    pthread_mutex_lock(&app->lock);

    /* ...processing is enabled only in the GPU-mode */
    if (app->sv_gpu_mode)
    {
        /* ...check buffer type */
        if (i < IMR_DM)
        {
            /* ...smart-camera interface; put buffer into rendering queue */
            g_queue_push_tail(&app->sc_output[i], gst_buffer_ref(buffer));
    
            /* ...clear flag */
            if ((app->sc_flags &= ~(1 << i)) == 0)
            {
                /* ...schedule rendering if possible */
                if (app_frame_ready(app))
                {
                    window_schedule_redraw(app->window);
                }
            }
        }
        else
        {
            objdet_meta_t      *ometa = gst_buffer_get_objdet_meta(buffer);
            texture_data_t     *texture = (texture_data_t *)meta->priv2;
            vsp_mem_t          *mem = meta->priv;

            BUG(1, _x("invalid transaction"));

            /* ...driver-monitor interface; pass buffer to the object detection engine */
            if (objdet_engine_push_buffer(app->dm, buffer, vsp_mem_ptr(mem), &ometa->info, &ometa->scene, texture->image, meta->format))
            {
                TRACE(ERROR, _x("dm-%d: failed to submit buffer"), i - IMR_DM);
            }
            else
            {
                gst_buffer_ref(buffer);
            }
        }
    }
    
    /* ...release application lock */
    pthread_mutex_unlock(&app->lock);

    return 0;
}

/* ...IMR engine callback structure */
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
static int app_input_alloc(void *data, int i, GstBuffer *buffer)
{
    app_data_t     *app = data;
    vsink_meta_t   *vmeta = gst_buffer_get_vsink_meta(buffer);
    int             w = vmeta->width, h = vmeta->height;

    if (i >= VIN_SV_LEFT && i <= VIN_SV_REAR)
    {
        /* ...initialize surround-view application as needed - tbd */
        BUG(w != 1280 || h != 1080, _x("camera-%d: invalid buffer dimensions: %d*%d"), i, w, h);
    }
    else if (i == VIN_DM)
    {
        objdet_meta_t  *ometa;

        /* ...initialize driver-monitor application */
        BUG(w != 640 || h != 400, _x("camera-%d: invalid buffer dimensions: %d*%d"), i, w, h);

        /* ...driver-monitor interface; allocate objdet metadata */
        CHK_ERR(ometa = gst_buffer_add_objdet_meta(buffer), -errno);
        GST_META_FLAG_SET(ometa, GST_META_FLAG_POOLED);
    }
    else if (i >= VIN_SC_LEFT && i <= VIN_SC_REAR)
    {
        /* ...initialize smart-camera library */
        /* BUG(w != 1280 || h != 1080, _x("camera-%d: invalid buffer dimensions: %d*%d"), i, w, h); */
    }

    /* ...allocate texture to wrap the buffer */
    CHK_ERR(vmeta->priv = texture_create(w, h, vmeta->plane, vmeta->format), -errno);

    /* ...add custom destructor to the buffer */
    gst_mini_object_weak_ref(GST_MINI_OBJECT(buffer), __destroy_vsink_texture, app);

    /* ...do not need to do anything with the buffer allocation? */
    TRACE(INFO, _b("camera-%d: input buffer %p allocated"), i, buffer);

    return 0;
}

/* ...process new input buffer submitted from camera */
static int app_input_process(void *data, int i, GstBuffer *buffer)
{
    app_data_t     *app = data;
    vsink_meta_t   *vmeta = gst_buffer_get_vsink_meta(buffer);
    int             r;

    TRACE(DEBUG, _b("camera-%d: input buffer received"), i);

    /* ...make sure camera index is valid */
    CHK_ERR(i >= 0 && i < VIN_NUMBER, -EINVAL);

    /* ...make sure buffer dimensions are valid */
    CHK_ERR(vmeta, -EINVAL);

    /* ...lock access to the internal queue */
    pthread_mutex_lock(&app->lock);

    /* ...pass buffer to particular receiver */
    if (app_camera_is_sv(i))
    {
        r = sv_input_process(app, app_sv_camera_index(i), buffer);
    }
    else if (app_camera_is_dm(i))
    {
        r = dm_input_process(app, app_dm_camera_index(i), buffer);
    }
    else if (app_camera_is_sc(i))
    {
        r = sc_input_process(app, app_sc_camera_index(i), buffer);
    }
    else
    {
        TRACE(ERROR, _x("invalid camera index: %d"), i);
        r = -EINVAL;
    }

    /* ...unlock internal data */
    pthread_mutex_unlock(&app->lock);

    return r;
}

/* ...callbacks for camera back-end */
static camera_callback_t vin_cb = {
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

/* ...draw multiline string - hmm; need to put that stuff into display */
static inline void draw_string(cairo_t *cr, const char *fmt, ...)
{
    char                    buffer[4096], *p, *end;
    cairo_text_extents_t    text_extents;
    cairo_font_extents_t    font_extents;
    va_list                 argp;

    cairo_save(cr);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 40);
    cairo_font_extents(cr, &font_extents);

    va_start(argp, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, argp);
    va_end(argp);

    for (p = buffer; *p; p = end + 1)
    {
        /* ...output single line */
        ((end = strchr(p, '\n')) != NULL ? *end = 0 : 0);
        cairo_show_text(cr, p);
        cairo_text_extents (cr, p, &text_extents);
        cairo_rel_move_to (cr, -text_extents.x_advance, font_extents.height);
        TRACE(0, _b("print text-line: <%f,%f>"), text_extents.x_advance, font_extents.height);

        /* ...stop when last line processes */
        if (!end)
        {
            break;
        }
    }

    /* ...restore drawing context */
    cairo_restore(cr);
}

/*******************************************************************************
 * Runtime initialization
 ******************************************************************************/

static inline void gl_dump_state(void)
{
    GLint       iv[4];
    GLfloat     fv[4];
    GLboolean   bv[4];
    
    glGetIntegerv(GL_DEPTH_BITS, iv);
    TRACE(1, _b("depth-bits: %u"), iv[0]);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, fv);
    TRACE(1, _b("depth-clear-value: %f"), fv[0]);
    glGetIntegerv(GL_DEPTH_FUNC, iv);
    TRACE(1, _b("depth-func: %u"), iv[0]);
    glGetFloatv(GL_DEPTH_RANGE, fv);
    TRACE(1, _b("depth-range: %f/%f"), fv[0], fv[1]);
    glGetBooleanv(GL_DEPTH_TEST, bv);
    TRACE(1, _b("GL_DEPTH_TEST: %d"), bv[0]);
    glGetBooleanv(GL_DEPTH_WRITEMASK, bv);
    TRACE(1, _b("GL_DEPTH_WRITEMASK: %d"), bv[0]);
    glGetIntegerv(GL_ACTIVE_TEXTURE, iv);
    TRACE(1, _b("GL_ACTIVE_TEXTURE: %X"), iv[0]);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, iv);
    TRACE(1, _b("GL_ELEMENT_ARRAY_BUFFER_BINDING: %d"), iv[0]);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, iv);
    TRACE(1, _b("GL_FRAMEBUFFER_BINDING: %d"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_FAIL, iv);
    TRACE(1, _b("GL_STENCIL_BACK_FAIL: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_FUNC, iv);
    TRACE(1, _b("GL_STENCIL_BACK_FUNC: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_FAIL, iv);
    TRACE(1, _b("GL_STENCIL_BACK_PASS_DEPTH_FAIL: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, iv);
    TRACE(1, _b("GL_STENCIL_BACK_PASS_DEPTH_PASS: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_REF, iv);
    TRACE(1, _b("GL_STENCIL_BACK_REF: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, iv);
    TRACE(1, _b("GL_STENCIL_BACK_VALUE_MASK: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, iv);
    TRACE(1, _b("GL_STENCIL_BACK_WRITEMASK: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_BITS, iv);
    TRACE(1, _b("GL_STENCIL_BITS: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_CLEAR_VALUE, iv);
    TRACE(1, _b("GL_STENCIL_CLEAR_VALUE: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_FAIL, iv);
    TRACE(1, _b("GL_STENCIL_FAIL: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_FUNC, iv);
    TRACE(1, _b("GL_STENCIL_FUNC: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, iv);
    TRACE(1, _b("GL_STENCIL_PASS_DEPTH_FAIL: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, iv);
    TRACE(1, _b("GL_STENCIL_PASS_DEPTH_PASS: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_REF, iv);
    TRACE(1, _b("GL_STENCIL_REF: %u"), iv[0]);
    glGetBooleanv(GL_STENCIL_REF, bv);
    TRACE(1, _b("GL_STENCIL_REF: %d"), bv[0]);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, iv);
    TRACE(1, _b("GL_STENCIL_VALUE_MASK: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_WRITEMASK, iv);
    TRACE(1, _b("GL_STENCIL_WRITEMASK: %u"), iv[0]);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, iv);
    TRACE(1, _b("GL_TEXTURE_BINDING_2D: %u"), iv[0]);
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, iv);
    TRACE(1, _b("GL_TEXTURE_BINDING_CUBE_MAP: %u"), iv[0]);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, iv);
    TRACE(1, _b("GL_UNPACK_ALIGNMENT: %u"), iv[0]);
    glGetIntegerv(GL_VIEWPORT, iv);
    TRACE(1, _b("GL_VIEWPORT: %u/%u/%u/%u"), iv[0], iv[1], iv[2], iv[3]);
    glGetIntegerv(GL_SCISSOR_BOX, iv);
    TRACE(1, _b("GL_SCISSOR_BOX: %u/%u/%u/%u"), iv[0], iv[1], iv[2], iv[3]);
    glGetBooleanv(GL_SCISSOR_TEST, bv);
    TRACE(1, _b("GL_SCISSOR_TEST: %d"), bv[0]);
    glGetBooleanv(GL_COLOR_WRITEMASK, bv);
    TRACE(1, _b("GL_COLOR_WRITEMASK: %d/%d/%d/%d"), bv[0], bv[1], bv[2], bv[3]);
    glGetBooleanv(GL_CULL_FACE, bv);
    TRACE(1, _b("GL_CULL_FACE: %d"), bv[0]);
    glGetIntegerv(GL_CULL_FACE_MODE, iv);
    TRACE(1, _b("GL_CULL_FACE_MODE: %u"), iv[0]);                    
}


/* ...redraw main application window */
static void app_redraw(display_data_t *display, void *data)
{
    app_data_t     *app = data;
    window_data_t  *window = app->window;
    int             w = window_get_width(window);
    int             h = window_get_height(window);

    /* ...lock internal data */
    pthread_mutex_lock(&app->lock);

    while (app_frame_ready(app))
    {
        float       fps = window_frame_rate_update(window);
        cairo_t    *cr = NULL;
        GstBuffer  *sv_buffer[4] = { NULL }, *dm_buffer[1] = { NULL }, *sc_buffer[3] = { NULL };
        GstBuffer  *sv_output = NULL;
        int         i;
        int         sv_gpu_mode = app->sv_gpu_mode;
        
        if (sv_gpu_mode)
        {
            /* ...get the buffers from the head of corresponding queue */
            for (i = 0; i < 4; i++)
            {
                BUG(g_queue_is_empty(&app->sv_input[i]), _x("queue-%d is empty"), i);

                /* ...get the head of the queue */
                sv_buffer[i] = g_queue_pop_head(&app->sv_input[i]);

                /* ...update readiness flag */
                (g_queue_is_empty(&app->sv_input[i]) ? app->sv_flags |= (1 << i) : 0);
            }

            /* ....get the driver monitor buffer */
            for (i = 0; i < 1; i++)
            {
                BUG(g_queue_is_empty(&app->dm_output[i]), _x("queue-%d is empty"), i);
            
                /* ...get the head of the queue */
                dm_buffer[i] = g_queue_pop_head(&app->dm_output[i]);

                /* ...update readiness flag */
                (g_queue_is_empty(&app->dm_output[i]) ? app->dm_flags |= (1 << i) : 0);
            }

            /* ...get the smart-camera buffer */
            for (i = 0; i < 3; i++)
            {
                BUG(g_queue_is_empty(&app->sc_output[i]), _x("queue-%d is empty"), i);

                /* ...get the head of the queue */
                sc_buffer[i] = g_queue_pop_head(&app->sc_output[i]);

                /* ...update readiness flag */
                (g_queue_is_empty(&app->sc_output[i]) ? app->sc_flags |= (1 << i) : 0);
            }

            /* ...drop all pending IMR-SV buffers as needed */
            while (!g_queue_is_empty(&app->sv_output))
            {
                gst_buffer_unref(g_queue_pop_head(&app->sv_output));
            }
        }
        else
        {
            /* ...output queue must not be empty */
            BUG(g_queue_is_empty(&app->sv_output), _x("sv-output-queue is empty"));

            /* ...get the buffer from head of the ready queue */
            sv_output = g_queue_pop_head(&app->sv_output);

            /* ...drop all pending driver monitor buffers */
            for (i = 0; i < 1; i++)
            {
                while (!g_queue_is_empty(&app->dm_output[i]))
                {
                    gst_buffer_unref(g_queue_pop_head(&app->dm_output[i]));
                }
                
                /* ...update readiness flag */
                app->dm_flags |= (1 << i);
            }

            /* ...drop all pending smart-camera buffers */
            for (i = 0; i < 3; i++)
            {
                while (!g_queue_is_empty(&app->sc_output[i]))
                {
                    gst_buffer_unref(g_queue_pop_head(&app->sc_output[i]));
                }
                
                /* ...update readiness flag */
                app->sc_flags |= (1 << i);
            }
        }

        /* ...release the lock */
        pthread_mutex_unlock(&app->lock);

	cr = window_get_cairo(window);
        /* ...add some performance monitors here - tbd */
        TRACE(INFO, _b("redraw frame: %u"), app->frame_num++);

        /* ...clear window */
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClearDepthf(1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        /* ...process surround-view frames */
        if (sv_gpu_mode)
        {
            GLuint              tex[4];
            void               *plane[4];
            s64                 ts = 0;
	    /* void               *egl_images[4] = {NULL, NULL, NULL, NULL}; */
	    VehicleState       vehicle_info;

	    memset(&vehicle_info, 0, sizeof(vehicle_info));
	    
            /* ...collect the textures */
            for (i = 0; i < 4; i++)
            {
                vsink_meta_t   *vmeta = gst_buffer_get_vsink_meta(sv_buffer[i]);
                texture_data_t *texture = vmeta->priv;
                
                tex[i] = texture->tex;
                plane[i] = texture->data[0];
                ts += GST_BUFFER_DTS(sv_buffer[i]);
            }

            /* ...get average timestamp */
            ts /= 4;
            
            /* ...render surround-view scene */
	    /* cairo_surface_flush(cairo_get_target(cr)); */
	    sview_engine_process(app->sv, tex, (const uint8_t **)plane, /* egl_images,  */&vehicle_info);	    

	    glDisable(GL_CULL_FACE);

	    /* cairo_surface_mark_dirty(cairo_get_target(cr)); */

            /* ...draw border only if item is in focus */
            if (app->focus == 0)
            {
                if (app->alpha > 0)
                {
                    texture_view_t      inner, outer;
                    u32                 c0 = __app_cfg.sv_border.c0;
                    u32                 c1 = __app_cfg.sv_border.c1;
                    float               s = __app_cfg.sv_border.sharpness;

                    texture_set_view(&inner, 0.0, 0.0, 1.0, 1.0);
                    texture_set_view(&outer, 0.03 * h / w, 0.01, 1.0 - 0.03 * h / w, 0.99);
                    border_draw(&inner, &outer, c0 + (u32)(0xFF * app->alpha), c1, s);

                    TRACE(0, _b("main window fading: %f"), app->alpha);

                    /* ...exponentially decrease alpha to 0 */
                    ((app->alpha -= app->alpha * 1.0 / 15.0) < 0.01 ? app->alpha = 0 : 0);
                }
            }
        }
        else
        {
            imr_meta_t *meta = gst_buffer_get_imr_meta(sv_output);
            
            /* ...output the composited image */
            texture_draw(meta->priv2, NULL, NULL, 1.0);

            /* ...output the carousel menu on top */
            (app->imr_menu ? carousel_draw(app->imr_menu) : 0);
        }

        /* ...process driver monitor buffers */
        if (sv_gpu_mode)
        {
            /* ...save current cairo context */
            cairo_save(cr);

            for (i = 0; i < 1; i++)
            {
                vsink_meta_t     *vmeta = gst_buffer_get_vsink_meta(dm_buffer[i]);
                objdet_meta_t  *ometa = gst_buffer_get_objdet_meta(dm_buffer[i]);
                u32             c0 = 0xD2691E00, c1 = 0x00000000;
                float           s;
                int             mask = 1 << i;
                float           alpha;

                /* ...select color and transparency depending on focus and fading level */
                if (app->focus == 1 + i)
                {
                    /* ...border color gradient gradually fades away */
                    c0 = __app_cfg.sc_active_border.c0 + (u32)(0xFF * app->alpha);
                    c1 = __app_cfg.sc_active_border.c1;
                    s = __app_cfg.sc_active_border.sharpness;
                    
                    /* ...decrement alpha gradually from 1.0 to 0.7 */
                    if (app->alpha > 0.7)
                    {
                        ((app->alpha -= (app->alpha - 0.7) * 1.0 / 15) < 0.71 ? app->alpha = 0.7 : 0);
                    }

                    /* ...if item is inactive, draw with a half-level of alpha */
                    alpha = (app->focus_mask & mask ? 0.5 : 1.0);
                }
                else
                {
                    /* ...item is not in focus; draw black border with transparency 0.7 */
                    c0 = __app_cfg.sc_inactive_border.c0 + (u32)(0xFF * 0.7);
                    c1 = __app_cfg.sc_inactive_border.c1;
                    s = __app_cfg.sc_inactive_border.sharpness;

                    /* ...if item is inactive, do not draw it at all */
                    alpha = (app->focus_mask & mask ? 0 : 0.7);
                }

                /* ...output window if enabled */
                if (alpha > 0)
                {
                    /* ...flush cairo state before switching to native rendering */
                    cairo_surface_flush(cairo_get_target(cr));

                    /* ...output driver monitor data with transparency */
                    texture_draw(vmeta->priv, &app->dm_view[i][0], NULL, alpha);

                    /* ...draw border with constant black gradient */
                    border_draw(&app->dm_view[i][0], &app->dm_view[i][1], c0, c1, s);

                    /* ...set cairo transformation matrix corresponding to the view */
                    cairo_set_matrix(cr, &app->dm_mat[i]);

                    /* ...output graphics on top */
                    objdet_engine_draw(app->dm, &ometa->scene, cr);
                }
            }

            cairo_restore(cr);
        }

        /* ...process smart-cameras */
        if (sv_gpu_mode)
        {
            for (i = 0; i < 3; i++)
            {
                imr_meta_t         *meta = gst_buffer_get_imr_meta(sc_buffer[i]);
                texture_view_t     *v = &app->sc_view[i][0];
                texture_view_t     *b = &app->sc_view[i][1];
                u32                 c0, c1;
                float               s;
                u32                 mask = 1 << (i + 1);
                float               alpha;
                
                /* ...flush surface state before switching to native rendering */
                cairo_surface_flush(cairo_get_target(cr));

                /* ...select color and transparency depending on current focus and fading level */
                if (app->focus == i + 2)
                {
                    /* ...corder gradient gradually fades away */
                    c0 = __app_cfg.sc_active_border.c0 + (u32)(0xFF * app->alpha);
                    c1 = __app_cfg.sc_active_border.c1;
                    s = __app_cfg.sc_active_border.sharpness;
                    
                    /* ...decrement alpha gradually */
                    if (app->alpha > 0.7)
                    {
                        ((app->alpha -= (app->alpha - 0.7) * 1.0 / 15) < 0.71 ? app->alpha = 0.7 : 0);
                    }

                    /* ...if item is inactive, draw with a half-level of alpha */
                    alpha = (app->focus_mask & mask ? 0.5 : 1.0);
                }
                else
                {
                    /* ...item is not in focus; draw black border with transparency 0.7 */
                    c0 = __app_cfg.sc_inactive_border.c0 + (u32)(0xFF * 0.7);
                    c1 = __app_cfg.sc_inactive_border.c1;
                    s = __app_cfg.sc_inactive_border.sharpness;

                    /* ...if item is inactive, do not draw it at all */
                    alpha = (app->focus_mask & mask ? 0 : 0.7);
                }

                /* ...draw texture if visible */
                if (alpha > 0)
                {
                    /* ...output smart-camera buffer */
                    texture_draw(meta->priv2, v, NULL, alpha);

                    /* ...and its border */
                    border_draw(v, b, c0, c1, s);
                }
            }
        }

        if (1)
        {
		/* FIXME: This is a workaround to fix Cairo's scrambled context
		 *        that causes rendering of rectangles instead of text:
		 *        Flush surface by drawing transparent rectangle */
		cairo_rectangle(cr, 0, 0, 1, 1);
		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		cairo_fill(cr);
		/* FIXME: End of workaround */

		glViewport(0, 0, window_get_width(window), window_get_height(window));

		/* ...output FPS in upper left corner */
		cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
		cairo_move_to(cr, 40, 80);
		draw_string(cr, "%.1f FPS", fps);
		
		/* draw_text(cr, "sans 12", 16, 16, "FPS: %.1f", fps); */
        }

        /* ...release drawing context */
        window_put_cairo(window, cr);
        
        /* ...submit window to composer */
        window_draw(window);

        /* ...make sure the pipeline is processed fully before dropping the buffers */
        glFinish();

        if (sv_gpu_mode)
        {
            /* ...drop surround-view buffers */
            for (i = 0; i < 4; i++)     gst_buffer_unref(sv_buffer[i]);

            /* ...drop driver monitor buffers */
            for (i = 0; i < 1; i++)     gst_buffer_unref(dm_buffer[i]);

            /* ...drop smart-camera buffers */
            for (i = 0; i < 3; i++)     gst_buffer_unref(sc_buffer[i]);
        }
        else
        {
            /* ...drop surround view buffer */
            gst_buffer_unref(sv_output);
        }

        /* ...lock internal data access */
        pthread_mutex_lock(&app->lock);
    }

    /* ...release processing lock */
    pthread_mutex_unlock(&app->lock);

    TRACE(DEBUG, _b("drawing completed"));
}

/*******************************************************************************
 * Smart-camera mesh generation
 ******************************************************************************/

/** Brown-Douady (or radial Kannala-Brandt) undistortion method

    Accepts incoming distance to center of image
    and an array of distortion coefficients

    Calculates undistortion coefficient
*/
static inline float fisheye_undistort(float r, const float *D)
{
    /* incoming ray angle */
    float theta1 = atan(r);

    float theta2 = theta1*theta1,
          theta3 = theta2*theta1,
          theta4 = theta2*theta2,
          theta5 = theta3*theta2,
          theta7 = theta5*theta2,
          theta9 = theta5*theta4;

    /* t1, t3, t7, t9 can be multiplied by D in one shot:
       float t[] = { theta3, theta5, theta7, theta9 };
       float thetaD = theta + vec4_dot(D, t);
     */

    float theta_d = theta1 +
          D[0] * theta3 + D[1] * theta5 + D[2] * theta7 + D[3] * theta9;

    return ( (r > 0.0001) ? (theta_d / r) : 1.0f);
}

/* ...mesh dimensions for smart-camera */
#define SC_MESH_SIZE_X          30
#define SC_MESH_SIZE_Y          20

/* ...mesh generation function */
static int sc_mesh_setup(app_data_t *app, int i, __mat3x3 m, const __vec4 D, const __mat3x3 K)
{
#define K(i, j)     (K)[(j) * 3 + (i)]

    float      *uv, *_uv;
    float       u, v;
    int         x, y;
    imr_cfg_t  *cfg;
    
    /* intrisic matrix K structure:
       [  fx  skew  cx ]   [ x ]
       [   0   fy   cy ] * [ y ]
       [   0    0    1 ]   [ 1 ]
    */

    float   skew = K(1, 0);
    float   fx = K(0, 0);
    float   fy = K(1, 1);
    float   cx = K(2, 0);
    float   cy = K(2, 1);

    TRACE(1, _b("skew: %f, fx: %f, fy: %f, cx: %f, cy: %f"), skew, fx, fy, cx, cy);
    TRACE(1, _b("D0: %f, D1: %f, D2: %f, D3: %f"), D[0], D[1], D[2], D[3]);

#undef K

    /* ...generate rectangular mesh */
    CHK_ERR(uv = _uv = malloc(sizeof(float) * (SC_MESH_SIZE_X + 1) * (SC_MESH_SIZE_Y + 1) * 2), -(errno = ENOMEM));
   
    /* ...generate a lattice */
    for (y = 0; y <= SC_MESH_SIZE_Y; y++)
    {
        /* ...evaluate "v" coordinate */
        v = (float)(y - SC_MESH_SIZE_Y / 2) / SC_MESH_SIZE_Y;
        
        for (x = 0; x <= SC_MESH_SIZE_X; x++)
        {
            float       distortion, im_x, im_y; 
            __vec3      a, b;
            float       _x, _y;
            
            /* ...evaluate "u" coordinate */
            u = (float)(x - SC_MESH_SIZE_X / 2) / SC_MESH_SIZE_X;

            /* ...u, v get values from [-1,+1] range; multiply */
            a[0] = u, a[1] = v, a[2] = 1.0;

            /* ...calculate transformation: b = m * a */
            __mat3x3_mulv(m, a, b);

            /* ...extract components of p */
            _x = b[0], _y = b[1];
            
            /* ...calculate length of the (_x, _y) vector */
            distortion = fisheye_undistort(sqrt(_x * _x + _y * _y), D);

            /* ...calculate undistorted point */
            im_x = _x * distortion;
            im_y = _y * distortion;

            /* ...convert back to [0..1] range */
            *_uv++ = (im_x + 1) / 2;
            *_uv++ = (im_y + 1) / 2;
        }
    }

    /* ...create rectangular mesh with automatically generated destination coordinates */
    CHK_ERR(cfg = imr_cfg_mesh_src(app->imr, i, uv, SC_MESH_SIZE_Y + 1, SC_MESH_SIZE_X + 1, 0, 0, 1.0 / SC_MESH_SIZE_X, 1.0 / SC_MESH_SIZE_Y), -errno);

    /* ...apply the mesh */
    CHK_API(imr_cfg_apply(app->imr, i, cfg));
    
    /* ...destroy configuration right away */
    imr_cfg_destroy(cfg);
    
    /* ...destroy arrays */
    free(uv);

    return 0;
}

/* ...reset smart-camera configuration matrix */
static int __sc_mesh_reset(app_data_t *app, int i)
{
    sc_cfg_t            *sc = &app->sc_cfg[i];
    app_camera_cfg_t    *cfg = &__app_cfg.camera[i + 1];
    __mat3x3            __identity;

    /* ...create identity matrix */
    __mat3x3_identity(__identity);

    /* ...reset all accumulators */
    memset(sc->rot_acc, 0, sizeof(sc->rot_acc));
    sc->scl_acc = 1.0;

    /* ...update mesh */
    return CHK_API(sc_mesh_setup(app, i, __identity, cfg->D, cfg->K));
}

/* ...rotate/scale configuration matrix (called with a lock held) */
static void __sc_matrix_update(sc_cfg_t *sc, __mat3x3 m, int rx, int ry, int rz, int ts)
{
    static const float  speed_x = 1.0 / 3000;
    static const float  speed_y = 1.0 / 4000;
    static const float  speed_r = 1.0 / 180;
    static const float  speed_s = 1.0 / 1000;
    __mat3x3            t1, t2;
    
    /* ...update accumulators */
    sc->rot_acc[0] += speed_x * rx;
    sc->rot_acc[1] -= speed_y * ry;
    sc->rot_acc[2] += speed_r * rz;
    sc->scl_acc += speed_s * ts;

    /* ...clamp components */
    (sc->rot_acc[0] < -0.5 ? sc->rot_acc[0] = -0.5 : (sc->rot_acc[0] > 0.5 ? sc->rot_acc[0] = 0.5 : 0));
    (sc->rot_acc[1] < -0.5 ? sc->rot_acc[1] = -0.5 : (sc->rot_acc[1] > 0.5 ? sc->rot_acc[1] = 0.5 : 0));
    (sc->rot_acc[2] < -30.0 ? sc->rot_acc[2] = -30.0 : (sc->rot_acc[2] > 30.0 ? sc->rot_acc[2] = 30 : 0));

    sc->scl_acc = (sc->scl_acc < 0.5 ? 0.5 : (sc->scl_acc > 2.0 ? 2.0 : sc->scl_acc));

    /* ...get rotation matrix first (within the limits) */
    __matNxN_rotate(3, t1, 0, 1, sc->rot_acc[2]);

    /* ...scale the matrix */
    __matNxN_M_diag(3, 2, t2, sc->scl_acc);

    /* ...get multiplication */
    __mat3x3_mul(t2, t1, m);

    /* ...translate the matrix */
    __M(3, m, 0, 2) = sc->rot_acc[0];
    __M(3, m, 1, 2) = sc->rot_acc[1];

    __mat3x3_dump(m, "sc-cfg");
}

static inline int sc_spnav_event(app_data_t *app, int i, widget_spnav_event_t *event)
{
    sc_cfg_t           *sc = &app->sc_cfg[i];
    app_camera_cfg_t   *cfg = &__app_cfg.camera[i + 1];
    spnav_event        *e = event->e;
    int                 rx = e->motion.rx, ry = e->motion.ry, rz = e->motion.rz;
    int                 y = e->motion.y;
    __mat3x3            m;
    
    /* ...sanity check */
    BUG((u32)i >= (u32)3, _x("invalid sc-engine id: %d"), i);

    /* ...discard too fast changes (should be done differently - tbd) */
    if ((sc->spnav_delta += e->motion.period) < 50) return 0;

    /* ...clear delta */
    sc->spnav_delta = 0;

    /* ...ignore slight changes */
    (rx < 100 && rx > - 100 ? rx = 0 : 0);
    (ry < 100 && ry > - 100 ? ry = 0 : 0);
    (rz < 100 && rz > - 100 ? rz = 0 : 0);
    (y < 100 && y > - 100 ? y = 0 : 0);
    if ((rx | ry | rz | y) == 0)    return 0;

    TRACE(DEBUG, _b("spnav-event-motion: <x=%d,y=%d,z=%d>, <rx=%d,ry=%d,rz=%d>, p=%d"),
          e->motion.x, e->motion.y, e->motion.z,
          e->motion.rx, e->motion.ry, e->motion.rz,
          e->motion.period);

    TRACE(1, _b("sc-cam-%d: spnav"), i);

    /* ...calculate transformation matrix */
    __sc_matrix_update(sc, m, rz, rx, ry, y);

    /* ...update unfisheye transformation */
    sc_mesh_setup(app, i, m, cfg->D, cfg->K);

    return 0;
}

/*******************************************************************************
 * Focus processing timer
 ******************************************************************************/

/* ...timeout for focus reverting (ms) */
#define FOCUS_REVERT_TIMEOUT        (10 * 1000)

/* ...focus movement function */
static void __focus_switch(app_data_t *app, int focus)
{
    timer_source_t     *timer = app->timer;

    if ((app->focus = focus) == 0)
    {
        /* ...stop pending timer if running */
        timer_source_stop(timer);
    }
    else
    {
        /* ...start one-shot focus revertion timer */
        timer_source_start(timer, FOCUS_REVERT_TIMEOUT, 0);
    }

    /* ...reset focus highlighting machine */
    app->alpha = 1.0;
}

/* ...focus reverting timeout */
static gboolean focus_timeout(void *data)
{
    app_data_t     *app = data;

    /* ...get application access lock */
    pthread_mutex_lock(&app->lock);

    /* ...processing is active only in SV-based mode */
    if (app->sv_gpu_mode)
    {
        /* ...it may happen the focus has been toggled already */
        __focus_switch(app, 0);
    }

    /* ...stop timer (it is one-shot? - tbd) */
    timer_source_stop(app->timer);

    /* ...release application lock */
    pthread_mutex_unlock(&app->lock);

    /* ...source should not be deleted */
    return TRUE;
}

/* ...duration of long-press sequence in ms */
#define LONG_PRESS_TIMEOUT          (500)

/* ...long press timeout */
static gboolean long_press_timeout(void *data)
{
    app_data_t     *app = data;

    /* ...get application access lock */
    pthread_mutex_lock(&app->lock);

    /* ...processing is active only in SV-based mode */
    if (app->sv_gpu_mode)
    {
        /* ...process possible race condition */
        if (app->spnav_long_press && app->focus >= 1)
        {
            u32     mask = (1 << (app->focus - 1));
            
            /* ...toggle masking status */
            app->focus_mask ^= mask;

            /* ...clear long-press condition */
            app->spnav_long_press = 0;
            
            TRACE(1, _b("turn %s focus-%d"), (app->focus_mask & mask ? "off" : "on"), app->focus);
        }
    }

    /* ...release application lock */
    pthread_mutex_unlock(&app->lock);

    /* ...source should not be deleted */
    return TRUE;
}

/*******************************************************************************
 * Input events processing
 ******************************************************************************/

/* ...3D-joystick input processing */
static inline widget_data_t * app_spnav_event(app_data_t *app, widget_data_t *widget, widget_spnav_event_t *event)
{
    spnav_event    *e = event->e;

    pthread_mutex_lock(&app->lock);

    /* ...process switch between modes */
    if (e->type == SPNAV_EVENT_BUTTON && e->button.press == 1)
    {
        if (app->sv_gpu_mode)
        {
            if (e->button.bnum == 0)
            {
                /* ...disable pending timer */
                timer_source_stop(app->timer);

                /* ...left button pressed */
                if (app->focus == 0)
                {
                    /* ...switch to IMR mode */
                    app->sv_gpu_mode = 0;

                    /* ...reset carousel menu parameters */
                    (app->imr_menu ? carousel_reset(app->imr_menu) : 0);
                }
                else if (app->focus > 1)
                {
                    /* ...reset active smart-camera state */
                    __sc_mesh_reset(app, app->focus - 2);
                }
            }
            else
            {
                /* ...right button pressed; start long-press timeout */
                if (app->focus != 0)
                {
                    /* ...start long-press sequence */
                    app->spnav_long_press = 1;
                    
                    /* ...start one-shot timer */
                    timer_source_start(app->long_press_timer, LONG_PRESS_TIMEOUT, 0);
                }
            }
        }
        else
        {
            if (e->button.bnum == 0)
            {
                /* ...left button pressed; return back to the GPU-based mode */
                app->sv_gpu_mode = 1;

                /* ...set focus back to the surround-view (it must be there anyway) */
                __focus_switch(app, 0);
            }
            else
            {
                /* ...pass event to the carousel menu */
                (app->imr_menu ? carousel_spnav_event(app->imr_menu, e) : 0);
            }
        }
    }
    else if (e->type == SPNAV_EVENT_BUTTON && e->button.press == 0)
    {
        if (app->sv_gpu_mode)
        {
            if (e->button.bnum == 1)
            {
                /* ...check if long-press sequence was detected */
                if (app->spnav_long_press)
                {
                    /* ...timeout has not expired already; treat as a focus switch command */
                    app->spnav_long_press = 0;

                    /* ...cancel long-touch timer */
                    timer_source_stop(app->long_press_timer);

                    /* ...switch the focus */
                    __focus_switch(app, (++app->focus == 5 ? app->focus = 0 : app->focus));
                }
                else if (app->focus == 0)
                {
                    /* ...ignore event as we toggled the focus */
                    __focus_switch(app, (++app->focus == 5 ? app->focus = 0 : app->focus));
                }
            }
        }
    }
    else
    {
        if (app->sv_gpu_mode)
        {
            if (app->focus == 0)
            {
                /* ...pass event to surround-view app */
                sview_engine_spnav_event(app->sv, e);
            }
            else
            {
                /* ...reset focus revert timer */
                timer_source_start(app->timer, FOCUS_REVERT_TIMEOUT, 0);

                /* ...pass event to smart-camera object */
                if (app->focus >= 2)
                {
                    sc_spnav_event(app, app->focus - 2, event);
                }
            }
        }
        else
        {
            if (app->imr_menu)
            {
                /* ...pass event to the carousel menu */
                carousel_spnav_event(app->imr_menu, e);
            }
            else
            {
                /* ...pass input event to IMR engine (free-form rotation) */
                imr_sview_input_event(app->imr_sv, container_of(event, widget_event_t, spnav));
            }
        }
    }

    /* ...release application lock */
    pthread_mutex_unlock(&app->lock);

    return widget;
}

/* ...touch-screen event processing */
static inline widget_data_t * app_touch_event(app_data_t *app, widget_data_t *widget, widget_touch_event_t *event)
{
    pthread_mutex_lock(&app->lock);

    switch (event->type)
    {
    case WIDGET_EVENT_TOUCH_DOWN:
        sview_engine_touch(app->sv, TOUCH_DOWN, event->id, event->x, event->y);
        break;
            
    case WIDGET_EVENT_TOUCH_MOVE:
        sview_engine_touch(app->sv, TOUCH_MOVE, event->id, event->x, event->y);
        break;

    case WIDGET_EVENT_TOUCH_UP:
        sview_engine_touch(app->sv, TOUCH_UP, event->id, event->x, event->y);
        break;
    }

    pthread_mutex_unlock(&app->lock);
    
    return widget;
}

/* ...keyboard event processing */
static inline widget_data_t * app_kbd_event(app_data_t *app, widget_data_t *widget, widget_key_event_t *event)
{
    pthread_mutex_lock(&app->lock);

    if (event->type == WIDGET_EVENT_KEY_PRESS)
    {
        TRACE(DEBUG, _b("Key pressed: %i"), event->code);

        /* SPACE -- switch focus */
        if (event->code == KEY_SPACE)
        {
            if (event->state)
            {
                if (app->sv_gpu_mode)
                {
                    /* ...right button pressed; start long-press timeout */
                    if (app->focus != 0)
                    {
                        /* ...start long-press sequence */
                        app->spnav_long_press = 1;

                        /* ...start one-shot timer */
                        timer_source_start(app->long_press_timer, LONG_PRESS_TIMEOUT, 0);
                    }
                }
                else
                {
                    /* ...pass event to the carousel menu */
                    (app->imr_menu ? carousel_leave(app->imr_menu) : 0);
                }
            }
            else
            {
                /* ...check if long-press sequence was detected */
                if (app->spnav_long_press)
                {
                    /* ...timeout has not expired already; treat as a focus switch command */
                    app->spnav_long_press = 0;

                    /* ...cancel long-touch timer */
                    timer_source_stop(app->long_press_timer);

                    /* ...switch the focus */
                    __focus_switch(app, (++app->focus == 5 ? app->focus = 0 : app->focus));
                }
                else if (app->focus == 0)
                {
                    /* ...ignore event as we toggled the focus */
                    __focus_switch(app, (++app->focus == 5 ? app->focus = 0 : app->focus));
                }
            }
        }
        else if (event->code == KEY_ENTER)
        {
            if (event->state)
            {
                if (app->sv_gpu_mode)
                {
                    /* ...disable pending timer */
                    timer_source_stop(app->timer);

                    /* ...left button pressed */
                    if (app->focus == 0)
                    {
                        /* ...switch to IMR mode */
                        app->sv_gpu_mode = 0;

                        /* ...reset carousel menu parameters */
                        (app->imr_menu ? carousel_reset(app->imr_menu) : 0);
                    }
                    else if (app->focus > 1)
                    {
                        /* ...reset active smart-camera state */
                        __sc_mesh_reset(app, app->focus - 2);
                    }
                }
                else
                {
                    /* ...left button pressed; return back to the GPU-based mode */
                    app->sv_gpu_mode = 1;

                    /* ...set focus back to the surround-view (it must be there anyway) */
                    __focus_switch(app, 0);

                }
            }
        }
        else
        {
            /* All other keys forwarded to GPU SV */
            sview_engine_keyboard_key(app->sv, event->code, event->state);
        }
    }

    pthread_mutex_unlock(&app->lock);

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
 * IMR-SV-specific interface
 ******************************************************************************/
#if 0
/* ...static view descriptor */
typedef struct imr_sv_view
{
    /* ...thumbnail image */
    char           *thumbnail;
    
    /* ...pre-rendered image view */
    char           *image;

}   imr_sv_view_t;

#define RESOURCES_DIR       "./"
#define __IMR_SV_THUMB      RESOURCES_DIR "thumb"
#define __IMR_SV_SCENE      RESOURCES_DIR "car"

#define __IMR_SV_VIEW(rx, ry, rz, s1, s2)                                   \
    {                                                                       \
        .thumbnail = __IMR_SV_THUMB ":" #rx ":" #ry ":" #rz ":" #s1 ".png",  \
        .image = __IMR_SV_SCENE ":" #rx ":" #ry ":" #rz ":" #s2 ".png",      \
    }

/* ...static IMR-based surround-view scenes */
static imr_sv_view_t  __imr_sv_view[] = {
#if 0
    __IMR_SV_VIEW(0.0, 0.0, 0.0, 1.5, 1.0),
    __IMR_SV_VIEW(-45.0, 0.0, 0.0, 1.5, 1.0),
    __IMR_SV_VIEW(-45.0, 0.0, 45.0, 1.5, 1.0),
    __IMR_SV_VIEW(-45.0, 0.0, 90.0, 1.5, 1.0),
    __IMR_SV_VIEW(-45.0, 0.0, 135.0, 1.5, 1.0),
    __IMR_SV_VIEW(-45.0, 0.0, 180.0, 1.5, 1.0),
    __IMR_SV_VIEW(-45.0, 0.0, 225.0, 1.5, 1.0),
    __IMR_SV_VIEW(-45.0, 0.0, 270.0, 1.5, 1.0),
    __IMR_SV_VIEW(-45.0, 0.0, 315.0, 1.5, 1.0),
#else
    __IMR_SV_VIEW(0.0, 0.0, 0.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 0.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 45.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 90.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 135.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 180.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 225.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 270.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 315.0, 1.5, 1.0),
#endif
};

/* ...total number of static views */
#define IMR_SV_VIEWS_NUMBER             (sizeof(__imr_sv_view) / sizeof(__imr_sv_view[0]))
#endif

static inline const char * __imr_sview_get_thumbnail(app_view_cfg_t *view)
{
    return view->thumb;
}

static inline const char * __imr_sview_get_image(app_view_cfg_t *view)
{
    return view->scene;
}

static inline void __imr_sview_get_matrix(app_view_cfg_t *view, __vec3 rot, __scalar *s)
{
    const char     *image = strchr(__imr_sview_get_image(view), ':');
    
    (image ? sscanf(image, ":%f:%f:%f:%f", &rot[0], &rot[1], &rot[2], s) : 0);
}
        
/* ...thumbnail image name accessor */
static const char * imr_sv_thumbnail(void *cdata, int i, int j)
{
    app_view_cfg_t  *view = &__app_cfg.views[i * __app_cfg.carousel_x + j];

    /* ...basic sanity check */
    CHK_ERR((u32)i < (u32)__app_cfg.views_number, (errno = -EINVAL, NULL));

    /* ...put thumbnail name */
    return __imr_sview_get_thumbnail(view);
}

/* ...thumbnail selection hook */
static void imr_sv_select(void *cdata, int i, int j)
{
    app_data_t     *app = cdata;
    app_view_cfg_t *view = &__app_cfg.views[i * __app_cfg.carousel_x + j];
    __vec3          rot;
    __scalar        scale;
    const char     *image;
    
    /* ...make sure index is sane */
    BUG((u32)i >= (u32)__app_cfg.views_number, _x("invalid selection index: %d"), i);

    /* ...parse rotation matrix and scale factor from the file name */
    __imr_sview_get_matrix(view, rot, &scale);

    /* ...get image name */
    image = __imr_sview_get_image(view);
    
    /* ...set fixed view for the surround-view scene */
    imr_sview_set_view(app->imr_sv, rot, scale, (char*)image);

    TRACE(1, _b("set static view #%d: angle=%f/%f/%f, scale=%f, image='%s'"), i, rot[0], rot[1], rot[2], scale, image);
}

/* ...carousel menu configuration */
static carousel_cfg_t __imr_sv_carousel_cfg = {
    .width = 256,
    .height = 256,
    .size = 9,
    .window_size = 3,
    .window_size_y = 1,
    .x_center = 0.5,
    .y_center = 0.75,
    .x_length = 0.2,
    .y_length = 0.2,
    .thumbnail = imr_sv_thumbnail,
    .select = imr_sv_select,
};

/*******************************************************************************
 * Context initialization
 ******************************************************************************/

static const float __sv_view[4][4] = {
    {   0.0, 0.0, 0.5, 0.5 },
    {   0.5, 0.0, 1.0, 0.5 },
    {   0.0, 0.5, 0.5, 1.0 },
    {   0.5, 0.5, 1.0, 1.0 },
};
    
static const float __dm_view[1][4] = {
    /* ...driver-monitor window location */
    {   0.333, 0.666 - 0.01, 0.666, 0.99 },
};    

static const float __sc_view[3][4] = {
    /* ...left camera */
    {   0.01, 0.20, 0.21, 0.40 },

    /* ...right camera */
    {   0.79, 0.20, 0.99, 0.40 },

    /* ...rear-view camera */
    {   0.40, 0.01, 0.60, 0.21 },
};    

/* ...surround-view engine configuration */
static sview_cfg_t  __sv_cfg = {
    .pixformat = GST_VIDEO_FORMAT_UYVY,
    .config_path = "config.xml",
};

/* ...driver-monitor engine configuration */
static objdet_config_t  __dm_cfg = {
    .focal_length = 1200.0,
    .max_face_width_ratio = 0.16,
};


static const int   __imr_n = 2;

static const float __imr_uv[] = {
    0.25, 0.25, 0.25, 0.75, 0.75, 0.25, 
    0.75, 0.25, 0.25, 0.75, 0.75, 0.75, 
};

#define SCALE   0.001
static const float __imr_xy[] = {
    0.0, 0.0, 1.0, 0.0, SCALE, 1.0, SCALE, 0.0, 1.0, 
    SCALE, 0.0, 1.0, 0.0, SCALE, 1.0, SCALE, SCALE, 1.0,
};
   
/* ...initialize GL-processing context */
static int app_context_init(widget_data_t *widget, void *data)
{
    app_data_t     *app = data;
    window_data_t  *window = (window_data_t *)widget;
    int             w = window_get_width(window);
    int             h = window_get_height(window);
    const float     factor = (float)h / w;
    int             i;

    /* ...create VIN engine */
    CHK_ERR(app->vin = vin_init(vin_dev_name, VIN_NUMBER, &vin_cb, app), -errno);

    /* ...create IMR engine */
    CHK_ERR(app->imr = imr_init(imr_dev_name, IMR_NUMBER - 1 , &imr_cb, app), -errno);

    /* ...create driver-monitor engine */
    CHK_ERR(app->dm = objdet_engine_init(&dm_callback, app, 640, 400, 2, 1280, 1080, &__dm_cfg), -errno);

    /* ..set up color correction backchannel */
    for (i = 0; i < 4; i++)
    {
            __sv_cfg.vfd[i] = get_v4l2_fd(app->vin, i);
    }
    
    /* ...setup GPU-based surround-view engine */
    CHK_ERR(app->sv = sview_engine_init(&__sv_cfg, 1280, 1080), -errno);

    /* ...setup IMR-based surround-view engine (FullHD is a maximal possible resolution) */
    CHK_ERR(app->imr_sv = imr_sview_init(&imr_sv_callback, app, 1280, 1080, __vin_format, __vsp_width, __vsp_height, __car_width, __car_height, __shadow_rect), -errno);

    /* ...adjust carousel parameters for a given aspect ratio */
    __imr_sv_carousel_cfg.x_length *= (float)h / w;
    __imr_sv_carousel_cfg.size = __app_cfg.carousel_x;
    __imr_sv_carousel_cfg.size_y = __app_cfg.carousel_y;

    /* ...create IMR menu */
    /* CHK_ERR(app->imr_menu = carousel_create(&__imr_sv_carousel_cfg, app), -errno); */

    /* ...hack */
    app->imr_menu = NULL;
    
    /* ...create a focus change timer */
    app->timer = timer_source_create(focus_timeout, app, NULL, g_main_loop_get_context(app->loop));

    /* ...create a long-touch detection timer */
    app->long_press_timer = timer_source_create(long_press_timeout, app, NULL, g_main_loop_get_context(app->loop));

    /* ...initialize VINs for surround-view */
    for (i = 0; i < 4; i++)
    {
        const float *v = __sv_view[i];
        
        /* ...use 1280*1080 UYVY configuration; use pool of 5 buffers */
        CHK_API(vin_device_init(app->vin, i, 1280, 1080, V4L2_PIX_FMT_UYVY, 6));

        /* ...setup view-port - fill single quadrant */
        texture_set_view(&app->sv_view[i], v[0], v[1], v[2], v[3]);
    }

    /* ...reset buffers readiness flags */
    app->sv_flags = (1 << i) - 1;

    /* ...initialize VINs for driver-monitor */
    for (i = 0; i < 1; i++)
    {
        const float     *v = __dm_view[i];
        cairo_matrix_t  *m = &app->dm_mat[i];
        __mat3x3        __identity;

        /* ...create identity matrix */
        __mat3x3_identity(__identity);

        /* ...set camera into 640 * 400 NV16 configuration; use pool of 5 buffers */
        CHK_API(vin_device_init(app->vin, VIN_DM + i, 640, 400, V4L2_PIX_FMT_UYVY, 8));

        /* ...setup view-port */
        texture_set_view(&app->dm_view[i][0], v[0], v[1], v[2], v[3]);
        texture_set_view(&app->dm_view[i][1], v[0] - 0.03 * factor, v[1] - 0.03, v[2] + 0.03 * factor, v[3] + 0.03);

        /* ...initialize cairo transformation matrix for a drawing */
        m->xx = (v[2] - v[0]) * w / 640.0, m->xy = 0;
        m->yy = (v[3] - v[1]) * h / 400.0, m->yx = 0;
        m->x0 = v[0] * w, m->y0 = (1 - v[3]) * h;

#if 0
        /* ...allocate VSP buffers (we use user-pointer V4L2 configuration) */
        CHK_API(vsp_allocate_buffers(640, 400, V4L2_PIX_FMT_UYVY, app->dm_mem, 6));
        
        /* ...setup IMR engine */
        CHK_API(imr_setup(app->imr, IMR_DM, 1280, 1080, 640, 400, GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_UYVY, 6));

        /* ...set initial transformation matrix */
        CHK_API(sc_mesh_setup(app, IMR_DM, __identity, __app_cfg.camera[0].D, __app_cfg.camera[0].K));
#endif
    }

    /* ...reset buffers readiness flags */
    app->dm_flags = (1 << i) - 1;

    /* ...initialize VINs for smart-cameras */
    for (i = 0; i < 3; i++)
    {
        const float *v = __sc_view[i];
        const float factor = (float)h / w;
        
        /* ...set camera into 1280 * 1080 UYVY configuration; use pool of 5 buffers */
        CHK_API(vin_device_init(app->vin, VIN_SC_LEFT + i, 1280, 1080/* 640, 400 */, V4L2_PIX_FMT_UYVY, 5));

        /* ...setup view-port */
        texture_set_view(&app->sc_view[i][0], v[0], v[1], v[2], v[3]);
        texture_set_view(&app->sc_view[i][1], v[0] - 0.03 * factor, v[1] - 0.03, v[2] + 0.03 * factor, v[3] + 0.03);

        /* ...allocate VSP buffers (we use user-pointer V4L2 configuration) */
        CHK_API(vsp_allocate_buffers(1280, 1080, V4L2_PIX_FMT_UYVY, app->sc_mem[i], 2));
        
        /* ...setup IMR engine */
        CHK_API(imr_setup(app->imr, i, 1280, 1080/* 640, 400 */, 1280, 1080, GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_UYVY, IMR_POOL_SIZE));

        /* ...set initial transformation matrix */
        CHK_API(__sc_mesh_reset(app, i));
    }
    
    /* ...reset buffers readiness flags */
    app->sc_flags = (1 << i) - 1;

    /* ...start IMR engine */
    CHK_API(imr_start(app->imr));

    TRACE(INFO, _b("run-time initialized: %d*%d"), w, h);

    return 0;
}

/*******************************************************************************
 * Application thread
 ******************************************************************************/

void * app_thread(void *arg)
{
    app_data_t     *app = arg;

    C_UNUSED(app);
    
    g_main_loop_run(app->loop);
    
    while (1)   sleep(10);

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

/* ...module destructor */
static void app_destroy(gpointer data, GObject *obj)
{
    app_data_t  *app = data;

    TRACE(INIT, _b("destruct application data"));

    /* ...destroy main loop */
    g_main_loop_unref(app->loop);

    /* ...destroy main application window */
    (app->window ? window_destroy(app->window) : 0);
    
    /* ...free application data structure */
    free(app);

    TRACE(INIT, _b("module destroyed"));
}

/*******************************************************************************
 * Window parameters
 ******************************************************************************/

/* ...processing window parameters */
static window_info_t app_main_info = {
    .fullscreen = 1,
    .redraw = app_redraw,
};

/* ...main window widget parameters (input-interface + GUI?) */
static widget_info_t app_main_info2 = {
    .init = app_context_init,
    .event = app_input_event,
};

/*******************************************************************************
 * Entry point
 ******************************************************************************/


/* ...module initialization function */
app_data_t * app_init(display_data_t *display)
{
    app_data_t            *app;
    pthread_mutexattr_t    attr;

    /* ...sanity check - output device shall be positive */
    CHK_ERR(__output_main >= 0, (errno = EINVAL, NULL));

    /* ...create local data handle */
    CHK_ERR(app = calloc(1, sizeof(*app)), (errno = ENOMEM, NULL));

    /* ...set default flags */
    //app->flags = APP_FLAG_NEXT | 0*APP_FLAG_DEBUG | APP_FLAG_DEBUG_ALPHA_MESH;

    /* ...start in GPU-SV mode */
    app->sv_gpu_mode = 1, app->focus = 0;

    /* ...set output device number for a main window */
    app_main_info.output = __output_main;

    /* ...prebuild shaders - tbd - looks unstructured */
    if (border_shader_prebuild() != 0 || carousel_shader_prebuild())
    {
        TRACE(ERROR, _x("failed to prebuild shaders: %m"));
        goto error;
    }

    /* ...initialize data access lock */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&app->lock, &attr);
    pthread_mutexattr_destroy(&attr);

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

    /* ...create a pipeline (not used yet) */
    if ((app->pipe = gst_pipeline_new("app::pipe")) == NULL)
    {
        TRACE(ERROR, _x("pipeline creation failed"));
        errno = ENOMEM;
        goto error;
    }
    else
    {
        GstBus  *bus = gst_pipeline_get_bus(GST_PIPELINE(app->pipe));
        gst_bus_add_watch(bus, app_bus_callback, app);
        gst_object_unref(bus);
    }

    /* ...create full-screen window for processing results visualization */        
    if ((app->window = window_create(display, &app_main_info, &app_main_info2, app)) == NULL)
    {
        TRACE(ERROR, _x("failed to create main window: %m"));
        goto error;
    }

    /* ...start VIN interface */
    if (vin_start(app->vin) < 0)
    {
        TRACE(ERROR, _x("failed to start VIN: %m"));
        goto error;
    }
    
    /* ...add destructor to the pipe */
    g_object_weak_ref(G_OBJECT(app->pipe), app_destroy, app);
    
    TRACE(INIT, _b("application initialized"));

    return app;

error:
    /* ...destroy main loop */
    (app->loop ? g_main_loop_unref(app->loop) : 0);

    /* ...destroy main application window */
    (app->window ? window_destroy(app->window) : 0);

    /* ...destroy data handle */
    free(app);

    return NULL;
}
