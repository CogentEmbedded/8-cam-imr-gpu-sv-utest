/*******************************************************************************
 * utest-imr-sv.c
 *
 * IMR-based surround view engine
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

#define MODULE_TAG                      IMR-SV

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"
#include "utest-display.h"
#include "utest-imr-sv.h"
//#include "utest-app.h"
#include "utest-vsink.h"
#include "utest-imr.h"
#include "utest-mesh.h"
#include "utest-compositor.h"
#include "utest-png.h"
#include "utest-math.h"
#include <linux/videodev2.h>

/*******************************************************************************
 * To-be-removed
 ******************************************************************************/

/* ...IMR device names */
extern char * imr_dev_name[];

/* ...mesh data (tbd - move to track configuration) */
extern char * __mesh_file_name;

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * Local constants definitions
 ******************************************************************************/

/* ...IMR engines identifiers */
#define IMR_CAMERA_RIGHT                0
#define IMR_CAMERA_LEFT                 1
#define IMR_CAMERA_FRONT                2
#define IMR_CAMERA_REAR                 3
#define IMR_ALPHA_RIGHT                 4
#define IMR_ALPHA_LEFT                  5
#define IMR_ALPHA_FRONT                 6
#define IMR_ALPHA_REAR                  7
#define IMR_NUMBER                      8

#define IMR_CAMERA_0                    IMR_CAMERA_RIGHT
#define IMR_ALPHA_0                     IMR_ALPHA_RIGHT

/* ...VSP buffers indices */
#define VSP_CAMERA_RIGHT                0
#define VSP_CAMERA_LEFT                 1
#define VSP_CAMERA_FRONT                2
#define VSP_CAMERA_REAR                 3
#define VSP_ALPHA_RIGHT                 4
#define VSP_ALPHA_LEFT                  5
#define VSP_ALPHA_FRONT                 6
#define VSP_ALPHA_REAR                  7
#define VSP_CAR                         8
#define VSP_OUTPUT                      9
#define VSP_NUMBER                      10

#define VSP_ALPHA_0                     VSP_ALPHA_RIGHT

/* ...size of compositor buffers pool */
#define VSP_POOL_SIZE                   2

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

typedef struct imr_sview
{
    /* ...application callback */
    const imr_sview_cb_t     *cb;
    
    /* ...callback client data */
    void               *cdata;

    /* ...input stream dimensions */
    int                 width, height;

    /* ...miscellaneous control flags */
    u32                 flags, imr_flags;

    /* ...pending input buffers (waiting for IMR processing start) */
    GQueue              input[CAMERAS_NUMBER];

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

    /* ...alpha-planes IMR output buffers */
    vsp_mem_t          *alpha_plane[2][VSP_POOL_SIZE];
    
    /* ...alpha plane, car model buffers - tbd */
    vsp_mem_t          *alpha_input[1], *car_plane[2];

    /* ...VSP output buffers (compositor output) */
    vsp_mem_t          *output[VSP_POOL_SIZE];

    /* ...alpha-plane input buffer */
    GstBuffer          *alpha_buffer;

    /* ...active alpha-plane output buffers */
    GstBuffer          *alpha_active[4];
    
    /* ...car image buffers */
    GstBuffer          *car_buffer[2];

    /* ...active car-model buffer */
    GstBuffer          *car_active;

    /* ...image to use for car rendering */
    char               *car_image;

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
    mesh_data_t        *mesh;

    /* ...projection/view matrix */
    __mat4x4            pv_matrix;
    
    /* ...model matrix */
    __mat4x4            model_matrix;

    /* ...latched project/view/model matrix - tbd - do I need that? */
    __mat4x4            pvm_matrix;

    /* ...rotation matrix accumulators */
    __vec3              rot_acc, tr_rot_acc;

    /* ...scaling factor */
    __scalar            scl_acc, tr_scl_acc;

    /* ...current steps set for a model view */
    int                 step[3];

    /* ...number of milliseconds since last update */
    u32                 spnav_delta;

    /* ...touchscreen state processing */
    u32                 ts_flags;

    /* ...touchscreen latched positions */
    int                 ts_pos[2][2];

    /* ...distance between touch points */
    int                 ts_dist;

    /* ...transition animation timer */
    timer_source_t     *timer;

}   imr_sview_t;

/*******************************************************************************
 * Operation control flags
 ******************************************************************************/

/* ...output debugging info */
#define APP_FLAG_DEBUG                  (1 << 0)

/* ...renderer flushing condition */
#define APP_FLAG_EOS                    (1 << 1)

/* ...application termination request */
#define APP_FLAG_EXIT                   (1 << 4)

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

/* ...projection matrix */
static __mat4x4 __p_matrix;

/* ...default view matrix */
static __mat4x4 __v_matrix = {
    __MATH_FLOAT(1),    __MATH_FLOAT(0),    __MATH_FLOAT(0),    __MATH_FLOAT(0),
    __MATH_FLOAT(0),    __MATH_FLOAT(1),    __MATH_FLOAT(0),    __MATH_FLOAT(0),
    __MATH_FLOAT(0),    __MATH_FLOAT(0),    __MATH_FLOAT(1),    __MATH_FLOAT(0),
    __MATH_FLOAT(0),    __MATH_FLOAT(0),    __MATH_FLOAT(-1),   __MATH_FLOAT(1),
};

/*******************************************************************************
 * Compositor interface
 ******************************************************************************/

/* ...trigger surround-view scene composition if possible (called with VSP lock held) */
static int __vsp_compose(imr_sview_t *sv)
{
    vsp_mem_t      *mem[VSP_NUMBER];
    GstBuffer     **buf = sv->vsp_buffers;
    int             i;

    /* ...all buffers must be available */
    BUG(sv->vsp_ready != 0, _x("invalid state: %x"), sv->vsp_ready);
    
    /* ...collect memory descriptors */
    for (i = 0; i < VSP_NUMBER; i++)
    {
        GQueue         *queue = &sv->vsp_pending[i];
        GstBuffer      *buffer = g_queue_pop_head(queue);
        imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);

        /* ...save buffer in the VSP processing array (keep ownership) */
        buf[i] = buffer, mem[i] = meta->priv;

        /* ...check if queue gets empty */
        (g_queue_is_empty(queue) ? sv->vsp_ready |= 1 << i : 0);
    }

    /* ...make sure memory buffers are same for cameras */
    BUG(mem[VSP_CAMERA_LEFT] != mem[VSP_CAMERA_RIGHT], _x("left/right buffers mismatch: %p != %p"), mem[VSP_CAMERA_LEFT], mem[VSP_CAMERA_RIGHT]);
    BUG(mem[VSP_CAMERA_FRONT] != mem[VSP_CAMERA_REAR], _x("front/rear buffers mismatch: %p != %p"), mem[VSP_CAMERA_FRONT], mem[VSP_CAMERA_REAR]);

    /* ...make sure alpha-planes buffers are same for the sets */
    BUG(mem[VSP_ALPHA_LEFT] != mem[VSP_ALPHA_RIGHT], _x("left/right alpha buffers mismatch: %p != %p"), mem[VSP_ALPHA_LEFT], mem[VSP_ALPHA_RIGHT]);
    BUG(mem[VSP_ALPHA_FRONT] != mem[VSP_ALPHA_REAR], _x("front/rear alpha buffers mismatch: %p != %p"), mem[VSP_ALPHA_FRONT], mem[VSP_ALPHA_REAR]);

    /* ...place associated input buffers into active buffers queue */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        /* ...associated input buffers are saved in VSP queues */
        buf[VSP_NUMBER + i] = g_queue_pop_head(&sv->vsp_pending[VSP_NUMBER + i]);
    }

    /* ...mark buffer processing is ongoing */
    sv->vsp_ready |= 1 << VSP_NUMBER;

    /* ...submit a job to compositor */
    CHK_ERR(vsp_job_submit(sv->vsp, mem, mem[VSP_OUTPUT]) == 0, -(errno = EBADFD));

    TRACE(DEBUG, _b("job submitted..."));

    return 0;
}

