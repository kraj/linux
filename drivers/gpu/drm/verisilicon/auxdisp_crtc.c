/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/component.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include <drm/drm_device.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_framebuffer.h>

#include "vs_gem.h"
#include "vs_fb.h"
#include "vs_drv.h"

/* AuxDisp 寄存器偏移量 (A210基地址: 0x06740000) */
#define AUXDISP_EN                  0x00    /* 显示输出使能 */
#define AUXDISP_DTG0                0x04    /* htotal, vtotal */
#define AUXDISP_DTG1                0x08    /* hactive, vactive */
#define AUXDISP_DTG2                0x0c    /* hbp, hsw */
#define AUXDISP_DTG3                0x10    /* hfp */
#define AUXDISP_DTG4                0x14    /* vbp, vsw */
#define AUXDISP_DTG5                0x18    /* vfp */
#define AUXDISP_MODE                0x1c    /* 显示模式 */
#define AUXDISP_NEXT_CFG            0x20    /* next_cfg vdelay */
#define AUXDISP_POLARITY            0x24    /* 极性控制 */
#define AUXDISP_PATTERN             0x2c    /* 测试图案 */
#define AUXDISP_CFG_DONE            0x3c    /* 配置完成标志 */
#define AUXDISP_IN_FMT              0x40    /* 视频格式 */
#define AUXDISP_RES                 0x44    /* 视频分辨率 */
#define AUXDISP_ADDR0               0x48    /* Y/RGB 基地址 */
#define AUXDISP_ADDR1               0x4c    /* UV 基地址 */
#define AUXDISP_STRIDE0             0x58    /* Y/RGB stride */
#define AUXDISP_STRIDE1             0x5c    /* UV stride */
#define AUXDISP_BEAT                0x60    /* AXI beats */
#define AUXDISP_OFFSET              0x64    /* 水平裁剪偏移 */
#define AUXDISP_ABNORMAL_CTRL       0x70    /* AXI 复位控制 */
#define AUXDISP_IDLE                0x74    /* AXI 空闲状态 */
#define AUXDISP_AXI_CFG             0x80    /* AXI 参数 */
#define AUXDISP_INT_ENABLE          0x100   /* 中断使能 */
#define AUXDISP_INT_CLEAR           0x104   /* 中断清除 */
#define AUXDISP_INT_RAW             0x108   /* 原始中断状态 */
#define AUXDISP_INT_STATUS          0x10c   /* 中断状态 */

/* 中断位定义 (INT_STATUS 寄存器位定义) */
#define AUXDISP_INT_VSYNC           BIT(0)  /* Bit 0: disp vsync (VBlank) */
#define AUXDISP_INT_NEXT_CFG        BIT(1)  /* Bit 1: next cfg */
#define AUXDISP_INT_UNDERFLOW       BIT(2)  /* Bit 2: underflow */
#define AUXDISP_INT_AXI_ERR         BIT(3)  /* Bit 3: axi read resp error */
#define AUXDISP_INT_FRAME_DONE      BIT(5)  /* Bit 5: axi read one frame done */

/* 已知/已定义的中断掩码 */
#define AUXDISP_INT_ALL_KNOWN       (AUXDISP_INT_VSYNC | AUXDISP_INT_NEXT_CFG | \
                                     AUXDISP_INT_UNDERFLOW | AUXDISP_INT_AXI_ERR | \
                                     AUXDISP_INT_FRAME_DONE)

/* 为了兼容性，保留旧的 VBLANK 定义 */
#define AUXDISP_INT_VBLANK          AUXDISP_INT_VSYNC

/* AUXDISP_EN 寄存器位定义 */
#define AUXDISP_EN_ENABLE           BIT(0)  /* 显示使能 */

/* AUXDISP_MODE 寄存器位定义 */
#define AUXDISP_MODE_IF_HDMI        0x0     /* 接口类型: HDMI */
#define AUXDISP_MODE_IF_DSI         0x1     /* 接口类型: MIPI DSI */
#define AUXDISP_MODE_IF_DP          0x2     /* 接口类型: DP */
#define AUXDISP_MODE_IF_MASK        0x3

#define AUXDISP_MODE_FMT_RGB10      (0x0 << 4)  /* 输出格式: RGB10 */
#define AUXDISP_MODE_FMT_RGB8       (0x1 << 4)  /* 输出格式: RGB8 */
#define AUXDISP_MODE_FMT_RGB6       (0x2 << 4)  /* 输出格式: RGB6 */
#define AUXDISP_MODE_FMT_RGB565     (0x3 << 4)  /* 输出格式: RGB565 */
#define AUXDISP_MODE_FMT_YUV420_10  (0x4 << 4)  /* 输出格式: YUV420 10bit */
#define AUXDISP_MODE_FMT_YUV420_8   (0x5 << 4)  /* 输出格式: YUV420 8bit */
#define AUXDISP_MODE_FMT_MASK       (0xF << 4)

/* AUXDISP_POLARITY 位定义 */
#define AUXDISP_POL_VSYNC_HIGH      BIT(0)
#define AUXDISP_POL_HSYNC_HIGH      BIT(1)
#define AUXDISP_POL_DE_HIGH         BIT(2)

/* AUXDISP_PATTERN 位定义 */
#define AUXDISP_PATTERN_EN          BIT(0)       /* 图案使能 */
#define AUXDISP_PATTERN_SEL_SHIFT   4
#define AUXDISP_PATTERN_SEL_MASK    (0xF << 4)
#define AUXDISP_PATTERN_COLOR_SHIFT 8

/* 图案模式选择 */
#define AUXDISP_PATTERN_FIXED       0   /* 固定颜色 */
#define AUXDISP_PATTERN_GRAY_H      1   /* 灰度条-水平 */
#define AUXDISP_PATTERN_GRAY_V      2   /* 灰度条-垂直 */
#define AUXDISP_PATTERN_COLOR_H     3   /* 彩条-水平 */
#define AUXDISP_PATTERN_COLOR_V     4   /* 彩条-垂直 */

/* AUXDISP_IN_FMT 位定义 */
#define AUXDISP_IN_FMT_FMT_SHIFT    0
#define AUXDISP_IN_FMT_FMT_MASK     0xF
#define AUXDISP_IN_FMT_FMT_YUV420   0x0
#define AUXDISP_IN_FMT_FMT_RGB      0x3

#define AUXDISP_IN_FMT_BPP_SHIFT    4
#define AUXDISP_IN_FMT_BPP_MASK     (0xF << 4)
#define AUXDISP_IN_FMT_BPP_8BIT     (0x0 << 4)
#define AUXDISP_IN_FMT_BPP_10BIT    (0x1 << 4)
#define AUXDISP_IN_FMT_BPP_565      (0x2 << 4)

