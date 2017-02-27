/*******************************************************************************
 * utest-display.c
 *
 * Display support for unit-test application (Wayland-client)
 *
 * Copyright (c) 2014-2016 Cogent Embedded Inc. ALL RIGHTS RESERVED.
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

#define MODULE_TAG                      DISPLAY

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/epoll.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>

#include "utest-display.h"

#include <cairo-gl.h>

#include <gst/video/video-format.h>

//#include "utest-display.h"
//#include "utest-event.h"
#include <EGL/eglext_REL.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(EVENT, 1);
TRACE_TAG(DEBUG, 0);

/*******************************************************************************
 * Local constants
 ******************************************************************************/

/* ...predefined shaders */
#define SHADER_TEXTURE              0
#define SHADER_TEXTURE_GRAY         1
#define SHADER_VBO                  2
#define SHADERS_NUMBER              3

/*******************************************************************************
 * Local typedefs
 ******************************************************************************/

/* ...output device data */
typedef struct output_data
{
    /* ...list node */
    struct wl_list              link;

    /* ...Wayland output device handle */
    struct wl_output           *output;

    /* ...current output device width / height */
    u32                         width, height;

}   output_data_t;

/* ...input device data */
typedef struct input_data
{
    /* ...list node */
    struct wl_list              link;

    /* ...Wayland seat handle */
    struct wl_seat             *seat;

    /* ...seat capabilities */
    u32                         caps;

    /* ...pointer device interface */
    struct wl_pointer          *pointer;

    /* ...current focus for pointer device (should I make them different?) */
    widget_data_t              *pointer_focus;
    
    /* ...latched pointer position */
    int                         pointer_x, pointer_y;

    /* ...keyboard device interface */
    struct wl_keyboard         *keyboard;

    /* ...current focus for keyboard device (should I make them different?) */
    widget_data_t              *keyboard_focus;

    /* ...touch device interface */
    struct wl_touch            *touch;

    /* ...current focus widgets for touchscreen events */
    widget_data_t              *touch_focus;

}   input_data_t;

/* ...dispatch loop source */
typedef struct display_source_cb
{
    /* ...processing function */
    int           (*hook)(display_data_t *, struct display_source_cb *, u32 events);
    
}   display_source_cb_t;

/* ...texture shader data */
typedef struct shader_data
{
    /* ...shader program */
    GLuint          program;

    /* ...vertex/fragment shaders */
    GLuint          v_shader, f_shader;

    /* ...array of uniforms */
    GLint           uniform[];

}   shader_data_t;

/* ...display data */
struct display_data
{
    /* ...Wayland display handle */
    struct wl_display          *display;

    /* ...Wayland registry handle */
    struct wl_registry         *registry;

    /* ...screen compositor */
    struct wl_compositor       *compositor;

    /* ...subcompositor interface handle (not used) */
    struct wl_subcompositor    *subcompositor;

    /* ...shell interface handle */
    struct wl_shell            *shell;

    /* ...kms-buffers interface (not used?) */
    struct wl_kms              *kms;

    /* ...shared memory interface handle (not used?) */
    struct wl_shm              *shm;

    /* ...input/output device handles */
    struct wl_list              outputs, inputs;

    /* ...set of registered */
    struct wl_list              windows;

    /* ...EGL configuration data */
    egl_data_t                  egl;

    /* ...cairo device associated with EGL display */
    cairo_device_t             *cairo;

    /* ..."standard" shaders for textures drawing */
    shader_data_t              *shader[SHADERS_NUMBER];

    /* ...dispatch loop epoll descriptor */
    int                         efd;

    /* ...pending display event status */
    int                         pending;

    /* ...dispatch thread handle */
    pthread_t                   thread;

    /* ...display lock (need that really? - tbd) */
    pthread_mutex_t             lock;
};

/* ...widget data structure */
struct widget_data
{
    /* ...reference to owning window */
    window_data_t              *window;

    /* ...reference to parent widget */
    widget_data_t              *parent;
    
    /* ...pointer to the user-provided widget info */
    widget_info_t              *info;

    /* ...widget client data */
    void                       *cdata;

    /* ...cairo surface associated with this widget */
    cairo_surface_t            *cs;

    /* ...actual widget dimensions */
    int                         left, top, width, height;

    /* ...surface update request */
    int                         dirty;
};

/* ...output window data */
struct window_data
{
    /* ...root widget data (must be first) */
    widget_data_t               widget;

    /* ...reference to a display data */
    display_data_t             *display;

    /* ...list node in display windows list */
    struct wl_list              link;

    /* ...wayland surface */
    struct wl_surface          *surface;

    /* ...shell surface */
    struct wl_shell_surface    *shell;

    /* ...native EGL window */
    struct wl_egl_window       *native;

    /* ...window EGL context (used by native / cairo renderers) */
    EGLContext                  user_egl_ctx;

    /* ...EGL surface */
    EGLSurface                  egl;

    /* ...cairo device associated with current window context */
    cairo_device_t             *cairo;

    /* ...saved cairo program */
    GLint                       cprog;

    /* ...window information */
    const window_info_t        *info;

    /* ...client data for a callback */
    void                       *cdata;

    /* ...internal data access lock */
    pthread_mutex_t             lock;

    /* ...conditional variable for rendering thread */
    pthread_cond_t              wait;

    /* ...window rendering thread */
    pthread_t                   thread;

    /* ...processing flags */
    u32                         flags;

    /* ...frame-rate calculation */
    u32                         fps_ts, fps_acc;
};

/*******************************************************************************
 * Window processing flags
 ******************************************************************************/

/* ...redraw command pending */
#define WINDOW_FLAG_REDRAW              (1 << 0)

/* ...termination command pending */
#define WINDOW_FLAG_TERMINATE           (1 << 1)

/*******************************************************************************
 * Local variables
 ******************************************************************************/

/* ...this should be singleton for now - tbd */
static display_data_t   __display;

/*******************************************************************************
 * EGL functions binding (make them global; create EGL adaptation layer - tbd)
 ******************************************************************************/

/* ...EGL/GLES functions */
PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC eglSwapBuffersWithDamageEXT;
PFNGLMAPBUFFEROESPROC glMapBufferOES;
PFNGLUNMAPBUFFEROESPROC glUnmapBufferOES;
PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES;
PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES;
PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES;
PFNGLISVERTEXARRAYOESPROC glIsVertexArrayOES;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC glRenderbufferStorageMultisampleEXT;
PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT;
PFNGLDISCARDFRAMEBUFFEREXTPROC glDiscardFramebufferEXT;
PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;

/*******************************************************************************
 * Internal helpers
 ******************************************************************************/

/* ...regroup all that stuff down below - tbd */
static inline window_data_t * __window_lookup(struct wl_surface *surface)
{
    window_data_t  *window;
    
    if (!surface || !(window = wl_surface_get_user_data(surface)))  return NULL;
    if (window->surface != surface)     return NULL;
    return window;
}

/* ...get exclusive access to shared EGL context */
static inline void display_egl_ctx_get(display_data_t *display)
{
    /* ...we should not call that function in user-window context */
    BUG(eglGetCurrentContext() != EGL_NO_CONTEXT, _x("invalid egl context"));

    /* ...get shared context lock */
    pthread_mutex_lock(&display->lock);

    /* ...display context is shared with all windows; context is surfaceless */
    eglMakeCurrent(display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl.ctx);
}

/* ...release shared EGL context */
static inline void display_egl_ctx_put(display_data_t *display)
{
    /* ...display context is shared with all windows; context is surfaceless */
    eglMakeCurrent(display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    /* ...release shared context lock */
    pthread_mutex_unlock(&display->lock);
}

/*******************************************************************************
 * Display dispatch thread
 ******************************************************************************/

/* ...number of events expected */
#define DISPLAY_EVENTS_NUM      4

/* ...add handle to a display polling structure */
static inline int display_add_poll_source(display_data_t *display, int fd, display_source_cb_t *cb)
{
    struct epoll_event  event;
    
    event.events = EPOLLIN;
    event.data.ptr = cb;
    return epoll_ctl(display->efd, EPOLL_CTL_ADD, fd, &event);
}

/* ...remove handle from a display polling structure */
static inline int display_remove_poll_source(display_data_t *display, int fd)
{
    return epoll_ctl(display->efd, EPOLL_CTL_DEL, fd, NULL);
}

/* ...display dispatch thread */
static void * dispatch_thread(void *arg)
{
    display_data_t     *display = arg;
    struct epoll_event  event[DISPLAY_EVENTS_NUM];

    /* ...add display file descriptor */
    CHK_ERR(display_add_poll_source(display, wl_display_get_fd(display->display), NULL) == 0, NULL);

    /* ...start waiting loop */
    while (1)
    {
        int     disp = 0;
        int     i, r;

        /* ...as we are preparing to poll Wayland display, add polling prologue */
        while (wl_display_prepare_read(display->display) != 0)
        {
            /* ...dispatch all pending events and repeat attempt */
            wl_display_dispatch_pending(display->display);
        }

        /* ...flush all outstanding commands to a display */
        if (wl_display_flush(display->display) < 0)
        {
            TRACE(ERROR, _x("display flush failed: %m"));
            goto error;
        }

        /* ...wait for an event */
        if ((r = epoll_wait(display->efd, event, DISPLAY_EVENTS_NUM, -1)) < 0)
        {
            /* ...ignore soft interruptions */
            if (errno != EINTR)
            {
                TRACE(ERROR, _x("epoll failed: %m"));
                goto error;
            }
        }

        /* ...process all signalled events */
        for (i = 0; i < r; i++)
        {
            display_source_cb_t *dispatch = event[i].data.ptr;

            /* ...invoke event-processing function (ignore result code) */
            if (dispatch)
            {
                dispatch->hook(display, dispatch, event[i].events);
            }
            else if (event[i].events & EPOLLIN)
            {
                disp = 1;
            }
        }

        /* ...process display event separately */
        if (disp)
        {
            /* ...read display events */
            if (wl_display_read_events(display->display) < 0 && errno != EAGAIN)
            {
                TRACE(ERROR, _x("failed to read display events: %m"));
                goto error;
            }

            /* ...process pending display events (if any) */
            if (wl_display_dispatch_pending(display->display) < 0)
            {
                TRACE(ERROR, _x("failed to dispatch display events: %m"));
                goto error;
            }
        }
        else
        {
            /* ...if nothing was read from display, cancel initiated reading */
            wl_display_cancel_read(display->display);
        }
    }

    TRACE(INIT, _b("display dispatch thread terminated"));
    return NULL;

error:
    return (void *)(intptr_t)-errno;
}

/*******************************************************************************
 * Output device handling
 ******************************************************************************/

/* ...geometry change notification */
static void output_handle_geometry(void *data, struct wl_output *wl_output,
               int32_t x, int32_t y,
               int32_t physical_width, int32_t physical_height,
               int32_t subpixel,
               const char *make, const char *model,
               int32_t output_transform)
{
    output_data_t     *output = data;

    /* ...nothing but printing? */
    TRACE(INFO, _b("output[%p:%p]: %s:%s: x=%d, y=%d"), output, wl_output, make, model, x, y);
}

/* ...output device mode reporting processing */
static void output_handle_mode(void *data, struct wl_output *wl_output,
           uint32_t flags, int32_t width, int32_t height,
           int32_t refresh)
{
    output_data_t *output = data;

    /* ...check if the mode is current */
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0)    return;

    /* ...set current output device size */
    output->width = width, output->height = height;

    TRACE(INFO, _b("output[%p:%p] - %d*%d"), output, wl_output, width, height);
}

static const struct wl_output_listener output_listener =
{
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
};

/* ...add output device */
static inline void display_add_output(display_data_t *display, struct wl_registry *registry, uint32_t id)
{
    output_data_t   *output = calloc(1, sizeof(*output));

    BUG(!output, _x("failed to allocate memory"));

    output->output = wl_registry_bind(registry, id, &wl_output_interface, 1);
    wl_output_add_listener(output->output, &output_listener, output);
    wl_list_insert(display->outputs.prev, &output->link);

    /* ...force another round of display initialization */
    display->pending = 1;
}

/* ...get output device by number */
static output_data_t *display_get_output(display_data_t *display, int n)
{
    output_data_t  *output;

    /* ...traverse available outputs list */
    wl_list_for_each(output, &display->outputs, link)
        if (n-- == 0)
            return output;

    /* ...not found */
    return NULL;
}

