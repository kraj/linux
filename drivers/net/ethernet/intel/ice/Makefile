# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2018, Intel Corporation.

#
# Makefile for the Intel(R) Ethernet Connection E800 Series Linux Driver
#

subdir-ccflags-y += -I$(src)
obj-$(CONFIG_ICE) += ice.o

ice-y := ice_main.o	\
	 ice_controlq.o	\
	 ice_common.o	\
	 ice_nvm.o	\
	 ice_switch.o	\
	 ice_sched.o	\
	 ice_base.o	\
	 ice_lib.o	\
	 ice_txrx_lib.o	\
	 ice_txrx.o	\
	 ice_fltr.o	\
	 ice_irq.o	\
	 ice_pf_vsi_vlan_ops.o \
	 ice_vsi_vlan_ops.o \
	 ice_vsi_vlan_lib.o \
	 ice_fdir.o	\
	 ice_ethtool_fdir.o \
	 ice_vlan_mode.o \
	 ice_flex_pipe.o \
	 ice_flow.o	\
	 ice_parser.o    \
	 ice_parser_rt.o \
	 ice_idc.o	\
	 devlink/devlink.o	\
	 devlink/health.o \
	 devlink/port.o \
	 ice_sf_eth.o	\
	 ice_sf_vsi_vlan_ops.o \
	 ice_ddp.o	\
	 ice_fw_update.o \
	 ice_lag.o	\
	 ice_ethtool.o  \
	 ice_repr.o	\
	 ice_tc_lib.o	\
	 ice_fwlog.o	\
	 ice_debugfs.o  \
	 ice_adapter.o
ice-$(CONFIG_PCI_IOV) +=	\
	ice_sriov.o		\
	ice_virtchnl.o		\
	ice_virtchnl_allowlist.o \
	ice_virtchnl_fdir.o	\
	ice_vf_mbx.o		\
	ice_vf_vsi_vlan_ops.o	\
	ice_vf_lib.o
ice-$(CONFIG_PTP_1588_CLOCK) += ice_ptp.o ice_ptp_hw.o ice_dpll.o ice_tspll.o
ice-$(CONFIG_DCB) += ice_dcb.o ice_dcb_nl.o ice_dcb_lib.o
ice-$(CONFIG_RFS_ACCEL) += ice_arfs.o
ice-$(CONFIG_XDP_SOCKETS) += ice_xsk.o
ice-$(CONFIG_ICE_SWITCHDEV) += ice_eswitch.o ice_eswitch_br.o
ice-$(CONFIG_GNSS) += ice_gnss.o
ice-$(CONFIG_ICE_HWMON) += ice_hwmon.o
