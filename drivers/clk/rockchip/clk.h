/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
 *
 * based on
 *
 * samsung/clk.h
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 */

#ifndef CLK_ROCKCHIP_CLK_H
#define CLK_ROCKCHIP_CLK_H

#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/hashtable.h>

struct clk;

#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

/* register positions shared by PX30, RV1108, RK2928, RK3036, RK3066, RK3188 and RK3228 */
#define BOOST_PLL_H_CON(x)		((x) * 0x4)
#define BOOST_CLK_CON			0x0008
#define BOOST_BOOST_CON			0x000c
#define BOOST_SWITCH_CNT		0x0010
#define BOOST_HIGH_PERF_CNT0		0x0014
#define BOOST_HIGH_PERF_CNT1		0x0018
#define BOOST_STATIS_THRESHOLD		0x001c
#define BOOST_SHORT_SWITCH_CNT		0x0020
#define BOOST_SWITCH_THRESHOLD		0x0024
#define BOOST_FSM_STATUS		0x0028
#define BOOST_PLL_L_CON(x)		((x) * 0x4 + 0x2c)
#define BOOST_RECOVERY_MASK		0x1
#define BOOST_RECOVERY_SHIFT		1
#define BOOST_SW_CTRL_MASK		0x1
#define BOOST_SW_CTRL_SHIFT		2
#define BOOST_LOW_FREQ_EN_MASK		0x1
#define BOOST_LOW_FREQ_EN_SHIFT		3
#define BOOST_BUSY_STATE		BIT(8)

#define PX30_PLL_CON(x)			((x) * 0x4)
#define PX30_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define PX30_CLKGATE_CON(x)		((x) * 0x4 + 0x200)
#define PX30_GLB_SRST_FST		0xb8
#define PX30_GLB_SRST_SND		0xbc
#define PX30_SOFTRST_CON(x)		((x) * 0x4 + 0x300)
#define PX30_MODE_CON			0xa0
#define PX30_MISC_CON			0xa4
#define PX30_SDMMC_CON0			0x380
#define PX30_SDMMC_CON1			0x384
#define PX30_SDIO_CON0			0x388
#define PX30_SDIO_CON1			0x38c
#define PX30_EMMC_CON0			0x390
#define PX30_EMMC_CON1			0x394

#define PX30_PMU_PLL_CON(x)		((x) * 0x4)
#define PX30_PMU_CLKSEL_CON(x)		((x) * 0x4 + 0x40)
#define PX30_PMU_CLKGATE_CON(x)		((x) * 0x4 + 0x80)
#define PX30_PMU_MODE			0x0020

#define RV1108_PLL_CON(x)		((x) * 0x4)
#define RV1108_CLKSEL_CON(x)		((x) * 0x4 + 0x60)
#define RV1108_CLKGATE_CON(x)		((x) * 0x4 + 0x120)
#define RV1108_SOFTRST_CON(x)		((x) * 0x4 + 0x180)
#define RV1108_GLB_SRST_FST		0x1c0
#define RV1108_GLB_SRST_SND		0x1c4
#define RV1108_MISC_CON			0x1cc
#define RV1108_SDMMC_CON0		0x1d8
#define RV1108_SDMMC_CON1		0x1dc
#define RV1108_SDIO_CON0		0x1e0
#define RV1108_SDIO_CON1		0x1e4
#define RV1108_EMMC_CON0		0x1e8
#define RV1108_EMMC_CON1		0x1ec

#define RV1126_PMU_MODE			0x0
#define RV1126_PMU_PLL_CON(x)		((x) * 0x4 + 0x10)
#define RV1126_PMU_CLKSEL_CON(x)	((x) * 0x4 + 0x100)
#define RV1126_PMU_CLKGATE_CON(x)	((x) * 0x4 + 0x180)
#define RV1126_PMU_SOFTRST_CON(x)	((x) * 0x4 + 0x200)
#define RV1126_PLL_CON(x)		((x) * 0x4)
#define RV1126_MODE_CON			0x90
#define RV1126_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RV1126_CLKGATE_CON(x)		((x) * 0x4 + 0x280)
#define RV1126_SOFTRST_CON(x)		((x) * 0x4 + 0x300)
#define RV1126_GLB_SRST_FST		0x408
#define RV1126_GLB_SRST_SND		0x40c
#define RV1126_SDMMC_CON0		0x440
#define RV1126_SDMMC_CON1		0x444
#define RV1126_SDIO_CON0		0x448
#define RV1126_SDIO_CON1		0x44c
#define RV1126_EMMC_CON0		0x450
#define RV1126_EMMC_CON1		0x454

#define RK2928_PLL_CON(x)		((x) * 0x4)
#define RK2928_MODE_CON		0x40
#define RK2928_CLKSEL_CON(x)	((x) * 0x4 + 0x44)
#define RK2928_CLKGATE_CON(x)	((x) * 0x4 + 0xd0)
#define RK2928_GLB_SRST_FST		0x100
#define RK2928_GLB_SRST_SND		0x104
#define RK2928_SOFTRST_CON(x)	((x) * 0x4 + 0x110)
#define RK2928_MISC_CON		0x134

#define RK3036_SDMMC_CON0		0x144
#define RK3036_SDMMC_CON1		0x148
#define RK3036_SDIO_CON0		0x14c
#define RK3036_SDIO_CON1		0x150
#define RK3036_EMMC_CON0		0x154
#define RK3036_EMMC_CON1		0x158

#define RK3228_GLB_SRST_FST		0x1f0
#define RK3228_GLB_SRST_SND		0x1f4
#define RK3228_SDMMC_CON0		0x1c0
#define RK3228_SDMMC_CON1		0x1c4
#define RK3228_SDIO_CON0		0x1c8
#define RK3228_SDIO_CON1		0x1cc
#define RK3228_EMMC_CON0		0x1d8
#define RK3228_EMMC_CON1		0x1dc