/*******************************************************************************
 * Input device handling
 ******************************************************************************/

/* ...pointer entrance notification */
static void pointer_handle_enter(void *data, struct wl_pointer *pointer,
            uint32_t serial, struct wl_surface *surface,
            wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    input_data_t   *input = data;
    int             sx = wl_fixed_to_int(sx_w);
    int             sy = wl_fixed_to_int(sy_w);
    window_data_t  *window;
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(0, _b("input[%p]-enter: surface: %p, serial: %u, sx: %d, sy: %d"), input, surface, serial, sx, sy);
    
    /* ...check the surface is valid */
    if (!(window = __window_lookup(surface)))   return;

    /* ...latch pointer position */
    input->pointer_x = sx, input->pointer_y = sy;

    /* ...set current focus */
    focus = &window->widget;

    /* ...drop event if no processing is associated */
    if (!(info = focus->info) || !info->event)      return;

    /* ...pass event to the root widget */
    event.type = WIDGET_EVENT_MOUSE_ENTER;
    event.mouse.x = sx, event.mouse.y = sy;
    input->pointer_focus = info->event(focus, focus->cdata, &event);
}

/* ...pointer leave notification */
static void pointer_handle_leave(void *data, struct wl_pointer *pointer,
            uint32_t serial, struct wl_surface *surface)
{
    input_data_t   *input = data;
    window_data_t  *window;
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(0, _b("input[%p]-leave: surface: %p, serial: %u"), input, surface, serial);

    /* ...check the surface is valid */
    if (!(window = __window_lookup(surface)))   return;

    /* ...drop event if no focus is defined */
    if (!(focus = input->pointer_focus))    return;

    /* ...clear pointer-device focus */
    input->pointer_focus = NULL;

    /* ...drop event if no processing is associated */
    if (!(info = focus->info) || !info->event)  return;

    /* ...pass event to the current widget */
    event.type = WIDGET_EVENT_MOUSE_LEAVE;

    /* ...pass event to active widget */
    input->pointer_focus = info->event(focus, focus->cdata, &event);

    (focus != input->pointer_focus ? TRACE(DEBUG, _b("focus updated: %p"), input->pointer_focus) : 0);
}

/* ...handle pointer motion */
static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
            uint32_t time, wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    input_data_t   *input = data;
    int             sx = wl_fixed_to_int(sx_w);
    int             sy = wl_fixed_to_int(sy_w);
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(0, _b("input[%p]: motion: sx=%d, sy=%d"), input, sx, sy);

    /* ...drop event if no current focus set */
    if (!(focus = input->pointer_focus))    return;

    /* ...latch input position */
    input->pointer_x = sx, input->pointer_y = sy;

    /* ...drop event if no processing hook set */
    if (!(info = focus->info) || !info->event)  return;

    /* ...pass event to current widget */
    event.type = WIDGET_EVENT_MOUSE_MOVE;
    event.mouse.x = sx;
    event.mouse.y = sy;
    input->pointer_focus = info->event(focus, focus->cdata, &event);
}

/* ...button press/release processing */
static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
            uint32_t time, uint32_t button, uint32_t state)
{
    input_data_t   *input = data;
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(0, _b("input[%p]: serial=%u, button=%u, state=%u"), input, serial, button, state);

    /* ...drop event if no current focus set */
    if (!(focus = input->pointer_focus))    return;
    
    /* ...drop event if no processing hook set */
    if (!(info = focus->info) || !info->event)  return;

    /* ...pass event to current widget */
    event.type = WIDGET_EVENT_MOUSE_BUTTON;
    event.mouse.x = input->pointer_x;
    event.mouse.y = input->pointer_y;
    event.mouse.button = button;
    event.mouse.state = (state == WL_POINTER_BUTTON_STATE_PRESSED);   
    input->pointer_focus = info->event(focus, focus->cdata, &event);
}

/* ...button wheel (?) processing */
static void pointer_handle_axis(void *data, struct wl_pointer *pointer,
            uint32_t time, uint32_t axis, wl_fixed_t value)
{
    input_data_t   *input = data;
    int             v = wl_fixed_to_int(value);
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(0, _x("input[%p]: axis=%u, value=%d"), input, axis, v);

    /* ...drop event if no current focus set */
    if (!(focus = input->pointer_focus))    return;
    
    /* ...drop event if no processing hook set */
    if (!(info = focus->info) || !info->event)  return;

    /* ...pass event to current widget */
    event.type = WIDGET_EVENT_MOUSE_AXIS;
    event.mouse.x = input->pointer_x;
    event.mouse.y = input->pointer_y;
    event.mouse.axis = axis;
    event.mouse.value = v;
    input->pointer_focus = info->event(focus, focus->cdata, &event);
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_handle_enter,
    pointer_handle_leave,
    pointer_handle_motion,
    pointer_handle_button,
    pointer_handle_axis,
};

/*******************************************************************************
 * Touchscreen support
 ******************************************************************************/

static void touch_handle_down(void *data, struct wl_touch *wl_touch,
          uint32_t serial, uint32_t time, struct wl_surface *surface,
          int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
    input_data_t   *input = data;
    int             sx = wl_fixed_to_int(x_w);
    int             sy = wl_fixed_to_int(y_w);
    window_data_t  *window;
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;
 
    TRACE(0, _b("input[%p]-touch-down: surface=%p, id=%u, sx=%d, sy=%d"), input, surface, id, sx, sy);
    
    /* ...get window associated with a surface */
    if (!(window = __window_lookup(surface)))   return;

    /* ...get touch focus if needed */
    focus = (input->touch_focus ? : &window->widget);

    /* ...drop event if no processing is registered */
    if (!(info = focus->info) || !info->event)  return;
    
    /* ...pass event to root widget */
    event.type = WIDGET_EVENT_TOUCH_DOWN;
    event.touch.x = sx;
    event.touch.y = sy;
    event.touch.id = id;

    input->touch_focus = info->event(focus, focus->cdata, &event);

    if (!input->touch_focus)    TRACE(DEBUG, _x("touch focus lost!"));
}

/* ...touch removal event notification */
static void touch_handle_up(void *data, struct wl_touch *wl_touch,
        uint32_t serial, uint32_t time, int32_t id)
{
    input_data_t   *input = data;
    widget_data_t  *focus = input->touch_focus;
    widget_info_t  *info;
    widget_event_t  event;
    
    TRACE(0, _b("input[%p]-touch-up: serial=%u, id=%u"), input, serial, id);

    /* ...drop event if no focus defined */
    if (!(focus = input->touch_focus))      return;

    /* ...reset touch focus pointer */
    input->touch_focus = NULL;
    
    /* ...drop event if no processing is registered */
    if (!(info = focus->info) || !info->event)  return;

    /* ...pass event to current widget */
    event.type = WIDGET_EVENT_TOUCH_UP;
    event.touch.id = id;
    input->touch_focus = info->event(focus, focus->cdata, &event);

    if (!input->touch_focus)    TRACE(DEBUG, _x("touch focus lost!"));
}

/* ...touch sliding event processing */
static void touch_handle_motion(void *data, struct wl_touch *wl_touch,
            uint32_t time, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
    input_data_t   *input = data;
    int             sx = wl_fixed_to_int(x_w);
    int             sy = wl_fixed_to_int(y_w);
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;
    
    TRACE(0, _b("input[%p]-move: id=%u, sx=%d, sy=%d (focus: %p)"), input, id, sx, sy, input->touch_focus);

    /* ...ignore event if no touch focus exists */
    if (!(focus = input->touch_focus))      return;

    /* ...drop event if no processing is registered */
    if (!(info = focus->info) || !info->event)  return;

    /* ...pass event to current widget */
    event.type = WIDGET_EVENT_TOUCH_MOVE;
    event.touch.x = sx;
    event.touch.y = sy;
    event.touch.id = id;
    input->touch_focus = info->event(focus, focus->cdata, &event);

    if (!input->touch_focus)    TRACE(DEBUG, _x("touch focus lost!"));
}

/* ...end of touch frame (gestures recognition?) */
static void touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
    input_data_t   *input = data;

    TRACE(DEBUG, _b("input[%p]-touch-frame"), input);
}

/* ...touch-frame cancellation (gestures recognition?) */
static void touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
    input_data_t   *input = data;

    TRACE(DEBUG, _b("input[%p]-frame-cancel"), input);
}

/* ...wayland touch device listener callbacks */
static const struct wl_touch_listener touch_listener = {
    touch_handle_down,
    touch_handle_up,
    touch_handle_motion,
    touch_handle_frame,
    touch_handle_cancel,
};

/*******************************************************************************
 * Keyboard events processing
 ******************************************************************************/

/* ...keymap handling */
static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
            uint32_t format, int fd, uint32_t size)
{
    input_data_t   *input = data;

    /* ...here we can remap keycodes - tbd */
    TRACE(DEBUG, _b("input[%p]: keymap format: %X, fd=%d, size=%u"), input, format, fd, size);
}

/* ...keyboard focus receive notification */
static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
            uint32_t serial, struct wl_surface *surface,
            struct wl_array *keys)
{
    input_data_t   *input = data;
    window_data_t  *window;
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(DEBUG, _b("input[%p]: key-enter: surface: %p"), input, surface);

    /* ...get window associated with a surface */
    if (!(window = __window_lookup(surface)))   return;

	/* ...set focus to root widget (? - tbd) */
	input->keyboard_focus = focus = &window->widget;

    /* ...drop event if no processing is registered */
    if (!(info = focus->info) || !info->event)      return;
    
    /* ...pass event to current widget */
    event.type = WIDGET_EVENT_KEY_ENTER;
    input->keyboard_focus = info->event(focus, focus->cdata, &event);

    /* ...process all pressed keys? modifiers? */
}

/* ...keyboard focus leave notification */
static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
            uint32_t serial, struct wl_surface *surface)
{
    input_data_t   *input = data;
    window_data_t  *window;
    widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(DEBUG, _b("input[%p]: key-leave: surface: %p"), input, surface);

    /* ...find a target widget */
    if (!(window = __window_lookup(surface)))   return;

    /* ...select active widget (root widget if nothing) */
    focus = (input->keyboard_focus ? : &window->widget);

    /* ...reset keyboard focus */
    input->keyboard_focus = NULL;

    /* ...drop message if no processing is defined */
    if (!(info = focus->info) || !info->event)  return;

    /* ...pass event to current widget */
    event.type = WIDGET_EVENT_KEY_LEAVE;
    input->keyboard_focus = info->event(focus, focus->cdata, &event);
}

/* ...key pressing event */
static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
            uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	input_data_t   *input = data;
	widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(DEBUG, _b("input[%p]: key-press: key=%u, state=%u"), input, key, state);

    /* ...ignore event if no focus defined */
    if (!(focus = input->keyboard_focus))   return;
    
    /* ...drop event if no processing is registered */
    if (!(info = focus->info) || !info->event)  return;

	/* ...pass notification to the widget */
    event.type = WIDGET_EVENT_KEY_PRESS;
    event.key.code = key;
    event.key.state = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
    input->keyboard_focus = info->event(focus, focus->cdata, &event);
}

/* ...modifiers state change */
static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
            uint32_t serial, uint32_t mods_depressed,
            uint32_t mods_latched, uint32_t mods_locked,
            uint32_t group)
{
	input_data_t   *input = data;
	widget_data_t  *focus;
    widget_info_t  *info;
    widget_event_t  event;

    TRACE(DEBUG, _b("input[%p]: mods-press: press=%X, latched=%X, locked=%X, group=%X"), input, mods_depressed, mods_latched, mods_locked, group);

    /* ...ignore event if no focus defined */
    if (!(focus = input->keyboard_focus))   return;
    
    /* ...drop event if no processing is registered */
    if (!(info = focus->info) || !info->event)  return;

	/* ...pass notification to the widget */
    event.type = WIDGET_EVENT_KEY_MODS;
    event.key.mods_on = mods_latched;
    event.key.mods_off = mods_depressed;
    event.key.mods_locked = mods_locked;
	input->keyboard_focus = info->event(focus, focus->cdata, &event);
}

/* ...keyboard listener callback */
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
};

/*******************************************************************************
 * Input device registration
 ******************************************************************************/

