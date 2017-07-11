/*******************************************************************************
 * utest-imr-gui.c
 *
 * Graphical user interface
 *
 * Copyright (c) 2015-2016 Cogent Embedded Inc. ALL RIGHTS RESERVED.
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

#define MODULE_TAG                      GUI

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-app.h"
#include "utest-gui.h"
#include "utest-png.h"
#include <math.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

struct carousel
{
    /* ...viewport for menu visualization */
    texture_view_t          view;

    /* ...current position for horizontal movement */
    float                   position, target_position;

    /* ...current position for vertical movement */
    float                   position_y, target_position_y;

    /* ...fade-out machine state */
    float                   alpha, alpha_rate;

    /* ...width of the horizontal/vertical windows */
    float                   width, height;

    /* ...activity status */
    int                     active;

    /* ...texture containing the thumbnails */
    GLuint                  tex;

    /* ...menu configuration */
    const carousel_cfg_t   *cfg;
    
    /* ...client data for callback passing */
    void                   *cdata;
    
    /* ...gradient scale factor */
    float                   scale;

    /* ...total number of items in the menu */
    int                     size;

    /* ...previous selection item */
    int                     select, select_y;

    /* ...current item in focus */
    int                     focus, focus_y;

    /* ...spacenav accumulator for "forward/backward" transitions */
    int                     spnav_rewind;

    /* ...spacenav accumulator for "upward/downward" transitions */
    int                     spnav_rewind_y;

    /* ...spacenav accumulator for "push" event */
    int                     spnav_push;

    /* ...spacenav buttons state */
    int                     spnav_buttons;
};

/*******************************************************************************
 * Global constants
 ******************************************************************************/

#define RESOURCES_DIR   "."

/*******************************************************************************
 * Local constants
 ******************************************************************************/

/* ...identity matrix - not needed really */
static const GLfloat    __identity[4 * 4] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

/*******************************************************************************
 * Carousel menu commands processing
 ******************************************************************************/

/*******************************************************************************
 * Commands processing
 ******************************************************************************/

/* ...carousel menu commands */
#define GUI_CMD_NONE                    0
#define GUI_CMD_ENTER                   1
#define GUI_CMD_LEAVE                   2
#define GUI_CMD_FORWARD                 3
#define GUI_CMD_BACKWARD                4
#define GUI_CMD_UPWARD                  5
#define GUI_CMD_DOWNWARD                6
#define GUI_CMD_SELECT                  7
#define GUI_CMD_CLOSE                   8

