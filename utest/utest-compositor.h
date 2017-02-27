/*******************************************************************************
 * utest-compositor.h
 *
 * VSP compositor for IMR-based surround-view application
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

#ifndef __UTEST_COMPOSITOR_H
#define __UTEST_COMPOSITOR_H

/*******************************************************************************
 * Opaque declarations
 ******************************************************************************/

typedef struct vsp_compositor   vsp_compositor_t;
typedef struct vsp_mem          vsp_mem_t;
typedef struct vsp_dmabuf       vsp_dmabuf_t;

/*******************************************************************************
 * Compositor processing callback
 ******************************************************************************/

typedef void (*vsp_callback_t)(void *data, int result);

/*******************************************************************************
 * Public API
 ******************************************************************************/

/* ...contiguous memory allocation */
extern vsp_mem_t * vsp_mem_alloc(u32 size);

/* ...contiguous memory destruction */
extern void vsp_mem_free(vsp_mem_t *mem);

/* ...memory buffer accessor */
extern void * vsp_mem_ptr(vsp_mem_t *mem);

/* ...memory size */
extern u32 vsp_mem_size(vsp_mem_t *mem);

/* ...export DMA file-descriptor representing contiguous block */
extern vsp_dmabuf_t * vsp_dmabuf_export(vsp_mem_t *mem, u32 offset, u32 size);

/* ...DMA-buffer descriptor accessor */
extern int vsp_dmabuf_fd(vsp_dmabuf_t *dmabuf);

/* ...close DMA file-descriptor */
extern void vsp_dmabuf_unexport(vsp_dmabuf_t *dmabuf);

/*******************************************************************************
 * Compositor API
 ******************************************************************************/

/* ...module initialization function */
extern vsp_compositor_t * compositor_init(int w, int h, u32 ifmt, int W, int H, u32 ofmt, int cw, int ch, vsp_callback_t cb, void *cdata);

/* ...module destruction */
extern void compositor_destroy(vsp_compositor_t *vsp);

/* ...memory buffer pool allocation */
extern int vsp_allocate_buffers(int w, int h, u32 fmt, vsp_mem_t **output, int num);

/* ...export DMA file-descriptor representing contiguous block */
extern int vsp_buffer_export(vsp_mem_t *mem, int w, int h, u32 format, int *dmafd, u32 *offset, u32 *stride);

/* ...job submission */
extern int vsp_job_submit(vsp_compositor_t *vsp, vsp_mem_t **input, vsp_mem_t *output);

#endif  /* __UTEST_COMPOSITOR_H */
