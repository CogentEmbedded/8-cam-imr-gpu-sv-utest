/*******************************************************************************
 * utest-compositor.c
 *
 * VSP compositor for IMR-based surround-view application
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

#define MODULE_TAG                      VSP

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"
#include "utest-compositor.h"
#include <vspm_public.h>
#include <mmngr_user_public.h>
#include <mmngr_buf_user_public.h>
#include <linux/videodev2.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * Gen2/Gen3 wrapper
 ******************************************************************************/

#define __VSPM_GEN3

#ifdef __VSPM_GEN3
typedef struct vsp_src_t            VSP_SRC_T;
typedef struct vsp_dst_t            VSP_DST_T;
typedef struct vsp_alpha_unit_t     VSP_ALPHA_T;
typedef struct vsp_bru_t            VSP_BRU_T;
typedef struct vsp_bld_ctrl_t       VSP_BLEND_CTRL_T;
typedef struct vsp_bld_vir_t        VSP_BLEND_VIRTUAL_T;
typedef struct vsp_ctrl_t           VSP_CTRL_T;
typedef struct vsp_start_t          VSP_START_T;
typedef struct vspm_job_t           VSPM_JOB_T;
typedef void *                      VSPM_HANDLE_T;

#define __ADDR_CAST(addr)           (uintptr_t)(unsigned long)(addr)

#else

typedef T_VSP_IN                    VSP_SRC_T;
typedef T_VSP_OUT                   VSP_DST_T;
typedef T_VSP_ALPHA                 VSP_ALPHA_T;
typedef T_VSP_BRU                   VSP_BRU_T;
typedef T_VSP_BLEND_CONTROL         VSP_BLEND_CTRL_T;
typedef T_VSP_BLEND_VIRTUAL         VSP_BLEND_VIRTUAL_T;
typedef T_VSP_CTRL                  VSP_CTRL_T;
typedef T_VSP_START                 VSP_START_T;
typedef VSPM_IP_PAR                 VSPM_JOB_T;
typedef unsigned long               VSPM_HANDLE_T;

#define __ADDR_CAST(addr)           (void *)(uintptr_t)(addr)
#endif

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

typedef struct vsp_compositor
{
    /* ...source pads configuration */
	VSP_SRC_T               src_par[3];

    /* ...transparency plane for blending sources */
    VSP_ALPHA_T             alpha_par[3];

    /* ...destination pad configuration */
    VSP_DST_T               dst_par;

    /* ...blending unit configuration */
    VSP_BRU_T               bru_par;

    /* ...blending unit control structure (blending of camera planes, car model, background) */
	VSP_BLEND_CTRL_T        bld_par[3];

    /* ...blending unit virtual layer (background) */
	VSP_BLEND_VIRTUAL_T     vir_par;

    /* ...control module (pipeline description) */
    VSP_CTRL_T              ctrl_par;

    /* ...job control structure */
	VSP_START_T             vsp_par;

    /* ...another function? - tbd */
	VSPM_JOB_T              vspm_ip;

    /* ...display list memory */
    vsp_mem_t              *dl;

    /* ...driver handle */
    VSPM_HANDLE_T           handle;

    /* ...processing callback */
    vsp_callback_t          cb;
    
    /* ...callback client data */
    void                   *cdata;

    /* ...input parameters for cameras */
    u32                     format;

}   vsp_compositor_t;

/* ...DMA buffer descriptor */
struct vsp_dmabuf
{
    /* ...exported buffer identifier */
    int                 id;

    /* ...DMA file-descriptor */
    int                 fd;
};

/* ...memory descriptor */
struct vsp_mem
{
    /* ...identifier of the memory buffer */
    MMNGR_ID            id;

    /* ...user-accessible pointer */
    unsigned long       user_virt_addr;
    
    /* ...physical address(es?) */
    unsigned long       phy_addr, hard_addr;

    /* ...size of a chunk */
    unsigned long       size;

    /* ...exported DMA buffers */
    vsp_dmabuf_t      **dmabuf;

    /* ...number of planes */
    int                 planes;
    
    /* ...number of planes */
    u32                 offset[3];
};
    
/*******************************************************************************
 * Memory allocation
 ******************************************************************************/

