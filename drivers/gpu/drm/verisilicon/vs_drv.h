/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DRV_H__
#define __VS_DRV_H__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#include <drm/drm_gem.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#include <drm/drm_device.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 5, 0)
#include <drm/drmP.h>
#endif

#include "vs_plane.h"
#ifdef CONFIG_VERISILICON_MMU
#include "vs_dc_mmu.h"
#endif


/*
 *
 * @dma_dev: device for DMA API.
 *  - use the first attached device if support iommu
    else use drm device (only contiguous buffer support)
 * @domain: iommu domain for DRM.
 *  - all DC IOMMU share same domain to reduce mapping
 * @pitch_alignment: buffer pitch alignment required by sub-devices.
 *
 */
struct vs_drm_private {
    struct device *dma_dev;
    struct iommu_domain *domain;
#ifdef CONFIG_VERISILICON_MMU
    dc_mmu *mmu;
#endif

    unsigned int pitch_alignment;

#ifdef CONFIG_ZHIHE_AUXDISP
    struct device *dc_dev;              /* DC8200 设备指针 */
    struct device *auxdisp_dev;         /* AuxDisp 设备指针 */
    bool dc_clk_held_for_auxdisp;       /* DC8200 时钟是否被 AuxDisp 持有 */
    bool auxdisp_clk_held_for_dc;       /* AuxDisp 时钟是否被 DC8200 持有 */
#endif
};

int vs_drm_iommu_attach_device(struct drm_device *drm_dev,
                   struct device *dev);

void vs_drm_iommu_detach_device(struct drm_device *drm_dev,
                struct device *dev);

void vs_drm_update_pitch_alignment(struct drm_device *drm_dev,
                   unsigned int alignment);

static inline struct device *to_dma_dev(struct drm_device *dev)
{
    struct vs_drm_private *priv = dev->dev_private;

    return priv->dma_dev;
}

static inline bool is_iommu_enabled(struct drm_device *dev)
{
    struct vs_drm_private *priv = dev->dev_private;

    return priv->domain != NULL ? true : false;
}

/* Device compatible strings */
#define VS_DC8200_COMPATIBLE    "verisilicon,dc8200"
#define ZH_AUXDISP_COMPATIBLE   "zhihe,auxdisp"

#endif /* __VS_DRV_H__ */
