/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright 2025 NXP */

#ifndef _PHY_FSL_LYNX_CORE_H
#define _PHY_FSL_LYNX_CORE_H

#include <linux/phy.h>

enum lynx_lane_mode {
	LANE_MODE_UNKNOWN,
	LANE_MODE_1000BASEX_SGMII,
	LANE_MODE_10GBASER,
	LANE_MODE_USXGMII,
	LANE_MODE_25GBASER,
	LANE_MODE_MAX,
};

struct lynx_pccr {
	int offset;
	int width;
	int shift;
};

struct lynx_info {
	bool (*lane_supports_mode)(int lane, enum lynx_lane_mode mode);
	int first_lane;
};

const char *lynx_lane_mode_str(enum lynx_lane_mode lane_mode);
enum lynx_lane_mode phy_interface_to_lane_mode(phy_interface_t intf);

#endif
