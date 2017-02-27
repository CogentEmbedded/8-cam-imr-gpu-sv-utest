/*******************************************************************************
 * utest-car.c
 *
 * IMR unit test application - car rendering using OSMesa
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

#define MODULE_TAG                      CAR

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "sv/types.h"
#include "utest-display.h"
#include "utest-common.h"
#include <math.h>
#include "utest-model.h"
#include "utest-math.h"

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 0);

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

typedef struct car_renderer
{
    /* ...shader data */
    shader_data_t      *shader, *shader_vbo, *shader_cb;
    
    /* ...vertex array object */
    GLuint              vbo, *ibo;

    /* ...car image textures */
    GLuint             *tex;

    /* ...map of the indices for a material */
    int                *tex_map;

    /* ...off-screen renderer */
    fbo_data_t         *fbo;

    /* ...car 3D-model */
    wf_obj_data_t      *obj;

    /* ...buffer dimensions */
    int                 width, height;

    /* ...set data */
    obj_set_t          *wheels;

    /* ...car model matrix */
    __mat4x4            model_matrix;

}   car_renderer_t;

/*******************************************************************************
 * Auxiliary helpers
 ******************************************************************************/

#define GLCHECK()                                   \
({                                                  \
    GLint   __err = glGetError();                   \
                                                    \
    if (__err != 0)                                 \
        TRACE(ERROR, _x("gl-error: %X"), __err);    \
    __err;                                          \
})

/*******************************************************************************
 * Internal functions
 ******************************************************************************/

#define SH_SETPREC  "precision highp float;\n"

static const char *shVertCode =
    SH_SETPREC
    "attribute highp vec3 coord3d;\n"
    "attribute highp vec3 normal3d;\n"
    "attribute highp vec2 st;\n"

    "uniform mat4 pvmMat;\n"
    "uniform mat4 vmMat;\n"
    "uniform mat3 nMat;\n"

    "varying highp vec3 N;\n"
    "varying highp vec3 v;\n"
    "varying highp vec2 t;\n"

    "void main(void)\n"
    "{\n"
    "   v = vec3(vmMat * vec4(coord3d, 1.0));\n"
    "   N = normalize(nMat * normal3d);\n"
    "   t = st;\n"
    "   gl_Position = pvmMat * vec4(coord3d, 1.0);\n"
    "}\n";

static const char *shFragCode =
    SH_SETPREC
    "varying highp vec3 N;\n"
    "varying highp vec3 v;\n"
    "varying highp vec2 t;\n"

    "uniform vec3 lPos;\n"
    "uniform vec3 lCol;\n"
    "uniform float lIntens;\n"

    "uniform vec3 Ks;\n"
    "uniform vec3 Kd;\n"
    "uniform vec3 Ka;\n"
    "uniform float Ns;\n"
    "uniform float D;\n"
    "uniform bool useKa;\n"
    "uniform bool  useKd;\n"
    "uniform sampler2D texKa;\n"
    "uniform sampler2D texKd;\n"

    "void main (void)\n"
    "{\n"
    "  vec3 dirl = normalize(lPos - v);\n"
    "  vec3 view = normalize(-v);\n"
	"  vec4 colKd = (useKd ? texture2D(texKd, t).zyxw : vec4(Kd, 1.0));\n"

#if 0
	"  vec3 halfl = (dirl+view)/2.0;\n"
    "  vec3 lambert = colKd.xyz * lCol * max(dot(N, dirl), 0.0);\n"
	"  float dist = length(lPos - v);\n"
	"  float k = 0.0001;\n"
	"  float attenuation = lIntens / (k*dist*dist);\n"
	"  vec3 phong = Ks * lCol * pow(max(dot(N, halfl), 0.0), Ns);\n"
    //"  vec3 linear_color = Ka * colKd.xyz * lIntens + attenuation * (lambert + phong);"
    "  vec3 linear_color = Ka * colKd.xyz + 1.0 * (lambert + phong);"
    //"  vec3 linear_color = Ka * colKd.xyz + 1.0 * (lambert);"