/* ...submit particular buffer to a compositor (called with a VSP lock held) */
static int __vsp_submit_buffer(imr_sview_t *sv, int i, GstBuffer *buffer)
{
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);

    /* ...place buffer into pending IMR queue */
    g_queue_push_tail(&sv->vsp_pending[i], gst_buffer_ref(buffer));

    /* ...submit processing task if all buffers are available */
    if ((sv->vsp_ready &= ~(1 << i)) == 0)
    {
        CHK_API(__vsp_compose(sv));
    }
    else
    {
        TRACE(DEBUG, _b("buffer #<%d,%d> submitted: render-mask: %X"), i, meta->index, sv->vsp_ready);
    }

    return 0;
}

/* ...compositor processing callback */
static void vsp_callback(void *data, int result)
{
    imr_sview_t    *sv = data;
    GstBuffer     **buf = sv->vsp_buffers;
    u32             sequence = sv->sequence_out;
    int             i;
    
    /* ...lock application state */
    pthread_mutex_lock(&sv->lock);

    /* ...update output buffers sequence counter */
    sv->sequence_out = sequence + 1;

    /* ...test if we need to update current alpha- and car-model buffers */
    if ((sv->flags & APP_FLAG_UPDATE) && (sequence == sv->last_update))
    {
        /* ...latch new buffers for subsequent jobs - tbd */ 
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            sv->alpha_active[i] = gst_buffer_ref(buf[VSP_ALPHA_0 + i]);
        }

        /* ...latch active car buffer */
        sv->car_active = gst_buffer_ref(buf[VSP_CAR]);

        /* ...we might have submitted some number of jobs already */
        pthread_mutex_lock(&sv->vsp_lock);

        /* ...submit remaining number of jobs (compositing is now disabled) */
        while (++sequence != sv->sequence)
        {
            /* ...submit alpha buffers */
            for (i = 0; i < CAMERAS_NUMBER; i++)
            {
                __vsp_submit_buffer(sv, VSP_ALPHA_0 + i, sv->alpha_active[i]);
            }

            /* ...submit car buffers */
            __vsp_submit_buffer(sv, VSP_CAR, sv->car_active);
        }

        /* ...unlock VSP queues */
        pthread_mutex_unlock(&sv->vsp_lock);

        /* ...clear update sequence flag */
        sv->flags &= ~APP_FLAG_UPDATE;
    }

    /* ...release the lock before passing control to the application */
    pthread_mutex_unlock(&sv->lock);
    
    /* ...should I pass auxiliary buffers as well? ---everything at once? - tbd */
    sv->cb->ready(sv->cdata, buf);

    /* ...release all processed buffers */
    for (i = 0; i < VSP_NUMBER + CAMERAS_NUMBER; i++)
    {
        gst_buffer_unref(buf[i]);
    }

    /* ...lock VSP data access */
    pthread_mutex_lock(&sv->vsp_lock);

    /* ...clear compositing flag and submit another job if possible */
    if ((sv->vsp_ready &= ~(1 << VSP_NUMBER)) == 0)
    {
        __vsp_compose(sv);
    }

    /* ...unlock VSP data access */
    pthread_mutex_unlock(&sv->vsp_lock);
}

/*******************************************************************************
 * Input job processing interface
 ******************************************************************************/

extern __scalar     __sphere_gain;

/* ...setup IMR engines for a processing (called with an application lock held) */
static int __sv_map_setup(imr_sview_t *sv)
{
    __vec2     *uv[CAMERAS_NUMBER], *a[CAMERAS_NUMBER];
    __vec3     *xy[CAMERAS_NUMBER];
    int         n[CAMERAS_NUMBER];
    int         i;
    
    /* ...calculate projection transformations of the points (single-threaded?) */
    CHK_API(mesh_translate(sv->mesh, uv, a, xy, n, sv->pvm_matrix, __sphere_gain));

    /* ...setup individual engines */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        TRACE(INFO, _b("engine-%d mesh setup: n = %d"), i, n[i]);

        /* ...create new configuration - tbd - not that simple */
        CHK_ERR(sv->imr_cfg[i + IMR_CAMERA_0] = imr_cfg_create(sv->imr, i + IMR_CAMERA_0, uv[i][0], xy[i][0], n[i]), -errno);
        CHK_ERR(sv->imr_cfg[i + IMR_ALPHA_0] = imr_cfg_create(sv->imr, i + IMR_ALPHA_0, a[i][0], xy[i][0], n[i]), -errno);

        TRACE(INFO, _b("engine-%d configured"), i);
    }

    return 0;
}

/* ...submit new input job to IMR engines (function called with a lock held) */
static int __sv_job_submit(imr_sview_t *sv)
{
    u32         sequence = sv->sequence;
    GstBuffer  *buffer[CAMERAS_NUMBER];
    int         i;

    /* ...all buffers must be available */
    BUG(sv->input_ready != 0, _x("invalid state: %x"), sv->input_ready);

    /* ...submit the buffers to the engines */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        /* ...get buffer from the head of the pending input queue */
        buffer[i] = g_queue_pop_head(&sv->input[i]);

        /* ...submit to an engine for processing (increases refcount) */
        CHK_API(imr_engine_push_buffer(sv->imr, i, buffer[i]));
        
        /* ...check if queue gets empty */
        (g_queue_is_empty(&sv->input[i]) ? sv->input_ready |= 1 << i : 0);
    }

    /* ...increment sequence number */
    sv->sequence = sequence + 1;

    /* ...select alpha-plane and car-model buffers for a given job */
    pthread_mutex_lock(&sv->vsp_lock);

    /* ...submit alpha / car buffers if no active update sequence is ongoing */
    if ((sv->flags & APP_FLAG_UPDATE) == 0)
    {
        /* ...submit all alpha-buffers */
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            /* ...make sure we have active alpha buffer */
            BUG(!sv->alpha_active[i], _x("camera-%d: alpha-buffer is not ready"), i);

            /* ...pass current buffer for use with camera plane */
            __vsp_submit_buffer(sv, VSP_ALPHA_0 + i, sv->alpha_active[i]);
        }

        /* ...submit car model buffer */
        __vsp_submit_buffer(sv, VSP_CAR, sv->car_active);
    }

    /* ...save input buffers in the global pending queue (move ownership) */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        g_queue_push_tail(&sv->vsp_pending[VSP_NUMBER + i], buffer[i]);
    }

    /* ...unlock VSP queues */
    pthread_mutex_unlock(&sv->vsp_lock);

    TRACE(DEBUG, _b("job submitted: sequence=%u"), sequence);

    return 0;
}

