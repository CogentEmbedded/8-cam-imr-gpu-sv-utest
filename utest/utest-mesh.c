/*******************************************************************************
 * utest-mesh.c
 *
 * IMR unit test application - mesh support
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

#define MODULE_TAG                      MESH

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"
#include "utest-app.h"
#include "utest-mesh.h"
#include "utest-math.h"
#include "utest-model.h"
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

/* ...vertex indices */
typedef int     mesh_vbi_t[2];

/* ...mesh element indices */
typedef int     mesh_ibo_t[3];

/* ...mesh descriptor */
struct mesh_data
{
    /* ...vertex indices */
    mesh_vbi_t         *vbi;

    /* ...array of vertices (single array for all 4 cameras) */
    __vec3             *v;

    /* ...scratch buffer for projection transformation */
    __vec3             *b;

    /* ...total number of vertices */
    int                 vnum;
    
    /* ...IBO buffers corresponding to cameras */
    mesh_ibo_t         *ibo[4];

    /* ...IBO sizes */
    int                 fnum[4];

    /* ...texture coordinates */
    __vec2             *uv[4];

    /* ...alpha-plane coordinates */
    __vec2             *a[4];

    /* ...scratch buffer for XY coordinates */
    __vec3             *xy[4];
};

/*******************************************************************************
 * Reduce mesh stripping fully transparent faces
 ******************************************************************************/

/* ...alpha threshold for treating a point transparent */
#define __ALPHA_THRESHOLD       0.0

#if 0
/* ...set texture coordinates of particular vertex */
static inline void __texcoord_set(mesh_vbo_t *v, u16 *UV, u16 *A, int w, int h, int z)
{
    float   t;
    
    /* ...clamp texture coordinates to not exceed input dimensions */
    UV[0] = (u16)((t = v->u * w) < 0 ? 0 : (t > w - 1 ? w - 1 : round(t)));
    UV[1] = (u16)((t = v->v * h) < 0 ? 0 : (t > h - 1 ? h - 1 : round(t)));

    /* ...alpha-plane uses single dimension */
    A[0] = (u16)((t = v->w * z) < 0 ? 0 : (t > z - 1 ? z - 1 : round(z)));
    A[1] = 0;
}
#endif

/* ...threshold for deciding if the point is on the ground */
#define __GROUND_FLOOR_THRESHOLD            __MATH_FLOAT(0.0)

/* ...test if point is within the shadow region */
static inline int __vertex_inside_rect(__vec3 v, __vec4 r)
{
    if (v[2] > __GROUND_FLOOR_THRESHOLD)    return 0;
    if (v[0] < r[0] || v[0] > r[2])         return 0;
    if (v[1] < r[1] || v[1] > r[3])         return 0;

    return 1;
}