#else
	"   mediump float lambertian = max(dot(N, dirl), 0.0);\n"
	"   mediump vec3 lambert;\n"
	"   mediump vec3 phong;\n"
	"   mediump float dist = length(lPos - v);\n"
	"   mediump float attenuation = lIntens / (1.0 + 0.000005 * dist*dist);\n"
	"	mediump float specular;\n"

	"	if(lambertian > 0.0) { \n"
//	"   	mediump vec3 halfl = (dirl+view)/2.0;\n"
//	"		specular = pow(max(dot(N, halfl), 0.0), Ns);\n"
	"	    vec3 R = normalize(-reflect(dirl, N));\n"
	"		specular = pow(max(dot(R, view), 0.0), Ns / 4.0);\n"
    "       lambert = colKd.xyz * lCol * lambertian;\n"
	"       phong = Ks * lCol * specular;\n"
	"	}\n"
    "   else {\n"
    "       lambert = vec3(0.0);\n"
    "       phong = vec3(0.0);\n"
    "   }\n"

//	"  vec3 linear_color = (Ka + vec3(0.2))*colKd.xyz * lIntens + attenuation * (lambert + phong);"
	"  vec3 linear_color = (Ka + vec3(0.0))*colKd.xyz * lIntens + 1.0 * (lambert + phong);"
#endif
    "  gl_FragColor = vec4(linear_color, colKd.w * D);\n"
    "}\n";

/* ...build car modem renderer */
#define SHADER_TAG                      MODEL
#define SHADER_VERTEX_SOURCE            shVertCode
#define SHADER_FRAGMENT_SOURCE          shFragCode

/* ...shader uniforms */
#define SHADER_UNIFORMS                                                     \
    __U(pvmMat), __U(vmMat), __U(nMat),     /* ...matrices */               \
    __U(lPos), __U(lCol),  __U(lIntens),    /* ...color specification */    \
    __U(Ks), __U(Kd), __U(Ka),              /* ...reflectivity colors */    \
    __U(Ns), __U(D),                        /* ...sharpness/dissolve */     \
    __U(useKa), __U(texKa),                 /* ...Ka texture map */         \
    __U(useKd), __U(texKd),                  /* ...Kd texture map */

/* ...shader attributes */
#define SHADER_ATTRIBUTES                       \
    __A(coord3d), __A(normal3d), __A(st),

/* ...build shader lists */
#include "shader-impl.h"

/*******************************************************************************
 * Testing
 ******************************************************************************/

/* ...VBO vertex shader */
static const char *vbo_vertex_shader =
    SH_SETPREC
    "attribute vec3	v;\n"
    "uniform mat4	proj;\n"
    "varying vec3	vertex;\n"
    "void main(void)\n"
    "{\n"
    "	gl_Position = proj * vec4(v.xyz, 1.0);\n"
    //"   gl_PointSize = clamp(2.0 / 0.8 * v.z + 18.0 / 4.0, 1.0, 4.0);\n"
    "   gl_PointSize = clamp(100.0 *( v.z + 1.0), 0.0, 10.0);\n"
    //"   gl_PointSize = 1.0;\n"
    "	vertex = v;\n"
    "}\n";

/* ...VBO fragment shader */
static const char *vbo_fragment_shader =
    SH_SETPREC
    "uniform float maxdist;\n"
    "uniform vec4 color;\n"
    "varying vec3 vertex;\n"
    "void main()\n"
    "{\n"
    "   float distNorm = 1.0 * clamp(length(vertex.xy)/maxdist, 0.0, 1.0);\n"
    "   gl_FragColor = color * (1.0-distNorm);\n"
    "}\n";

/* ...build car modem renderer */
#define SHADER_TAG                      VBO
#define SHADER_VERTEX_SOURCE            vbo_vertex_shader
#define SHADER_FRAGMENT_SOURCE          vbo_fragment_shader

