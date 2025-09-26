/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright 2025 NXP */

#ifndef _PHY_FSL_LYNX_CORE_H
#define _PHY_FSL_LYNX_CORE_H

#include <linux/phy/phy-fsl-lynx.h>
#include <linux/phy/phy.h>
#include <linux/phy.h>
#include <linux/phy/phy-fsl-lynx.h>

#define LYNX_NUM_PLL				2

struct lynx_pccr {
	int offset;
	int width;
	int shift;
};

struct lynx_info {
	int (*get_pccr)(enum lynx_lane_mode lane_mode, int lane,
		        struct lynx_pccr *pccr);
	int (*get_pcvt_offset)(int lane, enum lynx_lane_mode mode);
	bool (*lane_supports_mode)(int lane, enum lynx_lane_mode mode);
	int first_lane;
	int num_lanes;
	bool has_hardcoded_usxgmii;
	int index;
};

struct lynx_priv;

struct lynx_pll {
	struct lynx_priv *priv;
	int id;
	int refclk_sel;
	int frate_sel;
	int ex_dly_clk_use_count;
	bool enabled;
	bool locked;
	/*
	 * There are fewer PLLs than lanes. This serializes calls to
	 * lynx_10g_pll_get_ex_dly_clk() and lynx_10g_pll_put_ex_dly_clk().
	 */
	spinlock_t lock;
	DECLARE_BITMAP(supported, LANE_MODE_MAX);
};

struct lynx_lane {
	struct lynx_priv *priv;
	struct phy *phy;
	bool powered_up;
	bool init;
	unsigned int id;
	enum lynx_lane_mode mode;
	u32 default_pccr[LANE_MODE_MAX];
};

struct lynx_priv {
	void __iomem *base;
	struct device *dev;
	const struct lynx_info *info;
	/* Serialize concurrent access to registers shared between lanes,
	 * like PCCn
	 */
	spinlock_t pcc_lock;
	bool big_endian;
	struct lynx_pll pll[LYNX_NUM_PLL];
	struct lynx_lane *lane;

	struct delayed_work cdr_check;
};

static inline u32 lynx_read(struct lynx_priv *priv, unsigned long off)
{
	void __iomem *reg = priv->base + off;

	if (priv->big_endian)
		return ioread32be(reg);

	return ioread32(reg);
}

static inline void lynx_write(struct lynx_priv *priv, unsigned long off, u32 val)
{
	void __iomem *reg = priv->base + off;

	if (priv->big_endian)
		return iowrite32be(val, reg);

	return iowrite32(val, reg);
}

static inline void lynx_rmw(struct lynx_priv *priv, unsigned long off, u32 val,
			    u32 mask)
{
	u32 orig, tmp;

	orig = lynx_read(priv, off);
	tmp = orig & ~mask;
	tmp |= val;
	if (orig != tmp)
		lynx_write(priv, off, tmp);
}

#define lynx_lane_rmw(lane, reg, val, mask)	\
	lynx_rmw((lane)->priv, reg(lane->id), val, mask)
#define lynx_lane_read(lane, reg)			\
	ioread32((lane)->priv->base + reg((lane)->id))
#define lynx_lane_write(lane, reg, val)		\
	iowrite32(val, (lane)->priv->base + reg((lane)->id))
#define lynx_pll_read(pll, reg)			\
	ioread32((pll)->priv->base + reg((pll)->id))
#define lynx_pll_write(pll, reg, val)		\
	iowrite32(val, (pll)->priv->base + reg((pll)->id))
#define lynx_pll_rmw(pll, reg, val, mask)      \
	lynx_rmw((pll)->priv, reg((pll)->id), val, mask)

const char *lynx_lane_mode_str(enum lynx_lane_mode lane_mode);
enum lynx_lane_mode phy_interface_to_lane_mode(phy_interface_t intf);
bool lynx_lane_supports_mode(struct lynx_lane *lane, enum lynx_lane_mode mode);

struct lynx_pll *lynx_pll_get(struct lynx_priv *priv, enum lynx_lane_mode mode);

int lynx_pccr_read(struct lynx_lane *lane, enum lynx_lane_mode mode, u32 *val);
int lynx_pccr_write(struct lynx_lane *lane, enum lynx_lane_mode mode, u32 val);
int lynx_pcvt_read(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		   u32 *val);
int lynx_pcvt_write(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		    u32 val);
int lynx_pcvt_rmw(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		  u32 val, u32 mask);

#endif