#define RK3288_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3288_MODE_CON			0x50
#define RK3288_CLKSEL_CON(x)		((x) * 0x4 + 0x60)
#define RK3288_CLKGATE_CON(x)		((x) * 0x4 + 0x160)
#define RK3288_GLB_SRST_FST		0x1b0
#define RK3288_GLB_SRST_SND		0x1b4
#define RK3288_SOFTRST_CON(x)		((x) * 0x4 + 0x1b8)
#define RK3288_MISC_CON			0x1e8
#define RK3288_SDMMC_CON0		0x200
#define RK3288_SDMMC_CON1		0x204
#define RK3288_SDIO0_CON0		0x208
#define RK3288_SDIO0_CON1		0x20c
#define RK3288_SDIO1_CON0		0x210
#define RK3288_SDIO1_CON1		0x214
#define RK3288_EMMC_CON0		0x218
#define RK3288_EMMC_CON1		0x21c

#define RK3308_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3308_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3308_CLKGATE_CON(x)		((x) * 0x4 + 0x300)
#define RK3308_GLB_SRST_FST		0xb8
#define RK3308_SOFTRST_CON(x)		((x) * 0x4 + 0x400)
#define RK3308_MODE_CON			0xa0
#define RK3308_SDMMC_CON0		0x480
#define RK3308_SDMMC_CON1		0x484
#define RK3308_SDIO_CON0		0x488
#define RK3308_SDIO_CON1		0x48c
#define RK3308_EMMC_CON0		0x490
#define RK3308_EMMC_CON1		0x494

#define RK3328_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3328_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3328_CLKGATE_CON(x)		((x) * 0x4 + 0x200)
#define RK3328_GRFCLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3328_GLB_SRST_FST		0x9c
#define RK3328_GLB_SRST_SND		0x98
#define RK3328_SOFTRST_CON(x)		((x) * 0x4 + 0x300)
#define RK3328_MODE_CON			0x80
#define RK3328_MISC_CON			0x84
#define RK3328_SDMMC_CON0		0x380
#define RK3328_SDMMC_CON1		0x384
#define RK3328_SDIO_CON0		0x388
#define RK3328_SDIO_CON1		0x38c
#define RK3328_EMMC_CON0		0x390
#define RK3328_EMMC_CON1		0x394
#define RK3328_SDMMC_EXT_CON0		0x398
#define RK3328_SDMMC_EXT_CON1		0x39C

#define RK3368_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3368_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3368_CLKGATE_CON(x)		((x) * 0x4 + 0x200)
#define RK3368_GLB_SRST_FST		0x280
#define RK3368_GLB_SRST_SND		0x284
#define RK3368_SOFTRST_CON(x)		((x) * 0x4 + 0x300)
#define RK3368_MISC_CON			0x380
#define RK3368_SDMMC_CON0		0x400
#define RK3368_SDMMC_CON1		0x404
#define RK3368_SDIO0_CON0		0x408
#define RK3368_SDIO0_CON1		0x40c
#define RK3368_SDIO1_CON0		0x410
#define RK3368_SDIO1_CON1		0x414
#define RK3368_EMMC_CON0		0x418
#define RK3368_EMMC_CON1		0x41c

#define RK3399_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3399_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3399_CLKGATE_CON(x)		((x) * 0x4 + 0x300)
#define RK3399_SOFTRST_CON(x)		((x) * 0x4 + 0x400)
#define RK3399_GLB_SRST_FST		0x500
#define RK3399_GLB_SRST_SND		0x504
#define RK3399_GLB_CNT_TH		0x508
#define RK3399_MISC_CON			0x50c
#define RK3399_RST_CON			0x510
#define RK3399_RST_ST			0x514
#define RK3399_SDMMC_CON0		0x580
#define RK3399_SDMMC_CON1		0x584
#define RK3399_SDIO_CON0		0x588
#define RK3399_SDIO_CON1		0x58c

#define RK3399_PMU_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3399_PMU_CLKSEL_CON(x)	((x) * 0x4 + 0x80)
#define RK3399_PMU_CLKGATE_CON(x)	((x) * 0x4 + 0x100)
#define RK3399_PMU_SOFTRST_CON(x)	((x) * 0x4 + 0x110)

#define RK3528_PMU_CRU_BASE		0x10000
#define RK3528_PCIE_CRU_BASE		0x20000
#define RK3528_DDRPHY_CRU_BASE		0x28000
#define RK3528_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3528_PCIE_PLL_CON(x)		((x) * 0x4 + RK3528_PCIE_CRU_BASE)
#define RK3528_DDRPHY_PLL_CON(x)	((x) * 0x4 + RK3528_DDRPHY_CRU_BASE)
#define RK3528_MODE_CON			0x280
#define RK3528_CLKSEL_CON(x)		((x) * 0x4 + 0x300)
#define RK3528_CLKGATE_CON(x)		((x) * 0x4 + 0x800)
#define RK3528_SOFTRST_CON(x)		((x) * 0x4 + 0xa00)
#define RK3528_SDMMC_CON(x)		((x) * 0x4 + 0x24)
#define RK3528_SDIO0_CON(x)		((x) * 0x4 + 0x4)
#define RK3528_SDIO1_CON(x)		((x) * 0x4 + 0xc)
#define RK3528_PMU_CLKSEL_CON(x)	((x) * 0x4 + 0x300 + RK3528_PMU_CRU_BASE)
#define RK3528_PMU_CLKGATE_CON(x)	((x) * 0x4 + 0x800 + RK3528_PMU_CRU_BASE)
#define RK3528_PCIE_CLKSEL_CON(x)	((x) * 0x4 + 0x300 + RK3528_PCIE_CRU_BASE)
#define RK3528_PCIE_CLKGATE_CON(x)	((x) * 0x4 + 0x800 + RK3528_PCIE_CRU_BASE)
#define RK3528_DDRPHY_CLKGATE_CON(x)	((x) * 0x4 + 0x800 + RK3528_DDRPHY_CRU_BASE)
#define RK3528_DDRPHY_MODE_CON		(0x280 + RK3528_DDRPHY_CRU_BASE)
#define RK3528_GLB_CNT_TH		0xc00
#define RK3528_GLB_SRST_FST		0xc08
#define RK3528_GLB_SRST_SND		0xc0c

