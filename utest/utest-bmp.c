/*******************************************************************************
 * utest-bmp.c
 *
 * IMR unit test application - BMP handling
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

#define MODULE_TAG                      BMP

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"
#include "utest-bmp.h"

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

typedef struct bmp_fheader
{
    u8              sig[2];
    u32             size;
    u16             reserved[2];
    u32             offset;
    
}  __attribute__((packed))  bmp_fheader_t;

typedef struct bmp_iheader
{
    u32             hsize;
    u32             width;
    u32             height;
    u16             planes;
    u16             bpp;
    u32             compression;
    u32             size;
    u32             xppm;
    u32             yppm;
    u32             colors;
    u32             imp_color_count;
    u32             r_mask;
    u32             g_mask;
    u32             b_mask;
    u32             a_mask;
    u32             cst;
    u32             r_gamma;
    u32             g_gamma;
    u32             b_gamma;
    u32             intent;
    u32             icc_profile_data;
    u32             icc_profile_size;
    u32             reserved;
    
} __attribute__((packed))  bmp_iheader_t;
    
/*******************************************************************************
 * BMP loading
 ******************************************************************************/

int create_bmp(const char *path, int *width, int *height, int *format, void **data)
{
    FILE           *fp;
    bmp_fheader_t   fh;
    bmp_iheader_t   ih;
    int             channels, bpp, fmt;
    int             w, h, stride, x, y;
    u8             *rows = NULL, *row;

    /* ...sanity check - data buffer pointer must be provided */
    CHK_ERR(data, -EINVAL);

    /* ...open image file */
	if ((fp = fopen(path, "rb")) == NULL)
    {
        TRACE(ERROR, _x("failed to open '%s': %m"), path);
        return -errno;
    }

    /* ...parse image header */
    if (fread(&fh, sizeof(fh), 1, fp) != 1 || fread(&ih, sizeof(ih), 1, fp) != 1)
    {
        TRACE(ERROR, _x("failed to read header: %m"));
        goto error;
    }

    /* ...check image parameters */
    w = ih.width, h = ih.height;
    if (!w || !h)
    {
        TRACE(ERROR, _x("invalid image '%s': %u*%u"), path, w, h);
        errno = EINVAL;
		goto error;
    }

    /* ...check image dimensions are expected */
    if ((width && *width && *width != w) || (height && *height && *height != h))
    {
        TRACE(ERROR, _x("wrong image '%s' dimensions: %u*%u (expected: %u*%u)"), path, w, h, (width ? *width : 0), (height ? *height : 0));
        errno = EINVAL;
        goto error;
    }
    else
    {
        /* ...save image dimensions */
        (width ? *width = w : 0), (height ? *height = h : 0);
    }

    /* ...get channels number */
    channels = (bpp = ih.bpp) >> 3;

    TRACE(1, _b("channels: %d, bpp: %d"), channels, bpp);
    
    /* ...check the palette type? - tbd */
    //colorsNum = *(int *)(ih + 32);

    /* ...check color space format */
    switch (channels)
    {
    case 4:
        fmt = GST_VIDEO_FORMAT_ARGB;
        break;
    case 3:
        fmt = GST_VIDEO_FORMAT_RGB;
        break;
    case 1:
        fmt = GST_VIDEO_FORMAT_GRAY8;
        break;
    default:
        TRACE(ERROR, _x("unsupported color type: channels=%d (%s)"), channels, path);
        errno = EINVAL;
        goto error;
	}

    TRACE(1, _b("a-mask: %08X, r-mask: %08X, g-mask: %08X, b-mask: %08X"),
          ih.a_mask, ih.r_mask, ih.g_mask, ih.b_mask);

    BUG(0, _x("breakpoint"));
    
    /* ...if format is specified, test we have correctly set it */
    if (format && *format && *format != fmt)
    {
        TRACE(ERROR, _x("wrong image '%s' colorspace: %d (expected %d)"), path, fmt, *format);
        errno = EINVAL;
        goto error;
    }

    /* ...get image stride */
    stride = (w * channels + 3) & ~3;

    /* ...set pixeldata pointer */
    if (*data == NULL)
    {
        /* ...allocate pixeldata */
        if ((rows = malloc(stride * h)) == NULL)
        {
            TRACE(ERROR, _x("failed to allocate image data"));
            errno = ENOMEM;
            goto error;
        }
    }
    else
    {
        /* ...use provided buffer for pixeldata */
        rows = *data;
    }

    /* ...skip the gap if given */
    if ((fh.offset ? fseek(fp, fh.offset, SEEK_SET) : 0))
    {
        TRACE(ERROR, _b("failed to seek the file: %m"));
        goto error;
    }

    /* ...read the lines starting from farthest */
    for (row = rows + (y = h) * stride; y > 0; y--)
    {
        if (fread(row -= stride, stride, 1, fp) != 1)
        {
            TRACE(ERROR, _b("failed to read data from file: %m"));
            goto error;
        }
    }

    /* ...convert ARGB to BGRA */
    if (fmt == GST_VIDEO_FORMAT_ARGB)
    {
        u8      shift_r = __builtin_ffs(ih.r_mask) - 1;
        u8      shift_g = __builtin_ffs(ih.g_mask) - 1;
        u8      shift_b = __builtin_ffs(ih.b_mask) - 1;
        u8      shift_a = __builtin_ffs(ih.a_mask) - 1;

        TRACE(1, _b("r:%d,g:%d,b:%d,a:%d"), shift_r, shift_g, shift_b, shift_a);
        
        for (y = 0; y < h; y++, row += stride)
        {            
            for (x = 0; x < w; x++)
            {
                u32     src = *(u32 *)(row + x * 4), dst;

                dst = ((src & ih.a_mask) >> shift_a) << 24;
                dst |= ((src & ih.r_mask) >> shift_r) << 16;
                dst |= ((src & ih.g_mask) >> shift_g) << 8;
                dst |= ((src & ih.b_mask) >> shift_b) << 0;

                *(u32 *)(row + x * 4) = dst;
            }
        }
    }
    else if (fmt == GST_VIDEO_FORMAT_RGB)
    {
        u8      shift_r = __builtin_ffs(ih.r_mask) - 1;
        u8      shift_g = __builtin_ffs(ih.g_mask) - 1;
        u8      shift_b = __builtin_ffs(ih.b_mask) - 1;

        for (y = 0; y < h; y++, row += stride)
        {            
            for (x = 0; x < w; x++)
            {
                u32     src = *(u32 *)(row + x * 3), dst;

                dst = ((src & ih.r_mask) >> shift_r) << 8;
                dst |= ((src & ih.g_mask) >> shift_g) << 16;
                dst |= ((src & ih.b_mask) >> shift_b) << 24;

                *(u32 *)(row + x * 3) = dst;
            }
        }
    }
    
    /* ...save row pointer if needed */
    (*data == NULL ? *data = rows : 0);

    TRACE(INIT, _b("BMP[%s] image %u*%u created: %p"), path, w, h, *data);

    /* ...close file handle */
    fclose(fp);
    
    return 0;
    
error:
    /* ...release memory if needed */
    (rows && !*data ? free(rows) : 0);

    /* ...close file handle */
    fclose(fp);
 
    return -errno;
}