/* ...process menu command */
static void menu_command(carousel_t *menu, int command)
{
    const carousel_cfg_t   *cfg = menu->cfg;

    /* ...reset fading machine */
    menu->alpha = 1.0, menu->alpha_rate = 0;

    /* ...process command */
    switch (command)
    {
    case GUI_CMD_SELECT:
        /* ...pass ENTER command to current focus item */
        TRACE(INFO, _b("select command: focus = %d:%d"), menu->focus, menu->focus_y);
        
        /* ...invoke application callback specifying the index of the carousel menu */
        if (menu->select != menu->focus || menu->select_y != menu->focus_y)
        {
            cfg->select(menu->cdata, menu->select_y = menu->focus_y, menu->select = menu->focus);
        }
        
        /* ...do not touch target position */
        return;

    case GUI_CMD_ENTER:
        /* ...focus receiving command */
        TRACE(INFO, _b("enter command (focus=%d)"), menu->focus);

        /* ...reset approaching / fading sequence */
        menu->position = menu->target_position;
        menu->position_y = menu->target_position_y;

        /* ...do not touch target position */
        return;

    case GUI_CMD_FORWARD:
        /* ...forward movement command */
        TRACE(INFO, _b("forward command (focus=%d)"), menu->focus);

        /* ...advance focus */
        if (++menu->focus == cfg->size)
        {
            menu->focus -= cfg->size, menu->position -= 1.0;
        }

        /* ...update target position */
        break;

    case GUI_CMD_BACKWARD:
        /* ...backward movement command */
        TRACE(INFO, _b("backward command (focus=%d)"), menu->focus);

        /* ...decrement focus */
        if (menu->focus-- == 0)
        {
            menu->focus += cfg->size, menu->position += 1.0;
        }

        /* ...update target position */
        break;

    case GUI_CMD_UPWARD:
        /* ...upward movement command */
        TRACE(INFO, _b("upward command (focus=%d)"), menu->focus_y);

        /* ...decrement focus */
        if (menu->focus_y-- == 0)
        {
            menu->focus_y += cfg->size_y, menu->position_y += 1.0;
        }

        /* ...update target position */
        break;

    case GUI_CMD_DOWNWARD:
        /* ...upward movement command */
        TRACE(INFO, _b("downward command (focus=%d)"), menu->focus_y);

        /* ...advace focus */
        if (++menu->focus_y == cfg->size_y)
        {
            menu->focus_y -= cfg->size_y, menu->position_y -= 1.0;
        }

        /* ...update target position */
        break;
        
    case GUI_CMD_CLOSE:
        /* ...disable GUI controls; pass through */
        TRACE(INFO, _b("close command: focus=%d"), menu->focus);
        
        /* ...reset both approaching and fading machines */
        menu->position = menu->target_position;
        menu->position_y = menu->target_position_y;

        /* ...disable drawing */
        menu->alpha = 0.1;

        return;

    case GUI_CMD_LEAVE:
        /* ...disable GUI controls without accepting command */
        TRACE(INFO, _b("leave command: focus=%d:%d, select=%d:%d"), menu->focus, menu->focus_y, menu->select, menu->select_y);
        
        /* ...revert focus to current selection */
        menu->focus = menu->select, menu->focus_y = menu->select_y;
        
        /* ...disable graphics */
        menu->alpha = 0;

        /* ...force update of target position */
        break;
    }

    /* ...adjust approaching state-machine */
    menu->target_position = ((float)menu->focus - (cfg->window_size - 1) * 0.5) / cfg->size;
    menu->target_position_y = ((float)menu->focus_y - (cfg->window_size_y - 1) * 0.5) / cfg->size_y;

    TRACE(INFO, _b("position=%f:%f, target=%f:%f, focus=%d:%d"),
          menu->position, menu->position_y, menu->target_position, menu->target_position_y,
          menu->focus, menu->focus_y);
}

/*******************************************************************************
 * Shader for border drawing
 ******************************************************************************/

/* ...vertex shader for fading border */
static const char * fading_vertex_shader =
    "precision highp float;\n"
    "uniform mat4 proj;\n"
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_position;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = proj * vec4(position, 0.0, 1.0);\n"
    "   v_texcoord = texcoord;\n"
    "   v_position = position;\n"
    "}\n";

/* ...fragment shader for fading border */
static const char * fading_fragment_shader =
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_position;\n"
    "uniform sampler2D tex;\n"
    "uniform vec4 color_start;\n"
    "uniform vec4 color_stop;\n"
    "uniform float sharpness;\n"
    "\n"
    "float get_alpha(float d)\n"
    "{\n"
    "   return pow(1.0 - d, sharpness);\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "   float x = get_alpha(clamp(length(v_texcoord), 0.0, 1.0));\n"
    "   gl_FragColor = x * color_start + (1.0 - x) * color_stop;\n"
    //"   gl_FragColor = vec4(color_start.rgb, (1.0 - alpha) * get_alpha(x) + alpha);\n"
#if 0
    "   if (x < 0.0) {\n"
    "       gl_FragColor = vec4(-x, 0.0, 0.0, 1.0);\n"
    "   } else {\n"
    "       gl_FragColor = vec4(0, x, 0.0, 1.0);\n"
    "   }\n"
#endif
    "}\n";

/* ...build gradient rendering shader */
#define SHADER_TAG                      FADING
#define SHADER_VERTEX_SOURCE            fading_vertex_shader
#define SHADER_FRAGMENT_SOURCE          fading_fragment_shader