/* ...trigger update of alpha-plane (called with a lock held) */
static inline int __sv_alpha_update(imr_sview_t *sv)
{
    int     i;
    
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        /* ...make sure there is no active buffer */
        BUG(sv->alpha_active[i], _x("alpha-%d: invalid state: %p"), i, sv->alpha_active[i]);

        /* ...push alpha-plane input buffer (use sequence id of next job to submit) */
        CHK_API(imr_engine_push_buffer(sv->imr, IMR_ALPHA_0 + i, sv->alpha_buffer));
    }

    return 0;
}

/*******************************************************************************
 * Mesh update (hmm; not well-positioned)
 ******************************************************************************/

/* ...reset application matrices (called with a lock held) */
static void __sv_matrix_reset(imr_sview_t *sv)
{
    extern __vec3 __default_view;
    
    /* ...reset accumulators */
    sv->rot_acc[0] = __default_view[0];
    sv->rot_acc[1] = __MATH_FLOAT(0);
    sv->rot_acc[2] = __default_view[1];
    sv->scl_acc = __default_view[2];
}

/* ...set new matrix */
static void __sv_matrix_set(imr_sview_t *sv, __scalar rx, __scalar ry, __scalar rz, __scalar s)
{
    sv->rot_acc[0] = rx, sv->rot_acc[1] = ry, sv->rot_acc[2] = rz;
    sv->scl_acc = s;
}

/* ...rotate/scale model matrix (called with a lock held) */
static void __sv_matrix_update(imr_sview_t *sv, int rx, int ry, int rz, int ts)
{
    static const float  speed = 1.0 / 360;

    /* ...update accumulators */
    sv->rot_acc[0] -= speed * rx;
    sv->rot_acc[1] += 0 * speed * ry;
    sv->rot_acc[2] += speed * rz;
    sv->scl_acc -= 4 * speed * speed * ts;

    /* ...clamp components */
    (sv->rot_acc[0] > 0 ? sv->rot_acc[0] = 0 : (sv->rot_acc[0] < -80 ? sv->rot_acc[0] = -80 : 0));
    ((sv->rot_acc[1] = fmodf(sv->rot_acc[1], 360)) < 0 ? sv->rot_acc[1] += 360 : 0);
    ((sv->rot_acc[2] = fmodf(sv->rot_acc[2], 360)) < 0 ? sv->rot_acc[2] += 360 : 0);
    sv->scl_acc = (sv->scl_acc < 0.75 ? 0.75 : (sv->scl_acc > 1.5 ? 1.5 : sv->scl_acc));
}

static inline int __sv_map_changed(imr_sview_t *sv)
{
    int                 step[3];
    char                buffer[256];
    extern int          __steps[3];
    extern char        *__model;

    /* ...check out if we crossed the boundaries */
    step[0] = (int)floor(sv->rot_acc[0] / -80 * __steps[0] + 0.5);
    step[1] = (int)floor(sv->rot_acc[2] / 360 * __steps[1] + 0.5);
    step[2] = (int)floor((sv->scl_acc - 0.75) * __steps[2] / 0.75 + 0.5);

    BUG(step[0] < 0, _x("invalid step[0]: %d (%d)"), step[0], __steps[0]);
    BUG(step[1] < 0, _x("invalid step[1]: %d (%d)"), step[1], __steps[1]);
    BUG(step[2] < 0, _x("invalid step[2]: %d (%d)"), step[2], __steps[2]);

    /* ...saturate steps */
    (step[0] >= __steps[0] ? step[0] = __steps[0] - 1 : 0);
    (step[1] >= __steps[1] ? step[1] -= __steps[1] : 0);
    (step[2] >= __steps[2] ? step[2] = __steps[2] - 1 : 0);

    TRACE(DEBUG, _b("angles: %.1f/%.1f/%.2f -> %d/%d/%d"), sv->rot_acc[0], sv->rot_acc[2], sv->scl_acc, step[0], step[1], step[2]);
    
    /* ...ignore update if we have same steps array */
    if (!memcmp(step, sv->step, sizeof(sv->step)))  return 0;
    
    /* ...update current steps */
    memcpy(sv->step, step, sizeof(sv->step));

    /* ...update rotation vectors */
    sv->rot_acc[0] = -80.0 * step[0] / __steps[0];
    sv->rot_acc[2] = 360.0 * step[1] / __steps[1];
    sv->scl_acc = 0.75 + 0.75 * step[2] / __steps[2];

    /* ...drop previous image */
    (sv->car_image ? free(sv->car_image) : 0);

    /* ...prepare filename */
    snprintf(buffer, sizeof(buffer), "%s-%d-%d-%d.png", __model, step[0], step[1], step[2]);

    /* ...make a copy of string */
    sv->car_image = strdup(buffer);

    TRACE(INFO, _b("select image '%s'"), sv->car_image);

    return 1;
}

/* ...mesh update thread */
static void * mesh_update_thread(void *arg)
{
    imr_sview_t     *sv = arg;

    /* ...protect intenal app data */
    pthread_mutex_lock(&sv->lock);

    /* ...wait for an update event */
    while (1)
    {
        /* ...wait for a mesh update flag */
        while ((sv->flags & (APP_FLAG_MAP_UPDATE | APP_FLAG_EOS)) == 0)
        {
            pthread_cond_wait(&sv->update, &sv->lock);
        }

        /* ...process termination request */
        if (sv->flags & APP_FLAG_EOS)
        {
            TRACE(INIT, _b("termination request received"));
            goto out;
        }
        
        /* ...update IMR mappings */
        if (__sv_map_setup(sv) != 0)
        {
            TRACE(ERROR, _x("maps update failed: %m"));
            goto out;
        }

        /* ...clear mesh update command condition */
        sv->flags &= ~APP_FLAG_MAP_UPDATE;

        /* ...start alpha-plane processing */
        __sv_alpha_update(sv);

        /* ...re-enable input */
        if ((sv->input_ready &= ~(1 << CAMERAS_NUMBER)) == 0)
        {
            /* ...force job submission */
            if (__sv_job_submit(sv) != 0)
            {
                TRACE(ERROR, _b("job submission failed: %m"));
                goto out;
            }
        }
    }

out:
    /* ...release application lock */
    pthread_mutex_unlock(&sv->lock);

    TRACE(INIT, _b("map update thread terminated"));

    return NULL;
}

