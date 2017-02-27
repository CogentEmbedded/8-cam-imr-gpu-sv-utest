/*******************************************************************************
 * imr.h
 *
 * IMR engine public interface
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

#ifndef __IMR_H
#define __IMR_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

/* ...tbd */

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...opaque handle to the objects detection library */
typedef struct imr_data     imr_data_t;

/* ...callback data for object detection library */
typedef struct imr_callback
{
    /* ...callback function indicating data readiness */
    void          (*ready)(void *cdata, void *cookie);

    /* ...callback function indicating the buffer is no longer used by detector */
    void          (*release)(void *cdata, void *cookie);

    /* ...error notification callback */
    void          (*error)(void *cdata, int error);
    
}   imr_callback_t;

/*******************************************************************************
 * Public API
 ******************************************************************************/

/* ...initialize distortion correction engine */
extern imr_data_t * imr_engine_init(imr_callback_t *cb, void *cdata, int w, int h, u32 fourcc);

/* ...destroy distortion correction engine */
extern void imr_engine_close(imr_data_t *handle);

/* ...input buffer submission */
extern int imr_engine_push_buffer(imr_data_t *handle, void *buffer, void **planes);

/* ...anything else? */
extern int imr_sview_set_view(imr_sview_t *sv, __vec3 rot, __scalar scale, const char *image);

#endif  /* __IMR_H */
