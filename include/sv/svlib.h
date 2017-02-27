/*******************************************************************************
 * svlib.h
 *
 * Surround-view library interface
 *
 * Copyright (c) 2017 Cogent Embedded Inc. ALL RIGHTS RESERVED.
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

#ifndef __SVLIB_H
#define __SVLIB_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <spnav.h>

#include <sv/trace.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <cairo/cairo.h>

enum Wheels
{
    WHEELS_FRONT = 0,
    WHEELS_FL    = 0,
    WHEELS_FR    = 1,

    WHEELS_REAR  = 2,
    WHEELS_RL    = 2,
    WHEELS_RR    = 3,
    WHEELS_MAX   = 4,
};

enum Direction
{
    DIRECTION_PARK    = 0,
    DIRECTION_NEUTRAL = 1,
    DIRECTION_FORWARD = 2,
    DIRECTION_REVERSE = 3,
    DIRECTION_INVALID = 4,
};

enum Gear
{
    GEAR_INVLID     = 0,
    GEAR_FWD_FIRST  = 1,
    GEAR_FWD_MAX    = 12,
    GEAR_NEUTRAL    = 13,
    GEAR_REVERSE    = 14,
    GEAR_PARK       = 15,
};

enum
{
    DOORS_LF    = 0,
    DOORS_RF    = 1,
    DOORS_LR    = 2,
    DOORS_RR    = 3,
    DOORS_TRUNK = 4,
    DOORS_TOTAL = 5,
};

typedef struct
{
    /* ...current speed (km/h) */
    float           speed;

    /* ...engine RPM */
    float           rpm;

    /* ...accelerator position (percents) */
    float           accelerator;

    /* ...steering wheel angle (degrees) */
    float           steering_angle;

    /* ...steering wheel rotation speed (degrees / sec) */
    float           steering_rotation;

    /* ...wheel arc heights (mm) */
    int             wheel_arc[4];

    /* ...brake status */
    int             brake_switch;

    /* ...brake pressure */
    float           brake_pressure;

    /* ...current gear */
    int             gear;

    /* ...direction switch indicator */
    int             direction_switch;

    /* ...wheel pulses counters */
    int             wheel_pulse_count[4];

    /* ...doors opened/closed flags */
    char            doors_state[16];
} VehicleState;


#define SV_NUM_CAMERAS 4
	
/*******************************************************************************
 * Types declarations
 ******************************************************************************/

/* ...opaque type declaration */
typedef struct sview sview_t;

/* ...single laser-data point */
typedef struct point3D
{
    float           position[3];

}   point3D;

typedef struct point3DElement
{
    float           position[3];
    float           normal[3];
    unsigned char   color[4];
    float           texCoord[2];
    float           length;

}   point3DElement;

/* ...set of points */
typedef struct PointCloud
{
    /* ...current timestamp of the cloud */
    uint64_t                 timestamp;

    /* ...number of points available */
    uint64_t                 num_points;

    /* ...set of 3D-points constituting a cloud */
    point3DElement     *points;

    /* ...vertex buffer object */
    GLuint              vbo;

    /* ...vertex buffer indices for triangles drawing */
    GLuint              ibo;

    /* ...triangles indices set */
    uint16_t                *indices;

}   PointCloud;

#define SVIEW_ADJUST_METHOD_PATTERN 0
#define SVIEW_ADJUST_METHOD_CIRCLES 1

/* ...application configuration data */
	typedef struct sview_cfg
{
    int     width;
    int     height;

    /* ... pattern parametrs */
    float   pattern_radius;
    int     pattern_num_circles;

    /* ... calibrator parametrs */
    int     calib_board_w;
    int     calib_board_h;
    int     calib_grab_interval;
    int     calib_boards_required;
    float   calib_cell_w;
    float   calib_cell_h;

    /* ... start point of view */
    int     start_view;

    int     pixformat;

    char*   cam_names[SV_NUM_CAMERAS];

    const char*   config_path;

    int     adjust_method;

    int     non_fisheye_camera;
    int     saveFrames;

    /* ... camera file descriptor */
    int     vfd[SV_NUM_CAMERAS];

    int     view_type;
    int     cam_width;
    int     cam_height;

    /** calibration parameters and input/output files & directories */
    char *intrinsic_frames_mask[SV_NUM_CAMERAS];
    char *extrinsic_frames_mask;

    char *intrinsic_output_directory;
    char *extrinsic_output_directory;

} sview_cfg_t;

/*******************************************************************************
 * Touch interface
 ******************************************************************************/

enum
{
    TOUCH_DOWN,
    TOUCH_MOVE,
    TOUCH_UP,
    MOUSE_BUTTON_STATE_PRESSED,
    MOUSE_BUTTON_STATE_RELEASED,
    MOUSE_MOVE,
    KEYBOARD_KEY_STATE_RELEASED,
    KEYBOARD_KEY_STATE_PRESSED,
};

/*******************************************************************************
 * Module API
 ******************************************************************************/

/* ...engine initialization */
extern sview_t * sview_engine_init(sview_cfg_t *cfg, int w, int h);

/* ...single processing step */
	extern void sview_engine_process(sview_t *sv,
					 GLuint *texes,
					 const uint8_t **planes,
					 VehicleState *vehicle_info);

/* ...retrieve current projection matrix */
extern void sview_engine_get_pvm(sview_t *sv, GLfloat *pvm);

/* ...engine destruction */
extern void sview_engine_destroy(sview_t *sv);

/* ...reinitialize bv */
sview_t * sview_bv_reinit(sview_t *sv, sview_cfg_t *cfg, int w, int h);

/* ...input event processing */
extern void sview_engine_spnav_event(sview_t *sv, spnav_event *event);
extern void sview_engine_mouse_motion(sview_t* svdata, int sx, int sy);
extern void sview_engine_mouse_button(sview_t* svdata, int button, int state);
extern void sview_engine_mouse_wheel(sview_t* svdata, int axis, int value);
extern void sview_engine_touch(sview_t* svdata, int type, int id, int x, int y);
extern void sview_engine_keyboard_key(sview_t* svdata, int key, int state);

	
#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif  /* __SVLIB_H */

