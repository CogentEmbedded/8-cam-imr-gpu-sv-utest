/*******************************************************************************
 * utest-png.c
 *
 * IMR unit test application - PNG handling
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

#define MODULE_TAG                      PNG

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"
#include "utest-png.h"
#include <png.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * PNG loading
 ******************************************************************************/

/* ...reader error handling routine */
static __attribute__((noreturn))  void __read_error(png_structp png_ptr, png_const_charp msg)
{
    jmp_buf    *jbp;

    TRACE(ERROR, _b("writepng libpng error: %s"), msg);
  
    if ((jbp = png_get_error_ptr(png_ptr)) == NULL)
    {
        BUG(1, _x("non-recoverable error"));
    }

    /* ...get back to the writer */
    longjmp(*jbp, EBADF);
}

int create_png(const char *path, int *width, int *height, int *format, void **data)
{
	FILE                   *fp;
    unsigned char           header[8];
	int                     y, w, h, fmt, stride = 0;
	png_byte                color_type;
	png_byte                bit_depth;
	png_structp             png_ptr;
	png_infop               info_ptr;
	png_bytep * volatile    row_pointers = NULL;
	png_bytep               row = NULL;
    jmp_buf                 jb;

    /* ...sanity check - data buffer pointer must be provided */
    CHK_ERR(data, -EINVAL);

    /* ...open image file */
	if ((fp = fopen(path, "rb")) == NULL)
    {
        TRACE(ERROR, _x("failed to open '%s': %m"), path);
        return -errno;
    }

    /* ...parse image header */
    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8))
    {
        TRACE(ERROR, _x("invalid image '%s'"), path);
        errno = EBADF;
        goto error;
    }
    
    /* ...initialize PNG library handle */
    if ((png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, &jb, __read_error, NULL)) == NULL)
    {
        TRACE(ERROR, _x("failed to read image '%s': %m"), path);
        errno = EBADF;
        goto error;
    }

    /* ...get image info */
    if ((info_ptr = png_create_info_struct(png_ptr)) == NULL)
    {
        TRACE(ERROR, _x("failed to read image '%s': %m"), path);
        errno = EBADF;
        goto error_png;
    }

    /* ...initialize PNG parser; prepare emergency return point */
    if ((errno = setjmp(jb)) != 0)
    {
        TRACE(ERROR, _x("operation failed"));
        goto error_png;
    }

    /* ...start parsing */
    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    /* ...check image parameters */
    w = png_get_image_width(png_ptr, info_ptr);
    h = png_get_image_height(png_ptr, info_ptr);
    if (!w || !h)
    {
        TRACE(ERROR, _x("invalid image '%s': %u*%u"), path, w, h);
        errno = EINVAL;
		goto error_png;
    }

    /* ...check image dimensions are expected */
    if ((width && *width && *width != w) || (height && *height && *height != h))
    {
        TRACE(ERROR, _x("wrong image '%s' dimensions: %u*%u (expected: %u*%u)"), path, w, h, (width ? *width : 0), (height ? *height : 0));
        errno = EINVAL;
        goto error_png;
    }
    else
    {
        /* ...save image dimensions */
        (width ? *width = w : 0), (height ? *height = h : 0);
    }

    /* ...process image colorspace */
    color_type = png_get_color_type(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	if (bit_depth < 8 || color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_expand(png_ptr);
	if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    /* ...test color type */
    switch (color_type)
    {
    case PNG_COLOR_TYPE_PALETTE:
        if (!format || *format == GST_VIDEO_FORMAT_ARGB || *format == GST_VIDEO_FORMAT_RGB)
        {
            png_set_palette_to_rgb(png_ptr);
        }
        else
        {
            TRACE(ERROR, _b("palette image '%s' for grayscale format is disabled"), path);
            errno = EINVAL;
            goto error_png;
        }
        break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
	case PNG_COLOR_TYPE_GRAY:
        if (!format || *format == GST_VIDEO_FORMAT_ARGB || *format == GST_VIDEO_FORMAT_RGB)
        {
            png_set_gray_to_rgb(png_ptr);
        }
		break;
    case PNG_COLOR_TYPE_RGB:
        if (!format || *format == GST_VIDEO_FORMAT_ARGB)
        {
            png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
        }
        break;
	default:
		break;
	}

    /* ...native format that we use is BGRA - hmm, tbd */
    if (color_type & PNG_COLOR_MASK_COLOR)
        png_set_bgr(png_ptr);

    /* ...update image info after parameters adjustment */
    png_read_update_info(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
	switch (color_type)
    {
	case PNG_COLOR_TYPE_RGB_ALPHA:
		fmt = GST_VIDEO_FORMAT_ARGB;
        TRACE(DEBUG, _b("argb image: %s"), path);
		break;
	case PNG_COLOR_TYPE_RGB:
		fmt = GST_VIDEO_FORMAT_RGB;
        TRACE(DEBUG, _b("rgb image: %s"), path);
		break;
    case PNG_COLOR_TYPE_GRAY:
        fmt = GST_VIDEO_FORMAT_GRAY8;
        break;
	default:
        TRACE(ERROR, _x("unsupported color type: %X (%s)"), color_type, path);
        errno = EINVAL;
        goto error_png;
	}

    /* ...if format is specified, test we have correctly set it */
    if (format && *format && *format != fmt)
    {
        TRACE(ERROR, _x("wrong image '%s' colorspace: %d (expected %d)"), path, fmt, *format);
        errno = EINVAL;
        goto error_png;
    }
    
    /* ...get image stride */
    stride = png_get_rowbytes(png_ptr, info_ptr);

    /* ...make it 4-bytes aligned */
    stride = (stride + 3) & ~3;

    /* ...set pixeldata pointer */
    if (*data == NULL)
    {
        /* ...allocate pixeldata */
        if ((row = malloc(stride * h)) == NULL)
        {
            TRACE(ERROR, _x("failed to allocate image data"));
            errno = ENOMEM;
            goto error_png;
        }
    }
    else
    {
        /* ...use provided buffer for pixeldata */
        row = *data;
    }

    /* ...allocate row pointers array */
    if ((row_pointers = malloc(sizeof(png_bytep) * h)) == NULL)
    {
        TRACE(ERROR, _x("failed to create row-pointers array"));
        errno = ENOMEM;
        goto error_png;
    }

    /* ...initialize row pointers */
    for (y = 0; y < h; y++, row += stride)
        row_pointers[y] = row;

    /* ...read image */
    png_read_image(png_ptr, row_pointers);

    /* ...save row pointer if needed */
    (*data == NULL ? *data = row_pointers[0] : 0);

    TRACE(INIT, _b("PNG[%s] image %u*%u created: %p"), path, w, h, *data);

    /* ...release interim memory */
	free(row_pointers);

    /* ...destroy PNG data */
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    fclose(fp);
    
    return 0;

error_png:
    /* ...release rows */
    (row && row != *data ? free(row) : 0), (row_pointers ? free(row_pointers) : 0);

    /* ...destroy PNG data */
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

error:
    /* ...close file handle */
	fclose(fp);
	return -1;
}

/*******************************************************************************
 * PNG storing
 ******************************************************************************/

/* ...writer error handling routine */
static __attribute__((noreturn))  void __write_error(png_structp png_ptr, png_const_charp msg)
{
    jmp_buf    *jbp;

    TRACE(ERROR, _b("writepng libpng error: %s"), msg);
  
    if ((jbp = png_get_error_ptr(png_ptr)) == NULL)
    {
        BUG(1, _x("non-recoverable error"));
    }

    /* ...get back to the writer */
    longjmp(*jbp, EBADF);
}

/* ...write PNG file */
int store_png(const char *path, int width, int height, int format, void *data)
{
	FILE                   *fp;
	int                     y, stride = 0;
	png_byte                color_type;
	png_byte                bit_depth;
	png_structp             png_ptr;
	png_infop               info_ptr;
	png_bytep               row = NULL;
    jmp_buf                 jb;

    /* ...sanity check - data buffer pointer must be provided */
    CHK_ERR(path && width > 0 && height >= 0 && format > 0 && data, -EINVAL);

    /* ...prepare file for writing */
    if ((fp = fopen(path, "wb")) == NULL)
    {
        TRACE(ERROR, _x("failed to open file '%s': %m"), path);
        return -errno;
    }

    /* ...create write structure */
    if (!(png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, &jb, __write_error, NULL)))
    {
        TRACE(ERROR, _x("failed to create write struct: %m"));
        errno = ENOMEM;
        goto error;
    }
  
    if (!(info_ptr = png_create_info_struct(png_ptr)))
    {
        TRACE(ERROR, _x("failed to create info struct: %m"));
        errno = ENOMEM;
        goto error_png;
    }

    /* ...prepare emergency return point */
    if ((errno = setjmp(jb)) != 0)
    {
        TRACE(ERROR, _x("operation failed"));
        goto error_png;
    }

    /* ...initialize I/O */
    png_init_io(png_ptr, fp);

    /* ...set compression level (zlib 0 to 9) */
    //png_set_compression_level(png_ptr, 9);

    /* ...set color format */
    switch (format)
    {
    case GST_VIDEO_FORMAT_GRAY8:
        color_type = PNG_COLOR_TYPE_GRAY, bit_depth = 8;
        break;
    case GST_VIDEO_FORMAT_ARGB:
        color_type = PNG_COLOR_TYPE_RGB_ALPHA, bit_depth = 32;
        break;
    case GST_VIDEO_FORMAT_RGB:
        color_type = PNG_COLOR_TYPE_RGB, bit_depth = 24;
        break;
    default:
        TRACE(ERROR, _x("unsupported format %d"), format);
        errno = EINVAL;
        goto error_png;
    }

    TRACE(1, _b("prepare writing: w=%d, h=%d, d=%d, c=%d"), width, height, bit_depth, color_type);

    /* ...native format that we use is BGRA - hmm, tbd */
    (color_type & PNG_COLOR_MASK_COLOR ? png_set_bgr(png_ptr) : 0);

    /* ...write header */
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    /* ...write info structure */
    png_write_info(png_ptr, info_ptr);

    /* ...write all image rows */
    row = data, stride = (width * bit_depth) >> 3;    
    for (y = 0; y < height; y++, row += stride)
        png_write_row(png_ptr, row);

    /* ...mark completion of the image */
    png_write_end(png_ptr, NULL);

    TRACE(INFO, _b("written file '%s': %d*%d, format=%d"), path, width, height, format);

    /* ...cleanup after ourselves */
    png_destroy_write_struct(&png_ptr, NULL);

    fclose(fp);

    return 0;

error_png:
    /* ...destroy write structure */
    png_destroy_write_struct(&png_ptr, NULL);

error:
    /* ...close file handle */
    fclose(fp);
    return -1;
}