#define RK3562_PMU0_CRU_BASE		0x10000
#define RK3562_PMU1_CRU_BASE		0x18000
#define RK3562_DDR_CRU_BASE		0x20000
#define RK3562_SUBDDR_CRU_BASE		0x28000
#define RK3562_PERI_CRU_BASE		0x30000

#define RK3562_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3562_PMU1_PLL_CON(x)		((x) * 0x4 + RK3562_PMU1_CRU_BASE + 0x40)
#define RK3562_SUBDDR_PLL_CON(x)	((x) * 0x4 + RK3562_SUBDDR_CRU_BASE + 0x20)
#define RK3562_MODE_CON			0x600
#define RK3562_PMU1_MODE_CON		(RK3562_PMU1_CRU_BASE + 0x380)
#define RK3562_SUBDDR_MODE_CON		(RK3562_SUBDDR_CRU_BASE + 0x380)
#define RK3562_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3562_CLKGATE_CON(x)		((x) * 0x4 + 0x300)
#define RK3562_SOFTRST_CON(x)		((x) * 0x4 + 0x400)
#define RK3562_DDR_CLKSEL_CON(x)	((x) * 0x4 + RK3562_DDR_CRU_BASE + 0x100)
#define RK3562_DDR_CLKGATE_CON(x)	((x) * 0x4 + RK3562_DDR_CRU_BASE + 0x180)
#define RK3562_DDR_SOFTRST_CON(x)	((x) * 0x4 + RK3562_DDR_CRU_BASE + 0x200)
#define RK3562_SUBDDR_CLKSEL_CON(x)	((x) * 0x4 + RK3562_SUBDDR_CRU_BASE + 0x100)
#define RK3562_SUBDDR_CLKGATE_CON(x)	((x) * 0x4 + RK3562_SUBDDR_CRU_BASE + 0x180)
#define RK3562_SUBDDR_SOFTRST_CON(x)	((x) * 0x4 + RK3562_SUBDDR_CRU_BASE + 0x200)
#define RK3562_PERI_CLKSEL_CON(x)	((x) * 0x4 + RK3562_PERI_CRU_BASE + 0x100)
#define RK3562_PERI_CLKGATE_CON(x)	((x) * 0x4 + RK3562_PERI_CRU_BASE + 0x300)
#define RK3562_PERI_SOFTRST_CON(x)	((x) * 0x4 + RK3562_PERI_CRU_BASE + 0x400)
#define RK3562_PMU0_CLKSEL_CON(x)	((x) * 0x4 + RK3562_PMU0_CRU_BASE + 0x100)
#define RK3562_PMU0_CLKGATE_CON(x)	((x) * 0x4 + RK3562_PMU0_CRU_BASE + 0x180)
#define RK3562_PMU0_SOFTRST_CON(x)	((x) * 0x4 + RK3562_PMU0_CRU_BASE + 0x200)
#define RK3562_PMU1_CLKSEL_CON(x)	((x) * 0x4 + RK3562_PMU1_CRU_BASE + 0x100)
#define RK3562_PMU1_CLKGATE_CON(x)	((x) * 0x4 + RK3562_PMU1_CRU_BASE + 0x180)
#define RK3562_PMU1_SOFTRST_CON(x)	((x) * 0x4 + RK3562_PMU1_CRU_BASE + 0x200)
#define RK3562_GLB_SRST_FST		0x614
#define RK3562_GLB_SRST_SND		0x618
#define RK3562_GLB_RST_CON		0x61c
#define RK3562_GLB_RST_ST		0x620
#define RK3562_SDMMC0_CON0		0x624
#define RK3562_SDMMC0_CON1		0x628
#define RK3562_SDMMC1_CON0		0x62c
#define RK3562_SDMMC1_CON1		0x630

#define RK3568_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3568_MODE_CON0		0xc0
#define RK3568_MISC_CON0		0xc4
#define RK3568_MISC_CON1		0xc8
#define RK3568_MISC_CON2		0xcc
#define RK3568_GLB_CNT_TH		0xd0
#define RK3568_GLB_SRST_FST		0xd4
#define RK3568_GLB_SRST_SND		0xd8
#define RK3568_GLB_RST_CON		0xdc
#define RK3568_GLB_RST_ST		0xe0
#define RK3568_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3568_CLKGATE_CON(x)		((x) * 0x4 + 0x300)
#define RK3568_SOFTRST_CON(x)		((x) * 0x4 + 0x400)
#define RK3568_SDMMC0_CON0		0x580
#define RK3568_SDMMC0_CON1		0x584
#define RK3568_SDMMC1_CON0		0x588
#define RK3568_SDMMC1_CON1		0x58c
#define RK3568_SDMMC2_CON0		0x590
#define RK3568_SDMMC2_CON1		0x594
#define RK3568_EMMC_CON0		0x598
#define RK3568_EMMC_CON1		0x59c

#define RK3568_PMU_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3568_PMU_MODE_CON0		0x80
#define RK3568_PMU_CLKSEL_CON(x)	((x) * 0x4 + 0x100)
#define RK3568_PMU_CLKGATE_CON(x)	((x) * 0x4 + 0x180)
#define RK3568_PMU_SOFTRST_CON(x)	((x) * 0x4 + 0x200)

#define RK3576_PHP_CRU_BASE		0x8000
#define RK3576_SECURE_NS_CRU_BASE	0x10000
#define RK3576_PMU_CRU_BASE		0x20000
#define RK3576_BIGCORE_CRU_BASE		0x38000
#define RK3576_LITCORE_CRU_BASE		0x40000
#define RK3576_CCI_CRU_BASE		0x48000