#define AUXDISP_IN_FMT_REMAP_SHIFT  8
#define AUXDISP_IN_FMT_REMAP_MASK   (0xF << 8)
#define AUXDISP_IN_FMT_REMAP_B_LSB  (0x0 << 8)  /* B in LSB (默认) */
#define AUXDISP_IN_FMT_REMAP_R_LSB  (0x1 << 8)  /* R in LSB */

/* AUXDISP_ABNORMAL_CTRL 位定义 */
#define AUXDISP_ABNORMAL_STOP       BIT(0)  /* 停止 AXI 读取 */
#define AUXDISP_ABNORMAL_RESET      BIT(1)  /* 复位 AXI */

static void auxdisp_set_display_path(struct drm_crtc *crtc, struct drm_atomic_state *state);

/* 时序参数缓存结构 */
struct auxdisp_timing {
    /* 时序参数 */
    u32 htotal, vtotal;
    u32 hactive, vactive;
    u32 hsw, hbp, hfp;
    u32 vsw, vbp, vfp;
    u32 polarity;
    unsigned long pixel_clock_hz;
    bool valid;                  /* 时序参数是否有效 */

    /* Framebuffer 配置参数（在 mode_set_nofb 中从 crtc->primary 获取） */
    bool fb_configured;          /* FB 是否已配置 */
    u32 fb_addr;                 /* Y/RGB 基地址 */
    u32 fb_addr_uv;              /* UV 基地址（YUV420） */
    u32 fb_width;                /* FB 宽度 */
    u32 fb_height;               /* FB 高度 */
    u32 fb_stride;               /* Y/RGB stride */
    u32 fb_stride_uv;            /* UV stride */
    u32 fb_in_fmt;               /* 输入格式 */
    u32 fb_out_fmt;              /* 输出格式（AUXDISP_MODE 寄存器的格式字段） */
    u32 fb_axibeat;              /* AXI beats */
    bool fb_has_uv;              /* 是否有 UV 平面 */
};

/* 设备私有数据结构 */
struct auxdisp_crtc {
    struct drm_crtc crtc;        /* 嵌入 CRTC 结构体 */
    struct drm_plane primary_plane; /* 嵌入 Plane 结构体 */

    struct drm_device *drm_dev;
    void __iomem *regs;          /* 硬件寄存器基地址 */
    struct device *dev;          /* 平台设备 */
    int irq;                     /* 中断号 */
    bool bound;                  /* 组件绑定状态 */
    bool display_enabled;        /* 显示输出是否已启用 */

    /* 时钟资源 */
    struct clk *aclk;            /* AXI 总线时钟 */
    struct clk *pclk;            /* APB 配置时钟 */
    struct clk *pixclk;          /* 像素时钟 */
    struct clk *hdmi_pixclk;     /* HDMI 输出像素时钟 */
    struct clk *mipi_pixclk;     /* MIPI 输出像素时钟 */
    struct clk *dptx_pixclk;     /* DPTX 输出像素时钟 */

    /* 当前显示路径配置 */
    u32 current_interface;       /* 当前接口类型: HDMI/DSI/DP */

    /* 缓存的时序参数（在 pclk 使能后才写入硬件） */
    struct auxdisp_timing pending_timing;

    /* 互斥锁保护共享数据（pending_timing, display_enabled, current_interface） */
    struct mutex config_lock;
};

static inline struct auxdisp_crtc *to_auxdisp_crtc(struct drm_crtc *crtc)
{
    return container_of(crtc, struct auxdisp_crtc, crtc);
}

static inline struct auxdisp_crtc *to_auxdisp_plane(struct drm_plane *plane)
{
    return container_of(plane, struct auxdisp_crtc, primary_plane);
}

static const uint32_t auxdisp_primary_plane_formats[] = {
    /* RGB 8bit 格式 */
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_XBGR8888,
    DRM_FORMAT_ABGR8888,

    /* RGB 565 格式 */
    DRM_FORMAT_RGB565,
    DRM_FORMAT_BGR565,

    /* RGB 10bit 格式 */
    DRM_FORMAT_XRGB2101010,
    DRM_FORMAT_ARGB2101010,
    DRM_FORMAT_XBGR2101010,
    DRM_FORMAT_ABGR2101010,

    /* YUV420 8bit 格式 */
    DRM_FORMAT_NV12,

    /* YUV420 10bit 格式 */
    DRM_FORMAT_P010,
};

static inline void auxdisp_write(struct auxdisp_crtc *auxdisp, u32 reg, u32 value)
{
    writel(value, auxdisp->regs + reg);
}

static inline u32 auxdisp_read(struct auxdisp_crtc *auxdisp, u32 reg)
{
    return readl(auxdisp->regs + reg);
}

static irqreturn_t auxdisp_irq_handler(int irq, void *data)
{
    struct auxdisp_crtc *auxdisp = data;
    u32 int_status;
    u32 handled_irqs = 0;

    /* 读取中断状态 */
    int_status = auxdisp_read(auxdisp, AUXDISP_INT_STATUS);

    if (!int_status)
        return IRQ_NONE;

    if (int_status & AUXDISP_INT_VSYNC) {
        drm_crtc_handle_vblank(&auxdisp->crtc);
        handled_irqs |= AUXDISP_INT_VSYNC;
    }

    if (int_status & AUXDISP_INT_NEXT_CFG) {
        handled_irqs |= AUXDISP_INT_NEXT_CFG;
    }

    if (int_status & AUXDISP_INT_UNDERFLOW) {
        u32 int_raw = auxdisp_read(auxdisp, AUXDISP_INT_RAW);
        dev_err(auxdisp->dev, "[IRQ] *** UNDERFLOW interrupt detected! ***\n");
        dev_err(auxdisp->dev, "This indicates AXI bandwidth is insufficient!\n");
        dev_err(auxdisp->dev, "INT_RAW=0x%08x, INT_STATUS=0x%08x\n", int_raw, int_status);
        handled_irqs |= AUXDISP_INT_UNDERFLOW;
    }

    if (int_status & AUXDISP_INT_AXI_ERR) {
        u32 int_raw = auxdisp_read(auxdisp, AUXDISP_INT_RAW);
        dev_err(auxdisp->dev, "[IRQ] *** AXI read response ERROR interrupt! ***\n");
        dev_err(auxdisp->dev, "AXI bus error detected - check memory address and bus status\n");
        dev_err(auxdisp->dev, "INT_RAW=0x%08x, INT_STATUS=0x%08x\n", int_raw, int_status);
        handled_irqs |= AUXDISP_INT_AXI_ERR;
    }

    if (int_status & AUXDISP_INT_FRAME_DONE) {
        handled_irqs |= AUXDISP_INT_FRAME_DONE;
    }

    auxdisp_write(auxdisp, AUXDISP_INT_CLEAR, int_status);

    return IRQ_HANDLED;
}