/* ...process mesh rotation (called with a lock held) */
static int __sv_map_update(imr_sview_t *sv)
{
    int     i;

    /* ...ignore update request if one is started */
    if (sv->flags & APP_FLAG_UPDATE)       return 0;

    /* ...check if matrix has been actually adjusted */
    if (!__sv_map_changed(sv))              return 0;

    /* ...calculate M matrix */
    if (1)
    {
        __vec3      rot = { sv->rot_acc[0], sv->rot_acc[1], 180.0 - sv->rot_acc[2] };

        __mat4x4_rotation(sv->model_matrix, rot, sv->scl_acc);
    }
    else
    {
        __mat4x4_rotation(sv->model_matrix, sv->rot_acc, sv->scl_acc);
    }
    

    /* ...multiply projection/view matrix by model matrix (create PVM matrix copy) */
    __mat4x4_mul(sv->pv_matrix, sv->model_matrix, sv->pvm_matrix);

    /* ...make sure both sequences has completed */
    BUG(sv->flags & (APP_FLAG_MAP_UPDATE | APP_FLAG_CAR_UPDATE), _x("invalid state: %X"), sv->flags);
    
    /* ...initiate point-of-view update sequence */
    sv->flags ^= APP_FLAG_UPDATE | APP_FLAG_MAP_UPDATE | APP_FLAG_CAR_UPDATE;

    /* ...latch sequence number of next job that will have updated configuration */
    sv->last_update = sv->sequence;

    /* ...force alpha-planes update */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        GstBuffer  *buffer = sv->alpha_active[i];
        
        /* ...update alpha-planes on next invocation - tbd */
        sv->sequence_imr[IMR_ALPHA_0 + i] = sv->last_update;

        /* ...drop active alpha-buffer as needed */
        (buffer ? gst_buffer_unref(buffer), sv->alpha_active[i] = NULL : 0);
    }

    /* ...disable input path for a duration of update sequence (tbd) */
    sv->input_ready |= (1 << CAMERAS_NUMBER);

    /* ...drop car-model buffers as needed */
    (sv->car_active ? gst_buffer_unref(sv->car_active), sv->car_active = NULL : 0);
    
    TRACE(DEBUG, _b("trigger update sequence"));

    /* ...kick update threads */
    pthread_cond_broadcast(&sv->update);

    return 0;
}

/* ...initialize mesh update thread */
static inline int sv_map_init(imr_sview_t *sv, int W, int H)
{
    pthread_attr_t  attr;
    int             r;

    /* ...reset initial matrices */
    __sv_matrix_reset(sv);

    /* ...generate perspective projection matrix */
    __mat4x4_perspective(__p_matrix, 45.0, (float)W / H, 0.1, 10.0);

    /* ...calculate PV matrix (which is constant for now) */
    __mat4x4_mul(__p_matrix, __v_matrix, sv->pv_matrix);

    /* ...initialize thread attributes (joinable, 128KB stack) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr, 128 << 10);

    /* ...create mesh update thread */
    r = pthread_create(&sv->mesh_thread, &attr, mesh_update_thread, sv);
    pthread_attr_destroy(&attr);
    CHK_API(r);

    return 0;
}

/*******************************************************************************
 * Distortion correction engine interface (all functions are interlocked)
 ******************************************************************************/

/* ...mapping between Gstreamer and V4L2 pixel-formats */
static inline u32 __pixfmt_gst_to_v4l2(int format)
{
    switch (format)
    {
    case GST_VIDEO_FORMAT_ARGB:         return V4L2_PIX_FMT_ARGB32;
    case GST_VIDEO_FORMAT_RGB16:        return V4L2_PIX_FMT_RGB565;
    case GST_VIDEO_FORMAT_RGB15:        return V4L2_PIX_FMT_RGB555;
    case GST_VIDEO_FORMAT_NV16:         return V4L2_PIX_FMT_NV16;
    case GST_VIDEO_FORMAT_NV12:         return V4L2_PIX_FMT_NV12;
    case GST_VIDEO_FORMAT_UYVY:         return V4L2_PIX_FMT_UYVY;
    case GST_VIDEO_FORMAT_YVYU:         return V4L2_PIX_FMT_YVYU;
    case GST_VIDEO_FORMAT_YUY2:         return V4L2_PIX_FMT_YUYV;
    case GST_VIDEO_FORMAT_GRAY8:        return V4L2_PIX_FMT_GREY;
    case GST_VIDEO_FORMAT_GRAY16_BE:    return V4L2_PIX_FMT_Y10;
    default:                            return 0;
    }
}

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
static int imr_buffer_allocate(void *cdata, int i, GstBuffer *buffer)
{
    imr_sview_t    *sv = cdata;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    int             w = meta->width, h = meta->height, format = meta->format;
    int             j = meta->index;
    void           *planes[3] = { NULL, };
    
    if (i < IMR_ALPHA_0)
    {
        /* ...camera plane */
        BUG((u32)j >= (u32)VSP_POOL_SIZE, _x("invalid buffer: <%d,%d>"), i, j);

        /* ...save pointer to the memory buffer */
        meta->priv = sv->camera_plane[i >> 1][j];
    }
    else
    {
        /* ...alpha plane */
        BUG((u32)j >= (u32)VSP_POOL_SIZE, _x("invalid buffer: <%d,%d>"), i, j);

        /* ...save pointer to the memory buffer */
        meta->priv = sv->alpha_plane[(i - IMR_ALPHA_0) >> 1][j];
    }

    /* ...assign camera buffer pointer */
    planes[0] = meta->buf->data = vsp_mem_ptr(meta->priv);

    /* ...create external texture (for debugging purposes only? - tbd) */
    CHK_ERR(meta->priv2 = texture_create(w, h, planes, format), -errno);

    /* ...add custom buffer destructor */
    gst_mini_object_weak_ref(GST_MINI_OBJECT(buffer), __destroy_imr_buffer, sv);

    TRACE(INFO, _b("imr-buffer <%d:%d> allocated: %u*%u (format=%u, data=%p)"), i, j, w, h, meta->format, meta->buf->data);

    return 0;
}

