/*******************************************************************************
 * utest-main.c
 *
 * Surround-view unit-test main function
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

#define MODULE_TAG                      MAIN

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-app.h"
#include <getopt.h>
#include <linux/videodev2.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 0);

/*******************************************************************************
 * Global variables definitions
 ******************************************************************************/

/* ...output device for main */
int     __output_main = 0;

/* ...log level (looks ugly) */
int     LOG_LEVEL = 1;

/* ...V4L2 device name */
char   *imr_dev_name[] = {
    "/dev/video8",
    "/dev/video9",
    "/dev/video10",
    "/dev/video11",
    "/dev/video8",
    "/dev/video9",
    "/dev/video10",
    "/dev/video11",
};

/* ...default joystick device name  */
char   *joystick_dev_name = "/dev/input/js0";


/*******************************************************************************
 * Live capturing from VIN cameras
 ******************************************************************************/

/* ...default V4L2 device names */
char * vin_dev_name[8] = {
    "/dev/video0",
    "/dev/video1",
    "/dev/video2",
    "/dev/video3",
    "/dev/video4",
    "/dev/video5",
    "/dev/video6",
    "/dev/video7",
};


/* ...meshes definitions */
char   *__mesh_file_name = "mesh.obj";

/* ...input (VIN) format */
u32     __vin_format = V4L2_PIX_FMT_UYVY;
int     __vin_width = 1280, __vin_height = 800;
int     __vin_buffers_num = 6;

/* ...VSP dimensions */
int     __vsp_width = 1920, __vsp_height = 1080;

/* ...car buffer dimensions */
int     __car_width = 1920, __car_height = 1080;

/* ...car shadow region */
__vec4  __shadow_rect = { __MATH_FLOAT(-0.5), __MATH_FLOAT(-0.2), __MATH_FLOAT(0.5), __MATH_FLOAT(0.2) };

/* ...sphere gain factor */
__scalar    __sphere_gain = 0.8;

/* ...background color */
u32     __bg_color = 0/* 0xFF026FA5 */;

/* ...default car orientation */
__vec3  __default_view = { __MATH_FLOAT(0), __MATH_FLOAT(0), __MATH_FLOAT(1.0) };

/* ...number of steps for model positions */
int     __steps[3] = { 8, 32, 8 };

/* ...model file prefix */
char   *__model = "./data/model";


/*******************************************************************************
 * Parameters parsing
 ******************************************************************************/

/* ...parse VIN device names */
static inline int parse_vin_devices(char *str, char **name, int n)
{
    char   *s;

    for (s = strtok(str, ","); n > 0 && s; n--, s = strtok(NULL, ","))
    {
        /* ...just copy a pointer (string is persistent) */
        *name++ = s;
    }

    /* ...make sure we have parsed all addresses */
    CHK_ERR(n == 0, -EINVAL);

    return 0;
}

/* ...parse camera format */
static inline u32 parse_format(char *str)
{
    if (strcasecmp(str, "uyvy") == 0)
    {
        return V4L2_PIX_FMT_UYVY;
    }
    else if (strcasecmp(str, "yuyv") == 0)
    {
        return V4L2_PIX_FMT_YUYV;
    }
    else if (strcasecmp(str, "nv16") == 0)
    {
        return V4L2_PIX_FMT_NV16;
    }
    else if (strcasecmp(str, "nv12") == 0)
    {
        return V4L2_PIX_FMT_NV12;
    }
    else
    {
        return 0;
    }
}

/* ...parse steps number */
static inline int parse_steps(char *str)
{
    CHK_ERR(sscanf(str, "%u:%u:%u", &__steps[0], &__steps[1], &__steps[2]) == 3, -(errno = EINVAL));
    
    return 0;
}

/* ...parse float-point value */
static inline int parse_scalar(char *str, __MATH_FLOAT *v)
{
    char    *p;

    *v = strtof(str, &p);
    CHK_ERR(*p == '\0', -(errno = EINVAL));
    return 0;
}

/* ...parse steps number */
static inline int parse_vec(char *str, __MATH_FLOAT *v, int N)
{
    char   *t = strtok(str, ":,;");
    
    while (t && N--)
    {
        CHK_API(parse_scalar(t, v++));

        /* ...go to next token */
        t = strtok(NULL, ":,;");
    }

    /* ...make sure string is valid */
    CHK_ERR(!t && !N, -(errno = EINVAL));
    
    return 0;
}

/* ...command-line options */
static const struct option    options[] = {
    {   "debug",    required_argument,  NULL,   'd' },
    {   "vin",      required_argument,  NULL,   'v' },
    {   "cfg",      required_argument,  NULL,   'c' },
    {   "output",   required_argument,  NULL,   'o' },
    {   "aux",      required_argument,  NULL,   'a' },
    {   "js",       required_argument,  NULL,   'j' },
    {   "imr",      required_argument,  NULL,   'r' },
    {   "format",   required_argument,  NULL,   'f' },
    {   "width",    required_argument,  NULL,   'w' },
    {   "height",   required_argument,  NULL,   'h' },
    {   "Width",    required_argument,  NULL,   'W' },
    {   "Height",   required_argument,  NULL,   'H' },
    {   "cwidth",   required_argument,  NULL,   'X' },
    {   "cheight",  required_argument,  NULL,   'Y' },
    {   "buffers",  required_argument,  NULL,   'n' },
    {   "steps",    required_argument,  NULL,   's' },
    {   "model",    required_argument,  NULL,   'm' },
    {   "mesh",     required_argument,  NULL,   'M' },
    {   "shadow",   required_argument,  NULL,   'S' },
    {   "gain",     required_argument,  NULL,   'g' },
    {   "bgcolor",  required_argument,  NULL,   'b' },
    {   "view",     required_argument,  NULL,   'V' },
    {   NULL,       0,                  NULL,   0   },
};