#define SHADER_UNIFORMS                                                         \
    __U(proj),                      /* ...projection matrix */                  \
    __U(maxdist),                   /* ...clamping for distance from center */  \
    __U(color),                     /* ...point/line color */

#define SHADER_ATTRIBUTES                                   \
    __A(v),                         /* ...point vertex */

#include "shader-impl.h"

/*******************************************************************************
 * Internal testing
 ******************************************************************************/

#define U(id)       u[UNIFORM(VBO, id)]
#define A(id)       ATTRIBUTE(VBO, id)

/* ...visualize VBO as an array of points */
static inline void line_draw(shader_data_t *shader, int i, u32 color, const GLfloat *pvm)
{
    const GLint    *u = shader_uniforms(shader);
    
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
    
    /* ...set current precompiled shader */
    glUseProgram(shader_program(shader));

    /* ...prepare shader uniforms */
    glUniformMatrix4fv(U(proj), 1, GL_FALSE, (pvm ? : __identity));
    glUniform1f(U(maxdist), 2.0);
    glUniform4f(U(color), ((color >> 24) & 0xFF) / 256.0, 
                ((color >> 16) & 0xFF) / 256.0, ((color >> 8) & 0xFF) / 256.0, ((color >> 0) & 0xFF) / 256.0);

    /* ...bind VBO */
    glVertexAttribPointer(A(v), 3, GL_FLOAT, GL_FALSE, 0, __coords + i * 6); GLCHECK();
    glEnableVertexAttribArray(A(v));

    /* ...draw VBOs in current viewport */
    glDrawArrays(GL_LINES, 0, 2); GLCHECK();

    /* ...cleanup GL state */
    glDisableVertexAttribArray(A(v));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

#undef U
#undef A

/*******************************************************************************
 * Checker-board pattern
 ******************************************************************************/

/* ...pattern vertex shader */
static const char *cb_vertex_shader =
    SH_SETPREC
    "uniform mat4 proj;\n"
    "attribute vec3 position;\n"
    "varying vec3 vertex;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = proj * vec4(position, 1.0);\n"
    "   vertex = position;\n"
    "}\n";

/* ...VBO fragment shader */
static const char *cb_fragment_shader =
    SH_SETPREC
    "uniform vec4 color;\n"
    "uniform float size;\n"
    "varying vec3 vertex;\n"
    "\n"
    "float cell_parity(float x, float y)\n"
    "{\n"
    "   return mod(floor(x / size) + floor(y / size), 2.0);\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "   gl_FragColor = vec4(color.rgb * cell_parity(vertex.x, vertex.z), color.a);\n"
    "}\n";

/* ...build car modem renderer */
#define SHADER_TAG                      CB
#define SHADER_VERTEX_SOURCE            cb_vertex_shader
#define SHADER_FRAGMENT_SOURCE          cb_fragment_shader

#define SHADER_UNIFORMS                                         \
    __U(proj),                      /* ...projection matrix */  \
    __U(color),                     /* ...odd-cell color */     \
    __U(size),                      /* ...size of the cell */

#define SHADER_ATTRIBUTES                                   \
    __A(position),                  /* ...vertex position*/

#include "shader-impl.h"

/*******************************************************************************
 * Checker-box rendering function
 ******************************************************************************/

#define U(id)       u[UNIFORM(CB, id)]
#define A(id)       ATTRIBUTE(CB, id)

#define __vcoord_rectangle(p, x0, z0, x1, z1, y)                        \
({                                                                      \
    GLfloat _x0 = (x0), _z0 = (z0), _x1 = (x1), _z1 = (z1), _y = (y);   \
    GLfloat *_p = (p);                                                  \
                                                                        \
    *_p++ = _x0, *_p++ = _y, *_p++ = _z0;                               \
    *_p++ = _x0, *_p++ = _y, *_p++ = _z1;                               \
    *_p++ = _x1, *_p++ = _y, *_p++ = _z0;                               \
    *_p++ = _x0, *_p++ = _y, *_p++ = _z1;                               \
    *_p++ = _x1, *_p++ = _y, *_p++ = _z1;                               \
    *_p++ = _x1, *_p++ = _y, *_p++ = _z0;                               \
    _p;                                                                 \
})

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

static int shadow_draw(car_renderer_t *car, const GLfloat *pvm, u32 color, float size)
{
    shader_data_t  *shader = car->shader_cb;
    const GLint    *u = shader_uniforms(shader);
    const float    *min, *max;
    GLfloat         verts[3 * 6];
    GLint           program = 0;

    /* ...retrieve car dimensions */
    obj_dimensions(car->obj, &min, &max);

    TRACE(1, _b("dim: %f,%f - %f,%f"), min[0], min[2], max[0], max[2]);
    
    /* ...fill the vertex pattern */
    __vcoord_rectangle(verts, min[0], min[2], max[0], max[2], min[1]);

    /* ...start shader program */
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glUseProgram(shader_program(shader));

    /* ...bind uniforms */
    glUniformMatrix4fv(U(proj), 1, GL_FALSE, pvm);
    glUniform4f(U(color), __rgba_r(color), __rgba_g(color), __rgba_b(color), __rgba_a(color));
    glUniform1f(U(size), size);

    /* ...set vertices array attribute ("position") */
    glVertexAttribPointer(A(position), 3, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(A(position));

    /* ...render triangles on the surface */
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* ...disable generic attributes arrays */
    glDisableVertexAttribArray(A(position));

    /* ...restore original program */
    glUseProgram(program);

    return 0;
}

#undef U
#undef A

/*******************************************************************************
 * Car rendering
 ******************************************************************************/

/* ...some constants - tbd - adjustible? */
static const GLfloat light_pos[4] = {
    0.0f, 0.0f, 1000.0f, 1.0f
};

static const GLfloat light_color[4] = {
    1.0f, 1.0f, 1.0f, 1.0f
};

static const GLfloat sceneLum = 0.7;

static const GLfloat transparency = 1.0;

#if 1
static const __mat4x4 __model_m = {
    1,  0,  0,  0,
    0,  0,  1,  0,
    0,  1,  0,  0,
    0,  0,  0,  1,
};
#else
static const __mat4x4 __model_m = {
    1,  0,  0,  0,
    0,  1,  0,  0,
    0,  0,  1,  0,
    0,  0,  0,  1,
};
#endif

/*******************************************************************************
 * Rendering function
 ******************************************************************************/

#define U(id)       u[UNIFORM(MODEL, id)]
#define A(id)       ATTRIBUTE(MODEL, id)

static void render(car_renderer_t *car, const GLfloat *pvm, const GLfloat *vm, const GLfloat *vm_norm)
{
    obj_set_t      *set = car->wheels;
    const GLint    *u = shader_uniforms(car->shader);
    obj_subset_t   *s;
    int             i;

    glUseProgram(shader_program(car->shader)); GLCHECK();

    /* ...bind uniforms; projection matrices and light source specification */
    glUniformMatrix4fv(U(pvmMat), 1, GL_FALSE, pvm); GLCHECK();
    glUniformMatrix4fv(U(vmMat), 1, GL_FALSE, vm); GLCHECK();
    glUniformMatrix3fv(U(nMat), 1, GL_FALSE, vm_norm);	GLCHECK();
    glUniform3fv(U(lPos), 1, light_pos); GLCHECK();
    glUniform3fv(U(lCol), 1, light_color); GLCHECK();
    glUniform1f(U(lIntens), 1.0); GLCHECK();
    
    /* ...bind vertex buffer */
    glBindBuffer(GL_ARRAY_BUFFER, car->vbo);    GLCHECK();

    /* ...set vertex coordinates */
    glEnableVertexAttribArray(A(coord3d));   GLCHECK();
    glVertexAttribPointer(A(coord3d), 3, GL_FLOAT, GL_FALSE, sizeof(obj_vertex_t), (void *)(uintptr_t)0);    GLCHECK();
    
    /* ...set normales */
    glEnableVertexAttribArray(A(normal3d));   GLCHECK();
    glVertexAttribPointer(A(normal3d), 3, GL_FLOAT, GL_FALSE, sizeof(obj_vertex_t), (void *)(uintptr_t)(sizeof(float) * 3)); GLCHECK(); 

    /* ...set texture coordinates */
    glEnableVertexAttribArray(A(st));   GLCHECK();
    glVertexAttribPointer(A(st), 2, GL_FLOAT, GL_FALSE, sizeof(obj_vertex_t), (void *)(uintptr_t)(sizeof(float) * 6)); GLCHECK(); 
    
    /* ...draw all opaque materials */
    for (i = 0, s = obj_subset_first(set); s; i++, s = obj_subset_next(set, s))
    {
        int                 size = obj_subset_ibo_size(s);
        obj_material_t     *m = obj_subset_material(set, s);
        GLfloat             ambient[3];
        int                 index;
        
        if (!size)      continue;

        /* ...skip all transparent materials for the moment */
        if (m->d < 1.0)
        {
            /* ...enable blending */
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            
            /* ...disable writing into depth buffer */
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glDepthMask(GL_TRUE);
        }

        TRACE(0, _b("mat[%d]: d=%f"), i, m->d);
        
        ambient[0] = m->Ka[0] * sceneLum;
        ambient[1] = m->Ka[1] * sceneLum;
        ambient[2] = m->Ka[2] * sceneLum;

        glUniform3fv(U(Kd), 1, m->Kd);  GLCHECK();
        glUniform3fv(U(Ks), 1, m->Ks);  GLCHECK();
        glUniform3fv(U(Ka), 1, ambient);  GLCHECK();
        glUniform1f(U(Ns), m->Ns);  GLCHECK();
        glUniform1f(U(D), m->d * transparency);  GLCHECK();

        /* ...bind Ka texture (ambient reflectivity) */
        if ((index = m->map_Ka - 1) >= 0)
        {
            glUniform1i(U(useKa), 1);  GLCHECK();
            glUniform1i(U(texKa), car->tex_map[index]); GLCHECK();
            glActiveTexture(GL_TEXTURE0 + car->tex_map[index]); GLCHECK();
            glBindTexture(GL_TEXTURE_2D, car->tex[index]); GLCHECK();
        }
        else
        {
            glUniform1i(U(useKa), 0);  GLCHECK();
        }

        /* ...bind Kd texture (diffuse reflectivity) */
        if ((index = m->map_Kd - 1) >= 0)
        {
            glUniform1i(U(useKd), 1);  GLCHECK();
            glUniform1i(U(texKd), index); GLCHECK();  GLCHECK();
            glActiveTexture(GL_TEXTURE0 + car->tex_map[index]); GLCHECK();
            glBindTexture(GL_TEXTURE_2D, car->tex[index]); GLCHECK();
        }
        else
        {
            glUniform1i(U(useKd), 0);  GLCHECK();
        }

        TRACE(DEBUG, _b("Kd: %f,%f,%f"), m->Kd[0], m->Kd[1], m->Kd[2]);
        TRACE(DEBUG, _b("Ks: %f,%f,%f"), m->Ks[0], m->Ks[1], m->Ks[2]);
        TRACE(DEBUG, _b("Ka: %f,%f,%f"), ambient[0], ambient[1], ambient[2]);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, car->ibo[i]); GLCHECK();
        glDrawElements(GL_TRIANGLES, size * 3, GL_UNSIGNED_INT, 0); GLCHECK();
    }

    /* ...flush all processing */
    //glFinish();

    /* ...clean GL state */
    glUseProgram(0);    GLCHECK();

    glBindTexture(GL_TEXTURE_2D, 0);  GLCHECK();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);  GLCHECK();
    glBindBuffer(GL_ARRAY_BUFFER, 0);  GLCHECK();
}

