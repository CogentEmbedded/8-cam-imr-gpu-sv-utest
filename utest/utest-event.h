/*******************************************************************************
 * utest-event.h
 *
 * ADAS unit test. Input events support
 *
 * Copyright (c) 2014-2015 Cogent Embedded Inc. ALL RIGHTS RESERVED.
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

#ifndef __UTEST_EVENT_H
#define __UTEST_EVENT_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <linux/input.h>
#include <linux/joystick.h>
#include <spnav.h>

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...generic event type */
typedef enum widget_event_type
{
    WIDGET_EVENT_NONE,
    WIDGET_EVENT_KEY,
    WIDGET_EVENT_MOUSE,
    WIDGET_EVENT_TOUCH,
    WIDGET_EVENT_JOYSTICK,
    WIDGET_EVENT_FD,
    WIDGET_EVENT_SPNAV,
    WIDGET_EVENT_NUMBER,

}   widget_event_type_t;

/* ...macro for event type construction */
#define __WIDGET_EVENT_TYPE(type, subtype)  ((type) | ((subtype) << 8))
#define WIDGET_EVENT_TYPE(type)             ((type) & 0xFF)
#define WIDGET_EVENT_SUBTYPE(type)          ((type) >> 8)

/* ...keyboard event types */
#define __WIDGET_EVENT_KEY(subtype)     __WIDGET_EVENT_TYPE(WIDGET_EVENT_KEY, subtype)
#define WIDGET_EVENT_KEY_ENTER          __WIDGET_EVENT_KEY(0)
#define WIDGET_EVENT_KEY_PRESS          __WIDGET_EVENT_KEY(1)
#define WIDGET_EVENT_KEY_MODS           __WIDGET_EVENT_KEY(2)
#define WIDGET_EVENT_KEY_LEAVE          __WIDGET_EVENT_KEY(3)

/* ...keyboard event descriptor */
typedef struct widget_key_event
{
    /* ...generic event type */
    u32             type;

    /* ...subtype-specific structures */
    union {
        /* ...key pressing */
        struct {
            u32         code;
            u32         state;
        };

        /* ...modifiers state change */
        struct {
            u32         mods_on;
            u32         mods_off;
            u32         mods_locked;
        };
    };

}   widget_key_event_t;

/*******************************************************************************
 * Touchscreen events
 ******************************************************************************/

/* ...event subtypes */
#define __WIDGET_EVENT_TOUCH(subtype)   __WIDGET_EVENT_TYPE(WIDGET_EVENT_TOUCH, subtype)
#define WIDGET_EVENT_TOUCH_ENTER        __WIDGET_EVENT_TOUCH(0)
#define WIDGET_EVENT_TOUCH_DOWN         __WIDGET_EVENT_TOUCH(1)
#define WIDGET_EVENT_TOUCH_MOVE         __WIDGET_EVENT_TOUCH(2)
#define WIDGET_EVENT_TOUCH_UP           __WIDGET_EVENT_TOUCH(3)
#define WIDGET_EVENT_TOUCH_LEAVE        __WIDGET_EVENT_TOUCH(4)

/* ...event handle definition */
typedef struct widget_touch_event
{
    /* ...generic event type */
    u32             type;

    /* ...location of the point */
    int             x, y;

    /* ...identifier of the touch event */
    int             id;

}   widget_touch_event_t;

/*******************************************************************************
 * Mouse events
 ******************************************************************************/

/* ...event subtypes */
#define __WIDGET_EVENT_MOUSE(subtype)   __WIDGET_EVENT_TYPE(WIDGET_EVENT_MOUSE, subtype)
#define WIDGET_EVENT_MOUSE_ENTER        __WIDGET_EVENT_MOUSE(0)
#define WIDGET_EVENT_MOUSE_MOVE         __WIDGET_EVENT_MOUSE(1)
#define WIDGET_EVENT_MOUSE_BUTTON       __WIDGET_EVENT_MOUSE(2)
#define WIDGET_EVENT_MOUSE_AXIS         __WIDGET_EVENT_MOUSE(3)
#define WIDGET_EVENT_MOUSE_LEAVE        __WIDGET_EVENT_MOUSE(4)

/* ...mouse input event */
typedef struct widget_mouse_event
{
    /* ...generic event type */
    u32             type;

    /* ...subtype-specific events */
    union {
        /* ...point location / button status */
        struct {
            int         x;
            int         y;
            union {
                struct {
                    u32         button;
                    u32         state;
                };
                struct {
                    u32         axis;
                    int         value;
                };
            };        
        };
    };

}   widget_mouse_event_t;

/* ...joystic input event */
typedef struct widget_joystick_event
{
    /* ...generic event type */
    u32                 type;

    /* ...joystick event structure pointer */
    struct js_event    *e;
    
}   widget_joystick_event_t;

/* ...generic file-descriptor event */
typedef struct widget_fd_event
{
    /* ...generic event type */
    u32             type;

    /* ...file-descriptor */
    int             fd;

    /* ...private data associated with a descriptor */
    void           *priv;

}   widget_fd_event_t;

/* ...SpaceNav 3D-joystick event */
typedef struct widget_spnav_event
{
    /* ...generic event type */
    u32             type;

    /* ...spacenav event pointer */
    spnav_event    *e;

}   widget_spnav_event_t;

/* ...widget event definition */
typedef union widget_event
{
    /* ...generic event type */
    u32                         type;

    /* ...keyboard event */
    widget_key_event_t          key;

    /* ...mouse event */
    widget_mouse_event_t        mouse;

    /* ...touchscreen event */
    widget_touch_event_t        touch;

    /* ...joystick event */
    widget_joystick_event_t     js;

    /* ...generic unix file-descriptor event */
    widget_fd_event_t           fd;

    /* ...3d-joystick event */
    widget_spnav_event_t        spnav;

}   widget_event_t;

#endif  /* __UTEST_EVENT_H */