/* ...input device capabilities registering */
static void seat_handle_capabilities(void *data, struct wl_seat *seat, enum wl_seat_capability caps)
{
    input_data_t   *input = data;

    /* ...process pointer device addition/removal */
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !input->pointer)
    {
        input->pointer = wl_seat_get_pointer(seat);
        wl_pointer_set_user_data(input->pointer, input);
        wl_pointer_add_listener(input->pointer, &pointer_listener, input);
        TRACE(INFO, _b("pointer-device %p added"), input->pointer);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && input->pointer)
    {
        TRACE(INFO, _b("pointer-device %p removed"), input->pointer);
        wl_pointer_destroy(input->pointer);
        input->pointer = NULL;
    }

    /* ...process keyboard addition/removal */
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !input->keyboard)
    {
        input->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_set_user_data(input->keyboard, input);
        wl_keyboard_add_listener(input->keyboard, &keyboard_listener, input);
        TRACE(INFO, _b("keyboard-device %p added"), input->keyboard);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && input->keyboard)
    {
        TRACE(INFO, _b("keyboard-device %p removed"), input->keyboard);
        wl_keyboard_destroy(input->keyboard);
        input->keyboard = NULL;
    }

    /* ...process touch device addition/removal */
    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !input->touch)
    {
        input->touch = wl_seat_get_touch(seat);
        wl_touch_set_user_data(input->touch, input);
        wl_touch_add_listener(input->touch, &touch_listener, input);
        TRACE(INFO, _b("touch-device %p added"), input->touch);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && input->touch)
    {
        TRACE(INFO, _b("touch-device %p removed"), input->touch);
        wl_touch_destroy(input->touch);
        input->touch = NULL;
    }
}

/* ...input device name (probably, for a mapping to particular output? - tbd) */
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name)
{
    input_data_t   *input = data;

    /* ...just output a name */
    TRACE(INFO, _b("input[%p]: device '%s' registered"), input, name);
}

/* ...input device wayland callback */
static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
    seat_handle_name
};

/* ...register input device */
static inline void display_add_input(display_data_t *display, struct wl_registry *registry, uint32_t id, uint32_t version)
{
    input_data_t   *input = calloc(1, sizeof(*input));

    BUG(!input, _x("failed to allocate memory"));

    /* ...bind seat interface */
    input->seat = wl_registry_bind(registry, id, &wl_seat_interface, MIN(version, 3));
    wl_seat_add_listener(input->seat, &seat_listener, input);
    wl_list_insert(display->inputs.prev, &input->link);

    /* ...force another round of display initialization */
    display->pending = 1;
}

/*******************************************************************************
 * Spacenav 3D-joystick support
 ******************************************************************************/

/* ...spacenav input event processing */
static int input_spacenav_event(display_data_t *display, display_source_cb_t *cb, u32 events)
{
    widget_event_t  event;
    spnav_event     e;
    window_data_t  *window;
    
    /* ...drop event if no reading flag set */
    if ((events & EPOLLIN) == 0)        return 0;
    
    /* ...retrieve poll event */
    if (CHK_API(spnav_poll_event(&e)) == 0)     return 0;

    /* ...preare widget event */
    event.type = WIDGET_EVENT_SPNAV;
    event.spnav.e = &e;

    /* ...pass to all windows */
    wl_list_for_each(window, &display->windows, link)
    {
        widget_data_t  *widget = &window->widget;
        widget_info_t  *info = widget->info;

        /* ...ignore window if no input event is registered */
        if (!info || !info->event)      continue;

        /* ...pass event to root widget (only one consumer?) */
        if (info->event(widget, window->cdata, &event) != NULL)   break;
    }

    return 0;
}

static display_source_cb_t spacenav_source = {
    .hook = input_spacenav_event,
};

/* ...spacenav event initializer */
static inline int input_spacenav_init(display_data_t *display)
{
    int     fd;
    
    /* ...open spacenav device (do not die if not found) */
    if (spnav_open() < 0)
    {
        TRACE(INIT, _b("spacenavd daemon is not running"));
        return 0;
    }
    
    if ((fd = spnav_fd()) < 0)
    {
        TRACE(ERROR, _x("failed to open spacenv connection: %m"));
        goto error;
    }
    
    /* ...add file-descriptor as display poll source */
    if (display_add_poll_source(display, fd, &spacenav_source) < 0)
    {
        TRACE(ERROR, _x("failed to add poll source: %m"));
        goto error;
    }
    
    TRACE(INIT, _b("spacenav input added"));
    
    return 0;

error:
    /* ...destroy connection to a server */
    spnav_close();

    return -errno;
}

/*******************************************************************************
 * Joystick support
 ******************************************************************************/

typedef struct joystick_data
{
    /* ...generic display source handle */
    display_source_cb_t     source;

    /* ...file descriptor */
    int                     fd;

    /* ...any axis? - button maps? - tbd - need to keep latched values */

}   joystick_data_t;

/* ...joystick input event processing */
static int input_joystick_event(display_data_t *display, display_source_cb_t *cb, u32 events)
{
    joystick_data_t    *js = (joystick_data_t *)cb;
    widget_event_t      event;
    struct js_event     e;
    window_data_t      *window;
    
    /* ...drop event if no reading flag set */
    if ((events & EPOLLIN) == 0)        return 0;

    /* ...retrieve poll event */
    CHK_ERR(read(js->fd, &e, sizeof(e)) == sizeof(e), -errno);

    /* ...preare widget event */
    event.type = WIDGET_EVENT_JOYSTICK;
    event.js.e = &e;

    TRACE(DEBUG, _b("joystick event: type=%x, value=%x, number=%x"), e.type & ~JS_EVENT_INIT, e.value, e.number);    

    /* ...pass to all windows */
    wl_list_for_each(window, &display->windows, link)
    {
        widget_data_t  *widget = &window->widget;
        widget_info_t  *info = widget->info;
        
        /* ...ignore window if no input event is registered */
        if (!info || !info->event)      continue;

        /* ...pass event to root widget (only one consumer?) */
        if (info->event(widget, window->cdata, &event) != NULL)   break;
    }

    return 0;
}

static joystick_data_t joystick_source = {
    .source = {
        .hook = input_joystick_event,
    },
};

/* ...spacenav event initializer */
static inline int input_joystick_init(display_data_t *display, const char *devname)
{
    int     fd;
    int     version = 0x800;
    int     axes = 2, buttons = 2;
    char    name[128] = { '\0' };

    /* ...open joystick device */
    if ((joystick_source.fd = fd = open(devname, O_RDONLY)) < 0)
    {
        TRACE(INIT, _b("no joystick connected"));
        return 0;
    }

    ioctl(fd, JSIOCGVERSION, &version);
	ioctl(fd, JSIOCGAXES, &axes);
	ioctl(fd, JSIOCGBUTTONS, &buttons);
	ioctl(fd, JSIOCGNAME(sizeof(name)), name);

    TRACE(INIT, _b("device: %s; version: %X, buttons: %d, axes: %d, name: %s"), devname, version, buttons, axes, name);
    
    /* ...put joystick into non-blocking mode */
    fcntl(fd, F_SETFL, O_NONBLOCK);

    /* ...add file descriptor to display poll set */
    if (display_add_poll_source(display, fd, &joystick_source.source) < 0)
    {
        TRACE(ERROR, _x("failed to add joystick: %m"));
        goto error;
    }

    TRACE(INIT, _b("joystick device '%s' added"), devname);
    
    return 0;

error:
    /* close device descriptor */
    close(fd);
    return -errno;
}

/*******************************************************************************
 * Registry listener callbacks
 ******************************************************************************/

/* ...interface registrar */
static void global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                                    const char *interface, uint32_t version)
{
    display_data_t     *display = data;

    if (strcmp(interface, "wl_compositor") == 0)
    {
        display->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    }
    else if (strcmp(interface, "wl_subcompositor") == 0)
    {
        display->subcompositor = wl_registry_bind(registry, id, &wl_subcompositor_interface, 1);
    }
    else if (strcmp(interface, "wl_shell") == 0)
    {
        display->shell = wl_registry_bind(registry, id, &wl_shell_interface, 1);
    }
    else if (strcmp(interface, "wl_output") == 0)
    {
        display_add_output(display, registry, id);
    }
    else if (strcmp(interface, "wl_seat") == 0)
    {
        display_add_input(display, registry, id, version);
    }
}

/* ...interface removal notification callback */
static void global_registry_remove(void *data, struct wl_registry *registry, uint32_t id)
{
    display_data_t     *display = data;

    TRACE(INIT, _b("display[%p]: id removed: %u"), display, id);
}

/* ...registry listener callbacks */
static const struct wl_registry_listener registry_listener =
{
    global_registry_handler,
    global_registry_remove
};

/*******************************************************************************
 * Shell surface interface implementation
 ******************************************************************************/

/* ...shell surface heartbeat callback */
static void handle_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
}

/* ...shell surface reconfiguration callback */
static void handle_configure(void *data, struct wl_shell_surface *shell_surface,
         uint32_t edges, int32_t width, int32_t height)
{
    TRACE(INFO, _b("shell configuration changed: W=%d, H=%d, E=%u"), width, height, edges);
}

/* ...focus removal notification */
static void handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
    TRACE(INFO, _b("focus removed - hmm..."));
}

/* ...shell surface callbacks */
static const struct wl_shell_surface_listener shell_surface_listener = {
    handle_ping,
    handle_configure,
    handle_popup_done
};

/*******************************************************************************
 * EGL helpers
 ******************************************************************************/

static const EGLint     __egl_context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};

/* ...destroy EGL context */
static void fini_egl(display_data_t *display)
{
    eglTerminate(display->egl.dpy);
    eglReleaseThread();
}

