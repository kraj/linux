/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 *    *** IMPORTANT ***
 * This file is not only included from C-code but also from devicetree source
 * files. As such this file MUST only contain comments and defines.
 *
 * Copyright (c) 2024 Dong Yan <yand@zhcomputing.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _ZH_IOMMU_H
#define _ZH_IOMMU_H

#define DIE0    0x0 << 6
#define DIE1    0x1 << 6
#define DIE2    0x2 << 6
#define DIE3    0x3 << 6

/* PERI1_SS */
#define DEVID_DIE0_IOMMU_PTW 0x0
#define DEVID_DIE0_PERI1_DFMU    0x1
#define DEVID_DIE0_DMAC_AP       0x2
#define DEVID_DIE0_GMAC_0        0x3
#define DEVID_DIE0_GMAC_1        0x4
#define DEVID_DIE0_GMAC_2        0x5
#define DEVID_DIE0_SD            0x6
#define DEVID_DIE0_EMMC          0x8

/* USB_SS */
#define DEVID_DIE0_USB_DFMU      0x9
#define DEVID_DIE0_USB3_0        0xa
#define DEVID_DIE0_USB2_0        0xb
#define DEVID_DIE0_USB2_1        0xc

/* PCIE_SS */
#define DEVID_DIE0_PCIE_DFMU     0x10
#define DEVID_DIE0_PCIE_0        0x11
#define DEVID_DIE0_PCIE_1        0x12
#define DEVID_DIE0_SATA_0        0x14
#define DEVID_DIE0_TEE_EIP120SI      0x16
#define DEVID_DIE0_TEE_EIP120SII     0x17
#define DEVID_DIE0_TEE_EIP120SIII    0x18
#define DEVID_DIE0_TEE_DMAC      0x19

/* VI_SS */
#define DEVID_DIE0_VI_DFMU       0x20
#define DEVID_DIE0_ISP           0x21
#define DEVID_DIE0_VIPRE         0x22
#define DEVID_DIE0_DW200         0x23

/* VP_SS */
#define DEVID_DIE0_VP_DFMU       0x25
#define DEVID_DIE0_VENC          0x26
#define DEVID_DIE0_VDEC          0x27
#define DEVID_DIE0_G2D           0x28

/* VO_SS */
#define DEVID_DIE0_VO_DFMU       0x2b
#define DEVID_DIE0_DISPLAY_0     0x2c
#define DEVID_DIE0_DISPLAY_1     0x2d
#define DEVID_DIE0_AUXDISP       0x2e

#define DEVID_DIE0_PIP_REC       0x2f

/* NPU_SS */
#define DEVID_DIE0_NPU_DFMU      0x30
#define DEVID_DIE0_NPU           0x31

/* PERI1_SS */
#define DEVID_DIE1_IOMMU_PTW     0x40
#define DEVID_DIE1_PERI1_DFMU    0x41
#define DEVID_DIE1_DMAC_AP       0x42
#define DEVID_DIE1_GMAC_0        0x43
#define DEVID_DIE1_GMAC_1        0x44
#define DEVID_DIE1_GMAC_2        0x45
#define DEVID_DIE1_SD            0x46
#define DEVID_DIE1_EMMC          0x48

/* USB_SS */
#define DEVID_DIE1_USB_DFMU      0x49
#define DEVID_DIE1_USB3_0        0x4a
#define DEVID_DIE1_USB2_0        0x4b
#define DEVID_DIE1_USB2_1        0x4c

/* PCIE_SS */
#define DEVID_DIE1_PCIE_DFMU     0x50
#define DEVID_DIE1_PCIE_0        0x51
#define DEVID_DIE1_PCIE_1        0x52
#define DEVID_DIE1_SATA_0        0x54
#define DEVID_DIE1_TEE_EIP120SI      0x56
#define DEVID_DIE1_TEE_EIP120SII     0x57
#define DEVID_DIE1_TEE_EIP120SIII    0x58
#define DEVID_DIE1_TEE_DMAC      0x59

/* VI_SS */
#define DEVID_DIE1_VI_DFMU       0x60
#define DEVID_DIE1_ISP           0x61
#define DEVID_DIE1_VIPRE         0x62
#define DEVID_DIE1_DW200         0x63

/* VP_SS */
#define DEVID_DIE1_VP_DFMU       0x65
#define DEVID_DIE1_VENC          0x66
#define DEVID_DIE1_VDEC          0x67
#define DEVID_DIE1_G2D           0x68

/* VO_SS */
#define DEVID_DIE1_VO_DFMU       0x6b
#define DEVID_DIE1_DISPLAY_0     0x6c
#define DEVID_DIE1_DISPLAY_1     0x6d
#define DEVID_DIE1_AUXDISP       0x6e

#define DEVID_DIE1_PIP_REC       0x6f

/* NPU_SS */
#define DEVID_DIE1_NPU_DFMU      0x70
#define DEVID_DIE1_NPU           0x71

/* Dummy device IDs */
#define DEVID_VIDEO_MEMORY_DUMMY 0xf0

/* GROUPS */
#define IOMMU_GROUP_PER_DEV      0x00
#define IOMMU_GROUP_VIDEO        0x01
#define IOMMU_GROUP_NPU          0x02

#endif
