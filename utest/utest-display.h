/*******************************************************************************
 * utest-display.h
 *
 * Display support for a Wayland
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

#ifndef __UTEST_DISPLAY_H
#define __UTEST_DISPLAY_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-event.h"

/* ...note - WSEGL header (<wayland-egl.h>) must be included before <EGL/egl.h> */
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cairo.h>

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/

typedef struct display_data     display_data_t;
typedef struct window_info      window_info_t;
typedef struct window_data      window_data_t;
typedef struct widget_info      widget_info_t;
typedef struct widget_data      widget_data_t;
typedef struct shader_data      shader_data_t;
typedef struct texture_data     texture_data_t;
typedef struct vbo_data         vbo_data_t;

/*******************************************************************************
 * EGL functions binding (make them global; create EGL adaptation layer - tbd)
 ******************************************************************************/

/* ...EGL extensions */
extern PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
extern PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
extern PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC eglSwapBuffersWithDamageEXT;
extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
extern PFNGLMAPBUFFEROESPROC glMapBufferOES;
extern PFNGLUNMAPBUFFEROESPROC glUnmapBufferOES;
extern PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES;
extern PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES;
extern PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES;
extern PFNGLISVERTEXARRAYOESPROC glIsVertexArrayOES;
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC glRenderbufferStorageMultisampleEXT;
extern PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT;
extern PFNGLDISCARDFRAMEBUFFEREXTPROC glDiscardFramebufferEXT;

extern PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
extern PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
extern PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...EGL configuration data */
typedef struct egl_data
{
    /* ...EGL display handle associated with current wayland display */
    EGLDisplay              dpy;

    /* ...shared EGL context */
    EGLContext              ctx;

    /* ...current EGL configuration */
    EGLConfig               conf;

}   egl_data_t;

/*******************************************************************************
 * Window support
 ******************************************************************************/

/* ...window configuration data */
struct window_info
{
    /* ...window title */
    const char         *title;

    /* ...fullscreen mode */
    int                 fullscreen;

    /* ...dimensions */
    u32                 width, height;

    /* ...output device id */
    u32                 output;

    /* ...context initialization function */
    int               (*init)(display_data_t *, window_data_t *, void *);
    
    /* ...resize hook */
    void              (*resize)(display_data_t *, void *);
    
    /* ...drawing completion callback */
    void              (*redraw)(display_data_t *, void *);

    /* ...custom context destructor */
    void              (*destroy)(window_data_t *, void *);
};

/* ...window creation/destruction */
extern window_data_t * window_create(display_data_t *display, window_info_t *info, widget_info_t *info2, void *data);
extern void window_destroy(window_data_t *window);

/* ...return EGL surface associated with window */
extern EGLSurface window_egl_surface(window_data_t *window);
extern EGLContext window_egl_context(window_data_t *window);

/* ...window size query */
extern int window_get_width(window_data_t *window);
extern int window_get_height(window_data_t *window);

/* ...schedule window redrawal */
extern void window_schedule_redraw(window_data_t *window);
extern void window_draw(window_data_t *window);

/* ...associated cairo surface handling */
extern cairo_t * window_get_cairo(window_data_t *window);
extern void window_put_cairo(window_data_t *window, cairo_t *cr);
extern cairo_device_t  *__window_cairo_device(window_data_t *window);

/* ...auxiliary helpers */
extern void window_frame_rate_reset(window_data_t *window);
extern float window_frame_rate_update(window_data_t *window);

/*******************************************************************************
 * Generic widgets support
 ******************************************************************************/

/* ...widget descriptor data */
typedef struct widget_info
{
    /* ...coordinates within parent window/widget */
    int                 left, top, width, height;

    /* ...initialization function */
    int               (*init)(widget_data_t *widget, void *cdata);

    /* ...redraw hook */
    void              (*draw)(widget_data_t *widget, void *cdata, cairo_t *cr);

    /* ...input event processing */
    widget_data_t *   (*event)(widget_data_t *widget, void *cdata, widget_event_t *event);

    /* ...deinitialization function? - need that? */
    void              (*destroy)(widget_data_t *widget, void *cdata);

}   widget_info_t;

/* ...widget creation/destruction */
extern widget_data_t * widget_create(window_data_t *window, widget_info_t *info, void *cdata);
extern void widget_destroy(widget_data_t *widget);

/* ...widget rendering */
extern void widget_render(widget_data_t *widget, cairo_t *cr, float alpha);
extern void widget_update(widget_data_t *widget, int flush);
extern void widget_schedule_redraw(widget_data_t *widget);
extern cairo_device_t * widget_get_cairo_device(widget_data_t *widget);

/* ...input event processing */
extern widget_data_t * widget_input_event(widget_data_t *widget, widget_event_t *event);
extern widget_data_t * widget_get_parent(widget_data_t *widget);

extern int window_set_invisible(window_data_t *window);

