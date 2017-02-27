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

#ifndef __UTEST_MESH_H
#define __UTEST_MESH_H

/*******************************************************************************
 * Opaque types declarations
 ******************************************************************************/

typedef struct mesh_data    mesh_data_t;

/*******************************************************************************
 * Public module API
 ******************************************************************************/

/* ...mesh creation */
extern mesh_data_t * mesh_create(const char *fname, __vec4 r);

/* ...mesh destruction */
extern void mesh_destroy(mesh_data_t *m);

/* ...convert mesh into set of UV/XY-triangles */
extern int mesh_translate(mesh_data_t *m, __vec2 **uv, __vec2 **a, __vec3 **xy, int *n, const __mat4x4 pvm, const __scalar scale);

/* ...mesh visualization */
extern void mesh_draw(mesh_data_t *m, texture_view_t *view, const float *p, const float *vm, u32 color);

/* ...tbd - png image parsing */
extern int create_png(const char *path, int *w, int *h, int *format, void **data);

#endif  /* __UTEST_MESH_H */