#define RK3576_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3576_MODE_CON0		0x280
#define RK3576_BPLL_MODE_CON0		(RK3576_BIGCORE_CRU_BASE + 0x280)
#define RK3576_LPLL_MODE_CON0		(RK3576_LITCORE_CRU_BASE + 0x280)
#define RK3576_PPLL_MODE_CON0		(RK3576_PHP_CRU_BASE + 0x280)
#define RK3576_CLKSEL_CON(x)		((x) * 0x4 + 0x300)
#define RK3576_CLKGATE_CON(x)		((x) * 0x4 + 0x800)
#define RK3576_SOFTRST_CON(x)		((x) * 0x4 + 0xa00)
#define RK3576_GLB_CNT_TH		0xc00
#define RK3576_GLB_SRST_FST		0xc08
#define RK3576_GLB_SRST_SND		0xc0c
#define RK3576_GLB_RST_CON		0xc10
#define RK3576_GLB_RST_ST		0xc04
#define RK3576_SDIO_CON0		0xC24
#define RK3576_SDIO_CON1		0xC28
#define RK3576_SDMMC_CON0		0xC30
#define RK3576_SDMMC_CON1		0xC34

#define RK3576_PHP_CLKSEL_CON(x)	((x) * 0x4 + RK3576_PHP_CRU_BASE + 0x300)
#define RK3576_PHP_CLKGATE_CON(x)	((x) * 0x4 + RK3576_PHP_CRU_BASE + 0x800)
#define RK3576_PHP_SOFTRST_CON(x)	((x) * 0x4 + RK3576_PHP_CRU_BASE + 0xa00)

#define RK3576_PMU_PLL_CON(x)		((x) * 0x4 + RK3576_PHP_CRU_BASE)
#define RK3576_PMU_CLKSEL_CON(x)	((x) * 0x4 + RK3576_PMU_CRU_BASE + 0x300)
#define RK3576_PMU_CLKGATE_CON(x)	((x) * 0x4 + RK3576_PMU_CRU_BASE + 0x800)
#define RK3576_PMU_SOFTRST_CON(x)	((x) * 0x4 + RK3576_PMU_CRU_BASE + 0xa00)

#define RK3576_SECURE_NS_CLKSEL_CON(x)	((x) * 0x4 + RK3576_SECURE_NS_CRU_BASE + 0x300)
#define RK3576_SECURE_NS_CLKGATE_CON(x)	((x) * 0x4 + RK3576_SECURE_NS_CRU_BASE + 0x800)
#define RK3576_SECURE_NS_SOFTRST_CON(x)	((x) * 0x4 + RK3576_SECURE_NS_CRU_BASE + 0xa00)

#define RK3576_CCI_CLKSEL_CON(x)	((x) * 0x4 + RK3576_CCI_CRU_BASE + 0x300)
#define RK3576_CCI_CLKGATE_CON(x)	((x) * 0x4 + RK3576_CCI_CRU_BASE + 0x800)
#define RK3576_CCI_SOFTRST_CON(x)	((x) * 0x4 + RK3576_CCI_CRU_BASE + 0xa00)

#define RK3576_BPLL_CON(x)		((x) * 0x4 + RK3576_BIGCORE_CRU_BASE)
#define RK3576_BIGCORE_CLKSEL_CON(x)	((x) * 0x4 + RK3576_BIGCORE_CRU_BASE + 0x300)
#define RK3576_BIGCORE_CLKGATE_CON(x)	((x) * 0x4 + RK3576_BIGCORE_CRU_BASE + 0x800)
#define RK3576_BIGCORE_SOFTRST_CON(x)	((x) * 0x4 + RK3576_BIGCORE_CRU_BASE + 0xa00)
#define RK3576_LPLL_CON(x)		((x) * 0x4 + RK3576_CCI_CRU_BASE)
#define RK3576_LITCORE_CLKSEL_CON(x)	((x) * 0x4 + RK3576_LITCORE_CRU_BASE + 0x300)
#define RK3576_LITCORE_CLKGATE_CON(x)	((x) * 0x4 + RK3576_LITCORE_CRU_BASE + 0x800)
#define RK3576_LITCORE_SOFTRST_CON(x)	((x) * 0x4 + RK3576_LITCORE_CRU_BASE + 0xa00)
#define RK3576_NON_SECURE_GATING_CON00	0xc48

#define RK3588_PHP_CRU_BASE		0x8000
#define RK3588_PMU_CRU_BASE		0x30000
#define RK3588_BIGCORE0_CRU_BASE	0x50000
#define RK3588_BIGCORE1_CRU_BASE	0x52000
#define RK3588_DSU_CRU_BASE		0x58000

#define RK3588_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3588_MODE_CON0		0x280
#define RK3588_B0_PLL_MODE_CON0		(RK3588_BIGCORE0_CRU_BASE + 0x280)
#define RK3588_B1_PLL_MODE_CON0		(RK3588_BIGCORE1_CRU_BASE + 0x280)
#define RK3588_LPLL_MODE_CON0		(RK3588_DSU_CRU_BASE + 0x280)
#define RK3588_CLKSEL_CON(x)		((x) * 0x4 + 0x300)
#define RK3588_CLKGATE_CON(x)		((x) * 0x4 + 0x800)
#define RK3588_SOFTRST_CON(x)		((x) * 0x4 + 0xa00)
#define RK3588_GLB_CNT_TH		0xc00
#define RK3588_GLB_SRST_FST		0xc08
#define RK3588_GLB_SRST_SND		0xc0c
#define RK3588_GLB_RST_CON		0xc10
#define RK3588_GLB_RST_ST		0xc04
#define RK3588_SDIO_CON0		0xC24
#define RK3588_SDIO_CON1		0xC28
#define RK3588_SDMMC_CON0		0xC30
#define RK3588_SDMMC_CON1		0xC34