#undef U
#undef A

/*******************************************************************************
 * Car model initialization
 ******************************************************************************/

/* ...car initialization */
static int car_init(car_renderer_t *car)
{
    wf_obj_data_t  *obj = car->obj;
    obj_set_t      *set;
    obj_subset_t   *s;
    int             n, m;
    void           *buffer;
    int             i;
    int             length;

    /* ...load car model */
    glGenBuffers(1, &car->vbo); GLCHECK();
    glBindBuffer(GL_ARRAY_BUFFER, car->vbo); GLCHECK();
    glBufferData(GL_ARRAY_BUFFER, obj_vbo_size(obj) * sizeof(obj_vertex_t), NULL, GL_STATIC_DRAW);

    /* ...upload VBO */
    CHK_ERR(buffer = glMapBufferOES(GL_ARRAY_BUFFER, GL_WRITE_ONLY_OES), -(errno = ENOMEM));
    length = obj_upload_vbo(obj, buffer, 3, 3, 2);
    BUG((size_t)length > obj_vbo_size(obj) * sizeof(obj_vertex_t), _x("invalid length: %d > %d * %zd"), length, obj_vbo_size(obj), sizeof(obj_vertex_t)); 
    glUnmapBufferOES(GL_ARRAY_BUFFER);

    /* ...get total number of textures in a model */
    if ((m = obj_textures_num(obj)) > 0)
    {
        /* ...allocate index remaping array for a set */
        CHK_ERR(car->tex_map = calloc(m, sizeof(int)), -errno);
    }

#if 0
    /* ...use just wheels and interior */
    CHK_ERR(car->wheels = set = obj_set_create(obj, "Wheel2"), -errno);
    CHK_API(obj_set_add(set, "Wheel4"));
    CHK_API(obj_set_add(set, "Wheel005"));
    CHK_API(obj_set_add(set, "Wheel006"));

#else
    /* ...complete car model */
    CHK_ERR(car->wheels = set = obj_set_create(obj, NULL/*"Wheel2"*/), -errno);
    //CHK_API(obj_set_add(set, "Wheel006"));
    //CHK_API(obj_set_add(set, "car75"));
#endif

    /* ...get total number of materials in a set */
    CHK_ERR((n = obj_set_subsets_number(set)) > 0, -(errno = EINVAL));

    /* ...create IBOs for all materials found in a set */
    CHK_ERR(car->ibo = malloc(n * sizeof(GLuint)), -(errno = ENOMEM));

    /* ...create element buffers for all materials */
    glGenBuffers(n, car->ibo); GLCHECK();

    /* ...get total number of textures in a set */
    if ((m = obj_set_textures_number(set)) > 0)
    {
        obj_texture_t  *t = obj_texture_first(set);
        int             width, height;
        void           *data;

        /* ...allocate textures vector */
        CHK_ERR(car->tex = malloc(m * sizeof(*car->tex)), -(errno = ENOMEM));

        /* ...generate array of textures */
        glGenTextures(m, car->tex); GLCHECK();

        /* ...upload all of them */
        for (i = 0; i < m; i++, t = obj_texture_next(set, t))
        {
            int     index = obj_texture_idx(set, t);
            
            /* ...read texture file */
            CHK_ERR(data = obj_texture_map(set, t, &width, &height), -errno);

            /* ...upload texture into GL context */
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, car->tex[i]); GLCHECK();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data); GLCHECK();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            /* ...do not need texture data any longer */
            obj_texture_unmap(set, t);

            /* ...put texture index in an array */
            car->tex_map[index] = i;

            TRACE(INFO, _b("texture-%d[%d] loaded: %d*%d (%p)"), i, car->tex[i], width, height, data);
        }
    }

    /* ...process all subsets */
    for (i = 0, s = obj_subset_first(set); s; i++, s = obj_subset_next(set, s))
    {
        int             size = obj_subset_ibo_size(s);
        obj_material_t *m = obj_subset_material(set, s);

        /* ...it cannot be empty in fact */
        if (!size)      continue;

        /* ...allocate IBO for a particular material */
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, car->ibo[i]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, size * sizeof(obj_element_t), NULL, GL_STATIC_DRAW);

        /* ...upload material IBO */
        CHK_ERR(buffer = glMapBufferOES(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY_OES), -(errno = ENOMEM));
        obj_subset_ibo_upload(set, s, buffer);
        glUnmapBufferOES(GL_ELEMENT_ARRAY_BUFFER);

        TRACE(INFO, _b("material #%d: transparency: %f"), i, m->d);
    }

    /* ...generate vertex array object */
    //glGenVertexArraysOES(1, &car->vao); GLCHECK();

    /* ...clean-up context state */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);  GLCHECK();
    glBindBuffer(GL_ARRAY_BUFFER, 0);  GLCHECK();
    glBindTexture(GL_TEXTURE_2D, 0);  GLCHECK();

    return 0;
}