/* ...allocate contiguous block */
vsp_mem_t * vsp_mem_alloc(u32 size)
{
    vsp_mem_t      *mem;
    int             err;

    /* ...allocate contiguous memory descriptor */
    CHK_ERR(mem = calloc(1, sizeof(*mem)), (errno = ENOMEM, NULL));

    /* ...all chunks must be page-size aligned */
    size = (size + 4095) & ~4095;

    /* ...allocate physically contiguous memory */
    switch (err = mmngr_alloc_in_user(&mem->id, size, &mem->phy_addr, &mem->hard_addr, &mem->user_virt_addr, MMNGR_VA_SUPPORT))
    {
    case R_MM_OK:
        /* ...memory allocated successfully */
        TRACE(DEBUG, _b("allocated %p[%u] block[%X] (pa=%08lx)"), (void *)(uintptr_t)mem->user_virt_addr, size, mem->id, mem->hard_addr);
        mem->size = size;
        return mem;

    case R_MM_NOMEM:
        /* ...insufficient memory */
        TRACE(ERROR, _x("failed to allocated contiguous memory block (%u bytes)"), size);
        errno = ENOMEM;
        break;
        
    default:
        /* ...internal allocation error */
        TRACE(ERROR, _x("memory allocation error (%u bytes), err=%d"), size, err);
        errno = EBADF;
    }

    free(mem);
    return NULL;
}

/* ...destroy contiguous memory block */
void vsp_mem_free(vsp_mem_t *mem)
{
    /* ...free allocated memory */
    mmngr_free_in_user(mem->id);
    
    TRACE(DEBUG, _b("destroyed block #%X (va=%p)"), mem->id, (void *)(uintptr_t)mem->user_virt_addr);

    /* ...destroy memory descriptor */
    free(mem);
}

/* ...memory buffer accessor */
void * vsp_mem_ptr(vsp_mem_t *mem)
{
    return (void *)(uintptr_t)mem->user_virt_addr;
}

/* ...memory size */
u32 vsp_mem_size(vsp_mem_t *mem)
{
    return mem->size;
}

/* ...export DMA file-descriptor representing contiguous block */
vsp_dmabuf_t * vsp_dmabuf_export(vsp_mem_t *mem, u32 offset, u32 size)
{
    vsp_dmabuf_t   *dmabuf;
    unsigned long   hard_addr;
    int             err;

    /* ...sanity check */
    CHK_ERR(offset + size <= mem->size, (errno = EINVAL, NULL));

    /* ...allocate DMA-buffer descriptor */
    CHK_ERR(dmabuf = malloc(sizeof(*dmabuf)), (errno = ENOMEM, NULL));

    /* ...get physical address of the mapped buffer (should we align that by page-size? - tbd) */
    hard_addr = mem->hard_addr + offset;

    /* ...exported chunks must be page-size aligned (tbd) */
    size = (size + 4095) & ~4095;

    /* ...export memory as file-descriptor */
#ifdef __VSPM_GEN3
    switch (err = mmngr_export_start_in_user_ext(&dmabuf->id, size, hard_addr, &dmabuf->fd, NULL))
#else
    switch (err = mmngr_export_start_in_user(&dmabuf->id, size, hard_addr, &dmabuf->fd))
#endif
    {
    case R_MM_OK:
        TRACE(DEBUG, _b("exported block[%X] (fd=%d): pa=0x%08lx, size=%u"), dmabuf->id, dmabuf->fd, hard_addr, size);
        return dmabuf;
        
    default:
        TRACE(ERROR, _x("failed to export DMA-fd: %d"), err);
        errno = EBADF;
    }

    free(dmabuf);
    return NULL;
}

/* ...DMA-buffer descriptor accessor */
int vsp_dmabuf_fd(vsp_dmabuf_t *dmabuf)
{
    return dmabuf->fd;
}

/* ...close DMA file-descriptor */
void vsp_dmabuf_unexport(vsp_dmabuf_t *dmabuf)
{
    /* ...release DMA-descriptor */
#ifdef __VSPM_GEN3
    mmngr_export_end_in_user_ext(dmabuf->id);
#else
    mmngr_export_end_in_user(dmabuf->id);
#endif

    /* ...destroy buffer handle */
    free(dmabuf);
}

/*******************************************************************************
 * VSPM job processing
 ******************************************************************************/