#define RK3588_PHP_CLKGATE_CON(x)	((x) * 0x4 + RK3588_PHP_CRU_BASE + 0x800)
#define RK3588_PHP_SOFTRST_CON(x)	((x) * 0x4 + RK3588_PHP_CRU_BASE + 0xa00)

#define RK3588_PMU_PLL_CON(x)		((x) * 0x4 + RK3588_PHP_CRU_BASE)
#define RK3588_PMU_CLKSEL_CON(x)	((x) * 0x4 + RK3588_PMU_CRU_BASE + 0x300)
#define RK3588_PMU_CLKGATE_CON(x)	((x) * 0x4 + RK3588_PMU_CRU_BASE + 0x800)
#define RK3588_PMU_SOFTRST_CON(x)	((x) * 0x4 + RK3588_PMU_CRU_BASE + 0xa00)

#define RK3588_B0_PLL_CON(x)		((x) * 0x4 + RK3588_BIGCORE0_CRU_BASE)
#define RK3588_BIGCORE0_CLKSEL_CON(x)	((x) * 0x4 + RK3588_BIGCORE0_CRU_BASE + 0x300)
#define RK3588_BIGCORE0_CLKGATE_CON(x)	((x) * 0x4 + RK3588_BIGCORE0_CRU_BASE + 0x800)
#define RK3588_BIGCORE0_SOFTRST_CON(x)	((x) * 0x4 + RK3588_BIGCORE0_CRU_BASE + 0xa00)
#define RK3588_B1_PLL_CON(x)		((x) * 0x4 + RK3588_BIGCORE1_CRU_BASE)
#define RK3588_BIGCORE1_CLKSEL_CON(x)	((x) * 0x4 + RK3588_BIGCORE1_CRU_BASE + 0x300)
#define RK3588_BIGCORE1_CLKGATE_CON(x)	((x) * 0x4 + RK3588_BIGCORE1_CRU_BASE + 0x800)
#define RK3588_BIGCORE1_SOFTRST_CON(x)	((x) * 0x4 + RK3588_BIGCORE1_CRU_BASE + 0xa00)
#define RK3588_LPLL_CON(x)		((x) * 0x4 + RK3588_DSU_CRU_BASE)
#define RK3588_DSU_CLKSEL_CON(x)	((x) * 0x4 + RK3588_DSU_CRU_BASE + 0x300)
#define RK3588_DSU_CLKGATE_CON(x)	((x) * 0x4 + RK3588_DSU_CRU_BASE + 0x800)
#define RK3588_DSU_SOFTRST_CON(x)	((x) * 0x4 + RK3588_DSU_CRU_BASE + 0xa00)

enum rockchip_pll_type {
	pll_rk3036,
	pll_rk3066,
	pll_rk3328,
	pll_rk3399,
	pll_rk3588,
	pll_rk3588_core,
	pll_rk3588_ddr,
};

#define RK3036_PLL_RATE(_rate, _refdiv, _fbdiv, _postdiv1,	\
			_postdiv2, _dsmpd, _frac)		\
{								\
	.rate	= _rate##U,					\
	.fbdiv = _fbdiv,					\
	.postdiv1 = _postdiv1,					\
	.refdiv = _refdiv,					\
	.postdiv2 = _postdiv2,					\
	.dsmpd = _dsmpd,					\
	.frac = _frac,						\
}

#define RK3066_PLL_RATE(_rate, _nr, _nf, _no)	\
{						\
	.rate	= _rate##U,			\
	.nr = _nr,				\
	.nf = _nf,				\
	.no = _no,				\
	.nb = ((_nf) < 2) ? 1 : (_nf) >> 1,	\
}

#define RK3066_PLL_RATE_NB(_rate, _nr, _nf, _no, _nb)		\
{								\
	.rate	= _rate##U,					\
	.nr = _nr,						\
	.nf = _nf,						\
	.no = _no,						\
	.nb = _nb,						\
}

#define RK3588_PLL_RATE(_rate, _p, _m, _s, _k)			\
{								\
	.rate   = _rate##U,					\
	.p = _p,						\
	.m = _m,						\
	.s = _s,						\
	.k = _k,						\
}

enum rockchip_grf_type {
	grf_type_sys = 0,
	grf_type_pmu0,
	grf_type_pmu1,
	grf_type_ioc,
	grf_type_vo,
	grf_type_vpu,
};

/* ceil(sqrt(enums in rockchip_grf_type - 1)) */
#define GRF_HASH_ORDER 2

/**
 * struct rockchip_aux_grf - entry for the aux_grf_table hashtable
 * @grf: pointer to the grf this entry references
 * @type: what type of GRF this is
 * @node: hlist node
 */
struct rockchip_aux_grf {
	struct regmap *grf;
	enum rockchip_grf_type type;
	struct hlist_node node;
};

/**
 * struct rockchip_clk_provider - information about clock provider
 * @reg_base: virtual address for the register base.
 * @clk_data: holds clock related data like clk* and number of clocks.
 * @cru_node: device-node of the clock-provider
 * @grf: regmap of the general-register-files syscon
 * @aux_grf_table: hashtable of auxiliary GRF regmaps, indexed by grf_type
 * @lock: maintains exclusion between callbacks for a given clock-provider.
 */
struct rockchip_clk_provider {
	void __iomem *reg_base;
	struct clk_onecell_data clk_data;
	struct device_node *cru_node;
	struct regmap *grf;
	DECLARE_HASHTABLE(aux_grf_table, GRF_HASH_ORDER);
	spinlock_t lock;
};

struct rockchip_pll_rate_table {
	unsigned long rate;
	union {
		struct {
			/* for RK3066 */
			unsigned int nr;
			unsigned int nf;
			unsigned int no;
			unsigned int nb;
		};
		struct {
			/* for RK3036/RK3399 */
			unsigned int fbdiv;
			unsigned int postdiv1;
			unsigned int refdiv;
			unsigned int postdiv2;
			unsigned int dsmpd;
			unsigned int frac;
		};
		struct {
			/* for RK3588 */
			unsigned int m;
			unsigned int p;
			unsigned int s;
			unsigned int k;
		};
	};
};