/* ...initialize EGL */
static int init_egl(display_data_t *display)
{
    /* ...EGL configuration attributes */
    EGLint                  config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
 		EGL_BUFFER_SIZE, 32,
 		EGL_DEPTH_SIZE, 16,
 		EGL_RED_SIZE, 8,
 		EGL_GREEN_SIZE, 8,
 		EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint          major, minor, n, count, i, size;
    EGLConfig      *configs;
    EGLDisplay      dpy;
    const char     *extensions;

    /* ...get Wayland EGL display */
    CHK_ERR(display->egl.dpy = dpy = eglGetDisplay(display->display), -ENOENT);

    /* ...initialize EGL module? */
    if (!eglInitialize(dpy, &major, &minor))
    {
        TRACE(ERROR, _x("failed to initialize EGL: %m (%X)"), eglGetError());
        goto error;
    }
    else if (!eglBindAPI(EGL_OPENGL_ES_API))
    {
        TRACE(ERROR, _x("failed to bind API: %m (%X)"), eglGetError());
        goto error;
    }
    else
    {
        TRACE(INIT, _b("EGL display opened: %p, major:minor=%u:%u"), dpy, major, minor);
    }

    /* ...get total number of configurations */
    if (!eglGetConfigs(dpy, NULL, 0, &count))
    {
        TRACE(ERROR, _x("failed to get EGL configs number"));
        goto error;
    }
    else if (count == 0)
    {
        TRACE(ERROR, _x("no configurations found"));
        goto error;
    }

    /* ...retrieve available configurations */
    if ((configs = calloc(count, sizeof(*configs))) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate %zu bytes"), sizeof(*configs));
        goto error;
    }
    else if (!eglChooseConfig(dpy, config_attribs, configs, count, &n))
    {
        TRACE(ERROR, _x("failed to get matching configuration"));
        goto error_cfg;
    }
    else if (n == 0)
    {
        TRACE(ERROR, _x("no matching configurations"));
        goto error_cfg;
    }

    /* ...select configuration? */
    for (i = 0; i < n; i++)
    {
        EGLint      id = -1;

        eglGetConfigAttrib(dpy, configs[i], EGL_NATIVE_VISUAL_ID, &id);

        /* ...get buffer size of that configuration */
        eglGetConfigAttrib(dpy, configs[i], EGL_BUFFER_SIZE, &size);

        TRACE(INFO, _b("config[%u of %u]: id=%X, size=%X"), i, n, id, size);

        /* ...check if we have a 32-bit buffer size - tbd */
        if (size != 32)     continue;

        /* ...found a suitable configuration - print it? */
        display->egl.conf = configs[i];

        goto found;
    }

    TRACE(ERROR, _x("did not find suitable configuration"));
    errno = -ENODEV;
    goto error_cfg;

found:
    /* ...bind extensions */
    eglCreateImageKHR = (void *)eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (void *)eglGetProcAddress("eglDestroyImageKHR");
    eglSwapBuffersWithDamageEXT = (void *)eglGetProcAddress("eglSwapBuffersWithDamageEXT");
    glEGLImageTargetTexture2DOES = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    glMapBufferOES = (void *)eglGetProcAddress("glMapBufferOES");
    glUnmapBufferOES = (void *)eglGetProcAddress("glUnmapBufferOES");
    glBindVertexArrayOES = (void *)eglGetProcAddress("glBindVertexArrayOES");
    glDeleteVertexArraysOES = (void *)eglGetProcAddress("glDeleteVertexArraysOES");
    glGenVertexArraysOES = (void *)eglGetProcAddress("glGenVertexArraysOES");
    glIsVertexArrayOES = (void *)eglGetProcAddress("glIsVertexArrayOES");
    glRenderbufferStorageMultisampleEXT = (void *)eglGetProcAddress("glRenderbufferStorageMultisampleEXT");
    glFramebufferTexture2DMultisampleEXT = (void *)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
    glDiscardFramebufferEXT = (void *)eglGetProcAddress("glDiscardFramebufferEXT");

    eglCreateSyncKHR = (void *)eglGetProcAddress("eglCreateSyncKHR");
    eglDestroySyncKHR = (void *)eglGetProcAddress("eglDestroySyncKHR");
    eglClientWaitSyncKHR = (void *)eglGetProcAddress("eglClientWaitSyncKHR");

    /* ...make sure we have eglImageKHR extension */
    BUG(!(eglCreateImageKHR && eglDestroyImageKHR), _x("breakpoint"));

    /* ...check for specific EGL extensions */
    if ((extensions = eglQueryString(display->egl.dpy, EGL_EXTENSIONS)) != NULL)
    {
        TRACE(INIT, _b("EGL extensions: %s"), extensions);
    }

    /* ...create display (shared?) EGL context */
    if ((display->egl.ctx = eglCreateContext(dpy, display->egl.conf, EGL_NO_CONTEXT, __egl_context_attribs)) == NULL)
    {
        TRACE(ERROR, _x("failed to create EGL context: %m/%X"), eglGetError());
        goto error_cfg;
    }

    /* ...free configuration array (ha-ha display->egl.conf) */
    free(configs);

#if 0
    /* ...create dedicated cairo device EGL context */
    if ((display->egl.cairo_ctx = eglCreateContext(dpy, display->egl.conf, EGL_NO_CONTEXT, context_attribs)) == NULL)
    {
        TRACE(ERROR, _x("failed to create cairo EGL context: %m"));
        goto error_cfg;
    }

    /* ...create cairo device */
    display->cairo = cairo_egl_device_create(dpy, display->egl.cairo_ctx);
    if (cairo_device_status(display->cairo) != CAIRO_STATUS_SUCCESS)
    {
        TRACE(ERROR, _x("failed to create cairo device: %m"));
        goto error;
    }
#endif

    TRACE(INIT, _b("EGL initialized"));

    return 0;

error_cfg:
    /* ...destroy configuration array */
    free(configs);

error:
    /* ...close a display */
    fini_egl(display);

    return -1;
}

/*******************************************************************************
 * Shaders support
 ******************************************************************************/

/* ...shader compilation code */
static inline GLuint compile_shader(GLenum type, int count, const char * const *sources, char *msg, int len)
{
    GLuint      s;
    GLint       status;

    CHK_ERR(s = glCreateShader(type), GL_NONE);
    glShaderSource(s, count, sources, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        glGetShaderInfoLog(s, len, NULL, msg);
        glDeleteShader(s);
        TRACE(ERROR, _b("shader compilation error: %s"), msg);
        return GL_NONE;
    }
    else
    {
        return s;
    }
}

/* ...create shader object */
shader_data_t * shader_create(const shader_desc_t *desc)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();
    shader_data_t      *shader;
    char                msg[512];
    GLint               status;
    GLuint              program;
    const char * const *attr = desc->attr;
    const char * const *uni = desc->uni;
    int                 i;
    
    /* ...sanity check */
    CHK_ERR(desc && desc->attr && desc->uni && desc->v_src && desc->f_src, (errno = EINVAL, NULL));

    /* ...allocate shader data structure */
    CHK_ERR(shader = calloc(1, sizeof(*shader) + sizeof(GLint) * desc->uni_num), (errno = ENOMEM, NULL));

    /* ...get GL context if needed */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);
    
    /* ...vertex/fragment shaders compilation (single source) */
    if ((shader->v_shader = compile_shader(GL_VERTEX_SHADER, 1, desc->v_src, msg, sizeof(msg))) == GL_NONE)
    {
        TRACE(ERROR, _x("failed to compile vertex shader"));
        errno = EINVAL;
        goto error;
    }
    else if ((shader->f_shader = compile_shader(GL_FRAGMENT_SHADER, 1, desc->f_src, msg, sizeof(msg))) == GL_NONE)
    {
        TRACE(ERROR, _x("failed to compile fragment shader"));
        errno = EINVAL;
        goto error;
    }

    /* ...create a program */
    if ((shader->program = program = glCreateProgram()) == GL_NONE)
    {
        TRACE(ERROR, _x("failed to create a program: %X"), glGetError());
        errno = EINVAL;
        goto error;
    }

    /* ...attach vertex/fragment shaders to program */
    glAttachShader(program, shader->v_shader);
    glAttachShader(program, shader->f_shader);

    /* ...bind all user-supplied attributes */
    for (i = 0; i < desc->attr_num; i++)
    {
        glBindAttribLocation(program, i, *attr++);

        /* ...emit warning if something goes wrong */
        if ((status = glGetError()) != GL_NO_ERROR)
        {
            TRACE(ERROR, _x("attribute #%d ('%s') binding failed: %X"), i, attr[-1], status);
        }
    }

    /* ...link a program */
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status)
    {
        glGetProgramInfoLog(program, sizeof(msg), NULL, msg);
        TRACE(ERROR, _x("program link error: %s"), msg);
        errno = EINVAL;
        goto error;
    }

    /* ...locate user-provided uniforms */
    for (i = 0; i < desc->uni_num; i++)
    {
        /* ...emit warning if uniform is not found */
        if ((shader->uniform[i] = glGetUniformLocation(program, *uni++)) < 0)
        {
            TRACE(WARNING, _b("uniform #%d ('%s') not found"), i, uni[-1]);
        }    
    }

    /* ...all correct; release shared context as needed */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    TRACE(INFO, _b("shader[%p] created (%d attributes, %d uniforms)"), shader, desc->attr_num, desc->uni_num);
    
    return shader;

error:
    /* ...destroy vertex shader as needed */
    (shader->v_shader ? glDeleteShader(shader->v_shader) : 0);
    
    /* ...destroy fragment shader as needed */
    (shader->f_shader ? glDeleteShader(shader->f_shader) : 0);

    /* ...destroy program */
    (shader->program ? glDeleteProgram(shader->program) : 0);

    /* ...release shared context as needed */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    /* ...free data structure */
    free(shader);
    
    return NULL;
}

/* ...shader destruction */
void shader_destroy(shader_data_t *shader)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();

    /* ...get GL context if needed */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    /* ...destroy vertex/fragment shaders */
    glDeleteShader(shader->v_shader);
    glDeleteShader(shader->f_shader);

    /* ...destroy program */
    glDeleteProgram(shader->program);

    /* ...release shared context as needed */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    /* ...free data structure */
    free(shader);

    TRACE(INFO, _b("shader[%p] destroyed"), shader);
}

/* ...get shader program */
GLuint shader_program(shader_data_t *shader)
{
    return shader->program;
}

/* ...get shader uniforms locations array */
const GLint * shader_uniforms(shader_data_t *shader)
{
    return shader->uniform;
}

/*******************************************************************************
 * Predefined ("standard") shaders
 ******************************************************************************/

/* ...vertex shader program for textures visualization */
static const char * texture_vertex_shader =
    "uniform mat4 proj;\n"
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = proj * vec4(position, 0.0, 1.0);\n"
    "   v_texcoord = texcoord;\n"
    "}\n";

/* ...fragment shader for color textures visualization (YUV etc) */
static const char * texture_fragment_shader_ext =
    "#extension GL_OES_EGL_image_external : enable\n"
    "varying mediump vec2 v_texcoord;\n"
    "uniform samplerExternalOES tex;\n"
    "uniform mediump float alpha;\n"
    "void main()\n"
    "{\n"
    "   gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, alpha);\n"
    "}\n";

/* ...fragment shader for grayscale textures visualization */
static const char * texture_fragment_shader_gray_ext =
    "#extension GL_OES_EGL_image_external : enable\n"
    "varying mediump vec2 v_texcoord;\n"
    "uniform samplerExternalOES tex;\n"
    "uniform mediump float alpha;\n"
    "void main()\n"
    "{\n"
    "   mediump float  r = texture2D(tex, v_texcoord).r;\n"
    "   gl_FragColor = vec4(r, r, r, alpha);\n"
    "}\n";


/* ...build color texture rendering shader */
#define SHADER_TAG                      TEX
#define SHADER_VERTEX_SOURCE            texture_vertex_shader
#define SHADER_FRAGMENT_SOURCE          texture_fragment_shader_ext

/* ...attributes definition */
#define SHADER_ATTRIBUTES                           \
    __A(position),      /* ...vertex position */    \
    __A(texcoord),      /* ...texture coordinate */

/* ...uniforms definition */
#define SHADER_UNIFORMS                                                     \
    __U(proj),          /* ...PVM matrix */                                 \
    __U(tex),           /* ...2D-texture sampler */                         \
    __U(alpha),         /* ...transparency value to use for a texture */

/* ...instantiate indices/names lists */
#include "shader-impl.h"

/* ...build grayscale texture rendering shader (attributes and uniforms are same as for TEX) */
#define SHADER_TAG                      GRAY
#define SHADER_VERTEX_SOURCE            texture_vertex_shader
#define SHADER_FRAGMENT_SOURCE          texture_fragment_shader_gray_ext

/* ...attributes definition (important to make same as for TEX-shader) */
#define SHADER_ATTRIBUTES                           \
    __A(position),      /* ...vertex position */    \
    __A(texcoord),      /* ...texture coordinate */

/* ...uniforms definition (important to make same as for TEX-shader) */
#define SHADER_UNIFORMS                                                     \
    __U(proj),          /* ...PVM matrix */                                 \
    __U(tex),           /* ...2D-texture sampler */                         \
    __U(alpha),         /* ...transparency value to use for a texture */

/* ...instantiate indices/names lists */
#include "shader-impl.h"

/*******************************************************************************
 * VBO shader (for points/lines drawing)
 ******************************************************************************/

/* ...VBO vertex shader */
static const char *vbo_vertex_shader =
    "precision mediump float;\n"	
    "attribute vec3	v;\n"
    "uniform mat4	proj;\n"
    "varying vec3	vertex;\n"
    "void main(void)\n"
    "{\n"
    "   vec4 t = proj * vec4(v.xyz, 1.0);\n"
    "	gl_Position = vec4(t.x, -t.y, t.z, t.w);\n"
    "   gl_PointSize = clamp(4.0 - gl_Position.z * 3.0, 0.0, 4.0);\n"
    "	vertex = v;\n"
    "}\n";

/* ...VBO fragment shader */
static const char *vbo_fragment_shader =
    "precision mediump float;\n"	
    "uniform float maxdist;\n"
    "uniform vec4 color;\n"
    "varying vec3 vertex;\n"
    "void main()\n"
    "{\n"
    "   float distNorm = clamp(length(vertex.xy)/maxdist, 0.0, 1.0);\n"
    "   gl_FragColor = color * (1.0-distNorm);\n"
    "}\n";

/* ...specify shader name */
#define SHADER_TAG                  VBO
#define SHADER_VERTEX_SOURCE        vbo_vertex_shader
#define SHADER_FRAGMENT_SOURCE      vbo_fragment_shader

/* ...define list of attributes */
#define SHADER_ATTRIBUTES                               \
    __A(v),                     /* ...vertex vector */

/* ...define list of uniforms */
#define SHADER_UNIFORMS                                                             \
    __U(proj),                  /* ...projection matrix */                          \
    __U(maxdist),               /* ...clamping for maximal distance from pole */    \
    __U(color),                 /* ...color for a point/line */

/* ...build shader lists */
#include "shader-impl.h"

/*******************************************************************************
 * Compile code
 ******************************************************************************/
    