/*******************************************************************************
 * Car rendering API
 ******************************************************************************/

static inline int model_matrix_init(car_renderer_t *car)
{
    const float    *min, *max;
    __mat4x4        m, s, r;
    __scalar        x0, y0, z0;
    __scalar        L;

    /* ...retrieve car dimensions */
    obj_dimensions(car->obj, &min, &max);

    /* ...get car length (maximal dimension) */
    L = (max[0] - min[0]);

    /* ...get translation parameters */
    x0 = -(max[0] + min[0]) / 2;
    y0 = -min[1];
    z0 = -(max[2] + min[2]) / 2;

    /* ...setup translation matrix */
    __mat4x4_translation(m, x0, y0, z0);

    /* ...scale to make length equal to 0.75 */
    __matNxN_M_diag(4, 3, s, 1.1/* 0.8 */ / L);
    
    /* ...get resulting matrix */
    __mat4x4_mul(s, m, r);

    /* ...apply "standard" rotation/flipping to the resulting matrix */
    __mat4x4_mul(__model_m, r, car->model_matrix);
    //__mat4x4_dump(car->model_matrix, "C");
    
    return 0;
}

car_renderer_t * car_renderer_init(char *file, int w, int h)
{
    car_renderer_t *car;
    
    /* ...allocate renderer data */
    CHK_ERR(car = calloc(1, sizeof(*car)), (errno = ENOMEM, NULL));

    /* ...set car buffer dimensions */
    car->width = w, car->height = h;

    /* ...load car 3D-model */
    if ((car->obj = obj_create(file)) == NULL)
    {
        TRACE(ERROR, _x("failed to create car model: %m"));
        goto error;
    }
    else
    {
        int     n = obj_groups_num(car->obj), i;
        
        /* ...enumerate all groups */
        for (i = 0; i < n; i++)
        {
            TRACE(INFO, _b("group[%d]: %s"), i, obj_group(car->obj, i));
        }
    }

    /* ...initialize car model matrix */
    model_matrix_init(car);

    /* ...create framebuffer for off-screen rendering */
    if ((car->fbo = fbo_create(w, h)) == NULL)
    {
        TRACE(ERROR, _x("failed to create fbo: %m"));
        goto error_obj;
    }

    /* ...acquire GL rendering context */
    if (fbo_get(car->fbo))
    {
        TRACE(ERROR, _x("failed to acquire rendering context: %m"));
        goto error_fbo;
    }
    
    /* ...initialize rendering shader */
    if ((car->shader = shader_create(&SHADER_DESC(MODEL))) == NULL)
    {
        TRACE(ERROR, _x("shader initialization failed: %m"));
        goto error_ctx;
    }

    /* ...VBO shader initialization */
    if ((car->shader_vbo = shader_create(&SHADER_DESC(VBO))) == NULL)
    {
        TRACE(ERROR, _x("vbo shader failed: %m"));
        goto error_ctx;
    }

    /* ...checker-board shader initialization */
    if ((car->shader_cb = shader_create(&SHADER_DESC(CB))) == NULL)
    {
        TRACE(ERROR, _x("cb shader failed: %m"));
        goto error_ctx;
    }
    
    /* ...initialize materials - tbd */
    if (car_init(car))
    {
        TRACE(ERROR, _x("failed to setup car model renderer: %m"));
        goto error_ctx;
    }

    /* ...release framebuffer context */
    fbo_put(car->fbo);

    TRACE(INIT, _b("car-renderer created: %p (%d*%d)"), car, w, h);
    
    return car;

error_ctx:
    /* ...destroy shaders as required */
    (car->shader_vbo ? shader_destroy(car->shader_vbo) : 0);
    (car->shader ? shader_destroy(car->shader) : 0);
    
    /* ...release rendering context */
    fbo_put(car->fbo);

error_fbo:
    /* ...destroy framebuffer */
    fbo_destroy(car->fbo);

error_obj:
    /* ...destroy car model */
    obj_destroy(car->obj);

error:
    /* ...free car renderer handle */
    free(car);
    return NULL;
}