/**
 * struct rockchip_pll_clock - information about pll clock
 * @id: platform specific id of the clock.
 * @name: name of this pll clock.
 * @parent_names: name of the parent clock.
 * @num_parents: number of parents
 * @flags: optional flags for basic clock.
 * @con_offset: offset of the register for configuring the PLL.
 * @mode_offset: offset of the register for configuring the PLL-mode.
 * @mode_shift: offset inside the mode-register for the mode of this pll.
 * @lock_shift: offset inside the lock register for the lock status.
 * @type: Type of PLL to be registered.
 * @pll_flags: hardware-specific flags
 * @rate_table: Table of usable pll rates
 *
 * Flags:
 * ROCKCHIP_PLL_SYNC_RATE - check rate parameters to match against the
 *	rate_table parameters and adjust them if necessary.
 * ROCKCHIP_PLL_FIXED_MODE - the pll operates in normal mode only
 */
struct rockchip_pll_clock {
	unsigned int		id;
	const char		*name;
	const char		*const *parent_names;
	u8			num_parents;
	unsigned long		flags;
	int			con_offset;
	int			mode_offset;
	int			mode_shift;
	int			lock_shift;
	enum rockchip_pll_type	type;
	u8			pll_flags;
	struct rockchip_pll_rate_table *rate_table;
};

#define ROCKCHIP_PLL_SYNC_RATE		BIT(0)
#define ROCKCHIP_PLL_FIXED_MODE		BIT(1)

#define PLL(_type, _id, _name, _pnames, _flags, _con, _mode, _mshift,	\
		_lshift, _pflags, _rtable)				\
	{								\
		.id		= _id,					\
		.type		= _type,				\
		.name		= _name,				\
		.parent_names	= _pnames,				\
		.num_parents	= ARRAY_SIZE(_pnames),			\
		.flags		= CLK_GET_RATE_NOCACHE | _flags,	\
		.con_offset	= _con,					\
		.mode_offset	= _mode,				\
		.mode_shift	= _mshift,				\
		.lock_shift	= _lshift,				\
		.pll_flags	= _pflags,				\
		.rate_table	= _rtable,				\
	}

struct clk *rockchip_clk_register_pll(struct rockchip_clk_provider *ctx,
		enum rockchip_pll_type pll_type,
		const char *name, const char *const *parent_names,
		u8 num_parents, int con_offset, int grf_lock_offset,
		int lock_shift, int mode_offset, int mode_shift,
		struct rockchip_pll_rate_table *rate_table,
		unsigned long flags, u8 clk_pll_flags);

struct rockchip_cpuclk_clksel {
	int reg;
	u32 val;
};

#define ROCKCHIP_CPUCLK_NUM_DIVIDERS	6
#define ROCKCHIP_CPUCLK_MAX_CORES	4
struct rockchip_cpuclk_rate_table {
	unsigned long prate;
	struct rockchip_cpuclk_clksel divs[ROCKCHIP_CPUCLK_NUM_DIVIDERS];
	struct rockchip_cpuclk_clksel pre_muxs[ROCKCHIP_CPUCLK_NUM_DIVIDERS];
	struct rockchip_cpuclk_clksel post_muxs[ROCKCHIP_CPUCLK_NUM_DIVIDERS];
};

/**
 * struct rockchip_cpuclk_reg_data - register offsets and masks of the cpuclock
 * @core_reg[]:	register offset of the cores setting register
 * @div_core_shift[]:	cores divider offset used to divide the pll value
 * @div_core_mask[]:	cores divider mask
 * @num_cores:	number of cpu cores
 * @mux_core_reg:       register offset of the cores select parent
 * @mux_core_alt:       mux value to select alternate parent
 * @mux_core_main:	mux value to select main parent of core
 * @mux_core_shift:	offset of the core multiplexer
 * @mux_core_mask:	core multiplexer mask
 */
struct rockchip_cpuclk_reg_data {
	int	core_reg[ROCKCHIP_CPUCLK_MAX_CORES];
	u8	div_core_shift[ROCKCHIP_CPUCLK_MAX_CORES];
	u32	div_core_mask[ROCKCHIP_CPUCLK_MAX_CORES];
	int	num_cores;
	int	mux_core_reg;
	u8	mux_core_alt;
	u8	mux_core_main;
	u8	mux_core_shift;
	u32	mux_core_mask;
};

struct clk *rockchip_clk_register_cpuclk(const char *name,
			const char *const *parent_names, u8 num_parents,
			const struct rockchip_cpuclk_reg_data *reg_data,
			const struct rockchip_cpuclk_rate_table *rates,
			int nrates, void __iomem *reg_base, spinlock_t *lock);

struct clk *rockchip_clk_register_mmc(const char *name,
				const char *const *parent_names, u8 num_parents,
				void __iomem *reg,
				struct regmap *grf, int grf_reg,
				int shift);

/*
 * DDRCLK flags, including method of setting the rate
 * ROCKCHIP_DDRCLK_SIP: use SIP call to bl31 to change ddrclk rate.
 */
#define ROCKCHIP_DDRCLK_SIP		BIT(0)

struct clk *rockchip_clk_register_ddrclk(const char *name, int flags,
					 const char *const *parent_names,
					 u8 num_parents, int mux_offset,
					 int mux_shift, int mux_width,
					 int div_shift, int div_width,
					 int ddr_flags, void __iomem *reg_base,
					 spinlock_t *lock);

#define ROCKCHIP_INVERTER_HIWORD_MASK	BIT(0)

struct clk *rockchip_clk_register_inverter(const char *name,
				const char *const *parent_names, u8 num_parents,
				void __iomem *reg, int shift, int flags,
				spinlock_t *lock);