/* ...compile predefined shaders */
static int compile_shaders(display_data_t *display)
{
    /* ...build all preset shaders */
    CHK_ERR(display->shader[SHADER_TEXTURE] = shader_create(&SHADER_DESC(TEX)), -errno);
    CHK_ERR(display->shader[SHADER_TEXTURE_GRAY] = shader_create(&SHADER_DESC(GRAY)), -errno);
    CHK_ERR(display->shader[SHADER_VBO] = shader_create(&SHADER_DESC(VBO)), -errno);

    TRACE(INIT, _b("shaders built"));

    return 0;
}

/*******************************************************************************
 * Display dispatch thread
 ******************************************************************************/

/* ...return cairo device associated with a display */
cairo_device_t  *__display_cairo_device(display_data_t *display)
{
    return display->cairo;
}

egl_data_t  * display_egl_data(display_data_t *display)
{
    return &display->egl;
}

/* ...return cairo device associated with a display */
cairo_device_t  *__window_cairo_device(window_data_t *window)
{
    BUG(cairo_device_status(window->cairo) != CAIRO_STATUS_SUCCESS, _x("invalid device[%p] state: %s"), window->cairo, cairo_status_to_string(cairo_device_status(window->cairo)));
    
    return window->cairo;
}

/* ...return EGL surface associated with window */
EGLSurface window_egl_surface(window_data_t *window)
{
    return window->egl;
}

EGLContext window_egl_context(window_data_t *window)
{
    return window->user_egl_ctx;
}

/*******************************************************************************
 * Window support
 ******************************************************************************/