/* ...processing completion callback */
#ifdef __VSPM_GEN3
static void vspm_job_callback(unsigned long job_id, long result, void *user_data)
#else
static void vspm_job_callback(unsigned long job_id, long result, unsigned long user_data)
#endif
{
#ifdef __VSPM_GEN3
    vsp_compositor_t   *vsp = user_data;
#else
    vsp_compositor_t   *vsp = (void *)(uintptr_t)user_data;
#endif
    sigset_t            set;

    /* ...unblock signal (allow interruption once processing is complete) */
    sigfillset(&set);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    if (result != 0)
    {
        TRACE(ERROR, _b("job #%lx failed: %ld"), job_id, result);
    }
    else
    {
        TRACE(DEBUG, _b("job #%lx completed"), job_id);
    }

    /* ...notify application on processing completion */
    vsp->cb(vsp->cdata, (result ? -EBADF : 0));
}

static inline void __vsp_set_addr(VSP_SRC_T *src, vsp_mem_t *input)
{
    src->addr = __ADDR_CAST(input->hard_addr + input->offset[0]);
    src->addr_c0 = __ADDR_CAST(input->hard_addr + input->offset[1]);
    src->addr_c1 = __ADDR_CAST(input->hard_addr + input->offset[2]);
}

/* ...job submission */
int vsp_job_submit(vsp_compositor_t *vsp, vsp_mem_t **input, vsp_mem_t *output)
{
    unsigned long   job_id;
    long            err;
    sigset_t        set;

    /* ...set input buffers addresses */
    __vsp_set_addr(&vsp->src_par[0], input[0]);
    __vsp_set_addr(&vsp->src_par[1], input[2]);
    __vsp_set_addr(&vsp->src_par[2], input[8]);

    /* ...set transparency plane addresses */
    vsp->alpha_par[0].addr_a = __ADDR_CAST(input[4]->hard_addr);
    vsp->alpha_par[1].addr_a = __ADDR_CAST(input[6]->hard_addr);

    /* ...set destination plane address */
    vsp->dst_par.addr = __ADDR_CAST(output->hard_addr);

#if 0
#ifdef __VSPM_GEN3
    BUG(vsp->src_par[0].alpha->asel != VSP_ALPHA_NUM2, _x("invalid alpha"));
    BUG(vsp->src_par[1].alpha->asel != VSP_ALPHA_NUM2, _x("invalid alpha"));
#else
    BUG(vsp->src_par[0].alpha_blend->asel != VSP_ALPHA_NUM2, _x("invalid alpha"));
    BUG(vsp->src_par[1].alpha_blend->asel != VSP_ALPHA_NUM2, _x("invalid alpha"));
#endif
#endif

    /* ...block all signals for a duration of the job */
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    /* ...submit a job (use "default" priority 126 - tbd) */
#ifdef __VSPM_GEN3
    err = vspm_entry_job(vsp->handle, &job_id, 126, &vsp->vspm_ip, vsp, vspm_job_callback);
#else
    err = VSPM_lib_Entry(vsp->handle, &job_id, 126, &vsp->vspm_ip, (unsigned long)(uintptr_t)vsp, vspm_job_callback);
#endif

    TRACE(DEBUG, _b("job #%lx submitted: %ld"), job_id, err);

    /* ...unblock the signals in case of error */
    (err < 0 ? pthread_sigmask(SIG_UNBLOCK, &set, NULL) : 0);

    CHK_ERR(err >= 0, -(errno = EBADFD));

    return 0;
}

/*******************************************************************************
 * Pads configuration
 ******************************************************************************/