struct clk *rockchip_clk_register_muxgrf(const char *name,
				const char *const *parent_names, u8 num_parents,
				int flags, struct regmap *grf, int reg,
				int shift, int width, int mux_flags);

struct clk *rockchip_clk_register_gate_grf(const char *name,
				const char *parent_name, unsigned long flags,
				struct regmap *regmap, unsigned int reg,
				unsigned int shift, u8 gate_flags);

#define PNAME(x) static const char *const x[] __initconst

enum rockchip_clk_branch_type {
	branch_composite,
	branch_mux,
	branch_grf_mux,
	branch_divider,
	branch_fraction_divider,
	branch_gate,
	branch_grf_gate,
	branch_linked_gate,
	branch_mmc,
	branch_grf_mmc,
	branch_inverter,
	branch_factor,
	branch_ddrclk,
	branch_half_divider,
};

struct rockchip_clk_branch {
	unsigned int			id;
	enum rockchip_clk_branch_type	branch_type;
	const char			*name;
	const char			*const *parent_names;
	u8				num_parents;
	unsigned long			flags;
	int				muxdiv_offset;
	u8				mux_shift;
	u8				mux_width;
	u8				mux_flags;
	u32				*mux_table;
	int				div_offset;
	u8				div_shift;
	u8				div_width;
	u8				div_flags;
	struct clk_div_table		*div_table;
	int				gate_offset;
	u8				gate_shift;
	u8				gate_flags;
	unsigned int			linked_clk_id;
	enum rockchip_grf_type		grf_type;
	struct rockchip_clk_branch	*child;
};

#define COMPOSITE(_id, cname, pnames, f, mo, ms, mw, mf, ds, dw,\
		  df, go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_DIV_OFFSET(_id, cname, pnames, f, mo, ms, mw,	\
			     mf, do, ds, dw, df, go, gs, gf)	\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.div_offset	= do,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NOMUX(_id, cname, pname, f, mo, ds, dw, df,	\
			go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NOMUX_DIVTBL(_id, cname, pname, f, mo, ds, dw,\
			       df, dt, go, gs, gf)		\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.div_table	= dt,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NODIV(_id, cname, pnames, f, mo, ms, mw, mf,	\
			go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NOGATE(_id, cname, pnames, f, mo, ms, mw, mf,	\
			 ds, dw, df)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= -1,				\
	}

#define COMPOSITE_NOGATE_DIVTBL(_id, cname, pnames, f, mo, ms,	\
				mw, mf, ds, dw, df, dt)		\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.div_table	= dt,				\
		.gate_offset	= -1,				\
	}

#define COMPOSITE_FRAC(_id, cname, pname, f, mo, df, go, gs, gf)\
	{							\
		.id		= _id,				\
		.branch_type	= branch_fraction_divider,	\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= 16,				\
		.div_width	= 16,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_FRACMUX(_id, cname, pname, f, mo, df, go, gs, gf, ch) \
	{							\
		.id		= _id,				\
		.branch_type	= branch_fraction_divider,	\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= 16,				\
		.div_width	= 16,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
		.child		= ch,				\
	}

#define COMPOSITE_FRACMUX_NOGATE(_id, cname, pname, f, mo, df, ch) \
	{							\
		.id		= _id,				\
		.branch_type	= branch_fraction_divider,	\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= 16,				\
		.div_width	= 16,				\
		.div_flags	= df,				\
		.gate_offset	= -1,				\
		.child		= ch,				\
	}

#define COMPOSITE_DDRCLK(_id, cname, pnames, f, mo, ms, mw,	\
			 ds, dw, df)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_ddrclk,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset  = mo,                           \
		.mux_shift      = ms,                           \
		.mux_width      = mw,                           \
		.div_shift      = ds,                           \
		.div_width      = dw,                           \
		.div_flags	= df,				\
		.gate_offset    = -1,                           \
	}

#define MUX(_id, cname, pnames, f, o, s, w, mf)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_mux,			\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.mux_shift	= s,				\
		.mux_width	= w,				\
		.mux_flags	= mf,				\
		.gate_offset	= -1,				\
	}

#define MUXTBL(_id, cname, pnames, f, o, s, w, mf, mt)		\
	{							\
		.id		= _id,				\
		.branch_type	= branch_mux,			\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.mux_shift	= s,				\
		.mux_width	= w,				\
		.mux_flags	= mf,				\
		.gate_offset	= -1,				\
		.mux_table	= mt,				\
	}

#define MUXGRF(_id, cname, pnames, f, o, s, w, mf, gt)		\
	{							\
		.id		= _id,				\
		.branch_type	= branch_grf_mux,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.mux_shift	= s,				\
		.mux_width	= w,				\
		.mux_flags	= mf,				\
		.gate_offset	= -1,				\
		.grf_type	= gt,				\
	}

#define DIV(_id, cname, pname, f, o, s, w, df)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_divider,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.div_shift	= s,				\
		.div_width	= w,				\
		.div_flags	= df,				\
		.gate_offset	= -1,				\
	}

#define DIVTBL(_id, cname, pname, f, o, s, w, df, dt)		\
	{							\
		.id		= _id,				\
		.branch_type	= branch_divider,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.div_shift	= s,				\
		.div_width	= w,				\
		.div_flags	= df,				\
		.div_table	= dt,				\
	}

#define GATE(_id, cname, pname, f, o, b, gf)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_gate,			\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.gate_offset	= o,				\
		.gate_shift	= b,				\
		.gate_flags	= gf,				\
	}

#define GATE_GRF(_id, cname, pname, f, o, b, gf, gt)		\
	{							\
		.id		= _id,				\
		.branch_type	= branch_grf_gate,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.gate_offset	= o,				\
		.gate_shift	= b,				\
		.gate_flags	= gf,				\
		.grf_type	= gt,				\
	}