static int auxdisp_get_fb_format_config(u32 format, u32 width, u32 *in_fmt, u32 *out_fmt, u32 *axibeat)
{
    switch (format) {
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_ABGR8888:
        *in_fmt = AUXDISP_IN_FMT_FMT_RGB | AUXDISP_IN_FMT_BPP_8BIT | AUXDISP_IN_FMT_REMAP_R_LSB;
        *out_fmt = AUXDISP_MODE_FMT_RGB8;
        *axibeat = (width + 3) / 4;
        break;

    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ARGB8888:
        *in_fmt = AUXDISP_IN_FMT_FMT_RGB | AUXDISP_IN_FMT_BPP_8BIT | AUXDISP_IN_FMT_REMAP_B_LSB;
        *out_fmt = AUXDISP_MODE_FMT_RGB8;
        *axibeat = (width + 3) / 4;
        break;

    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_ABGR2101010:
        *in_fmt = AUXDISP_IN_FMT_FMT_RGB | AUXDISP_IN_FMT_BPP_10BIT | AUXDISP_IN_FMT_REMAP_R_LSB;
        *out_fmt = AUXDISP_MODE_FMT_RGB10;
        *axibeat = (width + 3) / 4;
        break;

    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_ARGB2101010:
        *in_fmt = AUXDISP_IN_FMT_FMT_RGB | AUXDISP_IN_FMT_BPP_10BIT | AUXDISP_IN_FMT_REMAP_B_LSB;
        *out_fmt = AUXDISP_MODE_FMT_RGB10;
        *axibeat = (width + 3) / 4;
        break;

    case DRM_FORMAT_RGB565:
        *in_fmt = AUXDISP_IN_FMT_FMT_RGB | AUXDISP_IN_FMT_BPP_565 | AUXDISP_IN_FMT_REMAP_B_LSB;
        *out_fmt = AUXDISP_MODE_FMT_RGB565;
        *axibeat = (width + 7) / 8;
        break;

    case DRM_FORMAT_BGR565:
        *in_fmt = AUXDISP_IN_FMT_FMT_RGB | AUXDISP_IN_FMT_BPP_565 | AUXDISP_IN_FMT_REMAP_R_LSB;
        *out_fmt = AUXDISP_MODE_FMT_RGB565;
        *axibeat = (width + 7) / 8;
        break;

    case DRM_FORMAT_NV12:
        *in_fmt = AUXDISP_IN_FMT_FMT_YUV420 | AUXDISP_IN_FMT_BPP_8BIT | AUXDISP_IN_FMT_REMAP_R_LSB;
        *out_fmt = AUXDISP_MODE_FMT_YUV420_8;
        *axibeat = (width + 15) / 16;
        break;

    case DRM_FORMAT_P010:
        *in_fmt = AUXDISP_IN_FMT_FMT_YUV420 | AUXDISP_IN_FMT_BPP_10BIT | AUXDISP_IN_FMT_REMAP_B_LSB;
        *out_fmt = AUXDISP_MODE_FMT_YUV420_10;
        *axibeat = (width + 11) / 12;
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

static void auxdisp_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_crtc(crtc);
    struct drm_display_mode *mode = &crtc->state->adjusted_mode;
    struct drm_encoder *encoder;
    struct drm_plane *primary;
    struct drm_framebuffer *fb;
    struct vs_gem_object *vs_obj;
    struct vs_gem_object *vs_obj_uv;
    dma_addr_t dma_addr;
    dma_addr_t dma_addr_uv;
    u32 htotal, vtotal, hactive, vactive;
    u32 hsw, hbp, hfp, vsw, vbp, vfp;
    u32 polarity = 0;
    u32 width, height, stride;
    u32 in_fmt, axibeat, out_fmt;
    u32 stride_uv;
    unsigned long pixel_clock_hz = mode->clock * 1000;

    auxdisp->current_interface = AUXDISP_MODE_IF_HDMI;
    drm_for_each_encoder_mask(encoder, auxdisp->drm_dev,
                              auxdisp->crtc.state->encoder_mask) {
        switch (encoder->encoder_type) {
        case DRM_MODE_ENCODER_TMDS:
            auxdisp->current_interface = AUXDISP_MODE_IF_HDMI;
            break;
        case DRM_MODE_ENCODER_DSI:
            auxdisp->current_interface = AUXDISP_MODE_IF_DSI;
            break;
        case DRM_MODE_ENCODER_DPMST:
            auxdisp->current_interface = AUXDISP_MODE_IF_DP;
            break;
        default:
            break;
        }
        break;
    }

    htotal = mode->htotal;
    vtotal = mode->vtotal;
    hactive = mode->hdisplay;
    vactive = mode->vdisplay;

    hsw = mode->hsync_end - mode->hsync_start;
    hbp = mode->htotal - mode->hsync_end;
    hfp = mode->hsync_start - mode->hdisplay;

    vsw = mode->vsync_end - mode->vsync_start;
    vbp = mode->vtotal - mode->vsync_end;
    vfp = mode->vsync_start - mode->vdisplay;

    if (mode->flags & DRM_MODE_FLAG_PVSYNC)
        polarity |= AUXDISP_POL_VSYNC_HIGH;
    if (mode->flags & DRM_MODE_FLAG_PHSYNC)
        polarity |= AUXDISP_POL_HSYNC_HIGH;
    polarity |= AUXDISP_POL_DE_HIGH;

    auxdisp->pending_timing.htotal = htotal;
    auxdisp->pending_timing.vtotal = vtotal;
    auxdisp->pending_timing.hactive = hactive;
    auxdisp->pending_timing.vactive = vactive;
    auxdisp->pending_timing.hsw = hsw;
    auxdisp->pending_timing.hbp = hbp;
    auxdisp->pending_timing.hfp = hfp;
    auxdisp->pending_timing.vsw = vsw;
    auxdisp->pending_timing.vbp = vbp;
    auxdisp->pending_timing.vfp = vfp;
    auxdisp->pending_timing.polarity = polarity;
    auxdisp->pending_timing.pixel_clock_hz = pixel_clock_hz;
    auxdisp->pending_timing.valid = true;

    primary = crtc->primary;
    if (primary && primary->state && primary->state->fb) {
        fb = primary->state->fb;

        vs_obj = vs_fb_get_gem_obj(fb, 0);
        if (!vs_obj) {
            auxdisp->pending_timing.fb_configured = false;
        } else {
            dma_addr = vs_obj->iova + fb->offsets[0];
            width = fb->width;
            height = fb->height;
            stride = fb->pitches[0];

            if (auxdisp_get_fb_format_config(fb->format->format, width,
                                            &in_fmt, &out_fmt, &axibeat) != 0) {
                auxdisp->pending_timing.fb_configured = false;
                return;
            }

            auxdisp->pending_timing.fb_configured = true;
            auxdisp->pending_timing.fb_addr = (u32)dma_addr;
            auxdisp->pending_timing.fb_width = width;
            auxdisp->pending_timing.fb_height = height;
            auxdisp->pending_timing.fb_stride = stride;
            auxdisp->pending_timing.fb_in_fmt = in_fmt;
            auxdisp->pending_timing.fb_out_fmt = out_fmt;
            auxdisp->pending_timing.fb_axibeat = axibeat;

            /* YUV420 格式需要特殊处理像素时钟*/
            if (out_fmt == AUXDISP_MODE_FMT_YUV420_8 || out_fmt == AUXDISP_MODE_FMT_YUV420_10) {
                auxdisp->pending_timing.pixel_clock_hz = pixel_clock_hz / 2;
            }

            if (fb->format->num_planes > 1) {
                vs_obj_uv = vs_fb_get_gem_obj(fb, 1);
                if (!vs_obj_uv) {
                    auxdisp->pending_timing.fb_has_uv = false;
                } else {
                    dma_addr_uv = vs_obj_uv->iova + fb->offsets[1];
                    stride_uv = fb->pitches[1];

                    auxdisp->pending_timing.fb_addr_uv = (u32)dma_addr_uv;
                    auxdisp->pending_timing.fb_stride_uv = stride_uv;
                    auxdisp->pending_timing.fb_has_uv = true;
                }
            } else {
                auxdisp->pending_timing.fb_has_uv = false;
            }
        }
    } else {
        auxdisp->pending_timing.fb_configured = false;
    }
}

static int auxdisp_wait_cfg_done(struct auxdisp_crtc *auxdisp)
{
    int timeout = 1000;
    u32 cfg_status;

    while (timeout-- > 0) {
        cfg_status = auxdisp_read(auxdisp, AUXDISP_CFG_DONE);
        if (cfg_status == 0) {
            return 0;
        }
        usleep_range(100, 200);
    }

    return -ETIMEDOUT;
}

static void auxdisp_configure_timing(struct auxdisp_crtc *auxdisp)
{
    u32 display_mode;

    if (!auxdisp->pending_timing.valid)
        return;

    auxdisp_write(auxdisp, AUXDISP_DTG0,
                 (auxdisp->pending_timing.vtotal << 16) | auxdisp->pending_timing.htotal);
    auxdisp_write(auxdisp, AUXDISP_DTG1,
                 (auxdisp->pending_timing.vactive << 16) | auxdisp->pending_timing.hactive);
    auxdisp_write(auxdisp, AUXDISP_DTG2,
                 (auxdisp->pending_timing.hbp << 16) | auxdisp->pending_timing.hsw);
    auxdisp_write(auxdisp, AUXDISP_DTG3, auxdisp->pending_timing.hfp);
    auxdisp_write(auxdisp, AUXDISP_DTG4,
                 (auxdisp->pending_timing.vbp << 16) | auxdisp->pending_timing.vsw);
    auxdisp_write(auxdisp, AUXDISP_DTG5, auxdisp->pending_timing.vfp);

    auxdisp_write(auxdisp, AUXDISP_POLARITY, auxdisp->pending_timing.polarity);

    display_mode = auxdisp->current_interface;

    if (auxdisp->pending_timing.fb_configured)
        display_mode |= auxdisp->pending_timing.fb_out_fmt;
    else
        display_mode |= AUXDISP_MODE_FMT_RGB8;

    auxdisp_write(auxdisp, AUXDISP_MODE, display_mode);

    auxdisp_write(auxdisp, AUXDISP_NEXT_CFG, 0x10);
}

static int auxdisp_configure_clocks(struct auxdisp_crtc *auxdisp, unsigned long pixel_clock_hz)
{
    int ret;

    ret = clk_set_rate(auxdisp->pixclk, pixel_clock_hz);
    if (ret) {
        dev_err(auxdisp->dev, "Failed to set pixel clock rate: %d\n", ret);
        return ret;
    }

    ret = clk_prepare_enable(auxdisp->pixclk);
    if (ret) {
        dev_err(auxdisp->dev, "Failed to enable pixclk: %d\n", ret);
        return ret;
    }

    return 0;
}

static void auxdisp_configure_framebuffer(struct auxdisp_crtc *auxdisp)
{
    if (!auxdisp->pending_timing.fb_configured) {
        return;
    }

    auxdisp_write(auxdisp, AUXDISP_IN_FMT, auxdisp->pending_timing.fb_in_fmt);
    auxdisp_write(auxdisp, AUXDISP_RES,
                 (auxdisp->pending_timing.fb_height << 16) | auxdisp->pending_timing.fb_width);
    auxdisp_write(auxdisp, AUXDISP_ADDR0, auxdisp->pending_timing.fb_addr);
    auxdisp_write(auxdisp, AUXDISP_STRIDE0, auxdisp->pending_timing.fb_stride);
    auxdisp_write(auxdisp, AUXDISP_BEAT, auxdisp->pending_timing.fb_axibeat);
    auxdisp_write(auxdisp, AUXDISP_OFFSET, 0);

    if (auxdisp->pending_timing.fb_has_uv) {
        auxdisp_write(auxdisp, AUXDISP_ADDR1, auxdisp->pending_timing.fb_addr_uv);
        auxdisp_write(auxdisp, AUXDISP_STRIDE1, auxdisp->pending_timing.fb_stride_uv);
    }
}

static void auxdisp_configure_axi(struct auxdisp_crtc *auxdisp)
{
    u32 axi_cfg;
    u32 pixel_count = auxdisp->pending_timing.hactive * auxdisp->pending_timing.vactive;

    if (pixel_count >= (3840 * 2160)) {
        axi_cfg = 0x10FFF00;
    } else if (pixel_count >= (1920 * 1080)) {
        axi_cfg = 0x10CC000;
    } else {
        axi_cfg = 0x808000;
    }

    auxdisp_write(auxdisp, AUXDISP_AXI_CFG, axi_cfg);
}

static void auxdisp_configure_interrupts(struct auxdisp_crtc *auxdisp)
{
    u32 int_enable_mask = AUXDISP_INT_ALL_KNOWN;

    auxdisp_write(auxdisp, AUXDISP_INT_CLEAR, 0xFFFFFFFF);

    auxdisp_write(auxdisp, AUXDISP_INT_ENABLE, int_enable_mask);
}

static void auxdisp_crtc_enable(struct drm_crtc *crtc,
                               struct drm_atomic_state *state)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_crtc(crtc);
    int ret;

    if (!auxdisp) {
        dev_err(auxdisp->dev, "ERROR: auxdisp pointer is NULL!\n");
        return;
    }

    if (!auxdisp->aclk) {
        dev_err(auxdisp->dev, "ERROR: auxdisp->aclk is NULL!\n");
        return;
    }

    ret = pm_runtime_get_sync(auxdisp->dev);
    if (ret < 0) {
        dev_err(auxdisp->dev, "Failed to resume device via runtime PM: %d\n", ret);
        pm_runtime_put_noidle(auxdisp->dev);
        return;
    }

    auxdisp_configure_timing(auxdisp);

    ret = auxdisp_configure_clocks(auxdisp, auxdisp->pending_timing.pixel_clock_hz);
    if (ret) {
        pm_runtime_put(auxdisp->dev);
        return;
    }

    auxdisp_configure_framebuffer(auxdisp);

    auxdisp_configure_axi(auxdisp);

    auxdisp_configure_interrupts(auxdisp);

    drm_crtc_vblank_on(crtc);

    auxdisp_write(auxdisp, AUXDISP_CFG_DONE, 1);

    auxdisp_write(auxdisp, AUXDISP_EN, AUXDISP_EN_ENABLE);
    auxdisp->display_enabled = true;

    auxdisp_wait_cfg_done(auxdisp);
}