static inline void vsp_src_setup(VSP_SRC_T *src_par, int w, int h, u32 fmt, int W, int H)
{
    switch (fmt)
    {
    case V4L2_PIX_FMT_UYVY:
        src_par->stride = w * 2;
        src_par->format = VSP_IN_YUV422_INT0_YUY2;
        src_par->swap = VSP_SWAP_W | VSP_SWAP_L | VSP_SWAP_LL;
        src_par->csc = VSP_CSC_ON;
        break;

    case V4L2_PIX_FMT_YUYV:
        src_par->stride = w * 2;
        src_par->format = VSP_IN_YUV422_INT0_UYVY;
        src_par->swap = VSP_SWAP_W | VSP_SWAP_L | VSP_SWAP_LL;
        src_par->csc = VSP_CSC_ON;
        break;
        
    case V4L2_PIX_FMT_NV16:
        src_par->stride = w;
        src_par->stride_c = w;
        src_par->format = VSP_IN_YUV422_SEMI_NV16;
        src_par->swap = VSP_SWAP_B | VSP_SWAP_W | VSP_SWAP_L | VSP_SWAP_LL;
        src_par->csc = VSP_CSC_ON;
        break;

    case V4L2_PIX_FMT_ARGB32:
        src_par->stride = w * 4;
        src_par->format = VSP_IN_ARGB8888;
        src_par->swap = VSP_SWAP_L | VSP_SWAP_LL;
        src_par->csc = VSP_CSC_OFF;
        break;
    }
    
    /* ...specify format-independent source pad configuration */
    src_par->width = w;
    src_par->height = h;
    src_par->pwd = VSP_LAYER_CHILD;
    src_par->cipm = VSP_CIPM_BI_LINEAR;
    src_par->iturbt = VSP_ITURBT_601;
    src_par->clrcng = VSP_FULL_COLOR;
    src_par->connect = VSP_BRU_USE;

    /* ...center image on the screen (with cropping) */
    if (w > W)  src_par->x_offset = (w - W) >> 1;
    else        src_par->x_position = (W - w) >> 1;

    if (h > H)  src_par->y_offset = (h - H) >> 1;
    else        src_par->y_position = (H - h) >> 1;
}

/* ...alpha-plane setup */
static inline void vsp_alpha_setup(VSP_ALPHA_T *alpha_par, int w, int h, int mask)
{
    if (w > 0)
    {
#ifdef __VSPM_GEN3
        alpha_par->stride_a = w;
        alpha_par->swap = VSP_SWAP_B | VSP_SWAP_W | VSP_SWAP_L | VSP_SWAP_LL;
#else
        alpha_par->astride = w;
        alpha_par->aswap = VSP_SWAP_B | VSP_SWAP_W | VSP_SWAP_L | VSP_SWAP_LL;
#endif
        alpha_par->asel = VSP_ALPHA_NUM2;
    }
    else if (w == 0)
    {
        alpha_par->asel = VSP_ALPHA_NUM5;
        alpha_par->afix = 0xFF;
    }
    else
    {
        alpha_par->asel = VSP_ALPHA_NUM1;
    }
}

/* ...destination pad setup */
static inline void vsp_dst_setup(VSP_DST_T *dst_par, int w, int h, u32 fmt)
{
    /* ...specify ARGB image stride */
    dst_par->stride = w * 4;
    dst_par->width = w;
    dst_par->height = h;
    dst_par->format = VSP_OUT_PRGB8888;
    dst_par->swap = VSP_SWAP_L | VSP_SWAP_LL;
    dst_par->pxa = VSP_PAD_IN;
    dst_par->csc = VSP_CSC_OFF;
    dst_par->iturbt = VSP_ITURBT_601;
    dst_par->clrcng = VSP_FULL_COLOR;
}