/* ...attributes definition */
#define SHADER_ATTRIBUTES                           \
    __A(position),      /* ...vertex position */    \
    __A(texcoord),      /* ...texture coordinate */

/* ...uniforms definition */
#define SHADER_UNIFORMS                                         \
    __U(proj),          /* ...PVM matrix */                     \
    __U(color_start),   /* ...fading color */                   \
    __U(color_stop),    /* ...fading color - stop */            \
    __U(sharpness),     /* ...gradient function sharpness */

/* ...instantiate indices/names lists */
#include "shader-impl.h"

/* ...shader data singleton */
static shader_data_t   *__fading_shader;

/*******************************************************************************
 * Fading border drawing function
 ******************************************************************************/

#define __vcoord_rectangle(p, x0, y0, x1, y1)               \
({                                                          \
    GLfloat _x0 = (x0), _y0 = (y0), _x1 = (x1), _y1 = (y1); \
    GLfloat *_p = (p);                                      \
                                                            \
    *_p++ = _x0, *_p++ = _y0;                               \
    *_p++ = _x1, *_p++ = _y0;                               \
    *_p++ = _x0, *_p++ = _y1;                               \
    *_p++ = _x0, *_p++ = _y1;                               \
    *_p++ = _x1, *_p++ = _y0;                               \
    *_p++ = _x1, *_p++ = _y1;                               \
    _p;                                                     \
})

#define __tcoord_rectangle(t, Ax, Ay, Bx, By, Cx, Cy, Dx, Dy)   \
({                                                              \
    GLfloat _ax = (Ax), _ay = (Ay), _bx = (Bx), _by=(By);       \
    GLfloat _cx = (Cx), _cy = (Cy), _dx = (Dx), _dy=(Dy);       \
    GLfloat *_t = (t);                                          \
                                                                \
    *_t++ = _ax, *_t++ = _ay;                                   \
    *_t++ = _bx, *_t++ = _by;                                   \
    *_t++ = _dx, *_t++ = _dy;                                   \
    *_t++ = _dx, *_t++ = _dy;                                   \
    *_t++ = _bx, *_t++ = _by;                                   \
    *_t++ = _cx, *_t++ = _cy;                                   \
    _t;                                                         \
})

/* ...texture coordinates of a constant rectangle */
#define __tcoord_rectangle_const(Ax, Ay, Bx, By, Cx, Cy, Dx, Dy)    \
    (Ax), (Ay),                                                     \
    (Bx), (By),                                                     \
    (Dx), (Dy),                                                     \
    (Dx), (Dy),                                                     \
    (Bx), (By),                                                     \
    (Cx), (Cy)


/* ...auxiliary helpers (color components) */
static inline GLfloat __rgba_r(u32 color)
{
    return ((color >> 24) & 0xFF) / 255.0;
}

static inline GLfloat __rgba_g(u32 color)
{
    return ((color >> 16) & 0xFF) / 255.0;
}

static inline GLfloat __rgba_b(u32 color)
{
    return ((color >> 8) & 0xFF) / 255.0;
}

static inline GLfloat __rgba_a(u32 color)
{
    return ((color >> 0) & 0xFF) / 255.0;
}

static inline float texture_view_x0(const texture_view_t *v)
{
    return (*v)[0];
}

static inline float texture_view_y0(const texture_view_t *v)
{
    return (*v)[1];
}

static inline float texture_view_x1(const texture_view_t *v)
{
    return (*v)[2];
}

static inline float texture_view_y1(const texture_view_t *v)
{
    return (*v)[5];
}

#define U(id)       u[UNIFORM(FADING, id)]
#define A(id)       ATTRIBUTE(FADING, id)

