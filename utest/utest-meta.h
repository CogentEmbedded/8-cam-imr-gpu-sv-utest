/*******************************************************************************
 * utest-meta.h
 *
 * Custom buffer metadata definition
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

#ifndef __UTEST_META_H
#define __UTEST_META_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

/* ...vehicle status information */
#include "vehicle-info.h"

/* ...road-scene info */
#include "road_scene.h"

/*******************************************************************************
 * Custom output buffer metadata
 ******************************************************************************/

/* ...metadata structure */
typedef struct objdet_meta
{
    /* ...common metadata object */
    GstMeta             meta;

    /* ...saved vehicle information */
    vehicle_info_t      info;

    /* ...road-scene description */
    road_scene_t        scene;

}   objdet_meta_t;

/* ...metadata API type accessor */
extern GType objdet_meta_api_get_type(void);
#define OBJDET_META_API_TYPE            (objdet_meta_api_get_type())

/* ...metadata information handle accessor */
extern const GstMetaInfo * objdet_meta_get_info(void);
#define OBJDET_META_INFO                (objdet_meta_get_info())

/* ...get access to the buffer metadata */
#define gst_buffer_get_objdet_meta(b)   \
    ((objdet_meta_t *)gst_buffer_get_meta((b), OBJDET_META_API_TYPE))

/* ...attach metadata to the buffer */
#define gst_buffer_add_objdet_meta(b)   \
    ((objdet_meta_t *)gst_buffer_add_meta((b), OBJDET_META_INFO, NULL))

#endif  /* __UTEST_META_H */