/* ...setup blending unit */
static inline int vsp_bru_setup(VSP_BRU_T *bru_par, VSP_BLEND_CTRL_T *bld_par, VSP_BLEND_VIRTUAL_T *vir_par, int w, int h)
{
    /* ...set virtual rpf (background plane) */
    vir_par->width = w;
    vir_par->height	= h;
    vir_par->pwd = VSP_LAYER_PARENT;
    vir_par->color = /* 0xFF800000 */0;

    /* ...set blend parameters for camera planes: sum of input alpha-levels */
    bld_par[0].rbc = VSP_RBC_BLEND;
    bld_par[0].blend_coefx = VSP_COEFFICIENT_BLENDX1;
    bld_par[0].blend_coefy = VSP_COEFFICIENT_BLENDY3;
    bld_par[0].aformula = VSP_FORM_ALPHA0;
    bld_par[0].acoefx = VSP_COEFFICIENT_ALPHAX5;
    bld_par[0].acoefy = VSP_COEFFICIENT_ALPHAY5;
    bld_par[0].acoefx_fix = 0xFF;
    bld_par[0].acoefy_fix = 0xFF;

    /* ...blending parameter for car model: use source alpha-buffer */
    bld_par[1].rbc = VSP_RBC_BLEND;
    bld_par[1].blend_coefx = VSP_COEFFICIENT_BLENDX4;
    bld_par[1].blend_coefy = VSP_COEFFICIENT_BLENDY5;
    bld_par[1].aformula = VSP_FORM_ALPHA0;
    bld_par[1].acoefx = VSP_COEFFICIENT_ALPHAX4;
    bld_par[1].acoefy = VSP_COEFFICIENT_ALPHAY5;
    bld_par[1].acoefx_fix = 0xFF;
    bld_par[1].acoefy_fix = 0xFF;

    /* ...set blend parameters for background: use destination alpha buffer  */
    bld_par[2].rbc = VSP_RBC_BLEND;
    bld_par[2].blend_coefx = VSP_COEFFICIENT_BLENDX1;
    bld_par[2].blend_coefy = VSP_COEFFICIENT_BLENDY2;
    bld_par[2].aformula = VSP_FORM_ALPHA0;
    bld_par[2].acoefx = VSP_COEFFICIENT_ALPHAX1;
    bld_par[2].acoefy = VSP_COEFFICIENT_ALPHAY2;

    /* ...set blending order */
    bru_par->lay_order = (VSP_LAY_1 << 0) | (VSP_LAY_2 << 4) | (VSP_LAY_3 << 8) | (VSP_LAY_VIRTUAL << 12);
    bru_par->blend_virtual = vir_par;
#ifdef __VSPM_GEN3
    bru_par->blend_unit_a = &bld_par[0];
    bru_par->blend_unit_b = &bld_par[1];
    bru_par->blend_unit_c = &bld_par[2];
#else
    bru_par->blend_control_a = &bld_par[0];
    bru_par->blend_control_b = &bld_par[1];
    bru_par->blend_control_c = &bld_par[2];
#endif
    bru_par->connect = 0;

    /* ...return number of active RPF sources (excluding virtual RPF) */
    return 3;
}

static inline u32 __vsp_pixfmt_size(int w, int h, u32 fmt)
{
    switch(fmt)
    {
    case V4L2_PIX_FMT_GREY:     return 1 * w * h;
    case V4L2_PIX_FMT_UYVY:     return 2 * w * h;
    case V4L2_PIX_FMT_YUYV:     return 2 * w * h;
    case V4L2_PIX_FMT_NV16:     return 2 * w * h;
    case V4L2_PIX_FMT_ARGB32:   return 4 * w * h;
    default:                    return 0;
    }
}

/* ...determine planes parameters for a given format */
static inline int __vsp_pixfmt_planes(int w, int h, u32 fmt, u32 *size, u32 *stride)
{
    int     N = w * h;

    switch(fmt)
    {
    case V4L2_PIX_FMT_GREY:
        return size[0] = N, stride[0] = w, 1;
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_YUYV:
        return size[0] = N * 2, stride[0] = w * 2, 1;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
        return size[0] = N, size[1] = N >> 1, stride[0] = stride[1] = w, 2;
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_NV61:
        return size[0] = size[1] = N, stride[0] = stride[1] = w, 2;
    case V4L2_PIX_FMT_ARGB32:
    case V4L2_PIX_FMT_XRGB32:
        return size[0] = N * 4, stride[0] = w * 4, 1;
    case V4L2_PIX_FMT_YUV420:
        return size[0] = N, size[1] = size[2] = N >> 2, stride[0] = w, stride[1] = stride[2] = w >> 1, 3;
    default:
        return TRACE(ERROR, _b("unrecognized format: %X: %c%c%c%c"), fmt, __v4l2_fmt(fmt)), 0;
    }
}