/* ...border drawing function */
int border_draw(const texture_view_t *inner, const texture_view_t *outer, u32 c0, u32 c1, float s)
{
    shader_data_t *shader = __fading_shader;

    /* ...vertices coordinates for entire screen */
    static const GLfloat    __default_outer[] = {
        __tcoord_rectangle_const(-1, -1, 1, -1, 1, 1, -1, 1),
    };

    /* ...texture coordinates */
    static const GLfloat    tcoords[] = {
        __tcoord_rectangle_const(1, 1, 0, 1, 0, 0, 1, 0),
        __tcoord_rectangle_const(0, 1, 0, 1, 0, 0, 0, 0),
        __tcoord_rectangle_const(0, 1, 1, 1, 1, 0, 0, 0),
        __tcoord_rectangle_const(0, 0, 1, 0, 1, 0, 0, 0),
        __tcoord_rectangle_const(0, 0, 1, 0, 1, 1, 0, 1),
        __tcoord_rectangle_const(0, 0, 0, 0, 0, 1, 0, 1),
        __tcoord_rectangle_const(1, 0, 0, 0, 0, 1, 1, 1),
        __tcoord_rectangle_const(0, 1, 0, 0, 0, 0, 0, 1),
    };

    GLfloat         X0, X1, Y0, Y1, x0, x1, y0, y1;
    GLfloat         verts[8 * 12], *p = verts;
    const GLint    *u;
    GLint           program;

    /* ...make sure fading shader is built */
    CHK_ERR(shader, -EINVAL);

    /* ...get uniforms */
    u = shader_uniforms(shader);

    /* ...if outer texture is not defined, consider whole area [<0,0>:<1,1>] */
    (!outer ? outer = &__default_outer : 0);

    /* ...get viewport coordinates */
    X0 = texture_view_x0(outer);
    X1 = texture_view_x1(outer);
    Y0 = texture_view_y0(outer);
    Y1 = texture_view_y1(outer);
    x0 = texture_view_x0(inner);
    x1 = texture_view_x1(inner);
    y0 = texture_view_y0(inner);
    y1 = texture_view_y1(inner);
    TRACE(0, _b("X0=%f,Y0=%f,x0=%f,y0=%f,X1=%f,Y1=%f,x1=%f,y1=%f"), X0, Y0, x0, y0, X1, Y1, x1, y1);
    
    /* ...fill-in vertices coordinates; clockwise starting from upper-left corner */
    p = __vcoord_rectangle(p, X0, Y0, x0, y0);
    p = __vcoord_rectangle(p, x0, Y0, x1, y0);
    p = __vcoord_rectangle(p, x1, Y0, X1, y0);
    p = __vcoord_rectangle(p, x1, y0, X1, y1);
    p = __vcoord_rectangle(p, x1, y1, X1, Y1);
    p = __vcoord_rectangle(p, x0, y1, x1, Y1);
    p = __vcoord_rectangle(p, X0, y1, x0, Y1);
    p = __vcoord_rectangle(p, X0, y0, x0, y1);

    /* ...start shader program */
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glUseProgram(shader_program(shader));

    /* ...bind uniforms */
    glUniformMatrix4fv(U(proj), 1, GL_FALSE, __identity);
    glUniform4f(U(color_start), __rgba_r(c0), __rgba_g(c0), __rgba_b(c0), __rgba_a(c0));
    glUniform4f(U(color_stop), __rgba_r(c1), __rgba_g(c1), __rgba_b(c1), __rgba_a(c1));
    glUniform1f(U(sharpness), s);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    
    /* ...set vertices array attribute ("position") */
    glVertexAttribPointer(A(position), 2, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(A(position));

    /* ...set vertex coordinates attribute ("texcoord") */
    glVertexAttribPointer(A(texcoord), 2, GL_FLOAT, GL_FALSE, 0, tcoords);
    glEnableVertexAttribArray(A(texcoord));

    /* ...render triangles on the surface */
    glDrawArrays(GL_TRIANGLES, 0, 6 * 8);

    /* ...disable generic attributes arrays */
    glDisableVertexAttribArray(A(position));
    glDisableVertexAttribArray(A(texcoord));

    /* ...restore original program */
    glUseProgram(program);

    return 0;
}

#undef U
#undef A

/* ...border-drawing shader pre-initialization */
int border_shader_prebuild(void)
{
    /* ...compile shader */
    if ((__fading_shader = shader_create(&SHADER_DESC(FADING))) == NULL)
    {
        TRACE(ERROR, _x("failed to compile border shader: %m"));
        return -errno;
    }

    TRACE(INIT, _b("border-fading shader prebuilt"));

    return 0;
}

/*******************************************************************************
 * Rendering functions
 ******************************************************************************/

/* ...vertex shader program for textures visualization */
static const char * carousel_vertex_shader =
    "precision highp float;\n"
    "uniform mat4 proj;\n"
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_position;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = proj * vec4(position, 0.0, 1.0);\n"
    "   v_texcoord = texcoord;\n"
    "   v_position = position;\n"
    "}\n";

/* ...fragment shader for color textures visualization (YUV etc) */
static const char * carousel_fragment_shader =
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_position;\n"
    "uniform sampler2D tex;\n"
    "uniform float alpha;\n"
    "uniform float scale;\n"
    "\n"
    "float get_alpha(float d)\n"
    "{\n"
    "   return (d < 1.0 / 9.0 ? 1.0 : ((1.0 - 0.45) / (8.0 * d) + (9.0 * 0.45 - 1.0) / 8.0));\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "   float x = v_position.x * scale;\n"
    "   float distance = pow(x, 2.0);\n"
    "   float a = 1.0 - get_alpha(distance);\n"
    "   vec3  color = texture2D(tex, v_texcoord).rgb + vec3(a, a, a);\n"
    "   gl_FragColor = vec4(color, alpha);\n"
#if 0
    "   if (x < 0.0) {\n"
    "       gl_FragColor = vec4(-x, 0.0, 0.0, 1.0);\n"
    "   } else {\n"
    "       gl_FragColor = vec4(0, x, 0.0, 1.0);\n"
    "   }\n"
#endif
    "}\n";

/* ...build color texture rendering shader */
#define SHADER_TAG                      CAROUSEL
#define SHADER_VERTEX_SOURCE            carousel_vertex_shader
#define SHADER_FRAGMENT_SOURCE          carousel_fragment_shader

/* ...attributes definition */
#define SHADER_ATTRIBUTES                           \
    __A(position),      /* ...vertex position */    \
    __A(texcoord),      /* ...texture coordinate */

/* ...uniforms definition */
#define SHADER_UNIFORMS                                                     \
    __U(proj),          /* ...PVM matrix */                                 \
    __U(tex),           /* ...2D-texture sampler */                         \
    __U(scale),         /* ...scale for a gradient */                       \
    __U(alpha),         /* ...transparency value to use for a texture */

/* ...instantiate indices/names lists */
#include "shader-impl.h"

/* ...shader data singleton */
static shader_data_t   *__carousel_shader;

/*******************************************************************************
 * Carousel menu rendering function
 ******************************************************************************/

#define U(id)       u[UNIFORM(CAROUSEL, id)]
#define A(id)       ATTRIBUTE(CAROUSEL, id)

/* ...widget rendering function */
void carousel_draw(carousel_t *menu)
{
    shader_data_t  *sh = __carousel_shader;
    const GLint    *u = shader_uniforms(sh);
    u32             c0 = __app_cfg.carousel_border.c0;
    u32             c1 = __app_cfg.carousel_border.c1;
    float           s = __app_cfg.carousel_border.sharpness;
    GLfloat         start, stop, start_y, stop_y;
    texture_crop_t  tcoord;
    GLint           saved_program = 0;
    float           delta, delta_y;
    
    /* ...bail out if menu is inactive */
    if (menu->alpha <= 0)       return;

    /* ...make sure fading shader is built */
    if (!sh)                    return;

    /* ...save original program */
    glGetIntegerv(GL_CURRENT_PROGRAM, &saved_program);

    /* ...set shader program */
    glUseProgram(shader_program(sh));

    /* ...advance position towards target */
    delta = menu->target_position - menu->position;
    delta_y = menu->target_position_y - menu->position_y;

    /* ...check if we approach "switch" threshold */
    if (delta != 0 || delta_y != 0)
    {
        /* ...exponential approach to the target */
        menu->position += delta / 10.0;
        menu->position_y += delta_y / 10.0;

        /* ...process approaching to the target position */
        if (fabs(delta) < 0.0005 && fabs(delta_y) < 0.0005)
        {
            /* ...issue entrance command */
            menu_command(menu, GUI_CMD_ENTER);
        }
        else if (fabs(delta) < 0.01 && fabs(delta_y) < 0.01 && (menu->select != menu->focus || menu->select_y != menu->focus_y))
        {
            /* ...issue selection command - update view */
            menu_command(menu, GUI_CMD_SELECT);
        }
    }
    else
    {
        /* ...approach machine is inactive; fade-out sequence is running */
        if (1) menu->alpha_rate -= 0.5 / 30;

        /* ...quadratic descend of alpha (? - hmm) */
        if (1) menu->alpha += menu->alpha_rate / 30;

        /* ...exponential descend of alpha */
        if (0) ((menu->alpha -= menu->alpha / 30.0) < 0.001 ? menu->alpha = 0.0 : 0);
    }

    TRACE(0, _b("position: %f, target: %f, focus: %d, alpha=%f"), menu->position, menu->target_position, menu->focus, menu->alpha);

    /* ...set texture coordinates (it's okay if it wraps over 1) */
    stop = (start = menu->position) + menu->width;
    stop_y = (start_y = menu->position_y) + menu->height;
    
    /* ...set cropping coordinates */
    texture_set_crop(&tcoord, start, start_y, stop, stop_y);

    /* ...triangle coordinates */
    static const GLfloat    texcoords[] = {
        0,  1,
        1,  1,
        0,  0,
        0,  0,
        1,  1,
        1,  0,
    };

    /* ...bind uniforms */
    glUniformMatrix4fv(U(proj), 1, GL_FALSE, __identity);
    glUniform1f(U(alpha), menu->alpha);
    glUniform1f(U(scale), menu->scale);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    
    /* ...bind a texture */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, menu->tex);
    
    /* ...set vertices array attribute ("position") */
    glVertexAttribPointer(A(position), 2, GL_FLOAT, GL_FALSE, 0, menu->view);
    glEnableVertexAttribArray(A(position));

    /* ...set vertex coordinates attribute ("texcoord") */
    glVertexAttribPointer(A(texcoord), 2, GL_FLOAT, GL_FALSE, 0, (1 ? tcoord : texcoords));
    glEnableVertexAttribArray(A(texcoord));

    /* ...render triangles on the surface */
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* ...disable generic attributes arrays */
    glDisableVertexAttribArray(A(position));
    glDisableVertexAttribArray(A(texcoord));

    /* ...unbind texture */
    glBindTexture(GL_TEXTURE_2D, 0);

    /* ...restore original program */
    glUseProgram(saved_program);

    /* ...draw a border */
    border_draw(&menu->view, NULL, c0 + (u32)(menu->alpha * 0xFF), c1 + (u32)(menu->alpha * 0x80), s);
}

