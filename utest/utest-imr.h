/*******************************************************************************
 * utest-imr.h
 *
 * Distortion correction using V4L2 IMR module
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

#ifndef __UTEST_IMR_H
#define __UTEST_IMR_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-common.h"
#include "utest-camera.h"

/*******************************************************************************
 * Opaque handles
 ******************************************************************************/

/* ...handle data */
typedef struct imr_data     imr_data_t;

/* ...opaque data */
typedef struct imr_cfg      imr_cfg_t;

/*******************************************************************************
 * IMR output buffer data
 ******************************************************************************/

typedef struct imr_buffer
{
    /* ...data pointer */
    void               *data;
    
    /* ...associated GStreamer input/output buffers */
    GstBuffer          *input, *output;

}   imr_buffer_t;

/*******************************************************************************
 * Custom buffer metadata
 ******************************************************************************/

/* ...metadata structure */
typedef struct imr_meta
{
    GstMeta             meta;

    /* ...buffer dimensions */
    int                 width, height;

    /* ...buffer format */
    int                 format;

    /* ...user-specific private data */
    void               *priv, *priv2;

    /* ...IMR buffer pool reference */
    imr_buffer_t       *buf;

    /* ...buffer identifier (device-id) */
    int                 id;

    /* ...buffer index in a pool */
    int                 index;

    /* ...job sequence id */
    u32                 sequence;

}   imr_meta_t;

/* ...metadata API type accessor */
extern GType imr_meta_api_get_type(void);
#define IMR_META_API_TYPE               (imr_meta_api_get_type())

/* ...metadata information handle accessor */
extern const GstMetaInfo *imr_meta_get_info(void);
#define IMR_META_INFO                   (imr_meta_get_info())

/* ...get access to the buffer metadata */
#define gst_buffer_get_imr_meta(b)      \
    ((imr_meta_t *)gst_buffer_get_meta((b), IMR_META_API_TYPE))

/* ...attach metadata to the buffer */
#define gst_buffer_add_imr_meta(b)    \
    ((imr_meta_t *)gst_buffer_add_meta((b), IMR_META_INFO, NULL))

/*******************************************************************************
 * Public module API
 ******************************************************************************/

/* ...IMR engine initialization */
extern imr_data_t * imr_init(char **devname, int num, camera_callback_t *cb, void *cdata);

/* ...IMR device configuration */
extern int imr_setup(imr_data_t *imr, int i, int w, int h, int W, int H, int ifmt, int ofmt, int size);

/* ...start IMR operation */
extern int imr_start(imr_data_t *imr);

/* ...resume/suspend streaming */
extern int imr_enable(imr_data_t *imr, int enable);

/* ...initialize IMR engine runtime */
extern int imr_engine_setup(imr_data_t *imr, int i, float *uv, float *xy, int n);

/* ...buffer submission */
extern int imr_engine_push_buffer(imr_data_t *imr, int i, GstBuffer *buffer);

/* ...module termination */
extern void imr_engine_close(imr_data_t *imr);

/* ...average buffer-processing time */
extern u32 imr_engine_avg_time(imr_data_t *imr, int i);

/* ...create mesh configuration */
extern imr_cfg_t * imr_cfg_create(imr_data_t *imr, int i, float *uv, float *xy, int n);

/* ...create rectangular mesh with automatically generated destination coordinates */
extern imr_cfg_t * imr_cfg_mesh_src(imr_data_t *imr, int i, float *uv, int rows, int columns, float x0, float y0, float dx, float dy);

/* ...destroy mesh configuration structure */
extern void imr_cfg_destroy(imr_cfg_t *cfg);

/* ...set mesh confguration */
extern int imr_cfg_apply(imr_data_t *imr, int i, imr_cfg_t *cfg);

#endif  /* __UTEST_IMR_H */
