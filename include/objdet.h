/*******************************************************************************
 * objdet.h
 *
 * Object detection library public interface
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

#ifndef __OBJDET_H
#define __OBJDET_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

/* ...detection results structures */
#include "road_scene.h"

/* ...vehicle state information */
#include "vehicle-info.h"

/* ...drawing */
#include <cairo/cairo.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...opaque handle to the objects detection library */
typedef struct objdet   objdet_t;

/* ...callback data for object detection library */
typedef struct objdet_callback
{
    /* ...callback function indicating data readiness */
    void          (*ready)(void *cdata, void *cookie, road_scene_t *scene);

    /* ...callback function indicating the buffer is no longer used by detector */
    void          (*release)(void *cdata, void *cookie);

    /* ...error notification callback */
    void          (*error)(void *cdata, int error);

}   objdet_callback_t;

/*******************************************************************************
 * Detector configuration structure
 ******************************************************************************/

typedef struct objdet_config
{
#define SIGN_DETECT_SPEED_LIMIT     0x01
#define SIGN_DETECT_STOP            0x02
#define SIGN_DETECT_YIELD           0x04

    /* ...detected signs bitmask, see SIGN_DETECT_* macros */
    u32             signs_to_detect;

    /* ...minimal speed (km/h) at which lane-departure warning activates */
    int             lane_detection_threshold;

    /* ...forward collision danger threashold (sec) */
    float           critical_time_to_collision;

    /* ...face detection parameters */
    float focal_length, max_face_width_ratio;

}   objdet_config_t;

/*******************************************************************************
 * Public API
 ******************************************************************************/

/* ...initialize object-detection library for given camera dimensions */
extern objdet_t * objdet_engine_init(objdet_callback_t *cb, void *cdata, int width, int height, int bytesPerPixel, int window_width, int window_height, objdet_config_t *cfg);

/* ...destroy object-detection data */
extern void objdet_engine_close(objdet_t *handle);

/* ...input buffer submission */
extern int objdet_engine_push_buffer(objdet_t *handle, void *buffer, u8 *y, vehicle_info_t *info, road_scene_t *scene, EGLImageKHR image, int format);

/* ...draw the LDW result */
extern void objdet_engine_draw(objdet_t *objdet, road_scene_t *scene, cairo_t* cr);

#endif  /* __OBJDET_H */
