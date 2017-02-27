/*******************************************************************************
 * vehicle-info.h
 *
 * Vehicle status information
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

#ifndef __VEHICLE_INFO_H
#define __VEHICLE_INFO_H

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...vehicle information data */
typedef struct vehicle_info
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
    
}   vehicle_info_t;

#endif  /* __VEHICLE_INFO_H */