#undef U
#undef A

/*******************************************************************************
 * Reset carousel menu
 ******************************************************************************/

/* ...reset carousel menu */
void carousel_reset(carousel_t *menu)
{
    /* ...clear all accumulators */
    menu->spnav_rewind = 0, menu->spnav_push = 0;

    /* ...reset fading / approaching sequences */
    menu->alpha = 0, menu->position = menu->target_position;
}

/*******************************************************************************
 * Input processing
 ******************************************************************************/

/* ...threshold for any pending sequence reset (in milliseconds) */
#define __SPNAV_SEQUENCE_THRESHOLD      200

/* ...threshold for a rewinding (tbd - taken out of the air) */
#define __SPNAV_REWIND_THRESHOLD        5000

/* ...threshold for a push event detection (again - out of the air - tbd) */
#define __SPNAV_PUSH_THRESHOLD          300

void carousel_leave(carousel_t *menu)
{
    /* ...invoke "leave" command */
    menu_command(menu, GUI_CMD_LEAVE);
}

int carousel_spnav_event(carousel_t *menu, spnav_event *e)
{
    if (e->type == SPNAV_EVENT_BUTTON && e->button.press && e->button.bnum == 1)
    {
        /* ...exit button pressed */
        TRACE(DEBUG, _b("selected: position = %f"), menu->position);

        /* ...invoke "leave" command */
        menu_command(menu, GUI_CMD_LEAVE);
    }
    else if (e->type == SPNAV_EVENT_MOTION)
    {
        int     rewind = menu->spnav_rewind;
        int     rewind_y = menu->spnav_rewind_y;
        int     push = menu->spnav_push;

        TRACE(0, _b("spnav-event-motion: <x=%d,y=%d,z=%d>, <rx=%d,ry=%d,rz=%d>, p=%d"),
              e->motion.x, e->motion.y, e->motion.z,
              e->motion.rx, e->motion.ry, e->motion.rz,
              e->motion.period);

        /* ...reset accumulator if period exceeds a threshold */
        (e->motion.period > __SPNAV_SEQUENCE_THRESHOLD ? rewind = rewind_y = push = 0 : 0);

        TRACE(DEBUG, _b("spnav event: rewind=%d, rewind_y=%d, push=%d, rz=%d, ry=%d, z=%d"), rewind, rewind_y, push, e->motion.rz, e->motion.ry, e->motion.z);
        
        /* ...use left-right movements for forward/backward movement */
        if ((rewind += e->motion.rz) > __SPNAV_REWIND_THRESHOLD)
        {
            TRACE(DEBUG, _b("spnav 'forward' event decoded"));
            menu_command(menu, GUI_CMD_FORWARD);
            rewind = rewind_y = push = 0;
        }
        else if (rewind < -__SPNAV_REWIND_THRESHOLD)
        {
            TRACE(DEBUG, _b("spnav 'backward' event decoded"));
            menu_command(menu, GUI_CMD_BACKWARD);
            rewind = rewind_y = push = 0;
        }

        /* ...use up-down movements for vertical movement */
        if ((rewind_y -= e->motion.rx) > __SPNAV_REWIND_THRESHOLD)
        {
            TRACE(DEBUG, _b("spnav 'upward' event decoded"));
            menu_command(menu, GUI_CMD_UPWARD);
            rewind = rewind_y = push = 0;
        }
        else if (rewind_y < -__SPNAV_REWIND_THRESHOLD)
        {
            TRACE(DEBUG, _b("spnav 'downward' event decoded"));
            menu_command(menu, GUI_CMD_DOWNWARD);
            rewind = rewind_y = push = 0;
        }

        /* ...process push-movement (no accumulator here, maybe?) */
        if (push == 0)
        {
            /* ...push-event detector is enabled */
            if (e->motion.y < -__SPNAV_PUSH_THRESHOLD)
            {
                TRACE(DEBUG, _b("spnav 'push' event decoded"));
                menu_command(menu, GUI_CMD_SELECT);
                menu_command(menu, GUI_CMD_CLOSE);
                rewind = rewind_y = 0, push = -1;
            }
        }
        else
        {
            /* ...enable push-event detector if joystick is unpressed */
            if (e->motion.y >= -__SPNAV_PUSH_THRESHOLD / 10)
            {
                TRACE(DEBUG, _b("spnav 'push' detector activated"));
                push = 0;
            }
        }

        /* ...update accumulators state */
        menu->spnav_rewind = rewind, menu->spnav_rewind_y = rewind_y, menu->spnav_push = push;
    }
 
    return 0;
}