/* ...render car image into texture */
int car_render(car_renderer_t *car, texture_data_t *texture, const __mat4x4 P, const __mat4x4 V, const __mat4x4 M, u32 cb_color)
{
    __mat4x4    pvm, vm, t1;
    __mat3x3    vmn;
    __scalar    det = 0;
    
    /* ...calculate VM matrix: t1 = V * M */
    __mat4x4_mul(V, M, t1);
    
    /* ...multiply by car model: vm = t1 * model */
    __mat4x4_mul(t1, car->model_matrix, vm);

    /* ...calculate PVM matrix: pvm = P * vm */
    __mat4x4_mul(P, vm, pvm);

    /* ...get inverse of major 3x3 minor of vm */
    __mat4x4_min3x3_inv(vm, vmn, NULL/*&det*/);

    TRACE(1, _b("det=%f"), det);
    
    /* ...transpose vmn */
    __mat3x3_tr(vmn);

    TRACE(DEBUG, _b("car-rendering start: fbo:%p, tex:%p"), car->fbo, texture);

    /* ...acquire rendering context */
    CHK_API(fbo_get(car->fbo));

    TRACE(DEBUG, _b("car-rendering context set"));

    /* ...attach texture */
    if (fbo_attach_texture(car->fbo, texture, 0) < 0)
    {
        fbo_put(car->fbo);
        return CHK_API(-errno);
    }

    /* ...cull-face enable - tbd - this stuff needs to be considered carefully */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE); GLCHECK();
    glCullFace(GL_BACK); GLCHECK();
    glFrontFace(GL_CW); GLCHECK();
    glEnable(GL_DEPTH_TEST); GLCHECK();
    glDepthMask(GL_TRUE);

    /* ...prepare buffer */
    glViewport(0, 0, car->width, car->height);    
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f); GLCHECK();
    glClearDepthf(1.0f); GLCHECK();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT); GLCHECK();

    /* ...draw shadow */
    if (cb_color)
    {
        shadow_draw(car, pvm, cb_color, 250.0);
    }
    
    /* ...render image */
    render(car, pvm, vm, vmn);

    if (LOG_LEVEL > 0)
    {
        /* ...add coordinate system indication */
        __mat4x4_mul(P, V, t1);
        __mat4x4_mul(t1, M, pvm);

        line_draw(car->shader_vbo, 0, 0xFF0000FF, pvm);
        line_draw(car->shader_vbo, 1, 0x00FF00FF, pvm);
        line_draw(car->shader_vbo, 2, 0x0000FFFF, pvm);
    }
    
    TRACE(DEBUG, _b("car-rendering context done"));

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    /* ...release rendering context (trigger rendering if no active GL context was set) */
    fbo_put(car->fbo);

    return 0;
}