#define GATE_LINK(_id, cname, pname, linkedclk, f, o, b, gf)	\
	{							\
		.id		= _id,				\
		.branch_type	= branch_linked_gate,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.linked_clk_id	= linkedclk,			\
		.num_parents	= 1,				\
		.flags		= f,				\
		.gate_offset	= o,				\
		.gate_shift	= b,				\
		.gate_flags	= gf,				\
	}

#define MMC(_id, cname, pname, offset, shift)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_mmc,			\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.muxdiv_offset	= offset,			\
		.div_shift	= shift,			\
	}

#define MMC_GRF(_id, cname, pname, offset, shift, grftype)	\
	{							\
		.id		= _id,				\
		.branch_type	= branch_grf_mmc,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.muxdiv_offset	= offset,			\
		.div_shift	= shift,			\
		.grf_type	= grftype,			\
	}

#define INVERTER(_id, cname, pname, io, is, if)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_inverter,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.muxdiv_offset	= io,				\
		.div_shift	= is,				\
		.div_flags	= if,				\
	}

#define FACTOR(_id, cname, pname,  f, fm, fd)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_factor,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.div_shift	= fm,				\
		.div_width	= fd,				\
	}

#define FACTOR_GATE(_id, cname, pname,  f, fm, fd, go, gb, gf)	\
	{							\
		.id		= _id,				\
		.branch_type	= branch_factor,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.div_shift	= fm,				\
		.div_width	= fd,				\
		.gate_offset	= go,				\
		.gate_shift	= gb,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_HALFDIV(_id, cname, pnames, f, mo, ms, mw, mf, ds, dw,\
			  df, go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_half_divider,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NOGATE_HALFDIV(_id, cname, pnames, f, mo, ms, mw, mf,	\
				 ds, dw, df)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_half_divider,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= -1,				\
	}

#define COMPOSITE_NOMUX_HALFDIV(_id, cname, pname, f, mo, ds, dw, df,	\
			go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_half_divider,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define DIV_HALF(_id, cname, pname, f, o, s, w, df)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_half_divider,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.div_shift	= s,				\
		.div_width	= w,				\
		.div_flags	= df,				\
		.gate_offset	= -1,				\
	}

/* SGRF clocks are only accessible from secure mode, so not controllable */
#define SGRF_GATE(_id, cname, pname)				\
		FACTOR(_id, cname, pname, 0, 1, 1)

static inline struct clk *rockchip_clk_get_lookup(struct rockchip_clk_provider *ctx,
						  unsigned int id)
{
	return ctx->clk_data.clks[id];
}

static inline void rockchip_clk_set_lookup(struct rockchip_clk_provider *ctx,
					   struct clk *clk, unsigned int id)
{
	ctx->clk_data.clks[id] = clk;
}

struct rockchip_gate_link_platdata {
	struct rockchip_clk_provider *ctx;
	struct rockchip_clk_branch *clkbr;
};

struct rockchip_clk_provider *rockchip_clk_init(struct device_node *np,
			void __iomem *base, unsigned long nr_clks);
struct rockchip_clk_provider *rockchip_clk_init_early(struct device_node *np,
			void __iomem *base, unsigned long nr_clks);
void rockchip_clk_finalize(struct rockchip_clk_provider *ctx);
void rockchip_clk_of_add_provider(struct device_node *np,
				struct rockchip_clk_provider *ctx);
unsigned long rockchip_clk_find_max_clk_id(struct rockchip_clk_branch *list,
					   unsigned int nr_clk);
void rockchip_clk_register_branches(struct rockchip_clk_provider *ctx,
				    struct rockchip_clk_branch *list,
				    unsigned int nr_clk);
void rockchip_clk_register_late_branches(struct device *dev,
					 struct rockchip_clk_provider *ctx,
					 struct rockchip_clk_branch *list,
					 unsigned int nr_clk);
void rockchip_clk_register_plls(struct rockchip_clk_provider *ctx,
				struct rockchip_pll_clock *pll_list,
				unsigned int nr_pll, int grf_lock_offset);
void rockchip_clk_register_armclk(struct rockchip_clk_provider *ctx,
			unsigned int lookup_id, const char *name,
			const char *const *parent_names, u8 num_parents,
			const struct rockchip_cpuclk_reg_data *reg_data,
			const struct rockchip_cpuclk_rate_table *rates,
			int nrates);
void rockchip_clk_protect_critical(const char *const clocks[], int nclocks);
void rockchip_register_restart_notifier(struct rockchip_clk_provider *ctx,
					unsigned int reg, void (*cb)(void));

#define ROCKCHIP_SOFTRST_HIWORD_MASK	BIT(0)

struct clk *rockchip_clk_register_halfdiv(const char *name,
					  const char *const *parent_names,
					  u8 num_parents, void __iomem *base,
					  int muxdiv_offset, u8 mux_shift,
					  u8 mux_width, u8 mux_flags,
					  u8 div_shift, u8 div_width,
					  u8 div_flags, int gate_offset,
					  u8 gate_shift, u8 gate_flags,
					  unsigned long flags,
					  spinlock_t *lock);

#ifdef CONFIG_RESET_CONTROLLER
void rockchip_register_softrst_lut(struct device_node *np,
				   const int *lookup_table,
				   unsigned int num_regs,
				   void __iomem *base, u8 flags);
#else
static inline void rockchip_register_softrst_lut(struct device_node *np,
				   const int *lookup_table,
				   unsigned int num_regs,
				   void __iomem *base, u8 flags)
{
}
#endif

static inline void rockchip_register_softrst(struct device_node *np,
					     unsigned int num_regs,
					     void __iomem *base, u8 flags)
{
	return rockchip_register_softrst_lut(np, NULL, num_regs, base, flags);
}

void rk3528_rst_init(struct device_node *np, void __iomem *reg_base);
void rk3562_rst_init(struct device_node *np, void __iomem *reg_base);
void rk3576_rst_init(struct device_node *np, void __iomem *reg_base);
void rk3588_rst_init(struct device_node *np, void __iomem *reg_base);

#endif