/*******************************************************************************
 * Entry points
 ******************************************************************************/

/* ...module initialization */
carousel_t * carousel_create(carousel_cfg_t *cfg, void *cdata)
{
    carousel_t     *menu;
    int             n, m;
    int             w, h, format;
    float           x0, y0, x1, y1;
    void           *data;
    GLuint          tex;
    int             i, j;

    /* ...basic sanity check */
    CHK_ERR(cfg && cfg->size && cfg->size_y && cfg->thumbnail && cfg->select, (errno = EINVAL, NULL));

    /* ...allocate data handle */
    CHK_ERR(menu = calloc(1, sizeof(*menu)), (errno = ENOMEM, NULL));

    /* ...set callback data */
    menu->cfg = cfg, menu->cdata = cdata;

    /* ...set menu parameters */
    n = cfg->size, m = cfg->size_y, w = cfg->width, h = cfg->height, format = GST_VIDEO_FORMAT_ARGB;

    /* ...force library to allocate buffer by its own */
    data = NULL;

    /* ...generate a texture (we are running from EGL context) */
    if (!CHK_GL(glGenTextures(1, &tex)))
    {
        errno = ENOMEM;
        goto error;
    }

    /* ...save texture id */
    menu->tex = tex;

    /* ...set texture parameters */
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (!CHK_GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, n * w, m * h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL)))
    {
        TRACE(ERROR, _x("failed to allocate GL texture: %d * %d * %d"), n, w, h);
        errno = ENOMEM;
        goto error_tex;
    }

    /* ...load thumbnails */
    for (i = 0, data = NULL; i < m; i++)
    {
        for (j = 0; j < n; j++)
        {
            const char  *path;
        
            /* ...get name of the thumbnail */
            if ((path = cfg->thumbnail(cdata, i, j)) == NULL)
            {
                TRACE(ERROR, _x("failed to get a thumbnail-%d name: %m"), i);
                goto error_tex;
            }
        
            /* ...load PNG (all thumbnails must have same dimensions) */
            if (create_png(path, &w, &h, &format, &data) != 0)
            {
                TRACE(ERROR, _x("failed to load thumbnail '%s': %m"), path);
                goto error_tex;
            }

            /* ...upload image to the texture */
            if (!CHK_GL(glTexSubImage2D(GL_TEXTURE_2D, 0, j * w, i * h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data)))
            {
                errno = ENOMEM;
                goto error_tex;
            }
        }
    }

    /* ...release data after all */
    free(data), data = NULL;

    /* ...release binding */
    glBindTexture(GL_TEXTURE_2D, 0);

    /* ...define number of simultaneously shown items */
    menu->width = (float)cfg->window_size / n;
    menu->height = (float)cfg->window_size_y / m;

    /* ...calculate normalization factor for scaling window to [-1,1] range */
    menu->scale = 1.0 / (cfg->window_size * cfg->x_length);

    /* ...set menu view-port */
    x0 = cfg->x_center - (cfg->window_size * cfg->x_length) / 2;
    x1 = cfg->x_center + (cfg->window_size * cfg->x_length) / 2;
    y0 = cfg->y_center - 1.0 / 2 * cfg->y_length;
    y1 = cfg->y_center + 1.0 / 2 * cfg->y_length;
    
    /* ...set menu view-port */
    texture_set_view(&menu->view, x0, y0, x1, y1);

    /* ...success; return widget handle */
    return menu;

#if 0
error_shader:
    /* ...destroy shader */
    shader_destroy(menu->shader);
#endif

error_tex:
    /* ...free image data buffer if needed */
    (data ? free(data) : 0);

    /* ...destroy texture */
    glDeleteTextures(1, &tex);
    
error:
    /* ...release texture binding */
    glBindTexture(GL_TEXTURE_2D, 0);

    /* ...release handle */
    free(menu);
    
    return NULL;
}

/* ...pre-build carousel drawing shader */
int carousel_shader_prebuild(void)
{
    /* ...prepare menu shader */
    if ((__carousel_shader = shader_create(&SHADER_DESC(CAROUSEL))) == NULL)
    {
        TRACE(ERROR, _x("shader initialization failed: %m"));
        return -errno;
    }

    TRACE(INIT, _b("carousel-fading shader prebuilt"));

    return 0;
}