/* ...buffer preparation callback */
static int imr_buffer_prepare(void *cdata, int i, GstBuffer *buffer)
{
    imr_sview_t    *sv = cdata;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    vsp_mem_t      *mem = meta->priv;
    int             j = meta->index;
    u32             sequence;
    
    /* ...protect internal data */
    pthread_mutex_lock(&sv->vsp_lock);

    /* ...latch current per-engine sequence number */
    sequence = sv->sequence_imr[i];

    /* ...setup engine if required */
    if ((sv->flags & APP_FLAG_UPDATE) && (sequence == sv->last_update))
    {
        imr_cfg_t  *cfg = sv->imr_cfg[i];
        
        /* ...configuration must be available */
        BUG(cfg == NULL, _x("imr-%d: no active configuration"), i);
            
        /* ...set new configuration (and release it) */
        imr_cfg_apply(sv->imr, i, cfg);
        imr_cfg_destroy(cfg);
        sv->imr_cfg[i] = NULL;
        TRACE(DEBUG, _b("imr-%d: cfg applied"), i);
    }

    /* ...update sequence number */
    sv->sequence_imr[i] = sequence + 1;    

    /* ...unlock internal data */
    pthread_mutex_unlock(&sv->vsp_lock);

    /* ...cleanup alpha-buffer (when it's a first buffer in a set) */
    if (i >= IMR_ALPHA_0)
    {
        u32     mask = (APP_FLAG_CLEAR_BUFFER << (((i - IMR_ALPHA_0) >> 1) + j * 2 + 2));

        /* ...camera-buffer preparation; reset memory if we didn't do that already */
        if (((sv->imr_flags ^= mask) & mask) != 0)
        {
            memset(vsp_mem_ptr(mem), 0, vsp_mem_size(mem));
            TRACE(DEBUG, _b("<%d,%d>: clear done (size=%u, addr=%p)"), i, j, vsp_mem_size(mem), vsp_mem_ptr(mem));
        }
        else
        {
            /* ...second buffer submitted; do not clear anything */
            TRACE(DEBUG, _b("<%d,%d>: no clear"), i, j);
        }
    }

    return 0;
}

/* ...output buffer callback */
static int imr_buffer_process(void *cdata, int i, GstBuffer *buffer)
{
    imr_sview_t     *sv = cdata;
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    int             j = meta->index;
    int             r;
    
    TRACE(DEBUG, _b("imr-buffer <%d:%d> ready: %p (refcount=%d)"), i, j, buffer, GST_MINI_OBJECT_REFCOUNT(buffer));

    /* ...lock VSP data access */
    pthread_mutex_lock(&sv->vsp_lock);

    /* ...submit buffer to a compositor (takes buffer ownership) */
    r = __vsp_submit_buffer(sv, i, buffer);

    /* ...release VSP data access lock */
    pthread_mutex_unlock(&sv->vsp_lock);

    return CHK_API(r);
}

/* ...engine callback structure */
static camera_callback_t   imr_cb = {
    .allocate = imr_buffer_allocate,
    .prepare = imr_buffer_prepare,
    .process = imr_buffer_process,
};

/*******************************************************************************
 * Compositor buffers pool
 ******************************************************************************/

/* ...output buffer dispose function */
static gboolean __vsp_buffer_dispose(GstMiniObject *obj)
{
    GstBuffer      *buffer = GST_BUFFER(obj);
    imr_sview_t     *sv = (imr_sview_t *)buffer->pool;
    gboolean        destroy = FALSE;

    /* ...lock VSP data access */
    pthread_mutex_lock(&sv->vsp_lock);
    
    /* ...submit output buffer to the compositor (adds ref) - tbd - graceful termination */
    __vsp_submit_buffer(sv, VSP_OUTPUT, buffer);

    /* ...unlock internal data */
    pthread_mutex_unlock(&sv->vsp_lock);

    return destroy;
}

/*******************************************************************************
 * Runtime initialization
 ******************************************************************************/

/* ...alpha input buffer disposal hook (called from a context of IMR thread) - function not needed at all? - tbd */
static gboolean __alpha_buffer_dispose(GstMiniObject *obj)
{
    GstBuffer      *buffer = GST_BUFFER(obj);
    //imr_sview_t     *sv = (imr_sview_t *)buffer->pool;
    gboolean        destroy = FALSE;

    /* ...lock internal data access */
    //pthread_mutex_lock(&sv->lock);

    /* ...process graceful termination - tbd */
    gst_buffer_ref(buffer);

    TRACE(DEBUG, _b("alpha-input buffer %p returned to pool"), buffer);

    /* ...unlock internal data */
    //pthread_mutex_unlock(&sv->lock);

    return destroy;
}

/* ...alpha-plane processing initialization */
static inline int sv_alpha_setup(imr_sview_t *sv, int W, int H)
{
    int             format = GST_VIDEO_FORMAT_GRAY8;
    u8             *alpha;
    GstBuffer      *buffer;
    vsink_meta_t   *vmeta;
    int             i, j;
    
    /* ...allocate single buffer for alpha-plane transformation */
    CHK_API(vsp_allocate_buffers(256, 1, V4L2_PIX_FMT_GREY, sv->alpha_input, 1));

    /* ...get writable pointer */
    alpha = vsp_mem_ptr(sv->alpha_input[0]);
    
    /* ...initialize buffer (single line) */
    for (j = 0; j < 256; j++)   alpha[j] = (u8)j;

    /* ...allocate single input buffer */
    CHK_ERR(buffer = gst_buffer_new(), -(errno = ENOMEM));

    /* ...add vsink meta-data (needed by IMR engine?) */
    vmeta = gst_buffer_add_vsink_meta(buffer);
    vmeta->plane[0] = alpha;
    vmeta->format = format;
    vmeta->width = 256;
    vmeta->height = 1;
    GST_META_FLAG_SET(vmeta, GST_META_FLAG_POOLED);

    /* ...modify buffer release callback */
    GST_MINI_OBJECT_CAST(buffer)->dispose = __alpha_buffer_dispose;

    /* ...save buffer custom data */
    (sv->alpha_buffer = buffer)->pool = (void *)sv;

    /* ...allocate two sets of alpha-planes for each bundle */
    CHK_API(vsp_allocate_buffers(W, H, V4L2_PIX_FMT_GREY, &sv->alpha_plane[0][0], 2 * VSP_POOL_SIZE));

    /* ...setup IMR engines */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        /* ...setup IMR engine (request two buffers) */
        CHK_API(imr_setup(sv->imr, IMR_ALPHA_0 + i, 256, 1, W, H, format, format, VSP_POOL_SIZE));
    }

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
    imr_sview_t     *sv = (imr_sview_t *)buffer->pool;
    gboolean        destroy = FALSE;

    /* ...lock internal data access */
    pthread_mutex_lock(&sv->lock);

    /* ...add buffer reference (tbd - process graceful termination command) */
    gst_buffer_ref(buffer);

    TRACE(0, _b("car image updated: flags=%X"), sv->flags);

    /* ...unlock internal data */
    pthread_mutex_unlock(&sv->lock);

    return destroy;
}