extern int config_parse(char *fname);

/* ...option parsing */
static int parse_cmdline(int argc, char **argv)
{
    int     index = 0;
    int     opt;

    /* ...process command-line parameters */
    while ((opt = getopt_long(argc, argv, "d:v:o:j:r:f:w:h:W:H:X:Y:n:s:m:M:S:g:c:b:V:", options, &index)) >= 0)
    {
        switch (opt)
        {
        case 'd':
            /* ...debug level */
            TRACE(INIT, _b("debug level: '%s'"), optarg);
            LOG_LEVEL = atoi(optarg);
            break;

        case 'v':
            /* ...VIN device names */
            TRACE(INIT, _b("VIN devices: '%s'"), optarg);
            CHK_API(parse_vin_devices(optarg, vin_dev_name, 4));
            break;

        case 'o':
            /* ...set global display output for a main window (surround-view scene) */
            __output_main = atoi(optarg);
            TRACE(INIT, _b("output for main window: %d"), __output_main);
            break;
            
        case 'j':
            /* ...set default joystick device name */
            TRACE(INIT, _b("joystick device: '%s'"), optarg);
            joystick_dev_name = optarg;
            break;

        case 'r':
            /* ...set default IMR device name */
            TRACE(INIT, _b("IMR device: '%s'"), optarg);
            CHK_API(parse_vin_devices(optarg, imr_dev_name, 8));
            break;

        case 'f':
            /* ...parse camera format */
            TRACE(INIT, _b("Format: '%s'"), optarg);
            CHK_ERR(__vin_format = parse_format(optarg), -(errno = EINVAL));
            break;

        case 'w':
            /* ...parse resolution */
            TRACE(INIT, _b("Width: '%s'"), optarg);
            CHK_ERR((u32)(__vin_width = atoi(optarg)) < 4096, -(errno = EINVAL));
            break;

        case 'h':
            /* ...parse resolution */
            TRACE(INIT, _b("Height: '%s'"), optarg);
            CHK_ERR((u32)(__vin_height = atoi(optarg)) < 4096, -(errno = EINVAL));
            break;

        case 'W':
            /* ...parse resolution */
            TRACE(INIT, _b("VSP width: '%s'"), optarg);
            CHK_ERR((u32)(__vsp_width = atoi(optarg)) < 4096, -(errno = EINVAL));
            break;

        case 'H':
            /* ...parse resolution */
            TRACE(INIT, _b("VSP height: '%s'"), optarg);
            CHK_ERR((u32)(__vsp_height = atoi(optarg)) < 4096, -(errno = EINVAL));
            break;

        case 'X':
            /* ...parse car buffer width */
            TRACE(INIT, _b("car buffer width: '%s'"), optarg);
            CHK_ERR((u32)(__car_width = atoi(optarg)) < 4096, -(errno = EINVAL));
            break;

        case 'Y':
            /* ...parse car buffer width */
            TRACE(INIT, _b("car buffer height: '%s'"), optarg);
            CHK_ERR((u32)(__car_height = atoi(optarg)) < 4096, -(errno = EINVAL));
            break;

        case 'n':
            /* ...parse number of buffers for VIN */
            TRACE(INIT, _b("Number of buffers: '%s'"), optarg);
            CHK_ERR((u32)(__vin_buffers_num = atoi(optarg)) < 64, -(errno = EINVAL));
            break;

        case 's':
            /* ...parse rotation axis steps */
            TRACE(INIT, _b("rotation steps: '%s'"), optarg);
            CHK_API(parse_steps(optarg));
            break;

        case 'm':
            /* ...set model image file prefix */
            __model = optarg;
            TRACE(INIT, _b("model file prefix: '%s'"), __model);
            break;

        case 'M':
            /* ...mesh model name */
            __mesh_file_name = optarg;
            TRACE(INIT, _b("mesh file: '%s'"), __mesh_file_name);
            break;

        case 'S':
            /* ...car shadow rectangle */
            TRACE(INIT, _b("shadow region: '%s'"), optarg);
            CHK_API(parse_vec(optarg, __shadow_rect, 4));
            break;

        case 'g':
            /* ...gain for sphere */
            TRACE(INIT, _b("gain factor: '%s'"), optarg);
            CHK_API(parse_scalar(optarg, &__sphere_gain));
            break;

        case 'b':
            /* ...background color */
            __bg_color = strtoul(optarg, NULL, 0);
            TRACE(INIT, _b("background color: 0x%X"), __bg_color);
            break;

        case 'V':
            /* ...default car orientation (CCW) */
            TRACE(INIT, _b("default view: '%s'"), optarg);
            CHK_API(parse_vec(optarg, __default_view, 3));
            break;
        case 'c':
            /* ...parse configuration file */
            TRACE(INIT, _b("configuration file: '%s'"), optarg);
            CHK_API(config_parse(optarg));
            break;
        default:
            return -EINVAL;
        }
    }

    return 0;
}

/*******************************************************************************
 * Entry point
 ******************************************************************************/

int main(int argc, char **argv)
{
    display_data_t  *display;
    app_data_t      *app;

    /* ...initialize tracer facility */
    TRACE_INIT("Smart-camera demo");

    /* ...initialize GStreamer */
    gst_init(&argc, &argv);

    /* ...parse application specific parameters */
    CHK_API(parse_cmdline(argc, argv));

    /* ...initialize display subsystem */
    CHK_ERR(display = display_create(), -errno);

    /* ...initialize unit-test application */
    CHK_ERR(app = app_init(display), -errno);

    /* ...execute mainloop thread */
    app_thread(app);

    TRACE(INIT, _b("application terminated"));
    
    return 0;
}