static void auxdisp_crtc_disable(struct drm_crtc *crtc,
                                struct drm_atomic_state *state)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_crtc(crtc);
    u32 idle_status;
    int timeout = 100;

    auxdisp_write(auxdisp, AUXDISP_INT_ENABLE, 0);

    auxdisp_write(auxdisp, AUXDISP_EN, 0);
    auxdisp->display_enabled = false;

    auxdisp_write(auxdisp, AUXDISP_ABNORMAL_CTRL, AUXDISP_ABNORMAL_STOP);

    while (timeout--) {
        idle_status = auxdisp_read(auxdisp, AUXDISP_IDLE);
        if (idle_status & 0x1) {
            break;
        }
        usleep_range(100, 200);
    }

    auxdisp_write(auxdisp, AUXDISP_ABNORMAL_CTRL, 0);

    auxdisp_write(auxdisp, AUXDISP_ABNORMAL_CTRL, AUXDISP_ABNORMAL_RESET);
    usleep_range(10, 20);
    auxdisp_write(auxdisp, AUXDISP_ABNORMAL_CTRL, 0);

    auxdisp_write(auxdisp, AUXDISP_INT_CLEAR, 0xFFFFFFFF);

    auxdisp_write(auxdisp, AUXDISP_CFG_DONE, 1);

    auxdisp->pending_timing.valid = false;
    auxdisp->pending_timing.fb_configured = false;

    drm_crtc_vblank_off(crtc);

    if (__clk_is_enabled(auxdisp->pixclk))
        clk_disable_unprepare(auxdisp->pixclk);

    spin_lock_irq(&crtc->dev->event_lock);
    if (crtc->state->event && !crtc->state->active) {
        drm_crtc_send_vblank_event(crtc, crtc->state->event);
        crtc->state->event = NULL;
    }
    spin_unlock_irq(&crtc->dev->event_lock);
}

