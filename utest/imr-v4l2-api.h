/*
 * rcar_imr.c  --  R-Car IMR-X2(4) driver public interface
 *
 * Copyright (C) 2015 Cogent Embedded, Inc.  <source@cogentembedded.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __IMR_V4L2_API_H
#define __IMR_V4L2_API_H

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...mapping specification descriptor */
struct imr_map_desc {
    /* ...mapping types */
    u32             type;

	/* ...total size of the mesh structure */
	u32             size;

    /* ...map-specific user-pointer */
    void           *data;

}   __attribute__((packed));

/*******************************************************************************
 * Mesh type flags
 ******************************************************************************/

/* ...regular mesh specification */
#define IMR_MAP_MESH                    (1 << 0)

/* ...auto-generated source coordinates */
#define IMR_MAP_AUTODG                  (1 << 1)

/* ...auto-generated destination coordinates */
#define IMR_MAP_AUTOSG                  (1 << 2)

/* ...relative source coordinates specification mode */
//#define IMR_MAP_DYDX                    (1 << 3)

/* ...relative destination coordinates specification mode */
//#define IMR_MAP_DUDV                    (1 << 4)

/* ...luminance correction flag */
#define IMR_MAP_LUCE                    (1 << 3)

/* ...chromacity correction flag */
#define IMR_MAP_CLCE                    (1 << 4)

/* ...vertex clockwise-mode order */
#define IMR_MAP_TCM                     (1 << 5)

/* ...source coordinate decimal point position bit index */
#define __IMR_MAP_UVDPOR_SHIFT          8
#define __IMR_MAP_UVDPOR_MASK           (0x7 << __IMR_MAP_UVDPOR_SHIFT)
#define IMR_MAP_UVDPOR(n)               ((n & 0x7) << __IMR_MAP_UVDPOR_SHIFT)

/* ...destination coordinate sub-pixel mode */
#define IMR_MAP_DDP                     (1 << 11)

/* ...luminance correction offset decimal point position */
#define __IMR_MAP_YLDPO_SHIFT           12
#define __IMR_MAP_YLDPO(v)              (((v) >> __IMR_MAP_YLDPO_SHIFT) & 0x7)
#define IMR_MAP_YLDPO(n)                ((n & 0x7) << __IMR_MAP_YLDPO_SHIFT)

/* ...chromacity (U) correction offset decimal point position */
#define __IMR_MAP_UBDPO_SHIFT           15
#define __IMR_MAP_UBDPO(v)              (((v) >> __IMR_MAP_UBDPO_SHIFT) & 0x7)
#define IMR_MAP_UBDPO(n)                ((n & 0x7) << __IMR_MAP_UBDPO_SHIFT)

/* ...chromacity (V) correction offset decimal point position */
#define __IMR_MAP_VRDPO_SHIFT           18
#define __IMR_MAP_VRDPO(v)              (((v) >> __IMR_MAP_VRDPO_SHIFT) & 0x7)
#define IMR_MAP_VRDPO(n)                ((n & 0x7) << __IMR_MAP_VRDPO_SHIFT)

/*******************************************************************************
 * Regular mesh specification
 ******************************************************************************/

struct imr_mesh {
    /* ...rectangular mesh size */
    u16             rows, columns;

    /* ...mesh parameters */
    u16             x0, y0, dx, dy;

}   __attribute__((packed));

/* ...irregular mesh specification */
struct imr_vbo {
    /* ...number of triangles */
    u16             num;

}   __attribute__((packed));

/* ...absolute coordinates specification */
struct imr_abs_coord {
    u16             v, u;
    s16             Y, X;

}   __attribute__((packed));

/* ...relative coordinates specification */
struct imr_rel_coord {
    s8              dv, dy, DY, DX;

}   __attribute__((packed));

/* ...partially specified source coordinates */
struct imr_src_coord {
    u16             v, u;

}   __attribute__((packed));

/* ...partially specified destination coordinates */
struct imr_dst_coord {
    s16             Y, X;

}   __attribute__((packed));

/* ...auto-generated coordinates with luminance/chrominance correction */
struct imr_auto_coord_correction {
    union {
        struct {
            u16         v, u;
        };
        
        struct {
            s16         Y, X;
        };    
    };
    union {
        struct {
            u8          lofs, lscal;
            u16         pad;
        };
        
        struct {
            u8          vrofs, vrscal;
            u8          cbofs, cbscal;
        };
    };
}   __attribute__((packed));

/* ...auto-generated coordinates with luminance and chrominance correction */
struct imr_auto_luce_clce_coord {
    union {
        struct {
            u16         v, u;
        };
        
        struct {
            s16         Y, X;
        };    
    };

    u8          lofs, lscal;
    u16         pad;
    u8          vrofs, vrscal;
    u8          cbofs, cbscal;

}   __attribute__((packed));

/* ...absolute coordinates with luminance/chrominance correction */
struct imr_abs_coord_correction {
    u16         v, u;
    s16         Y, X;

    union {
        struct {
            u8          lofs, lscal;
            u16         pad;
        };
        
        struct {
            u8          vrofs, vrscal;
            u8          cbofs, cbscal;
        };
    };    
}   __attribute__((packed));

/* ...absolute coordinates with full correction */
struct imr_abs_coord_luce_clce {
    u16         v, u;
    s16         Y, X;
    u8          lofs, lscal;
    u16         pad;
    u8          vrofs, vrscal;
    u8          cbofs, cbscal;

}   __attribute__((packed));
    
/*******************************************************************************
 * Cropping parameters
 ******************************************************************************/

struct imr_crop {
    u16             xmin, ymin;
    u16             xmax, ymax;
}   __attribute__((packed));
    
/*******************************************************************************
 * Private IOCTL codes
 ******************************************************************************/

#define VIDIOC_IMR_MESH                 _IOW('V', BASE_VIDIOC_PRIVATE + 0, struct imr_map_desc)

#endif  /* __IMR_V4L2_API_H */