/* ...helpers */
extern int widget_get_left(widget_data_t *widget);
extern int widget_get_top(widget_data_t *widget);
extern int widget_get_width(widget_data_t *widget);
extern int widget_get_height(widget_data_t *widget);

/*******************************************************************************
 * GL shaders support
 ******************************************************************************/

/* ...opaque type declaration (opaque?) */
typedef struct shader_data  shader_data_t;

/* ...shader descriptor */
typedef struct shader_desc
{
    /* ...vertex/fragment shader source code */
    const char * const *v_src;
    const char * const *f_src;

    /* ...vertex/fragment shader binary blobs */
    const void         *v_bin;
    const void         *f_bin;

    /* ...attributes names array */
    const char * const *attr;
    
    /* ...number of attributes */
    int         attr_num;

    /* ...uniforms names array */
    const char * const *uni;

    /* ...number of uniforms */
    int         uni_num;

}   shader_desc_t;

/* ...create shader object */
extern shader_data_t * shader_create(const shader_desc_t *desc);

/* ...shader destruction */
extern void shader_destroy(shader_data_t *shader);

/* ...get shader program */
extern GLuint shader_program(shader_data_t *shader);

/* ...get shader uniforms locations array */
extern const GLint * shader_uniforms(shader_data_t *shader);

/*******************************************************************************
 * External textures support
 ******************************************************************************/

/* ...external texture data */
struct texture_data
{
    /* ...drawable EGL pixmap */
    EGLImageKHR         image;
    
    /* ...GL texture index (in shared display EGL context) */
    GLuint              tex;

    /* ...buffer data pointer (per-plane; up to 3 planes) */
    void               *data[3];

    /* ...grayscale/color */
    int                 gray;
};

/* ...texture cropping data */
typedef GLfloat     texture_crop_t[6 * 2];

/* ...texture viewport data */
typedef GLfloat     texture_view_t[6 * 2];

/* ...external textures handling */
extern texture_data_t * texture_create(int w, int h, void **pb, int format);
extern void texture_destroy(texture_data_t *texture);
extern void texture_draw(texture_data_t *texture, texture_crop_t *crop, texture_view_t *view, GLfloat alpha);

/* ...texture viewport/cropping setting */
extern void texture_set_view(texture_view_t *vcoord, float x0, float y0, float x1, float y1);
extern void texture_set_crop(texture_crop_t *tcoord, float x0, float y0, float x1, float y1);
extern void texture_set_view_scale(texture_view_t *vcoord, int x, int y, int w, int h, int W, int H, int width, int height);

/*******************************************************************************
 * External VBO support
 ******************************************************************************/

/* ...external vertex-buffer object data */
typedef struct vbo_data
{
    /* ...buffer array object id */
    GLuint              vbo;

    /* ...index array object id */
    GLuint              ibo;

    /* ...individual vertex element size */
    u32                 size;

    /* ...number of elements in buffer */
    u32                 number;

    /* ...mapped vertex-buffer-objects array */
    void               *buffer;

    /* ...mapped index-buffer-objects array */
    void               *index;

}   vbo_data_t;

/* ...handling of VBOs */
extern vbo_data_t * vbo_create(u32 v_size, u32 v_number, u32 i_size, u32 i_number);
extern int vbo_map(vbo_data_t *vbo, int buffer, int index);
extern void vbo_draw(vbo_data_t *vbo, int offset, int stride, int number, u32 color, GLfloat *pvm);
extern void vbo_unmap(vbo_data_t *vbo);
extern void vbo_destroy(vbo_data_t *vbo);

/*******************************************************************************
 * Framebuffer support (rendering to texture)
 ******************************************************************************/

/* ...opaque definition */
typedef struct fbo_data fbo_data_t;

/* ...public API */
extern fbo_data_t * fbo_create(int w, int h);
extern void fbo_destroy(fbo_data_t *fbo);

/* ...acquire/release framebuffer rendering context */
extern int fbo_get(fbo_data_t *fbo);
extern void fbo_put(fbo_data_t *fbo);

/* ...attach texture as color-attachment #n */
extern int fbo_attach_texture(fbo_data_t *fbo, texture_data_t *texture, int n);

/*******************************************************************************
 * Public API
 ******************************************************************************/

/* ...connect to a display */
extern display_data_t * display_create(void);

/* ...cairo device accessor */
extern cairo_device_t  * __display_cairo_device(display_data_t *display);

/* ...get current EGL configuration data */
extern egl_data_t  * display_egl_data(display_data_t *display);

/*******************************************************************************
 * Miscellaneous helpers for 2D-graphics
 ******************************************************************************/

/* ...PNG images handling */
extern cairo_surface_t * widget_create_png(cairo_device_t *cairo, const char *path, int w, int h);
extern int widget_image_get_width(cairo_surface_t *cs);
extern int widget_image_get_height(cairo_surface_t *cs);

#endif  /* __UTEST_DISPLAY_H */