/* ...load car buffer with image */
static int sv_car_buffer_load(imr_sview_t *sv, GstBuffer *buffer, const char *path)
{
    imr_meta_t     *meta = gst_buffer_get_imr_meta(buffer);
    int             format = meta->format;
    int             w = meta->width, h = meta->height;
    void           *data = vsp_mem_ptr(meta->priv);
    u32             t0, t1;

    /* ...make sure we have a sane path pointer */
    CHK_ERR(path, -(errno = EINVAL));
    
    t0 = __get_time_usec();
    
    /* ...load PNG image into corresponding car plane */
    CHK_API(create_png(path, &w, &h, &format, &data));

    t1 = __get_time_usec();
    
    TRACE(INFO, _b("car-buffer ready: '%s' (time: %u)"), path, (u32)(t1 - t0));

    return 0;
}

/* ...car buffer update thread */
static void * car_update_thread(void *arg)
{
    imr_sview_t     *sv = arg;

    /* ...protect internal data access */
    pthread_mutex_lock(&sv->lock);
    
    /* ...wait for update event */
    while (1)
    {
        int     m;
        
        /* ...wait for car model update flag */
        while ((sv->flags & (APP_FLAG_CAR_UPDATE | APP_FLAG_EOS)) == 0)
        {
            pthread_cond_wait(&sv->update, &sv->lock);
        }

        /* ...check for a termination request (tbd) */
        if (sv->flags & APP_FLAG_EOS)
        {
            TRACE(INIT, _b("termination request received"));
            goto out;
        }

        /* ...get index of the buffer to load */
        m = (sv->flags & APP_FLAG_SET_INDEX ? 1 : 0);
        
        /* ...toggle buffers immediately */
        sv->flags ^= APP_FLAG_SET_INDEX;

        /* ...release internal data lock */
        pthread_mutex_unlock(&sv->lock);

        /* ...load car model (name is pretty fake) */
        if (sv_car_buffer_load(sv, sv->car_buffer[m], sv->car_image) != 0)
        {
            TRACE(ERROR, _x("car buffer loading failed: %m"));
        }

        /* ...reacquire application lock */
        pthread_mutex_lock(&sv->lock);

        /* ...submit buffer to a compositor pending queue */
        pthread_mutex_lock(&sv->vsp_lock);
        __vsp_submit_buffer(sv, VSP_CAR, sv->car_buffer[m]);
        pthread_mutex_unlock(&sv->vsp_lock);

        /* ...clear car-model update flag */
        sv->flags &= ~APP_FLAG_CAR_UPDATE;
    }

out:
    /* ...release data access lock */
    pthread_mutex_unlock(&sv->lock);

    TRACE(INIT, _b("car-model update thread terminated: %m"));
    
    return NULL;
}

/* ...car model initialization */
static int sv_car_setup(imr_sview_t *sv, int W, int H)
{
    pthread_attr_t  attr;
    int             j;
    int             r;

    /* ...car image plane allocation */
    CHK_API(vsp_allocate_buffers(W, H, V4L2_PIX_FMT_ARGB32, sv->car_plane, 2));

    /* ...create buffers for a car image */
    for (j = 0; j < 2; j++)
    {
        GstBuffer      *buffer = gst_buffer_new();
        imr_meta_t     *meta = gst_buffer_add_imr_meta(buffer);
        vsp_mem_t      *mem = sv->car_plane[j];
        int             format = GST_VIDEO_FORMAT_ARGB;
	void           *planes[3] = { NULL, };
    
        /* ...set memory descriptor */
        meta->priv = mem;
        meta->width = W;
        meta->height = H;
        meta->format = format;
        meta->index = j;
        GST_META_FLAG_SET(meta, GST_META_FLAG_POOLED);

	planes[0] = vsp_mem_ptr(meta->priv);

        /* ...create texture (for debugging porposes only?) */
	CHK_ERR(meta->priv2 = texture_create(W, H, planes, meta->format), -errno);

        /* ...modify buffer release callback */
        GST_MINI_OBJECT_CAST(buffer)->dispose = __car_buffer_dispose;

        /* ...save buffer pointer */
        (sv->car_buffer[j] = buffer)->pool = (void *)sv;
    }

    /* ...initialize thread attributes (joinable, 128KB stack) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr, 128 << 10);

    /* ...create car model update thread */
    r = pthread_create(&sv->car_thread, &attr, car_update_thread, sv);
    pthread_attr_destroy(&attr);
    CHK_API(r);

    return 0;
}


/* ...initialize runtime data */
static int sv_runtime_init(imr_sview_t *sv, int w, int h, u32 ifmt, int W, int H, int cw, int ch, __vec4 shadow)
{
    int     i, j;
    u32     ofmt = V4L2_PIX_FMT_ARGB32;

    /* ...create VSP compositor */
    CHK_ERR(sv->vsp = compositor_init(W, H, ifmt, W, H, ofmt, cw, ch, vsp_callback, sv), -errno);

    TRACE(INIT, _b("open mesh file: '%s'"), __mesh_file_name);
    
    /* ...load camera mesh data */
    sv->mesh = mesh_create(__mesh_file_name, shadow);

    /* ...create VSP memory pools for cameras planes (two sets hosting opposite cameras) */
    CHK_API(vsp_allocate_buffers(W, H, ifmt, &sv->camera_plane[0][0], 2 * VSP_POOL_SIZE));
    
    /* ...car image preparation */
    CHK_API(sv_car_setup(sv, cw, ch));

    /* ...create VSP memory pool for resulting image */
    CHK_API(vsp_allocate_buffers(W, H, ofmt, sv->output, VSP_POOL_SIZE));

    /* ...create output buffers */
    for (j = 0; j < VSP_POOL_SIZE; j++)
    {
        GstBuffer      *buffer = gst_buffer_new();
        imr_meta_t     *meta = gst_buffer_add_imr_meta(buffer);
        vsp_mem_t      *mem = sv->output[j];
        void           *planes[3];

        meta->width = W;
        meta->height = H;
        meta->format = GST_VIDEO_FORMAT_ARGB;
        meta->priv = mem;
        meta->index = j;
        GST_META_FLAG_SET(meta, GST_META_FLAG_POOLED);

        /* ...modify buffer release callback */
        GST_MINI_OBJECT_CAST(buffer)->dispose = __vsp_buffer_dispose;

	/* ...wrap buffer memory with a texture */
        planes[0] = vsp_mem_ptr(meta->priv);
        CHK_ERR(meta->priv2 = texture_create(W, H, planes, meta->format), -errno);

        /* ...save buffer custom data */
        (sv->buffer[j] = buffer)->pool = (void *)sv;

        /* ...submit buffer to the compositor */
        __vsp_submit_buffer(sv, VSP_OUTPUT, buffer);

        /* ...unref buffer (ownership passed to compositor) */
        gst_buffer_unref(buffer);
    }

    /* ...create IMR engines */
    CHK_ERR(sv->imr = imr_init(imr_dev_name, IMR_NUMBER, &imr_cb, sv), -errno);

    /* ...initialize IMR engines */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
	    int     fmt = __pixfmt_v4l2_to_gst(ifmt);
		    
        /* ...setup camera engine */
        CHK_API(imr_setup(sv->imr, i, w, h, W, H, fmt, fmt, VSP_POOL_SIZE));
    }

    /* ...alpha-plane processing setup */
    CHK_API(sv_alpha_setup(sv, W, H));

    /* ...start engine - tbd - move out of here */
    CHK_API(imr_start(sv->imr));

    /* ...set initial map */
    CHK_API(__sv_map_update(sv));

    TRACE(INFO, _b("run-time initialized"));

    return 0;
}

