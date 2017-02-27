/*******************************************************************************
 * utest-common.h
 *
 * ADAS unit-test common definitions
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

#ifndef __UTEST_COMMON_H
#define __UTEST_COMMON_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sched.h>
#include <gst/gst.h>
#include <gst/video/video-info.h>
#include <linux/videodev2.h>

/*******************************************************************************
 * Primitive typedefs
 ******************************************************************************/

typedef uint8_t         u8;
typedef uint16_t        u16;
typedef uint32_t        u32;
typedef uint64_t        u64;

typedef int8_t          s8;
typedef int16_t         s16;
typedef int32_t         s32;
typedef int64_t         s64;

/*******************************************************************************
 * Global constants definitions
 ******************************************************************************/

/* ...total number of cameras */
#define CAMERAS_NUMBER          4

/*******************************************************************************
 * Forward types declarations
 ******************************************************************************/

/* ...forward declaration */
typedef struct fd_source        fd_source_t;
typedef struct timer_source     timer_source_t;

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...double-linked list item */
typedef struct track_list
{
    /* ...double-linked list pointers */
    struct track_list      *next, *prev;
    
}   track_list_t;
    
/* ...retrieve next track descriptor */
extern track_list_t    *track_next(void);

/* ...retrieve previous track descriptor */
extern track_list_t    *track_prev(void);

/* ...retrieve CPU cycles count - due to thread migration, use generic clock interface */
static inline u32 __get_cpu_cycles(void)
{
    struct timespec     ts;

    /* ...retrieve value of monotonic clock */
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* ...translate value into milliseconds (ignore wrap-around) */
    return (u32)((u64)ts.tv_sec * 1000000000ULL + ts.tv_nsec);
}

static inline u32 __get_time_usec(void)
{
    struct timespec     ts;

    /* ...retrieve value of monotonic clock */
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* ...translate value into milliseconds (ignore wrap-around) */
    return (u32)((u64)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000);
}


/*******************************************************************************
 * External functions
 ******************************************************************************/


/* ...file source operations */
extern fd_source_t * fd_source_create(const char *filename, gint prio, GSourceFunc func, gpointer user_data, GDestroyNotify notify, GMainContext *context);
extern int fd_source_get_fd(fd_source_t *fsrc);
extern void fd_source_suspend(fd_source_t *fsrc);
extern void fd_source_resume(fd_source_t *fsrc);
extern int fd_source_is_active(fd_source_t *fsrc);

/* ...timer source operations */
extern timer_source_t * timer_source_create(GSourceFunc func, gpointer user_data, GDestroyNotify notify, GMainContext *context);
extern int timer_source_get_fd(timer_source_t *tsrc);
extern void timer_source_start(timer_source_t *tsrc, u32 interval, u32 period);
extern void timer_source_stop(timer_source_t *tsrc);
extern int timer_source_is_active(timer_source_t *tsrc);

/*******************************************************************************
 * Camera support
 ******************************************************************************/

/* ...opaque declaration */
typedef struct camera_data  camera_data_t;

/* ...cameras MAC addresses */
extern u8   (*camera_mac_address)[6];

/* ...jpeg decoder device name  */
extern char  *jpu_dev_name;

/* ...joystick device name */
extern char  *joystick_dev_name;

/*******************************************************************************
 * Surround-view application API
 ******************************************************************************/

typedef struct display_data     display_data_t;
typedef struct window_data      window_data_t;
typedef struct texture_data     texture_data_t;


#define CHK_GL(expr)                                                        \
({                                                                          \
    GLuint  _err;                                                           \
    (expr);                                                                 \
    _err = glGetError();                                                    \
    (_err != GL_NO_ERROR ? TRACE(ERROR, _x("GL error: %X"), _err), 0 : 1);  \
})
    
#define __v4l2_fmt(f)     ((f) >> 0) & 0xFF, ((f) >> 8) & 0xFF, ((f) >> 16) & 0xFF, ((f) >> 24) & 0xFF

/* ...mapping between Gstreamer and V4L2 pixel-formats */
static inline int __pixfmt_v4l2_to_gst(u32 format)
{
    switch (format)
    {
    case V4L2_PIX_FMT_ARGB32:           return GST_VIDEO_FORMAT_ARGB;
    case V4L2_PIX_FMT_RGB565:           return GST_VIDEO_FORMAT_RGB16;
    case V4L2_PIX_FMT_RGB555:           return GST_VIDEO_FORMAT_RGB15;
    case V4L2_PIX_FMT_NV16:             return GST_VIDEO_FORMAT_NV16;
    case V4L2_PIX_FMT_NV12:             return GST_VIDEO_FORMAT_NV12;
    case V4L2_PIX_FMT_UYVY:             return GST_VIDEO_FORMAT_UYVY;
    case V4L2_PIX_FMT_YUYV:             return GST_VIDEO_FORMAT_YUY2;
    case V4L2_PIX_FMT_YVYU:             return GST_VIDEO_FORMAT_YVYU;
    case V4L2_PIX_FMT_GREY:             return GST_VIDEO_FORMAT_GRAY8;
    case V4L2_PIX_FMT_Y10:              return GST_VIDEO_FORMAT_GRAY16_BE;
    default:                            return -1;
    }
}


#endif  /* __UTEST_COMMON_H */