static void auxdisp_set_display_path(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_crtc(crtc);
    struct clk *output_clk = NULL;
    const char *if_name = NULL;
    int ret;

    switch (auxdisp->current_interface) {
        case AUXDISP_MODE_IF_HDMI:
            output_clk = auxdisp->hdmi_pixclk;
            if_name = "HDMI";
            break;
        case AUXDISP_MODE_IF_DSI:
            output_clk = auxdisp->mipi_pixclk;
            if_name = "MIPI DSI";
            break;
        case AUXDISP_MODE_IF_DP:
            output_clk = auxdisp->dptx_pixclk;
            if_name = "DP";
            break;
        default:
            return;
    }

    if (!output_clk) {
        return;
    }

    ret = clk_set_parent(output_clk, auxdisp->pixclk);
    if (ret) {
        dev_err(auxdisp->dev, "Failed to set %s clock parent: %d\n", if_name, ret);;
    }
}

static int auxdisp_crtc_enable_vblank(struct drm_crtc *crtc)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_crtc(crtc);
    u32 int_enable_mask = AUXDISP_INT_ALL_KNOWN;

    auxdisp_write(auxdisp, AUXDISP_INT_ENABLE, int_enable_mask);

    return 0;
}

static void auxdisp_crtc_disable_vblank(struct drm_crtc *crtc)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_crtc(crtc);

    auxdisp_write(auxdisp, AUXDISP_INT_ENABLE, 0);
}

static void auxdisp_crtc_atomic_enable(struct drm_crtc *crtc,
                                      struct drm_atomic_state *state)
{
    struct drm_device *drm_dev = crtc->dev;
    struct vs_drm_private *priv = drm_dev->dev_private;

    auxdisp_crtc_enable(crtc, state);

    auxdisp_set_display_path(crtc, state);

    if (priv->dc_clk_held_for_auxdisp && priv->dc_dev) {
        priv->dc_clk_held_for_auxdisp = false;
        pm_runtime_put(priv->dc_dev);
    }
}

static void auxdisp_crtc_atomic_disable(struct drm_crtc *crtc,
                                       struct drm_atomic_state *state)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_crtc(crtc);
    struct drm_device *drm_dev = crtc->dev;
    struct vs_drm_private *priv = drm_dev->dev_private;
    struct drm_crtc *other_crtc;
    struct drm_crtc_state *old_crtc_state, *new_crtc_state;
    bool dc8200_enabling = false;
    int i;

    for_each_oldnew_crtc_in_state(state, other_crtc, old_crtc_state, new_crtc_state, i) {
        if (other_crtc == crtc)
            continue;

        if (other_crtc->port && other_crtc->port->parent &&
            of_device_is_compatible(other_crtc->port->parent, VS_DC8200_COMPATIBLE)) {

            if (!old_crtc_state->active && new_crtc_state->active) {
                dc8200_enabling = true;
                break;
            }
        }
    }

    auxdisp_crtc_disable(crtc, state);

    if (dc8200_enabling) {
        priv->auxdisp_clk_held_for_dc = true;
    }else{
        pm_runtime_put(auxdisp->dev);
    }
}