/* ...window rendering thread */
static void * window_thread(void *arg)
{
    window_data_t      *window = arg;
    display_data_t     *display = window->display;

    while (1)
    {
        /* ...serialize access to window state */
        pthread_mutex_lock(&window->lock);

        /* ...wait for a drawing command from an application */
        while (!(window->flags & (WINDOW_FLAG_REDRAW | WINDOW_FLAG_TERMINATE)))
        {
            TRACE(DEBUG, _b("window[%p] wait"), window);
            pthread_cond_wait(&window->wait, &window->lock);
        }

        TRACE(DEBUG, _b("window[%p] redraw (flags=%X)"), window, window->flags);

        /* ...break processing thread if requested to do that */
        if (window->flags & WINDOW_FLAG_TERMINATE)
        {
            pthread_mutex_unlock(&window->lock);
            break;
        }

        /* ...clear window drawing schedule flag */
        window->flags &= ~WINDOW_FLAG_REDRAW;

        /* ...release window access lock */
        pthread_mutex_unlock(&window->lock);

        /* ...re-acquire window GL context */
        eglMakeCurrent(display->egl.dpy, window->egl, window->egl, window->user_egl_ctx);

        /* ...invoke user-supplied hook */
        window->info->redraw(display, window->cdata);

        /* ...realease window GL context (not needed, actually) */
        //eglMakeCurrent(display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    TRACE(INIT, _b("window[%p] thread terminated"), window);

    /* ...release context eventually */
    eglMakeCurrent(display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    return NULL;
}

/*******************************************************************************
 * Internal helpers - getting messy - tbd
 ******************************************************************************/

/* ...check surface status */
static inline int __check_surface(cairo_surface_t *cs)
{
    cairo_status_t  status;
    
    switch (status = cairo_surface_status(cs))
    {
    case CAIRO_STATUS_SUCCESS:          return 0;
    case CAIRO_STATUS_READ_ERROR:       errno = EINVAL; break;
    case CAIRO_STATUS_FILE_NOT_FOUND:   errno = ENOENT; break;
    default:                            errno = ENOMEM; break;
    }

    TRACE(ERROR, _b("cairo surface error: '%s'"), cairo_status_to_string(status));
    
    return -errno;
}

/* ...check cairo device status */
static inline int __check_device(cairo_device_t *cairo)
{
    cairo_status_t  status;
    
    switch (status = cairo_device_status(cairo))
    {
    case CAIRO_STATUS_SUCCESS:          return 0;
    case CAIRO_STATUS_DEVICE_ERROR:     errno = EINVAL; break;
    default:                            errno = ENOMEM; break;
    }

    TRACE(ERROR, _b("cairo device error: '%s'"), cairo_status_to_string(status));
    
    return -errno;
}

/* ...surface status error */
static inline const char * __surface_strerr(cairo_surface_t *cs)
{
    return cairo_status_to_string(cairo_surface_status(cs));
}

/*******************************************************************************
 * Basic widgets support
 ******************************************************************************/

/* ...internal widget initialization function */
static int __widget_init(widget_data_t *widget, window_data_t *window, int W, int H, widget_info_t *info, void *cdata)
{
    cairo_device_t *cairo = window->cairo;
    int             w, h;

    /* ...set user-supplied data */
    widget->info = info, widget->cdata = cdata;

    /* ...set pointer to the owning window */
    widget->window = window;

    /* ...if width/height are not specified, take them from window */
    widget->width = w = (info && info->width ? info->width : W);
    widget->height = h = (info && info->height ? info->height : H);
    widget->top = (info ? info->top : 0);
    widget->left = (info ? info->left : 0);

    /* ...create cairo surface for a graphical content */
    if (widget == &window->widget)
    {
        widget->cs = cairo_gl_surface_create_for_egl(cairo, window->egl, w, h);
    }
    else
    {
        widget->cs = cairo_gl_surface_create(cairo, CAIRO_CONTENT_COLOR_ALPHA, w, h);
    }
    
    if (__check_surface(widget->cs) != 0)
    {
        TRACE(ERROR, _x("failed to create GL-surface [%u*%u]: %m"), w, h);
        return -errno;
    }

    /* ...initialize widget controls as needed */
    if (info && info->init)
    {
        if (info->init(widget, cdata) < 0)
        {
            TRACE(ERROR, _x("widget initialization failed: %m"));
            goto error_cs;
        }
     
        /* ...mark widget is dirty */
        widget->dirty = 1;
    }
    else
    {
        /* ...clear dirty flag */
        widget->dirty = 0;
    }

    TRACE(INIT, _b("widget [%p] initialized"), widget);

    return 0;

error_cs:
    /* ...destroy cairo surface */
    cairo_surface_destroy(widget->cs);
    
    return -errno;
}

/* ...create widget */
widget_data_t * widget_create(window_data_t *window, widget_info_t *info, void *cdata)
{
    int             w = window->widget.width;
    int             h = window->widget.height;;
    widget_data_t  *widget;

    /* ...allocate data handle */
    CHK_ERR(widget = malloc(sizeof(*widget)), (errno = ENOMEM, NULL));

    /* ...initialize widget data */
    if (__widget_init(widget, window, w, h, info, cdata) < 0)
    {
        TRACE(ERROR, _x("widget initialization error: %m"));
        goto error;
    }

    return widget;

error:
    /* ...destroy widget data */
    free(widget);

    return NULL;
}

/* ...widget destructor */
void widget_destroy(widget_data_t *widget)
{
    widget_info_t  *info = widget->info;
    
    /* ...invoke custom destructor function as needed */
    (info && info->destroy ? info->destroy(widget, widget->cdata) : 0);
    
    /* ...destroy cairo surface */
    cairo_surface_destroy(widget->cs);

    /* ...release data handle */
    free(widget);

    TRACE(INIT, _b("widget[%p] destroyed"), widget);
}

/* ...render widget content into given target context */
void widget_render(widget_data_t *widget, cairo_t *cr, float alpha)
{
    widget_info_t      *info = widget->info;

    /* ...update widget content as needed */
    widget_update(widget, 0);

    /* ...output widget content in current drawing context */
    cairo_save(cr);
    cairo_set_source_surface(cr, widget->cs, info->left, info->top);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

/* ...update widget content */
void widget_update(widget_data_t *widget, int flush)
{
    cairo_t    *cr;

    /* ...do nothing if update is not required */
    if (!widget->dirty)     return;

    /* ...clear dirty flag in advance */
    widget->dirty = 0;

    /* ...get curface drawing context */
    cr = cairo_create(widget->cs);

    /* ...update widget content */
    widget->info->draw(widget, widget->cdata, cr);

    /* ...make sure context is sane */
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS)
    {
        TRACE(ERROR, _x("widget[%p]: bad context: '%s'"), widget, cairo_status_to_string(cairo_status(cr)));
    }

    /* ...destroy context */
    cairo_destroy(cr);

    /* ...force widget surface update */
    (0 && flush ? cairo_surface_flush(widget->cs) : 0);
}

/* ...schedule widget redrawing */
void widget_schedule_redraw(widget_data_t *widget)
{
    /* ...mark widget is dirty */
    widget->dirty = 1;

    /* ...schedule redrawing of the parent window */
    window_schedule_redraw(widget->window);
}

/* ...input event processing */
widget_data_t * widget_input_event(widget_data_t *widget, widget_event_t *event)
{
    widget_info_t  *info = widget->info;
    
    return (info && info->event ? info->event(widget, widget->cdata, event) : NULL);
}

/* ...return current widget width */
int widget_get_width(widget_data_t *widget)
{
    return widget->width;
}

/* ...return current widget height */
int widget_get_height(widget_data_t *widget)
{
    return widget->height;
}

/* ...return left point */
int widget_get_left(widget_data_t *widget)
{
    return widget->left;
}

/* ...return top point */
int widget_get_top(widget_data_t *widget)
{
    return widget->top;
}

/* ...get cairo device associated with widget */
cairo_device_t * widget_get_cairo_device(widget_data_t *widget)
{
    return widget->window->cairo;
}

/* ...get parent window root widget */
widget_data_t * widget_get_parent(widget_data_t *widget)
{
    return &widget->window->widget;
}

/*******************************************************************************
 * Entry points
 ******************************************************************************/

/* ...create native window */
window_data_t * window_create(display_data_t *display, window_info_t *info, widget_info_t *info2, void *cdata)
{
    int                 width = info->width;
    int                 height = info->height;
    output_data_t      *output;
    window_data_t      *window;
    struct wl_region   *region;
    pthread_attr_t      attr;
    int                 r;

    /* ...make sure we have a valid output device */
    if ((output = display_get_output(display, info->output)) == NULL)
    {
        TRACE(ERROR, _b("invalid output device number: %u"), info->output);
        errno = EINVAL;
        return NULL;
    }

    /* ...if width/height are not specified, use output device dimensions */
    (!width ? width = output->width : 0), (!height ? height = output->height : 0);

    /* ...allocate a window data */
    if ((window = malloc(sizeof(*window))) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate memory"));
        errno = ENOMEM;
        return NULL;
    }

    /* ...initialize window data access lock */
    pthread_mutex_init(&window->lock, NULL);

    /* ...initialize conditional variable for communication with rendering thread */
    pthread_cond_init(&window->wait, NULL);

    /* ...save display handle */
    window->display = display;

    /* ...save window info data */
    window->info = info, window->cdata = cdata;

    /* ...clear window flags */
    window->flags = 0;

    /* ...reset frame-rate calculator */
    window_frame_rate_reset(window);

    /* ...get wayland surface (subsurface maybe?) */
    window->surface = wl_compositor_create_surface(display->compositor);

    /* ...specify window has the only opaque region */
    region = wl_compositor_create_region(display->compositor);
    wl_region_add(region, 0, 0, width, height);
    wl_surface_set_opaque_region(window->surface, region);
    wl_region_destroy(region);
    
    /* ...get desktop shell surface handle */
    window->shell = wl_shell_get_shell_surface(display->shell, window->surface);
    wl_shell_surface_add_listener(window->shell, &shell_surface_listener, window);
    (info->title ? wl_shell_surface_set_title(window->shell, info->title) : 0);
    wl_shell_surface_set_toplevel(window->shell);
    (info->fullscreen ? wl_shell_surface_set_fullscreen(window->shell, WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 0, output->output) : 0);

    /* ...set private data poitner */
    wl_surface_set_user_data(window->surface, window);

    /* ...create native window */
    window->native = wl_egl_window_create(window->surface, width, height);
    window->egl = eglCreateWindowSurface(display->egl.dpy, display->egl.conf, window->native, NULL);

    /* ...create window user EGL context (share textures with everything else?)*/
    window->user_egl_ctx = eglCreateContext(display->egl.dpy, display->egl.conf, display->egl.ctx, __egl_context_attribs);

    /* ...create cairo context */
    window->cairo = cairo_egl_device_create(display->egl.dpy, window->user_egl_ctx);
    if (__check_device(window->cairo) != 0)
    {
        TRACE(ERROR, _x("failed to create cairo device: %m"));
        goto error;
    }

    /* ...make it simple - we are handling thread context ourselves */
    cairo_gl_device_set_thread_aware(window->cairo, FALSE);

    /* ...reset cairo program */
    window->cprog = 0;

    /* ...set window EGL context */
    eglMakeCurrent(display->egl.dpy, window->egl, window->egl, window->user_egl_ctx);
   
    /* ...initialize root widget data */
    if (__widget_init(&window->widget, window, width, height, info2, cdata) < 0)
    {
        TRACE(INIT, _b("widget initialization failed: %m"));
        goto error;
    }    

    /* ...clear surface to flush all textures loading etc.. - looks a bit strange */
    if (1)
    {
        cairo_t    *cr = cairo_create(window->widget.cs);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
    }

    /* ...releaset window EGL context */
    eglMakeCurrent(display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    /* ...initialize thread attributes (joinable, default stack size) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* ...create rendering thread */
    r = pthread_create(&window->thread, &attr, window_thread, window);
    pthread_attr_destroy(&attr);
    if (r != 0)
    {
        TRACE(ERROR, _x("thread creation failed: %m"));
        goto error;
    }

    /* ...add window to global display list */
    wl_list_insert(display->windows.prev, &window->link);

    TRACE(INFO, _b("window created: %p:%p, %u * %u, output: %u"), window, window->egl, width, height, info->output);

    return window;

error:
    /* ...destroy window memory */
    free(window);
    return NULL;
}

/* ...hide window */
int window_set_invisible(window_data_t *window)
{
    /* ...we just pass null buffer */
    wl_surface_attach(window->surface, NULL, 0, 0);
    wl_surface_damage(window->surface, 0, 0, window->widget.width, window->widget.height);
    wl_surface_commit(window->surface);
    
    TRACE(DEBUG, _b("window minimized"));

    return 0;
}

static void __destroy_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
   pthread_mutex_t  *wait_lock = data;

   TRACE(DEBUG, _b("release wait lock"));

   /* ...release mutex */
   pthread_mutex_unlock(wait_lock);

   wl_callback_destroy(callback);
}

static const struct wl_callback_listener __destroy_listener = {
    __destroy_callback,
};

/* ...destroy a window */
void window_destroy(window_data_t *window)
{
    display_data_t         *display = window->display;
    EGLDisplay              dpy = display->egl.dpy;
    const window_info_t    *info = window->info;
    const widget_info_t    *info2 = window->widget.info;
    struct wl_callback     *callback;

    /* ...terminate window rendering thread */
    pthread_mutex_lock(&window->lock);
    window->flags |= WINDOW_FLAG_TERMINATE;
    pthread_cond_signal(&window->wait);
    pthread_mutex_unlock(&window->lock);

    /* ...wait until thread completes */
    pthread_join(window->thread, NULL);

    TRACE(DEBUG, _b("window[%p] thread joined"), window);

    /* ...remove window from global display list */
    wl_list_remove(&window->link);

    /* ...acquire window context before doing anything */
    eglMakeCurrent(dpy, window->egl, window->egl, window->user_egl_ctx);

    /* ...invoke custom widget destructor function as needed */
    (info2 && info2->destroy ? info2->destroy(&window->widget, window->cdata) : 0);

    /* ...destroy root widget cairo surface */
    cairo_surface_destroy(window->widget.cs);

    /* ...invoke custom window destructor function as needed */
    (info && info->destroy ? info->destroy(window, window->cdata) : 0);

    /* ...destroy cairo device */
    cairo_device_destroy(window->cairo);

    /* ...release EGL context before destruction */
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    /* ...destroy context */
    eglDestroyContext(dpy, window->user_egl_ctx);

    /* ...destroy EGL surface */
    eglDestroySurface(display->egl.dpy, window->egl);

    /* ...destroy native window */
    wl_egl_window_destroy(window->native);

    /* ...destroy shell surface */
    wl_shell_surface_destroy(window->shell);

    /* ....destroy wayland surface (shell surface gets destroyed automatically) */
    wl_surface_destroy(window->surface);

    /* ...make sure function is complete before we actually proceed */
    if (1 && (callback = wl_display_sync(display->display)) != NULL)
    {
        pthread_mutex_t     wait_lock = PTHREAD_MUTEX_INITIALIZER;

        pthread_mutex_lock(&wait_lock);
        wl_callback_add_listener(callback, &__destroy_listener, &wait_lock);

        wl_display_flush(display->display);

        /* ...mutex will be released in callback function executed from display thread context */
        pthread_mutex_lock(&wait_lock);
    }

    /* ...destroy window lock */
    pthread_mutex_destroy(&window->lock);

    /* ...destroy rendering thread conditional variable */
    pthread_cond_destroy(&window->wait);

    /* ...destroy object */
    free(window);

    TRACE(INFO, _b("window[%p] destroyed"), window);
}

/* ...return current window width */
int window_get_width(window_data_t *window)
{
    return window->widget.width;
}

/* ...return current window height */
int window_get_height(window_data_t *window)
{
    return window->widget.height;
}

/* ...schedule redrawal of the window */
void window_schedule_redraw(window_data_t *window)
{
    /* ...acquire window lock */
    pthread_mutex_lock(&window->lock);

    /* ...check if we don't have a flag already */
    if ((window->flags & WINDOW_FLAG_REDRAW) == 0)
    {
        /* ...set a flag */
        window->flags |= WINDOW_FLAG_REDRAW;

        /* ...and kick processing thread */
        pthread_cond_signal(&window->wait);

        TRACE(DEBUG, _b("schedule window[%p] redraw"), window);
    }

    /* ...release window access lock */
    pthread_mutex_unlock(&window->lock);
}

/* ...submit window to a renderer */
void window_draw(window_data_t *window)
{
    u32                 t0, t1;

    t0 = __get_cpu_cycles();

    /* ...swap buffers (finalize any pending 2D-drawing) */
    cairo_gl_surface_swapbuffers(window->widget.cs);
    
    /* ...make sure everything is correct */
    BUG(cairo_surface_status(window->widget.cs) != CAIRO_STATUS_SUCCESS, _x("bad status: %s"), cairo_status_to_string(cairo_surface_status(window->widget.cs)));

    t1 = __get_cpu_cycles();

    TRACE(DEBUG, _b("swap[%p]: %u (error=%X)"), window, t1 - t0, eglGetError());
}

/* ...retrieve associated cairo surface */
cairo_t * window_get_cairo(window_data_t *window)
{
    cairo_t    *cr;

    /* ...re-acquire window GL context */
    eglMakeCurrent(window->display->egl.dpy, window->egl, window->egl, window->user_egl_ctx);

    /* ...it is a bug if we lost a context */
    BUG(eglGetCurrentContext() != window->user_egl_ctx, _x("invalid GL context"));
    
    /* ...restore original cairo program */
    glUseProgram(window->cprog);
    
    /* ...create new drawing context */
    cr = cairo_create(window->widget.cs);

    /* ...make it a bug for a moment */
    BUG(cairo_status(cr) != CAIRO_STATUS_SUCCESS, _x("invalid status: (%d) - %s"), cairo_status(cr), cairo_status_to_string(cairo_status(cr)));

    return cr;
}

/* ...release associated cairo surface */
void window_put_cairo(window_data_t *window, cairo_t *cr)
{
    /* ...destroy cairo drawing interface */
    cairo_destroy(cr);

    /* ...re-acquire window GL context */
    eglMakeCurrent(window->display->egl.dpy, window->egl, window->egl, window->user_egl_ctx);

    /* ...save cairo program */
    glGetIntegerv(GL_CURRENT_PROGRAM, &window->cprog);
}

/*******************************************************************************
 * Display module initialization
 ******************************************************************************/

/* ...create display data */
display_data_t * display_create(void)
{
    display_data_t     *display = &__display;
    pthread_attr_t      attr;
    int                 r;

    /* ...reset display data */
    memset(display, 0, sizeof(*display));

    /* ...connect to Wayland display */
    if ((display->display = wl_display_connect(NULL)) == NULL)
    {
        TRACE(ERROR, _x("failed to connect to Wayland: %m"));
        errno = EBADFD;
        goto error;
    }
    else if ((display->registry = wl_display_get_registry(display->display)) == NULL)
    {
        TRACE(ERROR, _x("failed to get registry: %m"));
        errno = EBADFD;
        goto error_disp;
    }
    else
    {
        /* ...set global registry listener */
        wl_registry_add_listener(display->registry, &registry_listener, display);
    }

    /* ...initialize inputs/outputs lists */
    wl_list_init(&display->outputs);
    wl_list_init(&display->inputs);

    /* ...initialize windows list */
    wl_list_init(&display->windows);

    /* ...create a display command/response lock */
    pthread_mutex_init(&display->lock, NULL);

    /* ...create polling structure */
    if ((display->efd = epoll_create(DISPLAY_EVENTS_NUM)) < 0)
    {
        TRACE(ERROR, _x("failed to create epoll: %m"));
        goto error_disp;
    }

    /* ...pre-initialize global Wayland interfaces */
    do
    {
        display->pending = 0, wl_display_roundtrip(display->display);
    }
    while (display->pending);

    /* ...initialize EGL */
    if (init_egl(display) < 0)
    {
        TRACE(ERROR, _x("EGL initialization failed: %m"));
        goto error_disp;
    }

    /* ...make current display context */
    eglMakeCurrent(display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl.ctx);

    /* ...dump available GL extensions */
    TRACE(INIT, _b("GL version: %s"), (char *)glGetString(GL_VERSION));
    TRACE(INIT, _b("GL extension: %s"), (char *)glGetString(GL_EXTENSIONS));

    /* ...compile default shaders */
    if (compile_shaders(display) < 0)
    {
        TRACE(ERROR, _x("default shaders compilation failed"));
        goto error_egl;
    }

    /* ...release display EGL context */
    eglMakeCurrent(display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    /* ...initialize thread attributes (joinable, default stack size) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* ...create Wayland dispatch thread */
    r = pthread_create(&display->thread, &attr, dispatch_thread, display);
    pthread_attr_destroy(&attr);
    if (r != 0)
    {
        TRACE(ERROR, _x("thread creation failed: %m"));
        goto error_egl;
    }

    /* ...wait until display thread starts? */
    TRACE(INIT, _b("Wayland display interface initialized"));

    /* ...initialize extra input devices */
    input_spacenav_init(display);

    /* ...joystick device requires start-up events generation (should be window-specific) */
    input_joystick_init(display, joystick_dev_name);
    
    /* ...doesn't look good, actually - don't want to start thread right here */
    return display;

error_egl:
    /* ...destroy EGL context */
    fini_egl(display);

error_disp:
    /* ...disconnect display */
    wl_display_flush(display->display);
    wl_display_disconnect(display->display);

error:
    return NULL;
}

/*******************************************************************************
 * Textures handling
 ******************************************************************************/

/* ...calculate cropping and viewport parameters for a texture */
void texture_set_view(texture_view_t *vcoord, float x0, float y0, float x1, float y1)
{
    GLfloat    *p;

    /* ...adjust coordinates to GL-space */
    x0 = x0 * 2 - 1, y0 = y0 * 2 - 1;
    x1 = x1 * 2 - 1, y1 = y1 * 2 - 1;

    /* ...fill-in vertex coordinates map */
    p = *vcoord;
    *p++ = x0, *p++ = y0;
    *p++ = x1, *p++ = y0;
    *p++ = x0, *p++ = y1;
    *p++ = x0, *p++ = y1;
    *p++ = x1, *p++ = y0;
    *p++ = x1, *p++ = y1;
}

/* ...set texture cropping data */
void texture_set_crop(texture_crop_t *tcoord, float x0, float y0, float x1, float y1)
{
    GLfloat    *p = (GLfloat *)tcoord;

    /* ...fill-in texture coordinates */
    *p++ = x0, *p++ = y1;
    *p++ = x1, *p++ = y1;
    *p++ = x0, *p++ = y0;
    *p++ = x0, *p++ = y0;
    *p++ = x1, *p++ = y1;
    *p++ = x1, *p++ = y0;
}

/* ...scale texture to fill particular image area */
void texture_set_view_scale(texture_view_t *vcoord, int x, int y, int w, int h, int W, int H, int width, int height)
{
    float   x0 = (float)x / W, x1 = (float)(x + w) / W;
    float   y0 = (float)y / H, y1 = (float)(y + h) / H;
    int     t0 = height * w;
    int     t1 = width * h;
    int     t = t0 - t1;
    float   f;

    if (t > 0)
    {
        /* ...texture fills the area vertically */
        f = (0.5 * (x1 - x0) * t) / t0;

        texture_set_view(vcoord, x0 + f, y0, x1 - f, y1);
    }
    else
    {
        /* ...texture fills the window horizontally */
        f = (-0.5 * (y1 - y0) * t) / t1;

        texture_set_view(vcoord, x0, y0 + f, x1, y1 - f);
    }
}

/* ...draw external texture in given view-port */
void texture_draw(texture_data_t *texture, texture_view_t *view, texture_crop_t *crop, GLfloat alpha)
{
    display_data_t     *display = &__display;
    shader_data_t      *sh = display->shader[texture->gray ? SHADER_TEXTURE_GRAY : SHADER_TEXTURE];
    GLint               saved_program = 0;
    GLint               active_tex = 0;
    GLint               binding_oes = 0;
    
    /* ...identity matrix - not needed really */
    static const GLfloat    identity[4 * 4] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    /* ...vertices coordinates */
    static const GLfloat    verts[] = {
        -1,  -1,
        1,  -1,
        -1,  1,
        -1,  1,
        1,  -1,
        1,  1,
    };

    /* ...triangle coordinates */
    static const GLfloat    texcoords[] = {
        0,  1,
        1,  1,
        0,  0,
        0,  0,
        1,  1,
        1,  0,
    };

    if (view)
    {
        int         i;
        GLfloat    *p = *view;

        for (i = 0; i < 12; i += 2, p += 2)
        {
            TRACE(0, _b("view[%d] = (%.2f, %.2f)"), i / 2, p[0], p[1]);
        }
    }

    if (crop)
    {
        int         i;
        GLfloat    *p = *crop;

        for (i = 0; i < 12; i += 2, p += 2)
        {
            TRACE(0, _b("crop[%d] = (%.2f, %.2f)"), i / 2, p[0], p[1]);
        }
    }

    /* ...save current program (possible used by cairo) */
    glGetIntegerv(GL_CURRENT_PROGRAM, &saved_program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_tex);
    glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &binding_oes);
    
    /* ...set shader program */
    glUseProgram(sh->program);

    /* ...bind uniforms */
    glUniformMatrix4fv(sh->uniform[UNIFORM(TEX, proj)], 1, GL_FALSE, identity);
    glUniform1i(sh->uniform[UNIFORM(TEX, tex)], 0);
    glUniform1f(sh->uniform[UNIFORM(TEX, alpha)], alpha);
    
    /* ...bind textures */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture->tex);

    /* ...set vertices array attribute ("position") */
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (view ? *view : verts));
    glEnableVertexAttribArray(0);

    /* ...set vertex coordinates attribute ("texcoord") */
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (crop ? *crop : texcoords));
    glEnableVertexAttribArray(1);

    /* ...render triangles on the surface */
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* ...disable generic attributes arrays */
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    /* ...unbind texture */
    //glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    /* ...restore active program */
    glUseProgram(saved_program);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, binding_oes);
    glActiveTexture(active_tex);
}

