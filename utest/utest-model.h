/*******************************************************************************
 * utest-model.h
 *
 * IMR unit test application - 3D car model
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

#ifndef __UTEST_MODEL_H
#define __UTEST_MODEL_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-math.h"

/*******************************************************************************
 * Opaque declarations
 ******************************************************************************/

typedef struct wf_obj_data  wf_obj_data_t;
typedef struct wf_vertex    wf_vertex_t;
typedef struct wf_normale   wf_normale_t;
typedef struct wf_texcoord  wf_texcoord_t;
typedef struct obj_set      obj_set_t;
typedef struct obj_subset   obj_subset_t;
typedef struct obj_group    obj_group_t;
typedef struct obj_texture  obj_texture_t;

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...scalar value */
typedef float obj_scalar_t;

/* ...color code */
typedef float obj_color_t[3];

/* ...texture map data */
typedef struct obj_map
{
    /* ...buffer data (RGBA format) */
    void               *data;

    /* ...buffer dimensions */
    int                 width, height;
    
    /* ...buffer size */
    int                 size;

}   obj_map_t;
    
/* ...material descriptor */
typedef struct obj_material
{   
    /* ...material color and illumination specification */
    obj_color_t         Ka, Kd, Ks, Ke, Tf;
    obj_scalar_t        Ns, Nd, Ni, Tr, d;
    int                 illum;
    int                 sharpness;

    /* ...texture map indices (tbd - looks a bit odd) */
    int                 map_Ka, map_Kd;

}   obj_material_t;

/* ...VNT vertex descriptor */
typedef struct obj_vertex
{
    /* ...vertex coordinates */
    float       v[3];

    /* ...vertex normale coordinates */
    float       vn[3];

    /* ...texture coordinates */
    float       vt[2];

}   obj_vertex_t;

/* ...(face)element descriptor */
typedef struct obj_element
{
    /* ...indices of the vertices comprising a (face) element */
    int         index[3];

}   obj_element_t;

/*******************************************************************************
 * API
 ******************************************************************************/

/* ...create model from Wavefront OBJ file */
extern wf_obj_data_t * obj_create(const char *fname);

/* ...model destruction */
extern void obj_destroy(wf_obj_data_t *obj);

/*******************************************************************************
 * Groups/metrials enumeration
 ******************************************************************************/

/* ...get total number of materials */
extern int obj_materials_num(wf_obj_data_t *obj);

/* ...get material descriptor */
extern obj_material_t * obj_material(wf_obj_data_t *obj, int i);

/* ...get total number of groups */
extern int obj_groups_num(wf_obj_data_t *obj);

/* ...get group descriptor */
extern const char * obj_group(wf_obj_data_t *obj, int i);

/* ...get number of textures in a model */
extern int obj_textures_num(wf_obj_data_t *obj);

/* ...get texture descriptor (for a moment it is just a name) */
extern const char * obj_texture(wf_obj_data_t *obj, int i);

/*******************************************************************************
 * Objects sets management
 ******************************************************************************/

/* ...create set of objects for a specified group index */
extern obj_set_t * obj_set_create(wf_obj_data_t *obj, const char *group);

/* ...add particular group to the set */
extern int obj_set_add(obj_set_t *set, const char *group);

/* ...remove particular group from the set */
extern int obj_set_remove(obj_set_t *set, const char *group);

/* ...destroy set data */
extern void obj_set_destroy(obj_set_t *set);

/* ...subsets enumeration functions */
extern int obj_set_subsets_number(obj_set_t *set);
extern obj_subset_t * obj_subset_first(obj_set_t *set);
extern obj_subset_t * obj_subset_next(obj_set_t *set, obj_subset_t *s);
extern obj_material_t * obj_subset_material(obj_set_t *set, obj_subset_t *s);
extern void obj_subset_set_priv(obj_subset_t *s, void *priv);
extern void * obj_subset_get_priv(obj_subset_t *s);
extern int obj_subset_ibo_size(obj_subset_t *s);

/* ...upload IBO for a given subset */
extern int obj_subset_ibo_upload(obj_set_t *set, obj_subset_t *s, void *buffer);

/* ...get number of distinct groups in the set */
extern int obj_set_groups_number(obj_set_t *set);
extern obj_group_t * obj_group_first(obj_set_t *set);
extern obj_group_t * obj_group_next(obj_set_t *set, obj_group_t *g);
extern const char * obj_group_name(obj_set_t *set, obj_group_t *g);

/* ...textures enumeration / traverse functions */
extern int obj_set_textures_number(obj_set_t *set);
extern obj_texture_t * obj_texture_first(obj_set_t *set);
extern obj_texture_t * obj_texture_next(obj_set_t *set, obj_texture_t *t);
extern void * obj_texture_map(obj_set_t *set, obj_texture_t *t, int *w, int *h);
extern void obj_texture_unmap(obj_set_t *set, obj_texture_t *t);
extern int obj_texture_idx(obj_set_t *set, obj_texture_t *t);

/*******************************************************************************
 * VBO/IBO uploading
 ******************************************************************************/

/* ...get total number of elements for a given material */
extern int obj_ibo_size(wf_obj_data_t *obj, int i);

/* ...upload IBO data for selected material - tbd - group-filtering */
extern int obj_upload_ibo(wf_obj_data_t *obj, int i, void *buffer);

/* ...get total number of VNT-elements in the model */
extern int obj_vbo_size(wf_obj_data_t *obj);

/* ...upload VBO data */
extern int obj_upload_vbo(wf_obj_data_t *obj, void *buffer, int k0, int k1, int k2);

/* ...upload VBO indices  */
extern int obj_upload_vbi(wf_obj_data_t *obj, void *buffer, int k0, int k1, int k2);

/*******************************************************************************
 * Individual buffers accessors
 ******************************************************************************/

/* ...get raw buffers sizes */
extern int obj_raw_buffers_sizes(wf_obj_data_t *obj, int *vnum, int *vnnum, int *vtnum);

/* ...data conversion */
extern void obj_vertex_store(wf_obj_data_t *obj, int j, __MATH_FLOAT *a, int k);
extern void obj_normale_store(wf_obj_data_t *obj, int j, __MATH_FLOAT *a, int k);
extern void obj_texcoord_store(wf_obj_data_t *obj, int j, __MATH_FLOAT *a, int k);

/*******************************************************************************
 * Miscellaneous API
 ******************************************************************************/

/* ...object dimensions querying */
extern void obj_dimensions(wf_obj_data_t *obj, const float **min, const float **max);

/* ...add per-group dimensions; also rotation center/directions and stuff - tbd */

#endif  /* __UTEST_MODEL_H */