static void auxdisp_crtc_atomic_flush(struct drm_crtc *crtc,
                                     struct drm_atomic_state *state)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_crtc(crtc);
    struct drm_pending_vblank_event *event = crtc->state->event;
    struct drm_plane *plane = crtc->primary;
    bool has_fb = false;
    bool display_enabled;

    if (plane && plane->state && plane->state->fb) {
        has_fb = true;
    }

    display_enabled = auxdisp->display_enabled;

    if (has_fb) {
        /* Re-enable display if it was previously disabled */

        auxdisp_write(auxdisp, AUXDISP_CFG_DONE, 1);
        if (!display_enabled) {
            auxdisp_write(auxdisp, AUXDISP_EN, AUXDISP_EN_ENABLE);
            auxdisp->display_enabled = true;
        }
        auxdisp_wait_cfg_done(auxdisp);

        if (event) {
            spin_lock_irq(&crtc->dev->event_lock);
            drm_crtc_send_vblank_event(crtc, event);
            spin_unlock_irq(&crtc->dev->event_lock);

            crtc->state->event = NULL;
        }
    } else {
        auxdisp_write(auxdisp, AUXDISP_EN, 0);
        auxdisp->display_enabled = false;
        auxdisp_write(auxdisp, AUXDISP_CFG_DONE, 1);

        if (event) {
            spin_lock_irq(&crtc->dev->event_lock);
            drm_crtc_send_vblank_event(crtc, event);
            spin_unlock_irq(&crtc->dev->event_lock);

            crtc->state->event = NULL;
        }
    }

}

static const struct drm_crtc_helper_funcs auxdisp_crtc_helper_funcs = {
    .atomic_enable = auxdisp_crtc_atomic_enable,
    .atomic_disable = auxdisp_crtc_atomic_disable,
    .atomic_flush = auxdisp_crtc_atomic_flush,
    .mode_set_nofb = auxdisp_crtc_mode_set_nofb,
};

static const struct drm_crtc_funcs auxdisp_crtc_funcs = {
    .reset = drm_atomic_helper_crtc_reset,
    .destroy = drm_crtc_cleanup,
    .set_config = drm_atomic_helper_set_config,
    .page_flip = drm_atomic_helper_page_flip,
    .atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
    .atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
    .enable_vblank = auxdisp_crtc_enable_vblank,
    .disable_vblank = auxdisp_crtc_disable_vblank,
};

static int auxdisp_plane_atomic_check(struct drm_plane *plane,
                                     struct drm_atomic_state *state)
{
    struct auxdisp_crtc *auxdisp = to_auxdisp_plane(plane);
    struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
    struct drm_crtc_state *new_crtc_state;
    int ret;


    if (!new_plane_state->crtc)
        return 0;

    new_crtc_state = drm_atomic_get_new_crtc_state(state, new_plane_state->crtc);

    ret = drm_atomic_helper_check_plane_state(new_plane_state, new_crtc_state,
                                               DRM_PLANE_NO_SCALING,
                                               DRM_PLANE_NO_SCALING,
                                               true, true);
    if (ret) {
        dev_err(auxdisp->dev, "Plane state check failed: %d\n", ret);
        goto end;
    }

    if (new_plane_state->fb && new_crtc_state) {
        struct drm_framebuffer *fb = new_plane_state->fb;
        struct drm_display_mode *mode = &new_crtc_state->mode;
        int refresh_rate = drm_mode_vrefresh(mode);

        /* Check if FB is too small for the display mode */
        if (fb->width < mode->hdisplay || fb->height < mode->vdisplay) {
            dev_err(auxdisp->dev, "FB too small: fb=%ux%u, mode=%ux%u\n",
                   fb->width, fb->height,
                   mode->hdisplay, mode->vdisplay);
            ret = -EINVAL;
            goto end;
        }

        /* Warn if FB is larger than mode (will use top-left portion) */
        if (fb->width > mode->hdisplay || fb->height > mode->vdisplay) {
            dev_warn_once(auxdisp->dev,
                         "FB larger than mode: fb=%ux%u, mode=%ux%u (using top-left crop)\n",
                         fb->width, fb->height,
                         mode->hdisplay, mode->vdisplay);
        }

        /* NV12 format does not support resolutions > 4K@30 */
        if (fb->format->format == DRM_FORMAT_NV12 && refresh_rate > 30) {
            if (mode->hdisplay >= 3840 || mode->vdisplay >= 2160) {
                dev_err(auxdisp->dev, "NV12 format does not support %dHz at %ux%u (max resolution at 30Hz: 3840x2160)\n",
                       refresh_rate, mode->hdisplay, mode->vdisplay);
                ret = -EINVAL;
                goto end;
            }
        }
    }

end:
    return ret;
}

static void auxdisp_plane_atomic_update(struct drm_plane *plane,
                                       struct drm_atomic_state *state)
{
    struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state, plane);
    struct auxdisp_crtc *auxdisp = to_auxdisp_plane(plane);
    struct drm_framebuffer *fb = new_state->fb;
    struct vs_gem_object *vs_obj;
    struct vs_gem_object *vs_obj_uv;
    dma_addr_t dma_addr;
    dma_addr_t dma_addr_uv;
    u32 width, height, stride;
    u32 in_fmt, axibeat, out_fmt;
    u32 stride_uv;
    bool initial_config_done;

    if (!fb || !new_state->crtc)
        return;

    mutex_lock(&auxdisp->config_lock); 
    initial_config_done = auxdisp->pending_timing.fb_configured &&
                          (auxdisp->pending_timing.fb_width == fb->width) &&
                          (auxdisp->pending_timing.fb_height == fb->height) &&
                          (auxdisp->pending_timing.fb_stride == fb->pitches[0]);

    if (initial_config_done) {
        vs_obj = vs_fb_get_gem_obj(fb, 0);
        if (!vs_obj) {
            dev_err(auxdisp->dev, "Failed to get GEM object\n");
            goto end;
        }
        dma_addr = vs_obj->iova + fb->offsets[0];

        auxdisp_write(auxdisp, AUXDISP_ADDR0, (u32)dma_addr);

        if (fb->format->num_planes > 1) {
            vs_obj_uv = vs_fb_get_gem_obj(fb, 1);
            if (vs_obj_uv) {
                dma_addr_uv = vs_obj_uv->iova + fb->offsets[1];
                auxdisp_write(auxdisp, AUXDISP_ADDR1, (u32)dma_addr_uv);
            }
        }

        auxdisp->pending_timing.fb_addr = (u32)dma_addr;
        goto end;
    }

    vs_obj = vs_fb_get_gem_obj(fb, 0);
    if (!vs_obj) {
        dev_err(auxdisp->dev, "Failed to get GEM object\n");
        goto end;
    }
    dma_addr = vs_obj->iova + fb->offsets[0];

    width = fb->width;
    height = fb->height;
    stride = fb->pitches[0];

    if (auxdisp_get_fb_format_config(fb->format->format, width,
                                     &in_fmt, &out_fmt, &axibeat) != 0) {
        dev_err(auxdisp->dev, "Unsupported pixel format: %c%c%c%c\n",
               fb->format->format & 0xff,
               (fb->format->format >> 8) & 0xff,
               (fb->format->format >> 16) & 0xff,
               (fb->format->format >> 24) & 0xff);

        goto end;
    }

    auxdisp_write(auxdisp, AUXDISP_IN_FMT, in_fmt);
    auxdisp_write(auxdisp, AUXDISP_RES, (height << 16) | width);
    auxdisp_write(auxdisp, AUXDISP_ADDR0, (u32)dma_addr);
    auxdisp_write(auxdisp, AUXDISP_STRIDE0, stride);
    auxdisp_write(auxdisp, AUXDISP_BEAT, axibeat);
    auxdisp_write(auxdisp, AUXDISP_OFFSET, 0);

    if (fb->format->num_planes > 1) {
        vs_obj_uv = vs_fb_get_gem_obj(fb, 1);
        if (!vs_obj_uv) {
            dev_err(auxdisp->dev, "Failed to get GEM object for UV plane\n");
            goto end;
        }
        dma_addr_uv = vs_obj_uv->iova + fb->offsets[1];
        stride_uv = fb->pitches[1];

        auxdisp_write(auxdisp, AUXDISP_ADDR1, (u32)dma_addr_uv);
        auxdisp_write(auxdisp, AUXDISP_STRIDE1, stride_uv);
    }