#define EGL_NATIVE_PIXFORMAT_L8_REL     13
#define EGL_NATIVE_PIXFORMAT_NV16_REL   12
#define EGL_NATIVE_PIXFORMAT_YUYV_REL   6
#define EGL_NATIVE_PIXFORMAT_YVYU_REL   8

/* ...translate V4L2 pixel-format into EGL-format */
static inline int __pixfmt_gst_to_egl(int format)
{
    switch (format)
    {
    case GST_VIDEO_FORMAT_ARGB:     return EGL_NATIVE_PIXFORMAT_ARGB8888_REL;
    case GST_VIDEO_FORMAT_RGB16:    return EGL_NATIVE_PIXFORMAT_RGB565_REL;
    case GST_VIDEO_FORMAT_NV16:     return EGL_NATIVE_PIXFORMAT_NV16_REL;
    case GST_VIDEO_FORMAT_NV12:     return EGL_NATIVE_PIXFORMAT_NV12_REL;
    case GST_VIDEO_FORMAT_UYVY:     return EGL_NATIVE_PIXFORMAT_UYVY_REL;
    case GST_VIDEO_FORMAT_YUY2:     return EGL_NATIVE_PIXFORMAT_YUYV_REL;
    case GST_VIDEO_FORMAT_YVYU:     return EGL_NATIVE_PIXFORMAT_YVYU_REL;
    case GST_VIDEO_FORMAT_GRAY8:    return EGL_NATIVE_PIXFORMAT_L8_REL;
    default:                        return TRACE(ERROR, _x("unsupported format: %d"), format), -1;
    }
}

/* ...texture creation (in shared display context) */
texture_data_t * texture_create(int w, int h, void **data, int format)
{
    display_data_t         *display = &__display;
    EGLDisplay              dpy = display->egl.dpy;
    EGLContext              ctx = eglGetCurrentContext();
    EGLNativePixmapTypeREL  pixmap;
    texture_data_t         *texture;
    EGLImageKHR             image;
    
    /* ...map format to the internal value */
    CHK_ERR((pixmap.format = __pixfmt_gst_to_egl(format)) >= 0, (errno = EINVAL, NULL));

    /* ...allocate texture data */
    CHK_ERR(texture = malloc(sizeof(*texture)), (errno = ENOMEM, NULL));

    /* ...get shared display EGL context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    /* ...allocate texture */
    glGenTextures(1, &texture->tex);

    texture->gray = (pixmap.format == EGL_NATIVE_PIXFORMAT_L8_REL);

    /* ...save planes buffers pointers */
    memcpy(texture->data, data, sizeof(texture->data));

    /* ...create EGL surface for a pixmap */
    pixmap.width = w;
    pixmap.height = h;
    pixmap.stride = w;
    pixmap.usage = 0;
    memcpy(&pixmap.pixelData, texture->data, sizeof(pixmap.pixelData));
    texture->image = image = eglCreateImageKHR(dpy, NULL, EGL_NATIVE_PIXMAP_KHR, &pixmap, NULL);
    if (!image)
    {
        TRACE(ERROR, _x("failed to create image: %u*%u@%u, data: %p"), w, h, pixmap.format, pixmap.pixelData);
        glDeleteTextures(1, &texture->tex);
        free(texture), texture = NULL;
        errno = ENOMEM;
        goto out;
    }

    /* ...bind texture to the output device */
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture->tex);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    TRACE(INFO, _b("plane #0: image=%p, tex=%u, data=%p, format=%d (size: %zd)"), image, texture->tex, texture->data[0], pixmap.format, sizeof(pixmap.pixelData));

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

out:
    /* ...release shared display context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    return texture;
}

/* ...destroy texture data */
void texture_destroy(texture_data_t *texture)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();

    /* ...get display shared context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    /* ...destroy textures */
    glDeleteTextures(1, &texture->tex);

    /* ...destroy EGL image */
    eglDestroyImageKHR(display->egl.dpy, texture->image);

    /* ...release shared display context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    /* ...destroy texture structure */
    free(texture);
}

/*******************************************************************************
 * Framebuffer support
 ******************************************************************************/

/* ...internal structure definition */
struct fbo_data
{
    /* ...GL framebuffer / renderbuffer objects */
    GLuint          fbo, rbo;

    /* ...depth-buffer specification (need that? - tbd) */
    int             depth_width, depth_height;

    /* ...current EGL context (scratch data) */
    EGLContext      ctx;
};

/* ...create pixmap surface for off-screen rendering */
fbo_data_t * fbo_create(int w, int h)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();
    fbo_data_t         *fbo;
    GLint               samples = 0;

    /* ...allocate data handle */
    CHK_ERR(fbo = malloc(sizeof(*fbo)), (errno = ENOMEM, NULL));

    /* ...save depth-buffer specification */
    fbo->depth_width = w, fbo->depth_height = h;

    /* ...clear processing context */
    fbo->ctx = EGL_NO_CONTEXT;

    /* ...acquire rendering context as needed */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    /* ...get maximal number of samples in a multisample texture */
	glGetIntegerv(GL_MAX_SAMPLES_EXT, &samples);
    //samples = 0;
    
    /* ...create framebuffer/renderbuffer objects in current EGL context */
    glGenFramebuffers(1, &fbo->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo);
    glGenRenderbuffers(1, &fbo->rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo->rbo);

    /* ...specify depth buffer parameters */
    glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT16, w, h);

    TRACE(0, _b("attach %d-samples renderbuffer (gl-error: %X)"), samples, glGetError());

    /* ...attach depth buffer to the framebuffer */
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->rbo);

    /* ...release shared display context afterwards */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    TRACE(INFO, _b("framebuffer object created: %p (depth-buffer: %d*%d, samples: %d)"), fbo, w, h, samples);
    
    return fbo;
}

/* ...acquire display context for rendering into texture */
int fbo_get(fbo_data_t *fbo)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();
    GLenum              err;
    
    /* ...acquire rendering context as needed (save current in a scratch memory) */
    ((fbo->ctx = ctx) == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    /* ...bind framebuffer/renderbuffer to current EGL context */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo->rbo);
   
    /* ...make sure everything goes well */
    if ((err = glGetError()) != GL_NO_ERROR)
    {
        TRACE(ERROR, _x("failed to acquire framebuffer: %x"), err);

        /* ...release context as needed */
        (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

        return -(errno = EBADFD);
    }
    else
    {
        TRACE(DEBUG, _b("framebuffer %p acquired (%d,%d)"), fbo, fbo->fbo, fbo->rbo);
    
        return 0;
    }
}

/* ...release framebuffer from current context (triggers rendering as required) */
void fbo_put(fbo_data_t *fbo)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();
    GLenum              attach[1] = { GL_DEPTH_ATTACHMENT };
    
    /* ...make sure we have active GL context */
    BUG(ctx == EGL_NO_CONTEXT, _x("invalid context: %p"), ctx);

    /* ...discard framebuffer */
    glDiscardFramebufferEXT(GL_FRAMEBUFFER, 1, attach);

    /* ...unbind framebuffer/renderbuffer objects */
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* ...release shared display context if needed */
    (fbo->ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);
    
    TRACE(DEBUG, _b("framebuffer %p released"), fbo);
}

/* ...attach texture to the framebuffer as a color-attachment #n  */
int fbo_attach_texture(fbo_data_t *fbo, texture_data_t *texture, int n)
{
    EGLContext      ctx = eglGetCurrentContext();
    GLenum          status;
    GLint           samples;
    
    /* ...make sure we have active EGL context */
    BUG(ctx == EGL_NO_CONTEXT, _x("invalid context"));

	glGetIntegerv(GL_MAX_SAMPLES_EXT, &samples);
    //samples = 0;
    
    TRACE(0, _b("attach %d-samples texture"), samples);
    
    /* ...set texture as colour attachement #n */
    glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + n, GL_TEXTURE_2D, texture->tex, 0, samples);
    
    TRACE(0, _b("gl-error: %X"), glGetError());
    
    /* ...check the status of the framebuffer */
    if ((status = glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE)
    {
        TRACE(ERROR, _x("framebuffer %p state error: %x"), fbo, status);
        return -(errno = EINVAL);
    }
    else
    {
        TRACE(DEBUG, _b("fbo[%p]: texture[%p] attached as #%d"), fbo, texture, n);
        return 0;
    }
}

void fbo_destroy(fbo_data_t *fbo)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();

    /* ...get shared display context as needed */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    /* ...destroy framebuffer/renderbuffer objects */
    glDeleteRenderbuffers(1, &fbo->rbo);
    glDeleteFramebuffers(1, &fbo->fbo);

    /* ...release shared display context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    /* ...free data structure */
    free(fbo);

    TRACE(INFO, _b("fbo[%p] destroyed"), fbo);
}

/*******************************************************************************
 * VBO support
 ******************************************************************************/

