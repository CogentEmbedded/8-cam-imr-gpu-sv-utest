/*******************************************************************************
 * utest-imr-sv.h
 *
 * IMR-based surround view engine
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

#ifndef __UTEST_IMR_SV_H
#define __UTEST_IMR_SV_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-event.h"
#include "utest-math.h"
#include "utest-imr.h"

/*******************************************************************************
 * Types declarations
 ******************************************************************************/

/* ...opaque engine type */
typedef struct imr_sview    imr_sview_t;

/* ...engine callback */
typedef struct imr_sview_cb
{
    /* ...output buffer readiness callback */
    void    (*ready)(void *cdata, GstBuffer **buf);

    /* ...buffer release callback? */
    void    (*release)(void *cdata, GstBuffer **buf);

}   imr_sview_cb_t;

/* ...output buffer accessor */
extern GstBuffer * imr_sview_buf_output(GstBuffer **buf);

/*******************************************************************************
 * Module API
 ******************************************************************************/

/* ...input job submission */
extern int imr_sview_submit(imr_sview_t *sv, GstBuffer **buf);

/* ...event-processing function */
extern int imr_sview_input_event(imr_sview_t *sv, widget_event_t *event);

/* ...set static view */
extern int imr_sview_set_view(imr_sview_t *sv, __vec3 rot, __scalar scale, char *image);

/* ...module initialization function */
extern imr_sview_t * imr_sview_init(const imr_sview_cb_t *cb, void *cdata, int w, int h, int ifmt, int W, int H, int cw, int ch, __vec4 shadow);

#endif  /* __UTEST_IMR_SV_H */