/*******************************************************************************
 * Animated transitions
 ******************************************************************************/

static gboolean animation_timer(void *data)
{
    imr_sview_t    *sv = data;

    /* ...obtain a lock */
    pthread_mutex_lock(&sv->lock);

    TRACE(DEBUG, _b("timer tick: %f/%f/%f/%f"), sv->rot_acc[0], sv->rot_acc[1], sv->rot_acc[2], sv->scl_acc);

#if 0
    /* ...calculate new position */
    sv->tr_rot_acc[0] -= (sv->tr_rot_acc[0] - __MATH_ZERO) / 4;
    sv->tr_rot_acc[1] -= (sv->tr_rot_acc[1] - __MATH_ZERO) / 4;
    sv->tr_rot_acc[2] -= (sv->tr_rot_acc[2] - __MATH_ZERO) / 4;
    sv->tr_scl_acc -= (sv->tr_scl_acc - __MATH_ONE) / 4;

    /* ...copy parameters */
    memcpy(&sv->rot_acc, &sv->tr_rot_acc, sizeof(sv->rot_acc));
    memcpy(&sv->scl_acc, &sv->tr_scl_acc, sizeof(sv->scl_acc));
#else
    (sv->rot_acc[0] > 180 ? sv->rot_acc[0] -= 360 : 0);
    (sv->rot_acc[1] > 180 ? sv->rot_acc[1] -= 360 : 0);
    (sv->rot_acc[2] > 180 ? sv->rot_acc[2] -= 360 : 0);

    sv->rot_acc[0] -= (sv->rot_acc[0] - __MATH_ZERO) / 2;
    sv->rot_acc[1] -= (sv->rot_acc[1] - __MATH_ZERO) / 2;
    sv->rot_acc[2] -= (sv->rot_acc[2] - __MATH_ZERO) / 2;

    (sv->rot_acc[0] > 0 ? sv->rot_acc[0] -= 360 : 0);
    (sv->rot_acc[1] < 0 ? sv->rot_acc[1] += 360 : 0);
    (sv->rot_acc[2] < 0 ? sv->rot_acc[2] += 360 : 0);

    sv->scl_acc -= (sv->scl_acc - __MATH_ONE) / 2;
#endif        
    /* ...update view */
    __sv_map_update(sv);

    /* ...check if we should stop the sequence */
    if (fabs(sv->rot_acc[0]) < 1.0 && fabs(sv->rot_acc[1]) < 1.0 && fabs(sv->rot_acc[2]) < 1.0 && fabs(sv->scl_acc - 1.0) < 0.1)
    {
        timer_source_stop(sv->timer);
    }
    
    /* ...release the lock */
    pthread_mutex_unlock(&sv->lock);

    /* ...source should not be deleted */
    return TRUE;
}

/*******************************************************************************
 * Input events processing
 ******************************************************************************/

/* ...3D-joystick input processing */
static inline int sv_spnav_event(imr_sview_t *sv, widget_spnav_event_t *event)
{
    spnav_event    *e = event->e;

    if (e->type == SPNAV_EVENT_MOTION)
    {
        int     rx = e->motion.rx, ry = e->motion.ry, rz = e->motion.rz;
        int     y = e->motion.y;

        /* ...discard too fast changes (should be done differently - tbd) */
        //if ((sv->spnav_delta += e->motion.period) < 50) return 0;

        /* ...clear delta */
        sv->spnav_delta = 0;

        /* ...ignore slight changes */
        (rx < 100 && rx > - 100 ? rx = 0 : 0);
        (ry < 100 && ry > - 100 ? ry = 0 : 0);
        (rz < 100 && rz > - 100 ? rz = 0 : 0);
        (y < 100 && y > - 100 ? y = 0 : 0);
        if ((rx | ry | rz | y) == 0)    return 0;

        TRACE(1, _b("spnav-event-motion: <x=%d,y=%d,z=%d>, <rx=%d,ry=%d,rz=%d>, p=%d"),
              e->motion.x, e->motion.y, e->motion.z,
              e->motion.rx, e->motion.ry, e->motion.rz,
              e->motion.period);

        /* ...update all meshes */
        pthread_mutex_lock(&sv->lock);

        /* ...rotate/scale model matrix */
        __sv_matrix_update(sv, rx, ry, rz, y);

        /* ...update meshes */
        __sv_map_update(sv);

        pthread_mutex_unlock(&sv->lock);
    }
    else if (e->type == SPNAV_EVENT_BUTTON && e->button.press == 1 && e->button.bnum == 0)
    {
        pthread_mutex_lock(&sv->lock);

        /* ...copy current accumulators */
        memcpy(&sv->tr_rot_acc, &sv->rot_acc, sizeof(sv->tr_rot_acc));
        memcpy(&sv->tr_scl_acc, &sv->scl_acc, sizeof(sv->tr_scl_acc));
        
        /* ...start reset sequence */
        timer_source_start(sv->timer, 30, 30);

#if 0        
        /* ...reset model view matrix */
        __sv_matrix_reset(sv);

        /* ...update all meshes */
        __sv_map_update(sv);
#endif

        pthread_mutex_unlock(&sv->lock);
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
        
        pthread_mutex_lock(&sv->lock);

        t0 = __get_time_usec();
        
        /* ...get current car plane index */
        m = !!(sv->flags & APP_FLAG_SET_INDEX);
        buffer = sv->car_buffer[m];
        meta = gst_buffer_get_imr_meta(buffer);
        
        /* ...get current car plane buffer */
        store_png(fname, meta->width, meta->height, meta->format, vsp_mem_ptr(meta->priv));

        t1 = __get_time_usec();
        
        pthread_mutex_unlock(&sv->lock);

        TRACE(INFO, _b("snapshot '%s' stored (%u usec)"), fname, (u32)(t1 - t0));
    }

    return 0;
}

