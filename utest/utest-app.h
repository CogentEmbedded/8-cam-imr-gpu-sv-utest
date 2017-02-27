/*******************************************************************************
 * utest-app.h
 *
 * IMR unit-test application common definitions
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

#ifndef __UTEST_APP_H
#define __UTEST_APP_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-common.h"
#include "utest-display.h"
#include "utest-camera.h"
#include "utest-math.h"
//#include "imr.h"

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/

typedef struct app_data   app_data_t;

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...static view configuration */
typedef struct app_view_cfg
{
    /* ...thumbnail image */
    char       *thumb;

    /* ...scene view */
    char       *scene;

}   app_view_cfg_t;

/* ...camera intrinsic parameters */
typedef struct app_camera_cfg
{
    /* ...camera matrix */
    __mat3x3    K;

    /* ...unfisheye transformation parameters */
    __vec4      D;

}   app_camera_cfg_t;

/* ...gradient configuration */
typedef struct app_border_cfg
{
    /* ...start/stop gradient colors */
    u32         c0, c1;

    /* ...sharpness parameter */
    float       sharpness;

}   app_border_cfg_t;
    
/* ...application configuration data */
typedef struct app_cfg
{
    /* ...number of static views */
    app_view_cfg_t     *views;

    /* ...total number of static views */
    int                 views_number, views_alloc;

    /* ...smart-cameras intrinsics */
    app_camera_cfg_t    camera[4];

    /* ...carousel dimensions */
    int                 carousel_x, carousel_y;

    /* ...crousel gradient parameters */
    app_border_cfg_t    carousel_border;

    /* ...surround-view scene border gradient parameters */
    app_border_cfg_t    sv_border;

    /* ...smart-camera gradient configuration for active/inactve state */
    app_border_cfg_t    sc_active_border, sc_inactive_border;

}   app_cfg_t;

extern app_cfg_t    __app_cfg;

/*******************************************************************************
 * Configuration parsing
 ******************************************************************************/


/*******************************************************************************
 * Cameras mapping
 ******************************************************************************/

#define CAMERA_RIGHT                    0
#define CAMERA_LEFT                     1
#define CAMERA_FRONT                    2
#define CAMERA_REAR                     3

/* ...mapping of cameras into texture indices (the order if left/right/front/rear) */
static inline int camera_id(int i)
{
    return (i < 2 ? i ^ 1 : i);
}

static inline int camera_idx(int id)
{
    return (id < 2 ? id ^ 1 : id);
}

/*******************************************************************************
 * Global configuration options
 ******************************************************************************/

/* ...output devices for main / auxiliary windows */
extern int __output_main, __output_aux;

/* ...VIN device names */
extern char * vin_dev_name[];

/* ...IMR device names */
extern char * imr_dev_name[];

/* ...mesh data (tbd - move to track configuration) */
extern char * mesh_file_name[];

/*******************************************************************************
 * Public module API
 ******************************************************************************/

/* ...application data initialization */
extern app_data_t * app_init(display_data_t *display);

/* ...main application thread */
extern void * app_thread(void *arg);

#endif  /* __UTEST_APP_H */