/* ...create VBO object in shared display context */
vbo_data_t * vbo_create(u32 v_size, u32 v_number, u32 i_size, u32 i_number)
{
    display_data_t     *display = &__display;
    EGLContext         *ctx = eglGetCurrentContext();
    vbo_data_t         *vbo;
    GLenum              error;
    u32                 t0, t1, t2;

    /* ...allocate VBO handle */
    CHK_ERR(vbo = calloc(1, sizeof(*vbo)), (errno = ENOMEM, NULL));

    /* ...get display shared context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    t0 = __get_cpu_cycles();

    if ((error = glGetError()) != GL_NO_ERROR)
    {
        TRACE(ERROR, _x("failed to set context: %X, ctx=%p(%p)"), error, eglGetCurrentContext(), ctx);
        errno = ENOMEM;
        goto error;
    }

    /* ...generate buffer-object */
    glGenBuffers(1, &vbo->vbo);
    if ((error = glGetError()) != GL_NO_ERROR)
    {
        TRACE(ERROR, _x("failed to create VBO: %X"), error);
        errno = ENOMEM;
        goto error;
    }
    
    /* ...allocate vertex buffer memory */
    glBindBuffer(GL_ARRAY_BUFFER, vbo->vbo);
    glBufferData(GL_ARRAY_BUFFER, v_size * v_number, NULL, GL_STREAM_DRAW);
    error = glGetError();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (error != GL_NO_ERROR)
    {
        TRACE(ERROR, _x("failed to allocate VBO memory (%u * %u): %X"), v_size, v_number, error);
        errno = ENOMEM;
        goto error_vbo;
    }

    t1 = __get_cpu_cycles();
#if 0    
    /* ...allocate index buffer object */
    glGenBuffers(1, &vbo->ibo);
    if ((error = glGetError()) != GL_NO_ERROR)
    {
        TRACE(ERROR, _x("failed to allocate IBO: %X"), error);
        errno = ENOMEM;
        goto error_vbo;
    }
    
    /* ...allocate indices buffer memory */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, i_size * i_number, NULL, GL_STREAM_DRAW);
    error = glGetError();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (error != GL_NO_ERROR)
    {
        TRACE(ERROR, _x("failed to allocate IBO memory (%u * %u): %X"), i_size, i_number, error);
        errno = ENOMEM;
        goto error_ibo;
    }
#endif
    t2 = __get_cpu_cycles();

    /* ...do we need to set any parameters here? guess no */
    TRACE(DEBUG, _b("VBO=%u(%u*%u)/IBO=%u(%u*%u) allocated[%p] (%u / %u)"), vbo->vbo, v_size, v_number, vbo->ibo, i_size, i_number, vbo, t1 - t0, t2 - t1);

    goto out;

#if 0    
error_ibo:
    /* ...destroy index buffer object (deallocate memory as needed) */
    glDeleteBuffers(1, &vbo->ibo);
#endif
error_vbo:    
    /* ...destroy buffer object (deallocate memory as needed) */
    glDeleteBuffers(1, &vbo->vbo);

error:
    /* ...destroy data handle */
    free(vbo), vbo = NULL;

out:
    /* ...release display context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    return vbo;
}

/* ...get writable data pointer to the VBO */
int vbo_map(vbo_data_t *vbo, int buffer, int index)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();
    u32                 t0, t1, t2;
    GLenum              err;

    /* ...get display shared context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    t0 = __get_cpu_cycles();

    /* ...map vertex buffer if requested */
    if (buffer)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo->vbo);
        BUG((err = glGetError()) != GL_NO_ERROR, _x("error=%X (vbo=%u)"), err, vbo->vbo);
        vbo->buffer = glMapBufferOES(GL_ARRAY_BUFFER, GL_WRITE_ONLY_OES);
        BUG((err = glGetError()) != GL_NO_ERROR, _x("error=%X (vbo=%u)"), err, vbo->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        BUG((err = glGetError()) != GL_NO_ERROR, _x("error=%X (vbo=%u)"), err, vbo->vbo);
    }
    
    t1 = __get_cpu_cycles();

    /* ...map index buffer if requested */
    if (index)
    {        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo->ibo);
        BUG((err = glGetError()) != GL_NO_ERROR, _x("error=%X (ibo=%u)"), err, vbo->ibo);
        vbo->index = glMapBufferOES(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY_OES);
        BUG((err = glGetError()) != GL_NO_ERROR, _x("error=%X (ibo=%u)"), err, vbo->ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        BUG((err = glGetError()) != GL_NO_ERROR, _x("error=%X (ibo=%u)"), err, vbo->ibo);
    }
    
    t2 = __get_cpu_cycles();
    
    /* ...release shared display context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    TRACE(DEBUG, _b("VBO[%u]/IBO[%u] mapped: %p/%p (%u/%u)"), vbo->vbo, vbo->ibo, vbo->buffer, vbo->index, t1 - t0, t2 - t1);

    return 0;
}

/* ...unmap buffer data */
void vbo_unmap(vbo_data_t *vbo)
{
    display_data_t     *display = &__display;
    EGLContext          ctx = eglGetCurrentContext();
    u32                 t0, t1, t2;

    /* ...get display shared context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_get(display) : 0);

    t0 = __get_cpu_cycles();

    /* ...unmap buffer array if needed */
    if (vbo->buffer)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo->vbo);
        glUnmapBufferOES(GL_ARRAY_BUFFER);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        vbo->buffer = NULL;
    }

    t1 = __get_cpu_cycles();

    /* ...unmap index array if needed */
    if (vbo->index)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo->ibo);
        glUnmapBufferOES(GL_ELEMENT_ARRAY_BUFFER);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        vbo->index = NULL;
    }

    t2 = __get_cpu_cycles();

    /* ...release display context */
    (ctx == EGL_NO_CONTEXT ? display_egl_ctx_put(display) : 0);

    TRACE(DEBUG, _b("VBO[%u]/IBO[%u] unmapped (%u/%u)"), vbo->vbo, vbo->ibo, t1 - t0, t2 - t1);
}

/* ...visualize VBO as an array of points */
void vbo_draw(vbo_data_t *vbo, int offset, int stride, int number, u32 color, GLfloat *pvm)
{
    display_data_t     *display = &__display;
    shader_data_t      *sh = display->shader[SHADER_VBO];
    GLint               program = 0;
 
    /* ...identity matrix - not needed really */
    static const GLfloat    __identity[4 * 4] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    /* ...save current program (possible used by cairo) */
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);

    /* ...set current precompiled shader */
    glUseProgram(sh->program);

    /* ...prepare shader uniforms */
    glUniformMatrix4fv(sh->uniform[UNIFORM(VBO, proj)], 1, GL_FALSE, (pvm ? : __identity));
    glUniform1f(sh->uniform[UNIFORM(VBO, maxdist)], 2.0);
    glUniform4f(sh->uniform[UNIFORM(VBO, color)], ((color >> 24) & 0xFF) / 256.0, 
                ((color >> 16) & 0xFF) / 256.0, ((color >> 8) & 0xFF) / 256.0, ((color >> 0) & 0xFF) / 256.0);

    /* ...bind VBO */
    glBindBuffer(GL_ARRAY_BUFFER, vbo->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)offset);

    /* ...draw VBOs in current viewport */
    glDrawArrays(GL_POINTS, 0, number);

    /* ...cleanup GL state */
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* ...restore original program */
    glUseProgram(program);
}

/* ...visualize VBO as an array of points */
void __line_draw(int i, u32 color, const GLfloat *pvm)
{
    display_data_t     *display = &__display;
    shader_data_t      *sh = display->shader[SHADER_VBO];
    GLint               program = 0;

    /* ...identity matrix - not needed really */
    static const GLfloat    __identity[4 * 4] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    /* ...line coordinates */
    static const GLfloat    __coords[6 * 3] = {
        0, 0, 0, 1, 0, 0,
        0, 0, 0, 0, 1, 0,
        0, 0, 0, 0, 0, 1,
    };
    
    /* ...save current program (possible used by cairo) */
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);

    /* ...set current precompiled shader */
    glUseProgram(sh->program);

    /* ...prepare shader uniforms */
    glUniformMatrix4fv(sh->uniform[UNIFORM(VBO, proj)], 1, GL_FALSE, (pvm ? : __identity));
    glUniform1f(sh->uniform[UNIFORM(VBO, maxdist)], 20.0);
    glUniform4f(sh->uniform[UNIFORM(VBO, color)], ((color >> 24) & 0xFF) / 256.0, 
                ((color >> 16) & 0xFF) / 256.0, ((color >> 8) & 0xFF) / 256.0, ((color >> 0) & 0xFF) / 256.0);

    /* ...bind VBO */
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, __coords + i * 6);
    glEnableVertexAttribArray(0);

    /* ...draw VBOs in current viewport */
    glDrawArrays(GL_LINES, 0, 2);

    /* ...cleanup GL state */
    glDisableVertexAttribArray(0);

    /* ...restore original program */
    glUseProgram(program);
}

/* ...destroy buffer-object */
void vbo_destroy(vbo_data_t *vbo)
{
    display_data_t     *display = &__display;

    /* ...get display shared context */
    display_egl_ctx_get(display);

    /* ...delete buffer-object */
    glDeleteBuffers(1, &vbo->vbo);

    /* ...delete index-buffer object */
    glDeleteBuffers(1, &vbo->ibo);
    
    /* ...allocate memory (do not pass any data yet) */
    TRACE(INIT, _b("VBO[%u]/IBO[%u] object destroyed"), vbo->vbo, vbo->ibo);

    /* ...release display context */
    display_egl_ctx_put(display);

    /* ...destroy VBO handle */
    free(vbo);
}

/*******************************************************************************
 * Auxiliary frame-rate calculation functions
 ******************************************************************************/

/* ...reset FPS calculator */
void window_frame_rate_reset(window_data_t *window)
{
    /* ...reset accumulator and timestamp */
    window->fps_acc = 0, window->fps_ts = 0;
}

/* ...update FPS calculator */
float window_frame_rate_update(window_data_t *window)
{
    u32     ts_0, ts_1, delta, acc;
    float   fps;

    /* ...get current timestamp for a window frame-rate calculation */
    delta = (ts_1 = __get_time_usec()) - (ts_0 = window->fps_ts);

    /* ...check if accumulator is initialized */
    if ((acc = window->fps_acc) == 0)
    {
        if (ts_0 != 0)
        {
            /* ...initialize accumulator */
            acc = delta << 4;
        }
    }
    else
    {
        /* ...accumulator is setup already; do exponential averaging */
        acc += delta - ((acc + 8) >> 4);
    }

    /* ...calculate current frame-rate */
    if ((fps = (acc ? 1e+06 / ((acc + 8) >> 4) : 0)) != 0)
    {
        TRACE(DEBUG, _b("delta: %u, acc: %u, fps: %f"), delta, acc, fps);
    }

    /* ...update timestamp and accumulator values */
    window->fps_acc = acc, window->fps_ts = ts_1;

    return fps;
}

/*******************************************************************************
 * Auxiliary widget helper functions
 ******************************************************************************/

/* ...create GL surface from PNG */
cairo_surface_t * widget_create_png(cairo_device_t *cairo, const char *path, int w, int h)
{
    cairo_surface_t    *image;
    cairo_surface_t    *cs;
    cairo_t            *cr;
    int                 W, H;
    
    /* ...create PNG surface */
    image = cairo_image_surface_create_from_png(path);
    if (__check_surface(image) != 0)
    {
        TRACE(ERROR, _x("failed to create image: %m"));
        return NULL;
    }
    else
    {
        W = cairo_image_surface_get_width(image);
        H = cairo_image_surface_get_height(image);
    }

    /* ...set widget dimensions */
    (w == 0 ? w = W : 0), (h == 0 ? h = H : 0);

    /* ...create new GL surface of requested size */
    cs = cairo_gl_surface_create(cairo, CAIRO_CONTENT_COLOR_ALPHA, w, h);
    if (__check_surface(cs) != 0)
    {
        TRACE(ERROR, _x("failed to create %u*%u GL surface: %m"), w, h);
        cs = NULL;
        goto out;
    }

    /* ...fill GL-surface */
    cr = cairo_create(cs);
    cairo_scale(cr, (double)w / W, (double)h / H);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    TRACE(DEBUG, _b("created GL-surface [%d*%d] from '%s' [%d*%d]"), w, h, path, W, H);

out:
    /* ...release scratch image surface */
    cairo_surface_destroy(image);

    return cs;
}

/* ...get surface width */
int widget_image_get_width(cairo_surface_t *cs)
{
    return cairo_gl_surface_get_width(cs);
}

/* ...get surface height */
int widget_image_get_height(cairo_surface_t *cs)
{
    return cairo_gl_surface_get_height(cs);
}