/* ...allocate contiguous memory buffer pool */
int vsp_allocate_buffers(int w, int h, u32 fmt, vsp_mem_t **output, int num)
{
    u32     size;
    int     i, n;
    u32     psize[3], stride[3], offset[3];

    /* ...calculate size of the buffer */
    if ((size = __vsp_pixfmt_size(w, h, fmt)) == 0)
    {
        TRACE(ERROR, _x("unsupported format '%c%c%c%c'"), __v4l2_fmt(fmt));
        return -(errno = EINVAL);
    }

    /* ...calculate buffer properties */
    if ((n = __vsp_pixfmt_planes(w, h, fmt, psize, stride)) == 0)
    {
        TRACE(ERROR, _x("invalid format '%c%c%c%c'"), __v4l2_fmt(fmt));
        return -(errno = EINVAL);
    }

    /* ...set offsets of the planes */
    for (offset[0] = 0, i = 1; i < n; i++)
    {
        offset[i] = offset[i - 1] + psize[i - 1];
    }
    
    /* ...allocate memory descriptors */
    for (i = 0; i < num; i++)
    {
        if ((output[i] = vsp_mem_alloc(size)) == NULL)
        {
            TRACE(ERROR, _x("failed to allocate buffer pool"));
            goto error;
        }

        /* ...set planes offsets */
        memcpy(output[i]->offset, offset, sizeof(u32) * n);
        
        TRACE(DEBUG, _b("allocate buffer: fmt=%c%c%c%c, size=%u, addr=0x%08lX"), __v4l2_fmt(fmt), size, output[i]->hard_addr);
    }
    
    return 0;

error:
    /* ...destroy buffers allocated */
    while (i--)
    {
        vsp_mem_free(output[i]), output[i] = NULL;
    }

    return -(errno = ENOMEM);
}

/* ...allocate contiguous memory buffer pool */
int vsp_buffer_export(vsp_mem_t *mem, int w, int h, u32 fmt, int *dmafd, u32 *offset, u32 *stride)
{
    u32     size[GST_VIDEO_MAX_PLANES], o;
    int     n;
    int     i;

    /* ...verify format */
    CHK_ERR((n = __vsp_pixfmt_planes(w, h, fmt, size, stride)) > 0, -(errno = EINVAL));

    /* ...check if buffer is mapped already */
    if (mem->dmabuf)
    {
        for (i = 0; i < n; i++)
        {
            dmafd[i] = mem->dmabuf[i]->fd;
            offset[i] = 0;
        }

        return 0;
    }
    
    //CHK_ERR(!mem->dmabuf, -(errno = EBUSY));    

    /* ...allocate dma-buffers array */
    CHK_ERR(mem->dmabuf = calloc(n, sizeof(vsp_dmabuf_t *)), -(errno = ENOMEM));

    for (i = 0; i < n; i++)
    {
        TRACE(INFO, _b("plane-%d: fmt=%c%c%c%c, size=%u, stride=%u"), i, __v4l2_fmt(fmt), size[i], stride[i]);
    }
    
    /* ...allocate required amount of planes */
    for (i = 0, o = 0; i < n; i++)
    {
        /* ...export single plane (may fail if not page-size aligned - tbd) */
        if ((mem->dmabuf[i] = vsp_dmabuf_export(mem, o, size[i])) == NULL)
        {
            TRACE(ERROR, _x("failed to export DMA buffer: %m"));
            goto error;
        }
        else
        {
            dmafd[i] = mem->dmabuf[i]->fd;
            offset[i] = 0;
            o += size[i];
            TRACE(DEBUG, _b("plane-%d: fd=%d, offset=%X, size=%u, stride=%u"), i, dmafd[i], offset[i], size[i], stride[i]);
        }
    }

    TRACE(INFO, _b("exported memory (format=%c%c%c%c, %d planes)"), __v4l2_fmt(fmt), n);

    return 0;

error:    
    /* ...destroy all buffers exported thus far */
    while (i--)
    {
        vsp_dmabuf_unexport(mem->dmabuf[i]);
    }

    /* ...destroy DMA buffers descriptors */
    free(mem->dmabuf);
    
    return -errno;
}

/*******************************************************************************
 * Entry points
 ******************************************************************************/

