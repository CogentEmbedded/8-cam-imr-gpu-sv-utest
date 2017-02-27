/*******************************************************************************
 * utest-gui.h
 *
 * Graphical user interface
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

#ifndef __UTEST_GUI_H
#define __UTEST_GUI_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-display.h"

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...opaque handle to the carousel menu widget */
typedef struct carousel     carousel_t;

/* ...carousel menu configuration data */
typedef struct carousel_cfg
{
    /* ...width/height of the thumbnails */
    int             width, height;
    
    /* ...number of items in the carousel (single dimension only? - tbd) */
    int             size, size_y;

    /* ...number of items shown in carousel window */
    int             window_size, window_size_y;

    /* ...center of the menu in window coordinate space */
    float           x_center, y_center;

    /* ...size of individual thumbnail in window coordinate space */
    float           x_length, y_length;
    
    /* ...thumbnail image name */
    const char *  (*thumbnail)(void *cdata, int i, int j);
    
    /* ...callback to call when item is activated */
    void          (*select)(void *cdata, int i, int j);
    
}   carousel_cfg_t;

/*******************************************************************************
 * Carousel widget API
 ******************************************************************************/

/* ...module pre-initialization */
extern int carousel_shader_prebuild(void);

/* ...carousel menu initialization */
extern carousel_t * carousel_create(carousel_cfg_t *cfg, void *cdata);

/* ...carousel menu rendering */
extern void carousel_draw(carousel_t *menu);

/* ...spacenav input event processing */
extern int carousel_spnav_event(carousel_t *menu, spnav_event *e);

/* ...reset carousel menu */
extern void carousel_reset(carousel_t *menu);

/*******************************************************************************
 * Faded border drawing function
 ******************************************************************************/

/* ...draw border with gradient */
extern int border_draw(const texture_view_t *inner, const texture_view_t *outer, u32 c0, u32 c1, float s);

/* ...initialize shader for border drawing */
extern int border_shader_prebuild(void);


#endif  /* __UTEST_GUI_H */