end:
    mutex_unlock(&auxdisp->config_lock); 
}

static const struct drm_plane_helper_funcs auxdisp_plane_helper_funcs = {
    .atomic_check = auxdisp_plane_atomic_check,
    .atomic_update = auxdisp_plane_atomic_update,
};

static const struct drm_plane_funcs auxdisp_plane_funcs = {
    .update_plane = drm_atomic_helper_update_plane,
    .disable_plane = drm_atomic_helper_disable_plane,
    .destroy = drm_plane_cleanup,
    .reset = drm_atomic_helper_plane_reset,
    .atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
    .atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static int auxdisp_crtc_bind(struct device *dev, struct device *master, void *data)
{
    struct drm_device *drm_dev = data;
    struct vs_drm_private *priv = drm_dev->dev_private;
    struct auxdisp_crtc *auxdisp = dev_get_drvdata(dev);
    struct device_node *port;
    int ret;

    priv->auxdisp_dev = dev;

    auxdisp->drm_dev = drm_dev;

    ret = drm_universal_plane_init(drm_dev, &auxdisp->primary_plane, 0,
                                  &auxdisp_plane_funcs,
                                  auxdisp_primary_plane_formats,
                                  ARRAY_SIZE(auxdisp_primary_plane_formats),
                                  NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
    if (ret) {
        dev_err(auxdisp->dev, "Failed to create primary plane: %d\n", ret);
        goto err_cleanup;
    }

    drm_plane_helper_add(&auxdisp->primary_plane, &auxdisp_plane_helper_funcs);

    ret = drm_crtc_init_with_planes(drm_dev, &auxdisp->crtc, &auxdisp->primary_plane, NULL,
                                   &auxdisp_crtc_funcs, "AuxDisp");
    if (ret) {
        dev_err(auxdisp->dev, "Failed to create CRTC: %d\n", ret);
        goto err_cleanup_plane;
    }

    port = of_graph_get_port_by_id(dev->of_node, 0);
    if (port) {
        auxdisp->crtc.port = port;
    }

    drm_crtc_helper_add(&auxdisp->crtc, &auxdisp_crtc_helper_funcs);
    auxdisp->bound = true;

    return 0;

err_cleanup_plane:
    drm_plane_cleanup(&auxdisp->primary_plane);
err_cleanup:
    return ret;
}

static void auxdisp_crtc_unbind(struct device *dev, struct device *master, void *data)
{
    struct auxdisp_crtc *auxdisp = dev_get_drvdata(dev);

    if (!auxdisp->bound)
        return;

    mutex_lock(&auxdisp->config_lock); 
    if (auxdisp->crtc.port) {
        of_node_put(auxdisp->crtc.port);
        auxdisp->crtc.port = NULL;
    }

    auxdisp->bound = false;
    auxdisp->drm_dev = NULL;
    mutex_unlock(&auxdisp->config_lock); 
}

static const struct component_ops auxdisp_crtc_component_ops = {
    .bind = auxdisp_crtc_bind,
    .unbind = auxdisp_crtc_unbind,
};

static int auxdisp_crtc_probe(struct platform_device *pdev)
{
    struct auxdisp_crtc *auxdisp;
    struct resource *res;
    int ret;

    auxdisp = devm_kzalloc(&pdev->dev, sizeof(*auxdisp), GFP_KERNEL);
    if (!auxdisp)
        return -ENOMEM;

    auxdisp->dev = &pdev->dev;

    mutex_init(&auxdisp->config_lock);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        dev_err(auxdisp->dev, "Failed to get memory resource\n");
        return -ENXIO;
    }

    auxdisp->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(auxdisp->regs)) {
        ret = PTR_ERR(auxdisp->regs);
        dev_err(auxdisp->dev, "Failed to map registers: %d\n", ret);
        return ret;
    }

    auxdisp->irq = platform_get_irq(pdev, 0);
    if (auxdisp->irq < 0) {
        dev_err(&pdev->dev, "Failed to get IRQ: %d\n", auxdisp->irq);
        return auxdisp->irq;
    }

    ret = devm_request_irq(&pdev->dev, auxdisp->irq, auxdisp_irq_handler,
                          0, dev_name(&pdev->dev), auxdisp);
    if (ret < 0) {
        dev_err(&pdev->dev, "Failed to request IRQ %d: %d\n", auxdisp->irq, ret);
        return ret;
    }

    auxdisp->aclk = devm_clk_get(&pdev->dev, "aclk");
    if (IS_ERR(auxdisp->aclk)) {
        ret = PTR_ERR(auxdisp->aclk);
        dev_err(&pdev->dev, "Failed to get aclk: %d\n", ret);
        return ret;
    }

    auxdisp->pclk = devm_clk_get(&pdev->dev, "pclk");
    if (IS_ERR(auxdisp->pclk)) {
        ret = PTR_ERR(auxdisp->pclk);
        dev_err(&pdev->dev, "Failed to get pclk: %d\n", ret);
        return ret;
    }

    auxdisp->pixclk = devm_clk_get(&pdev->dev, "pixclk");
    if (IS_ERR(auxdisp->pixclk)) {
        ret = PTR_ERR(auxdisp->pixclk);
        dev_err(&pdev->dev, "Failed to get pixclk: %d\n", ret);
        return ret;
    }

    auxdisp->hdmi_pixclk = devm_clk_get_optional(&pdev->dev, "hdmi_pixclk");
    if (IS_ERR(auxdisp->hdmi_pixclk)) {
        ret = PTR_ERR(auxdisp->hdmi_pixclk);
        dev_err(&pdev->dev, "Failed to get hdmi_pixclk: %d\n", ret);
        return ret;
    }

    auxdisp->mipi_pixclk = devm_clk_get_optional(&pdev->dev, "mipi_pixclk");
    if (IS_ERR(auxdisp->mipi_pixclk)) {
        ret = PTR_ERR(auxdisp->mipi_pixclk);
        dev_err(&pdev->dev, "Failed to get mipi_pixclk: %d\n", ret);
        return ret;
    }

    auxdisp->dptx_pixclk = devm_clk_get_optional(&pdev->dev, "dptx_pixclk");
    if (IS_ERR(auxdisp->dptx_pixclk)) {
        ret = PTR_ERR(auxdisp->dptx_pixclk);
        dev_err(&pdev->dev, "Failed to get dptx_pixclk: %d\n", ret);
        return ret;
    }

    platform_set_drvdata(pdev, auxdisp);

    pm_runtime_enable(&pdev->dev);

    pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
    pm_runtime_use_autosuspend(&pdev->dev);

    ret = component_add(&pdev->dev, &auxdisp_crtc_component_ops);
    if (ret) {
        dev_err(auxdisp->dev, "Failed to add component: %d\n", ret);
        goto err_pm_disable;
    }

    return 0;

err_pm_disable:
    pm_runtime_disable(&pdev->dev);
    return ret;
}

