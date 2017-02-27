/*
 * road_scene.h
 *
 *  Created on: Jul 28, 2015
 *      Author: krokoziabla
 */

#ifndef SRC_ROAD_SCENE_H_
#define SRC_ROAD_SCENE_H_

#include <stdlib.h>

typedef struct point
{
    int             x,
                    y;
} point_t;

typedef struct rect
{
    point_t         top_left,
                    bottom_right;

} rect_t;

typedef enum
{
    STYLE_SOLID     = 0x01, /* solid style of a traffic line */
    STYLE_DASHED    = 0x02, /* dashed style of a traffic line */
    STYLE_DOTTED    = 0x04, /* dotted style of a traffic line */
    STYLE_CURB      = 0x08, /* curb as a lane's boundary */
    STYLE_UNKNOWN   = 0x00  /* unknown style of a traffic line */

} line_style_t;

typedef enum
{
    COLOR_WHITE     = 0xFFFFFF, /* white color of a traffic line */
    COLOR_YELLOW    = 0xFFFF00, /* yellow color of a traffic line */
    COLOR_UNKNOWN   = 0x000000  /* unknown color of a traffic line */

} line_color_t;

#define MAX_TRAFFIC_LINE_POINTS 4

typedef struct traffic_line
{
    line_style_t    style;
    line_color_t    color;

    size_t          points_num; /*  0 means no line */
    point_t         points[MAX_TRAFFIC_LINE_POINTS];

} traffic_line_t;

typedef struct traffic_lane
{
    /* 0.5 value - center of the lane.
       0 and 1 correspond to left and right boundaries. Usually you want to
       emit a warning if 0.2 or 0.8 value is reached.
       Returned value can be negative or more than 1. */
    float           car_location;

    traffic_line_t  left_boundary,
                    right_boundary;

} traffic_lane_t;

typedef struct forward_obstacle
{
    rect_t          bounding_box;

    float           time_to_collision;  /* seconds */
    float           range_to_collision; /* millimeters */
    int             dangerous;          /* 1 - dangerous, 0 - otherwise */

} forward_obstacle_t;

typedef struct stop_sign
{
    rect_t      boundary;

} stop_sign_t;

typedef struct school_sign
{
    rect_t      boundary;

} school_sign_t;

typedef struct yield_sign
{
    rect_t      boundary;

} yield_sign_t;


typedef enum measure_unit
{
    KILOMETERS_PER_HOUR, ///< km/h
    MILES_PER_HOUR       ///< mph
} measure_unit_t;

typedef struct speed_limit_sign
{
    measure_unit_t  measure_unit;
    float           limit;
    rect_t          boundary;

} speed_limit_sign_t;

#define MAX_SIGNS_NUMBER  4

typedef struct road_scene
{

    /*
     * 0x01             traffic_lane present
     * 0x02             forward_obstacle preset
     */
    char                presence_flags;

    traffic_lane_t      traffic_lane;
    forward_obstacle_t  forward_obstacle;

    size_t              stop_signs_num;
    stop_sign_t         stop_signs[MAX_SIGNS_NUMBER];

    size_t              yield_signs_num;
    yield_sign_t        yield_signs[MAX_SIGNS_NUMBER];

    size_t              school_signs_num;
    school_sign_t       school_signs[MAX_SIGNS_NUMBER];

    size_t              speed_limit_signs_num;
    speed_limit_sign_t  speed_limit_signs[MAX_SIGNS_NUMBER];

    int64_t             ts;
    void               *road;
} road_scene_t;

#endif /* SRC_ROAD_SCENE_H_ */