/* ...touchscreen input processing */
static inline int sv_touch_event(imr_sview_t *sv, widget_touch_event_t *event)
{
    int     i;
    int     dx = 0, dy = 0;

    /* ...ignore 3-finger events */
    if ((i = event->id) >= 2)        return 0;

    /* ...lock internal data access */
    pthread_mutex_lock(&sv->lock);
    
    /* ...save the touch state */
    switch (event->type)
    {
    case WIDGET_EVENT_TOUCH_MOVE:
        /* ...get difference between positions */
        dx = event->x - sv->ts_pos[i][0], dy = event->y - sv->ts_pos[i][1];

        /* ...and pass through */

    case WIDGET_EVENT_TOUCH_DOWN:
        /* ...update touch location */
        sv->ts_pos[i][0] = event->x, sv->ts_pos[i][1] = event->y;

        /* ...save touch point state */
        sv->ts_flags |= (1 << i);
        break;

    case WIDGET_EVENT_TOUCH_UP:
    default:
        /* ...update touch state */
        sv->ts_flags &= ~(1 << i);
    }

    /* ...process two-fingers movement */
    if (sv->ts_flags == 0x3)
    {
        int     t;
        int     dist;
        
        /* ...get difference between touch-points */
        t = sv->ts_pos[1][0] - sv->ts_pos[0][0], dist = t * t;        
        t = sv->ts_pos[1][1] - sv->ts_pos[0][1], dist += t * t;

        /* ...check if the distance has been latched already */
        if (sv->ts_dist && (t = sv->ts_dist - dist) != 0)
        {
            /* ...update scaling coefficient only */
            __sv_matrix_update(sv, 0, 0, 0, 10000.0 * t / sv->ts_dist);
        }

        /* ...latch current distance */
        sv->ts_dist = dist;
    }
    else
    {
        /* ...reset latched distance */
        sv->ts_dist = 0;

        /* ...apply rotation around X and Z axis */
        (dx || dy ? __sv_matrix_update(sv, -dy * 100, 0, dx * 100, 0) : 0);
    }

    /* ...update meshes */
    __sv_map_update(sv);

    /* ...get internal data lock access */
    pthread_mutex_unlock(&sv->lock);

    /* ...single touch is used for rotation around x and z axis */
    return 0;
}

/* ...touchscreen input processing */
static inline int sv_kbd_event(imr_sview_t *sv, widget_key_event_t *event)
{
    if (event->type == WIDGET_EVENT_KEY_PRESS && event->state)
    {
        switch (event->code)
        {
        case KEY_ESC:
            /* ...terminate application (for the moment, just exit - tbd) */
            TRACE(INIT, _b("terminate application"));
            return -1;
        }        
    }

    return 0;
}

/*******************************************************************************
 * Module initialization
 ******************************************************************************/

/* ...output buffer accessor */
GstBuffer * imr_sview_buf_output(GstBuffer **buf)
{
    static int  count = 0;
    
    if (count < 0)
    {
        imr_meta_t     *meta = gst_buffer_get_imr_meta(buf[VSP_OUTPUT]);
        void           *data = vsp_mem_ptr(meta->priv);
        
        TRACE(1, _b("pixel-data: %08X:%08X:%08X..."), ((u32 *)data)[0], ((u32 *)data)[1], ((u32 *)data)[2]);
        
        store_png("snapshot.png", meta->width, meta->height, meta->format, data);

        count++;
    }
    return buf[VSP_OUTPUT];
}

/* ...input job submission */
int imr_sview_submit(imr_sview_t *sv, GstBuffer **buf)
{
    int     i;
    int     r = 0;
    
    /* ...protect internal data */
    pthread_mutex_lock(&sv->lock);

    /* ...push input buffers to the queue */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        g_queue_push_tail(&sv->input[i], gst_buffer_ref(buf[i]));
    }
    
    /* ...submit job if possible */
    if ((sv->input_ready &= ~((1 << CAMERAS_NUMBER) - 1)) == 0)
    {
        r = __sv_job_submit(sv);
    }

    /* ...release data access lock */
    pthread_mutex_unlock(&sv->lock);
    
    return CHK_API(r);
}

/* ...event-processing function */
int imr_sview_input_event(imr_sview_t *sv, widget_event_t *event)
{
    /* ...pass event to GUI layer first */
    switch (WIDGET_EVENT_TYPE(event->type))
    {
    case WIDGET_EVENT_SPNAV:
        return sv_spnav_event(sv, &event->spnav);

    case WIDGET_EVENT_TOUCH:
        return sv_touch_event(sv, &event->touch);

    case WIDGET_EVENT_KEY:
        return sv_kbd_event(sv, &event->key);

    default:
        return 0;
    }
}

/*******************************************************************************
 * UI commands
 ******************************************************************************/


int imr_sview_set_view(imr_sview_t *sv, __vec3 rot, __scalar scale, char *image)
{
    int     r;
    
    /* ...lock application data */
    pthread_mutex_lock(&sv->lock);
    
    /* ...set new matrix */
    __sv_matrix_set(sv, rot[0], rot[1], rot[2], scale);
    
    /* ...set car image */
    sv->car_image = image;

    /* ...trigger update sequence */
    r = __sv_map_update(sv);

    /* ...release application lock */
    pthread_mutex_unlock(&sv->lock);

    return CHK_API(r);
}

/*******************************************************************************
 * Module initialization function
 ******************************************************************************/

/* ...module initialization function */
imr_sview_t * imr_sview_init(const imr_sview_cb_t *cb, void *cdata, int w, int h, int ifmt, int W, int H, int cw, int ch, __vec4 shadow)
{
    imr_sview_t           *sv;
    pthread_mutexattr_t    attr;

    /* ...create local data handle */
    CHK_ERR(sv = calloc(1, sizeof(*sv)), (errno = ENOMEM, NULL));

    /* ...save callback data */
    sv->cb = cb, sv->cdata = cdata;

    /* ...set default flags */
    sv->flags = 0*APP_FLAG_DEBUG;

    /* ...reset input frames readiness state */
    sv->input_ready = (1 << CAMERAS_NUMBER) - 1;

    /* ...reset output frames readiness state */
    sv->vsp_ready = (1 << VSP_NUMBER) - 1;

    /* ...last update sequence number */
    sv->last_update = ~0U;

    /* ...initialize internal data access locks */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&sv->lock, &attr);
    pthread_mutex_init(&sv->vsp_lock, &attr);
    pthread_mutexattr_destroy(&attr);

    /* ...initialize conditional variable for mesh configuration update threads */
    pthread_cond_init(&sv->update, NULL);

    /* ...create animated transition timer (use default context) */
    if ((sv->timer = timer_source_create(animation_timer, sv, NULL, NULL)) == NULL)
    {
        TRACE(ERROR, _x("failed to create animation timer: %m"));
        goto error;
    }

    /* ...create map update thread */
    if (sv_map_init(sv, W, H) != 0)
    {
        TRACE(ERROR, _x("failed to create map update thread: %m"));
        goto error;
    }

    /* ...initialize run-time data */
    if (sv_runtime_init(sv, w, h, ifmt, W, H, cw, ch, shadow) != 0)
    {
        TRACE(ERROR, _x("failed to initialize engine: %m"));
        goto error;
    }
    
    TRACE(INIT, _b("module initialized"));

    return sv;

error:
    /* ...destroy data handle */
    free(sv);

    return NULL;
}