/* ...module initialization function */
vsp_compositor_t * compositor_init(int w, int h, u32 ifmt, int W, int H, u32 ofmt, int cw, int ch, vsp_callback_t cb, void *cdata)
{
    vsp_compositor_t       *vsp;
    long                    err;
#ifdef __VSPM_GEN3
    struct vspm_init_t      init_par;
#endif

    /* ...allocate compositor data */
    CHK_ERR(vsp = calloc(1, sizeof(*vsp)), (errno = ENOMEM, NULL));

    /* ...save completion callback data */
    vsp->cb = cb, vsp->cdata = cdata;

    /* ...initialize VSPM driver */
#ifdef __VSPM_GEN3
    memset(&init_par, 0, sizeof(struct vspm_init_t));
    init_par.use_ch = VSPM_EMPTY_CH;
    init_par.mode = VSPM_MODE_MUTUAL;
    init_par.type = VSPM_TYPE_VSP_AUTO;
    err = vspm_init_driver(&vsp->handle, &init_par);
#else
    err = VSPM_lib_DriverInitialize(&vsp->handle);
#endif
    if (err != 0)
    {
        TRACE(ERROR, _b("failed to initialize driver: %ld"), err);
        goto error;
    }

    /* ...source pad setup - left + right cameras plane */
    vsp_src_setup(&vsp->src_par[0], w, h, ifmt, w, h);
    vsp_alpha_setup(&vsp->alpha_par[0], w, h, 1);
#ifdef __VSPM_GEN3
    vsp->src_par[0].alpha = &vsp->alpha_par[0];
#else
    vsp->src_par[0].alpha_blend = &vsp->alpha_par[0];
#endif

    /* ...source pad setup - front + center; exact copy of left + right plane */
    memcpy(&vsp->src_par[1], &vsp->src_par[0], sizeof(VSP_SRC_T));
    vsp_alpha_setup(&vsp->alpha_par[1], w, h, 0);
#ifdef __VSPM_GEN3
    vsp->src_par[1].alpha = &vsp->alpha_par[1];
#else
    vsp->src_par[1].alpha_blend = &vsp->alpha_par[1];
#endif

    /* ...car image pad setup; native ARGB */
    vsp_src_setup(&vsp->src_par[2], cw, ch, ofmt, w, h);
    vsp_alpha_setup(&vsp->alpha_par[2], -1, -1, 0);
#ifdef __VSPM_GEN3
    vsp->src_par[2].alpha = &vsp->alpha_par[2];
#else
    vsp->src_par[2].alpha_blend = &vsp->alpha_par[2];
#endif
    
    /* ...destination pad setup */
    vsp_dst_setup(&vsp->dst_par, W, H, ofmt);

    /* ...blending unit setup */
    vsp->vsp_par.rpf_num = vsp_bru_setup(&vsp->bru_par, vsp->bld_par, &vsp->vir_par, w, h);
    
    /* ...control structure setup */
    vsp->ctrl_par.bru = &vsp->bru_par;

    /* ...setup VSP job parameters */
    vsp->vsp_par.use_module = VSP_BRU_USE;
#ifdef __VSPM_GEN3
    vsp->vsp_par.src_par[0] = &vsp->src_par[0];
    vsp->vsp_par.src_par[1] = &vsp->src_par[1];
    vsp->vsp_par.src_par[2] = &vsp->src_par[2];
#else
    vsp->vsp_par.src1_par = &vsp->src_par[0];
    vsp->vsp_par.src2_par = &vsp->src_par[1];
    vsp->vsp_par.src3_par = &vsp->src_par[2];
#endif
    vsp->vsp_par.dst_par = &vsp->dst_par;
    vsp->vsp_par.ctrl_par = &vsp->ctrl_par;

#ifdef __VSPM_GEN3
    /* ...allocate DL memory (size is hardcoded?) */
    CHK_ERR(vsp->dl = vsp_mem_alloc((128 + 64 * 8) * 8), (errno = ENOMEM, NULL));
	vsp->vsp_par.dl_par.hard_addr = __ADDR_CAST(vsp->dl->hard_addr);
	vsp->vsp_par.dl_par.virt_addr = (void *)(uintptr_t)vsp->dl->user_virt_addr;
	vsp->vsp_par.dl_par.tbl_num = 128 + 64 * 8;
#endif

    /* ...prepare job descriptor */
#ifdef __VSPM_GEN3
	vsp->vspm_ip.type = VSPM_TYPE_VSP_AUTO;
	vsp->vspm_ip.par.vsp = &vsp->vsp_par;
#else
	vsp->vspm_ip.uhType = VSPM_TYPE_VSP_AUTO;
	vsp->vspm_ip.unionIpParam.ptVsp = &vsp->vsp_par;
#endif

    TRACE(INIT, _b("VSPM compositor initialized: %d*%d[%d] -> %d*%d[%d]"), w, h, ifmt, W, H, ofmt);

    return vsp;

error:
    /* ...destroy memory */
    free(vsp);
    
    return NULL;
}