/* ...reduce mesh by stripping transparent faces */
static int __mesh_reduce(wf_obj_data_t *obj, mesh_ibo_t *ibo, int n, int (*vbi)[2], __vec4 r, __vec2 **uv, __vec2 **a, __vec3 **xy)
{
    mesh_ibo_t     *IBO = ibo;
    __vec2         *UV, *A;
    int             N;
    int             i;
    int             m_inner = 0, m_transparent = 0;
    
    /* ...allocate storage for texture coordinates for image- and alpha-planes */
    if ((UV = malloc(3 * sizeof(*UV) * n)) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate %zu bytes"), n * sizeof(*UV));
        return -(errno = ENOMEM);
    }

    if ((A = malloc(3 * sizeof(*A) * n)) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate %zu bytes"), n * sizeof(*A));
        free(UV);
        return -(errno = ENOMEM);
    }

    /* ...process all faces in the IBO */
    for (i = N = 0; i < n; i++, ibo++)
    {
        int         i0 = (*ibo)[0], i1 = (*ibo)[1], i2 = (*ibo)[2];
        int         v0 = vbi[i0][0], v1 = vbi[i1][0], v2 = vbi[i2][0];
        int         t0 = vbi[i0][1], t1 = vbi[i1][1], t2 = vbi[i2][1];
        __vec3      t[3];
        __vec3      v[3];

        //BUG(i0 > == 0 || i1 == 0 || i2 == 0, _x("invalid face: %d/%d/%d"), i0, i1, i2);

        /* ...get texture coordinates (3D-points) */
        obj_texcoord_store(obj, t0, t[0], 3);
        obj_texcoord_store(obj, t1, t[1], 3);
        obj_texcoord_store(obj, t2, t[2], 3);

        /* ...get vertex coordinates (3D-points) */
        obj_vertex_store(obj, v0, v[0], 3);
        obj_vertex_store(obj, v1, v[1], 3);
        obj_vertex_store(obj, v2, v[2], 3);

        if (i < 3)
        {
            /* ...dump few original points */
            TRACE(0, _b("f:%d (%d,%d)/(%d,%d)/(%d,%d): v: (%f,%f,%f)/(%f,%f,%f)/(%f,%f,%f), UV = (%f,%f,%f)/(%f,%f,%f)/(%f,%f,%f)"), i,
                  v0, t0, v1, t1, v2, t2,
                  v[0][0], v[0][1], v[0][2],
                  v[1][0], v[1][1], v[1][2],
                  v[2][0], v[2][1], v[2][2],
                  t[0][0], t[0][1], t[0][2],
                  t[1][0], t[1][1], t[1][2],
                  t[2][0], t[2][1], t[2][2]);
        }

        /* ...drop the triangles that have all 3 vertices inside shadow region */
        if (__vertex_inside_rect(v[0], r) && __vertex_inside_rect(v[1], r) && __vertex_inside_rect(v[2], r))
        {
            TRACE(DEBUG, _b("discard inner face #%d: (%f,%f,%f)/(%f,%f,%f)/(%f,%f,%f) inside [%f,%f - %f,%f]"),
                  i,
                  v[0][0], v[0][1], v[0][2],
                  v[1][0], v[1][1], v[1][2],
                  v[2][0], v[2][1], v[2][2],
                  r[0], r[1], r[2], r[3]);
            
            m_inner++;
        }
        else if (t[0][2] <= __ALPHA_THRESHOLD && t[1][2] <= __ALPHA_THRESHOLD && t[2][2] <= __ALPHA_THRESHOLD)
        {
            /* ...discard the trasparent face */
            TRACE(0, _b("transparent face %d discarded: (%f,%f,%f) - (%f,%f,%f) - (%f,%f,%f)"), i,
                  t[0][0], t[0][1], t[0][2],
                  t[1][0], t[1][1], t[1][2],
                  t[2][0], t[2][1], t[2][2]);
            
            m_transparent++;
        }
        else
        {
            /* ...accept current point; adjust IBO in place */
            (ibo != IBO ? memcpy(IBO, ibo, sizeof(*ibo)) : 0);

            /* ...put texture coordinates (no conversion to fixed point format here? - tbd) */
            UV[0][0] = t[0][0], UV[0][1] = /*1 -*/ t[0][1], A[0][0] = t[0][2], A[0][1] = 0;
            UV[1][0] = t[1][0], UV[1][1] = /*1 -*/ t[1][1], A[1][0] = t[1][2], A[1][1] = 0;
            UV[2][0] = t[2][0], UV[2][1] = /*1 -*/ t[2][1], A[2][0] = t[2][2], A[2][1] = 0;

            if (N < 3)
            {
                /* ...dump first resulting points */
                TRACE(0, _b("f:%d (%d,%d)/(%d,%d)/(%d,%d): v: (%f,%f,%f)/(%f,%f,%f)/(%f,%f,%f), UV = (%f,%f,%f)/(%f,%f,%f)/(%f,%f,%f)"), N,
                      v0, t0, v1, t1, v2, t2,
                      v[0][0], v[0][1], v[0][2],
                      v[1][0], v[1][1], v[1][2],
                      v[2][0], v[2][1], v[2][2],
                      t[0][0], t[0][1], t[0][2],
                      t[1][0], t[1][1], t[1][2],
                      t[2][0], t[2][1], t[2][2]);
            }

            /* ...advance writing position */
            IBO++, N++, UV += 3, A += 3;
        }
    }

    /* ...realloc texture coordinates vectors (memory fragmentation issue - tbd) */
    *uv = realloc(UV - 3 * N, 3 * sizeof(*UV) * N), *a = realloc(A - 3 * N, 3 * sizeof(*A) * N);

    /* ...allocate buffer for vertex coordinates - tbd */
    if ((*xy = malloc(3 * sizeof(**xy) * N)) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate %zu bytes"), sizeof(**xy) * N);
        free(*uv), *uv = NULL;
        free(*a), *a = NULL;
        return -(errno = ENOMEM);
    }

    TRACE(INFO, _b("reduced mesh size: %d (of %d); inner: %d, transparent: %d"), N, n, m_inner, m_transparent);

    return N;
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

static const char * const __group_id[4] = {
    "Right",
    "Left",
    "Front",
    "Rear",
};

/* ...mesh loading from Wavefront OBJ file */
mesh_data_t * mesh_create(const char *fname, __vec4 rect)
{
    mesh_data_t    *m;
    wf_obj_data_t  *obj;
    __vec3         *v;
    int             vnum, vtnum, n;
    int             i, j;

    /* ...create mesh descriptor */
    CHK_ERR(m = calloc(1, sizeof(*m)), (errno = ENOMEM, NULL));

    /* ...parse mesh file */
    if ((obj = obj_create(fname)) == NULL)
    {
        TRACE(ERROR, _x("failed to parse mesh model: %m"));
        goto error;
    }
    else
    {
        /* ...get raw buffers sizes */
        n = obj_raw_buffers_sizes(obj, &vnum, NULL, &vtnum);
        TRACE(INFO, _b("model parsed: vertices: %d, texture coordinates: %d, elements: %d"), vnum, vtnum, n);
    }

    /* ...create copy of vertices (3D-points) */
    if ((m->v = v = malloc((m->vnum = vnum) * sizeof(*m->v))) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate %zu bytes"), vnum * sizeof(*m->v));
        errno = ENOMEM;
        goto error_obj;
    }
    else
    {
        /* ...upload 3D-points (seems a bit odd - tbd) */
        for (j = 0; j < vnum; j++, v++)
        {
            obj_vertex_store(obj, j + 1, *v, 3);

            /* ...invert Y coordinate (hmm - now that's a bit odd) */
            //(*v)[1] = -(*v)[1];   
        }
    }

    /* ...allocate interim scratch buffer for projective transformation */
    if ((m->b = malloc(sizeof(*m->b) * vnum)) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate %zu bytes"), sizeof(*m->b) * vnum);
        errno = ENOMEM;
        goto error_v;
    }

    /* ...upload VBO indices (vertex and texture coordinates only) */
    if ((m->vbi = malloc(sizeof(*m->vbi) * n)) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate %zu bytes"), sizeof(*m->vbi) * n);
        errno = ENOMEM;
        goto error_v;
    }
    else
    {
        /* ...upload only vertex and texture coordinates (no normales) */
        obj_upload_vbi(obj, m->vbi, 3, 0, 3);
        TRACE(INFO, _b("uploaded VBI: %d elements"), n);
    }

    /* ...process individual camera meshes */
    for (i = 0; i < 4; i++)
    {
        obj_set_t      *set;
        obj_subset_t   *s;
        mesh_ibo_t     *ibo;
        int             size;

        /* ...retrieve named group corresponding to particular camera mesh */
        if ((set = obj_set_create(obj, __group_id[i])) == NULL)
        {
            TRACE(ERROR, _x("camera-%d: failed to create set '%s'"), i, __group_id[i]);
            goto error_vbi;
        }
        else if (obj_set_subsets_number(set) != 1)
        {
            TRACE(ERROR, _x("invalid number of susbsets: %d"), obj_set_subsets_number(set));
            obj_set_destroy(set);
            goto error_vbi;
        }

        /* ...maybe I could support number of subsets - like, ground floor, spherical part.. - tbd */
        size = obj_subset_ibo_size(s = obj_subset_first(set));
        
        /* ...upload IBO for a given mash part */
        if ((ibo = malloc(size * sizeof(*ibo))) == NULL)
        {
            TRACE(ERROR, _x("failed to allocate %zu bytes"), size * sizeof(*ibo));
            errno = ENOMEM;
            obj_set_destroy(set);
            goto error_vbi;
        }
        else
        {
            /* ...upload mesh IBO */
            obj_subset_ibo_upload(set, s, ibo);

            TRACE(INFO, _b("mesh-%d['%s']: IBO size: %d"), i, __group_id[i], size);

            /* ...process mesh stripping the values having zero alpha levels */
            if ((m->fnum[i] = __mesh_reduce(obj, ibo, size, m->vbi, rect, &m->uv[i], &m->a[i], &m->xy[i])) < 0)
            {
                TRACE(ERROR, _x("operation failed: %m"));
                free(ibo);
                obj_set_destroy(set);
                goto error_vbi;
            }

            /* ...save updated IBO */
            m->ibo[i] = realloc(ibo, m->fnum[i] * sizeof(*m->ibo[i]));
            
            /* ...close set object */
            obj_set_destroy(set);
        }
    }

    TRACE(INFO, _b("mesh[%p] parsed from '%s'"), m, fname);

    /* ...close object file */
    obj_destroy(obj);

    return m;

error_vbi:
    /* ...destroy buffers objects */
    for (i = 0; i < 4; i++)
    {
        (m->uv[i] ? free(m->uv[i]) : 0);
        (m->a[i] ? free(m->a[i]) : 0);
        (m->ibo[i] ? free(m->ibo[i]) : 0);
    }
    
    /* ...destroy all sets allocated thus far */
    (m->vbi ? free(m->vbi) : 0);

error_v:
    /* ...destroy vertices buffer */
    (m->b ? free(m->b) : 0);
    (m->v ? free(m->v) : 0);

error_obj:
    /* ...destroy object data */
    obj_destroy(obj);

error:
    /* ...destroy mesh data */
    free(m);
    return NULL;
}

/* ...destroy mesh object */
void mesh_destroy(mesh_data_t *m)
{
    int     i;

    /* ...release texture-/alpha-coordinates buffers */
    for (i = 0; i < 4; i++)
    {
        (m->uv[i] ? free(m->uv[i]) : 0);
        (m->a[i] ? free(m->a[i]) : 0);
        (m->ibo[i] ? free(m->ibo[i]) : 0);
    }

    /* ...release buffer objects as needed */
    (m->vbi ? free(m->vbi) : 0);
    (m->v ? free(m->v) : 0);
    (m->b ? free(m->b) : 0);

    /* ...destroy mesh descriptor */
    free(m);

    TRACE(INIT, _b("mesh[%p] destroyed"), m);
}

/*******************************************************************************
 * Set IMR engine with a mesh data
 ******************************************************************************/

/* ...fill vertex coordinates */
static inline void __vertex_set(__vec3 B, __vec3 xy)
{
    xy[0] = 1 - (1 + B[0]) / 2, xy[1] = (1 + B[1]) / 2, xy[2] = B[2];
}

#if 0
/* ...fill texture coordinates */
static inline void __texcoord_set(float *VT, float *uv, float *a)
{
    /* ...texture coordinate setting */
    uv[0] = VT[0], uv[1] = VT[1];

    /* ...alpha-plane coordinate setting (single line) */
    a[0] = VT[2], a[1] = 0;
}
#endif

/* ...convert mesh into set of UV/XY-triangles */
int mesh_translate(mesh_data_t *m, __vec2 **uv, __vec2 **a, __vec3 **xy, int *n, const __mat4x4 pvm, const __scalar scale)
{
    mesh_vbi_t     *vbi = m->vbi;
    __vec3         *V = m->v, *B = m->b;
    int             vnum = m->vnum;
    int             i, j;
    u32             t0, t1, t2;
    
    t0 = __get_time_usec();

    /* ...transform all vertices with respect to given PVM matrix (in single thread now - tbd) */
    for (j = 0; j < vnum; j++, V++, B++)
    {
        /* ...transform individual vertex (shall skip the vertices which are not used) */
        __proj3_mul(pvm, *V, *B, scale);

        if (j < 3)
        {
            /* ...dump few transformed points */
            TRACE(0, _b("%d: V=%f/%f/%f, B=%f/%f/%f"), j, (*V)[0], (*V)[1], (*V)[2], (*B)[0], (*B)[1], (*B)[2]);
        }
    }

    t1 = __get_time_usec();
    
    /* ...process individual cameras */
    for (i = 0, B -= vnum + 1; i < 4; i++)
    {
        mesh_ibo_t     *ibo = m->ibo[i];
        __vec3         *XY;
 
        /* ...save pointers to texture coordinates (sources) */
        uv[i] = m->uv[i], a[i] = m->a[i];

        /* ...save pointer to destination coordinates */
        xy[i] = XY = m->xy[i];
        
        /* ...translate all faces into set of polygons */
        for (j = 0; j < m->fnum[i]; j++, ibo++, XY += 3)
        {
            int     i0 = (*ibo)[0], i1 = (*ibo)[1], i2 = (*ibo)[2];
            int     v0 = vbi[i0][0], v1 = vbi[i1][0], v2 = vbi[i2][0];

            TRACE(0, _b("%d:%d: index = %d/%d/%d"), i, j, i0, i1, i2);

            /* ...get triangle points (in transformed destination space) */
            __vertex_set(B[v0], XY[0]);
            __vertex_set(B[v1], XY[1]);
            __vertex_set(B[v2], XY[2]);

            if (j < 4)
            {
                TRACE(0, _b("%d:%d: <%d/%d/%d>: XY[0]=%f/%f/%f, XY[1]=%f/%f/%f, XY[2]=%f/%f/%f"),
                      i, j, i0, i1, i2,
                      XY[0][0], XY[0][1], XY[0][2],
                      XY[1][0], XY[1][1], XY[1][2],
                      XY[2][0], XY[2][1], XY[2][2]);
            }
        }

        /* ...save number of triangles */
        n[i] = m->fnum[i];
    }

    t2 = __get_time_usec();

    TRACE(INFO, _b("mesh recalculated: %d/%d (%d)"), (s32)(t1 - t0), (s32)(t2 - t1), (s32)(t2 - t0));

    /* ...return total number of triangles */
    return i;
}

#if 0
/*******************************************************************************
 * Fixed-point mesh generation
 ******************************************************************************/

/* ...convert mesh into set of UV/XY-triangles */
int mesh_translate_2(mesh_data_t *m, u16 **uv, u16 **a, s16 **xy, int *n, const __mat4x4 pvm, const __scalar scale)
{
    mesh_vbi_t     *vbi = m->vbi;
    __vec3         *V = m->v, *B = m->b;
    int             vnum = m->vnum;
    int             i, j;
    u32             t0, t1, t2;
    
    t0 = __get_time_usec();

    /* ...transform all vertices with respect to given PVM matrix (in single thread now - tbd) */
    for (j = 0; j < vnum; j++, V++, B++)
    {
        /* ...transform individual vertex (shall skip the vertices which are not used) */
        __proj3_mul(pvm, *V, *B, scale);
    }

    t1 = __get_time_usec();
    
    /* ...process individual cameras */
    for (i = 0, B -= vnum + 1; i < 4; i++)
    {
        mesh_ibo_t     *ibo = m->ibo[i];
        s16            *XY;
        u16            *uv = m->
        /* ...save pointers to texture coordinates (sources) */
        uv[i] = m->uv[i], a[i] = m->a[i];

        /* ...save pointer to destination coordinates */
        xy[i] = XY = m->xy[i];
        
        /* ...translate all faces into set of polygons */
        for (j = 0; j < m->fnum[i]; j++, ibo++, XY += 3)
        {
            int     i0 = (*ibo)[0], i1 = (*ibo)[1], i2 = (*ibo)[2];
            int     v0 = vbi[i0][0], v1 = vbi[i1][0], v2 = vbi[i2][0];

            TRACE(0, _b("%d:%d: index = %d/%d/%d"), i, j, i0, i1, i2);

            /* ...get triangle points (in transformed destination space) */
            __vertex_set(B[v0], XY[0]);
            __vertex_set(B[v1], XY[1]);
            __vertex_set(B[v2], XY[2]);

            if (j < 4)
            {
                TRACE(0, _b("%d:%d: <%d/%d/%d>: XY[0]=%f/%f/%f, XY[1]=%f/%f/%f, XY[2]=%f/%f/%f"),
                      i, j, i0, i1, i2,
                      XY[0][0], XY[0][1], XY[0][2],
                      XY[1][0], XY[1][1], XY[1][2],
                      XY[2][0], XY[2][1], XY[2][2]);
            }
        }

        /* ...save number of triangles */
        n[i] = m->fnum[i];
    }

    t2 = __get_time_usec();

    TRACE(INFO, _b("mesh recalculated: %d/%d (%d)"), (s32)(t1 - t0), (s32)(t2 - t1), (s32)(t2 - t0));

    /* ...return total number of triangles */
    return i;
}
#endif