static int auxdisp_crtc_remove(struct platform_device *pdev)
{
    component_del(&pdev->dev, &auxdisp_crtc_component_ops);

    pm_runtime_disable(&pdev->dev);

    if (!pm_runtime_status_suspended(&pdev->dev)) {
        pm_runtime_set_suspended(&pdev->dev);
    }

    return 0;
}

static const struct of_device_id auxdisp_crtc_dt_ids[] = {
    { .compatible = ZH_AUXDISP_COMPATIBLE },
    { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, auxdisp_crtc_dt_ids);

static int auxdisp_crtc_suspend(struct device *dev)
{
    struct auxdisp_crtc *auxdisp = dev_get_drvdata(dev);
    u32 idle_status;
    int timeout = 100;

    if (!auxdisp || !auxdisp->bound) {
        return 0;
    }

    if (!auxdisp->display_enabled) {
        return 0;
    }

    auxdisp->pending_timing.valid = false;

    if (!__clk_is_enabled(auxdisp->pclk)) {
        return 0;
    }

    auxdisp_write(auxdisp, AUXDISP_INT_ENABLE, 0);

    auxdisp_write(auxdisp, AUXDISP_EN, 0);
    auxdisp->display_enabled = false;

    auxdisp_write(auxdisp, AUXDISP_ABNORMAL_CTRL, AUXDISP_ABNORMAL_STOP);

    while (timeout--) {
        idle_status = auxdisp_read(auxdisp, AUXDISP_IDLE);
        if (idle_status & 0x1) {
            break;
        }
        usleep_range(100, 200);
    }

    auxdisp_write(auxdisp, AUXDISP_ABNORMAL_CTRL, 0);

    auxdisp_write(auxdisp, AUXDISP_ABNORMAL_CTRL, AUXDISP_ABNORMAL_RESET);
    usleep_range(10, 20);
    auxdisp_write(auxdisp, AUXDISP_ABNORMAL_CTRL, 0);

    auxdisp_write(auxdisp, AUXDISP_INT_CLEAR, 0xFFFFFFFF);

    if (__clk_is_enabled(auxdisp->pixclk))
        clk_disable_unprepare(auxdisp->pixclk);
    if (__clk_is_enabled(auxdisp->pclk))
        clk_disable_unprepare(auxdisp->pclk);
    if (__clk_is_enabled(auxdisp->aclk))
        clk_disable_unprepare(auxdisp->aclk);

    return 0;
}

static int auxdisp_crtc_resume(struct device *dev)
{
    struct auxdisp_crtc *auxdisp = dev_get_drvdata(dev);

    if (!auxdisp || !auxdisp->bound) {
        return 0;
    }

    return 0;
}

static int auxdisp_crtc_runtime_suspend(struct device *dev)
{
    struct auxdisp_crtc *auxdisp = dev_get_drvdata(dev);

    if (!auxdisp)
        return 0;

    clk_disable_unprepare(auxdisp->pclk);

    clk_disable_unprepare(auxdisp->aclk);

    return 0;
}

static int auxdisp_crtc_runtime_resume(struct device *dev)
{
    struct auxdisp_crtc *auxdisp = dev_get_drvdata(dev);
    int ret;

    if (!auxdisp)
        return 0;

    ret = clk_prepare_enable(auxdisp->aclk);
    if (ret) {
        dev_err(dev, "Failed to enable aclk in runtime resume: %d\n", ret);
        return ret;
    }

    ret = clk_prepare_enable(auxdisp->pclk);
    if (ret) {
        dev_err(dev, "Failed to enable pclk in runtime resume: %d\n", ret);
        clk_disable_unprepare(auxdisp->aclk);
        return ret;
    }

    return 0;
}
static const struct dev_pm_ops auxdisp_crtc_pm_ops = {
    .suspend = auxdisp_crtc_suspend,
    .resume = auxdisp_crtc_resume,
    .runtime_suspend = auxdisp_crtc_runtime_suspend,
    .runtime_resume = auxdisp_crtc_runtime_resume,
};

struct platform_driver auxdisp_crtc_driver = {
    .probe = auxdisp_crtc_probe,
    .remove = auxdisp_crtc_remove,
    .driver = {
        .name = "auxdisp",
        .of_match_table = auxdisp_crtc_dt_ids,
        .pm = &auxdisp_crtc_pm_ops,
    },
};

MODULE_AUTHOR("liqiang.zhang<zhanglq@zhihecomputing.com>");
MODULE_DESCRIPTION("AuxDisp Driver");
MODULE_LICENSE("GPL");
