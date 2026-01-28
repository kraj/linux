// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(C) 2021 Alibaba Communications Inc.
 * Author: David Li <liyong.li@alibaba-inc.com>
 */
#define DEBUG
#define CREATE_TRACE_POINTS
#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/rtc.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/dma-map-ops.h>
#include <linux/workqueue.h>
#include <linux/sort.h>
#include <asm/cache.h>
#include "trace.h"

#define A210_100P_V1

#ifndef false
#define false (0)
#endif

#ifndef true
#define true (!(false))
#endif

#define BMU_SYSREG_CFG 0x0
#define BMU_SYSREG_CFG1 0x04
#define BMU_VP_SYSREG_CLK_EN 0x200
#define BMU_VP_SYSREG_RSTN_EN 0x400

#define BMU_VI_SYSREG_CLK_EN1 0x200
#define BMU_VI_SYSREG_RSTN_EN1 0x400
//#define BMU_VI_SYSREG_CLK_EN2         0x0c
//#define BMU_VI_SYSREG_RSTN_EN2        0x14

#define BMU_VO_SYSREG_CLK_EN 0x200
#define BMU_VO_SYSREG_RSTN_EN 0x400
#define BMU_VO_SYSREG_REG004 0x004
#define BMU_VO_SYSREG_REG13c 0x13c

//gpu reg 0x06e00000
#define BMU_GPU_TOP_BPC_REG04 (0x400 + 0x04)
#define BMU_GPU_TOP_BPC_REG13C (0x400 + 0x13c)
#define BMU_GPU_TOP_CFG_ACLK_CCU_REG28 (0x1200 + 0x28)
#define BMU_GPU_TOP_ACLK_CCU_REG28 (0x1400 + 0x28)
#define BMU_GPU_TOP_PCLK_REG28 (0x1600 + 0x28)
#define BMU_GPU_TOP_PCU_REG24 (0x000 + 0x24)
#define BMU_GPU_TOP_PCU_REG0C (0x000 + 0xc)
#define BMU_GPU_TOP_PCU_REG08 (0x000 + 0x8)
#define BMU_GPU_TOP_PCA_REG10 (0x600 + 0x10)
//gpu 0x06d30000
#define BMU_GPU_ACLK_CCU_REG28 (0x200 + 0x28)
#define BMU_GPU_CORE_CLK_CCU_REG28 (0x400 + 0x28)
#define BMU_GPU_PCLK_CCU_REG28 (0x600 + 0x28)

//npu reg
#define BMU_NPU_PCTRL_PCU_REG24 (0x000 + 0x24)
#define BMU_NPU_PCTRL_PCA_REG20 (0x600 + 0x20)
#define BMU_NPU_PCTRL_BPC_REG00 (0x200 + 0x00)
#define BMU_NPU_PCU_SW_LPS_REG0C (0x000 + 0xc)
#define BMU_NPU_PCU_SW_LPR_REG08 (0x000 + 0x8)
#define BMU_NPU_PCU_ICR_REG28 (0x000 + 0x28)
#define BMU_NPU_PCU_ISR_REG30 (0x000 + 0x30)
#define BMU_NPU_CRG_REG200 (0x200)
#define BMU_NPU_CRG_REG210 (0x200 + 0x10)

//d2d reg
#define BMU_D2D_PCU_REG24 0x24
#define BMU_D2D_BPC_REG200 0x200
#define BMU_D2D_PCUEN_REG0C 0x0c
#define BMU_D2D_SW_LPREQ_REG08 0x08

//usb reg
#define BMU_USB_PCU_PCA_REG24 0x24
#define BMU_USB_BPC_REG0 (0x200 + 0x00)
#define BMU_USB_PCU_SW_LPS_REG0C 0x0c
#define BMU_USB_PCU_SW_LPREQ_REG08 0x08

#define PMU_CTRL 0x0
#define PMU_DURA_THRESHOLD 0x4
#define PMU_MST_ID 0x8
#define PMU_MON_PERIOD 0xC
#define PMU_FLT_CTRL 0x10
#define PMU_FLT_LEN 0x14
#define PMU_FLT_E_ADDR0 0x18
#define PMU_FLT_E_ADDR1 0x1C
#define PMU_FLT_E_ADDR2 0x20
#define PMU_FLT_E_ADDR3 0x24
#define PMU_FLT_CMD 0x28
#define PMU_FLT_S_ADDR0 0x2C
#define PMU_FLT_S_ADDR1 0x30
#define PMU_FLT_S_ADDR2 0x34
#define PMU_FLT_S_ADDR3 0x38
#define PMU_TARGET_WDATA 0x3C
#define PMU_READ 0x40
#define PMU_STS_REG_BASE 0x40
#define PMU_RD_STS0 0x40
#define PMU_RD_STS1 0x44
#define PMU_RD_STS2 0x48
#define PMU_RD_STS3 0x4C
#define PMU_WR_STS0 0x50
#define PMU_WR_STS1 0x54
#define PMU_WR_STS2 0x58
#define PMU_WR_STS3 0x5C
#define PMU_VRD_STS0 0x60
#define PMU_VRD_STS1 0x64
#define PMU_REUSE_CAP1 0x68
#define PMU_REUSE_CAP2 0x6c
#define PMU_VWR_STS0 0x70
#define PMU_VWR_STS1 0x74
#define PMU_REUSE_CAP3 0x78
#define PMU_REUSE_CAPc 0x7c
#define PMU_INT_REG 0x80
#define PMU_ERR_RESP_ID 0x84
#define PMU_VERSION0 0x88
#define PMU_VERSION1 0x8C
#define PMU_OSTD_STS 0x90
#define PMU_OSTD_CFG 0x94
#define PMU_TARGET_ADDR 0x98
#define PMU_RD_STS4 0xA0
#define PMU_WR_STS4 0xA4
#define BMU3_STS 0xA8
#define BMU3_CFG 0xAC
#define BMU3_DMA_CSR 0xB0
#define BMU3_DMA_CFG0 0xB4
#define BMU3_DMA_CFG1 0xB8
#define BMU3_DMA_CFG2 0xBc
#define BMU3_DMA_CFG3 0xe0
#define BMU3_DBG_STS 0xe4
#define BMU3_DBG_WADDR 0xe8
#define BMU3_DBG_RADDR 0xec
#define BMU3_DEV_ID_FILTER_CFG 0xf0
#define BMU3_EXT_INT 0xf4
#define BMU3_EXT_INT_EN 0xf8
#define BMU3_TRIGGER_CTRL 0xfc
#define BMU3_TRIGGER_COND_CFG 0x100
#define BMU3_TRIGGER_COND_MASK_CFG 0x104
#define BMU3_TRIGGER_STAT 0x108

#define BMU_SYSREG_ARST (1 << 0)
#define BMU_SYSREG_PRST (1 << 1)
#define PMU_RESET (1 << 0)
#define PMU_EN (1 << 1)
#define PMU_CLK_EN (1 << 2)
#define PMU_SRC_SEL_MASK (0x1f << 3)
#define SRC_ADDR_RANGE_HIT (1 << 3)
#define SRC_PERIOD_EXPIRED (1 << 4)
#define SRC_TARGET_WDATA (1 << 5)
#define SRC_ERROR_RESP (1 << 6)
#define SRC_CNT_OVERFLOW (1 << 7)
#define PMU_TRIG_MODE_MASK (1 << 8)
#define BMU3_CFG_RESET (1 << 0)
#define BMU3_CFG_EN (1 << 1)
#define BMU3_CFG_BUFF_PAUSE_EN (1 << 2)
#define BMU3_CFG_REG_REUSE_EN (1 << 3)
#define BMU3_CFG_FULL_AXI_INT_EN (1 << 4)
#define BMU3_CFG_AXI_RESP_ERR_INT_EN (1 << 5)
#define BMU3_CFG_BUFF_FULL_INT_EN (1 << 6)
#define BMU3_SIDEBAND_EN (1 << 7)
#define BMU3_SEND_LOOP_EN (1 << 8)
#define BMU3_ID_SRC_SEL (1 << 9)
#define BMU3_SEND_QUART_INT_EN (1 << 0)
#define BMU3_SEND_HALF_INT_EN (1 << 1)
#define BMU3_SEND_3QUART_INT_EN (1 << 2)
#define BMU3_TRIGGER_SRC_SEL (0x7 << 0)
#define BMU3_TRIGGER_CH_SEL (1 << 3)
#define BMU3_TRIGGER_EN (1 << 4)
#define BMU3_TRIGGER_CLR (1 << 5)
#define BMU3_TRIGGER_CNT_CLR (1 << 6)
#define BMU3_TRIGGER_COND (0xffffffff << 0)
#define BMU3_TRIGGER_COND_MASK (0xffffffff << 0)
#define BMU3_DEVID_MASK (0xffffffff << 0)

#define BMU3_SEND_QUART_INT (1 << 0)
#define BMU3_SEND_HALF_INT (1 << 1)
#define BMU3_SEND_3QUART_INT (1 << 2)
#define BMU3_STS_WATTING_RESP (1 << 31)
#define BMU3_STS_AXI_RESP_ERR (1 << 30)
#define BMU3_STS_AXI_FULL (1 << 29)
#define BMU3_STS_BUFF_FULL (1 << 28)
#define BMU3_STS_BUSY (1 << 27)
#define BMU3_SYNC_ID_WIDTH (0x7f << 20)
#define BMU3_LATENCY_WIDTH (0x1f << 15)
#define BMU3_TIMESTAMP_WIDTH (0x1f << 10)
#define BMU3_ID (0x0f << 6)
#define BMU3_RELEASE (0x07 << 3)
#define BMU3_VERSION (0x07 << 0)
#define BMU3_DMA_CSR_TRIGGER_TIMER (0xffff << 16)
#define BMU3_DMA_CSR_LEN_MOD (0x03 << 3)
#define BMU3_DMA_CSR_TRIGGER (0x01 << 2)
#define BMU3_DMA_CSR_EN (0x01 << 0)
#define BMU3_DMA_CSR_RESP_ERR (0x01 << 5)
#define BMU3_DMA_CSR_BUSY (0x01 << 1)
#define BMU3_DMA_CFG_WR_BASE_ADDR (0xfffffff << 0)
#define BMU3_DMA_CFG1_WR_LEN (0xffff << 16)
#define BMU3_DMA_RE_BASE_HADDR (0xff << 0)
#define BMU3_DMA_RE_BASE_LADDR (0xffffffff << 0)
#define BMU3_DMA_RE_LEN (0xffff << 0)
#define BMU3_AXIW_WR_ADDR_28BIT (0xfffffff << 0)
#define BMU3_AXIW_WR_LEN_32BIT (0xffffffff << 0)
#define BMU3_DEVICE_ID (0xff << 8)
#define BMU3_DEVICE_ID_MASK (0xff << 0)

#define PMU_DURA_THRESHOLD_W_SHIFT 0
#define PMU_DURA_THRESHOLD_W_MASK (0xffff << PMU_DURA_THRESHOLD_W_SHIFT)
#define PMU_DURA_THRESHOLD_R_SHIFT 16
#define PMU_DURA_THRESHOLD_R_MASK (0xffff << PMU_DURA_THRESHOLD_R_SHIFT)

#define PERIOD_MODE (0 << 8)
#define SINGLE_MODE (1 << 8)

#define AXID_SHIFT 16
#define AXID_EN (1 << 0)
#define AXID_MASK_SHIFT 0
#define AXID_MASK (0xffff << AXID_MASK_SHIFT)

// VRD0 and VWR0 share one configuration
// VRD1 and VWR1 share one configuration
#define ALIGN_FILTER_CNT0_EN (1 << 0)
#define ALIGN_FILTER_CNT1_EN (1 << 1)
#define ADDR_FILTER_CNT0_EN (1 << 4)
#define ADDR_FILTER_CNT1_EN (1 << 5)
#define SIZE_FILTER_CNT0_EN (1 << 8)
#define SIZE_FILTER_CNT1_EN (1 << 9)
#define LEN_FILTER_CNT0_EN (1 << 12)
#define LEN_FILTER_CNT1_EN (1 << 13)

#define SIZE_FLT_CNT0_SHIFT 16
#define SIZE_FLT_CNT0_MASK (7 << SIZE_FLT_CNT0_SHIFT)
#define SIZE_FLT_CNT1_SHIFT 19
#define SIZE_FLT_CNT1_MASK (7 << SIZE_FLT_CNT1_SHIFT)

#define LEN_FLT_CNT0_SHIFT 0
#define LEN_FLT_CNT0_MASK (0xff << LEN_FLT_CNT0_SHIFT)
#define LEN_FLT_CNT1_SHIFT 8
#define LEN_FLT_CNT1_MASK (0xff << LEN_FLT_CNT1_SHIFT)

#define ALIGN_FLT_CNT0_SHIFT 16
#define ALIGN_FLT_CNT0_MASK (0xf << ALIGN_FLT_CNT0_SHIFT)
#define ALIGN_FLT_CNT1_SHIFT 20
#define ALIGN_FLT_CNT1_MASK (0xf << ALIGN_FLT_CNT1_SHIFT)

#define PMU_IRQ_SRC_SHIFT 5
#define PMU_IRQ_SRC_MASK (0x3ff << PMU_IRQ_SRC_SHIFT)
#define WDATA_SEL_SHIFT 1
#define WDATA_SEL_MASK (0xf << WDATA_SEL_SHIFT)
#define PMU_CLEAR_INT (1 << 0)
#define BMU3_IRQ_SHIFT 4
#define BMU3_IRQ_MASK (0x07 << BMU3_IRQ_SHIFT)
#define BMU3_IRQ_RESTORE_RST (0)
#define BMU3_IRQ_RESTORE_ACTIVE (1 << 0)

#define IRQ_TARGET_ADDR_SHIFT 16
#define IRQ_TARGET_ADDR_MASK (0x3 << IRQ_TARGET_ADDR_SHIFT)

#define IRQ_SRC_CFG_SHIFT 27
#define IRQ_SRC_CFG_MASK (0x1f << IRQ_SRC_CFG_SHIFT)

#define IRQ_SRC_DMA_SHIFT 5
#define IRQ_SRC_DMA_MASK (0x1 << IRQ_SRC_CFG_SHIFT)

#define BMU3_EXT_INT_EN_SHIFT 0
#define BMU3_EXT_INT_EN_MASK (0x7 << BMU3_EXT_INT_EN_SHIFT)

#define BMU3_EXT_INT_SHIFT 0
#define BMU3_EXT_INT_MASK (0x7 << BMU3_EXT_INT_SHIFT)

// IRQ status define
#define IRQ_SRC_TIME_EXPIRED (1 << 0)
#define IRQ_SRC_TARGET_DATA_OCCUR (1 << 1)
#define IRQ_SRC_WRITE_ERROR_RESP_OCCUR (1 << 2)
#define IRQ_SRC_WRITE_DURATION_FIFOFULL (1 << 3)
#define IRQ_SRC_WRITE_DURATION_CNT_FULL (1 << 4)
#define IRQ_SRC_WRITE_TRANS_NOT_FINISH (1 << 5)
#define IRQ_SRC_READ_DURATION_FIFOFULL (1 << 6)
#define IRQ_SRC_READ_DURATION_CNT_FULL (1 << 7)
#define IRQ_SRC_READ_TRANS_NOT_FINISH (1 << 8)
#define IRQ_SRC_READ_ERROR_RESP_OCCUR (1 << 9)
#define IRQ_SRC_COMBINE_SHIFT 10
#define IRQ_SRC_TARGET_ADDR_R_OCCUR (1 << IRQ_SRC_COMBINE_SHIFT)
#define IRQ_SRC_TARGET_ADDR_W_OCCUR (1 << (IRQ_SRC_COMBINE_SHIFT + 1))
#define IRQ_SRC_TRACE_COMBINE_SHIFT 12
#define IRQ_SRC_CFG_BUSY_FULL (1 << IRQ_SRC_TRACE_COMBINE_SHIFT)
#define IRQ_SRC_CFG_BUFF_FULL (1 << (IRQ_SRC_TRACE_COMBINE_SHIFT + 1))
#define IRQ_SRC_CFG_AXI_FULL (1 << (IRQ_SRC_TRACE_COMBINE_SHIFT + 2))
#define IRQ_SRC_CFG_AXI_RESP_ERR (1 << (IRQ_SRC_TRACE_COMBINE_SHIFT + 3))
#define IRQ_SRC_CFG_WAITTING_ERR (1 << (IRQ_SRC_TRACE_COMBINE_SHIFT + 4))
#define IRQ_SRC_DMA_COMBINE_SHIFT 17
#define IRQ_SRC_CFG_DMA_RESP_ERR (1 << (IRQ_SRC_DMA_COMBINE_SHIFT))
#define IRQ_SRC_SEND_COMBINE_SHIFT 18
#define IRQ_SRC_SEND_QUARTER_EINT (1 << (IRQ_SRC_SEND_COMBINE_SHIFT))
#define IRQ_SRC_SEND_HALF_EINT (1 << (IRQ_SRC_SEND_COMBINE_SHIFT + 1))
#define IRQ_SRC_SEND_3QUARTER_EINT (1 << (IRQ_SRC_SEND_COMBINE_SHIFT + 2))

#define WRITE_ERR_RESP_ID_SHIFT 0
#define WRITE_ERR_RESP_ID_MASK (0xffff << WRITE_ERR_RESP_ID_SHIFT)
#define READ_ERR_RESP_ID_SHIFT 16
#define READ_ERR_RESP_ID_MASK (0xffff << READ_ERR_RESP_ID_SHIFT)

#define RD_MAX_OSTD_SHIFT 8
#define RD_MAX_OSTD_MASK (0xff << RD_MAX_OSTD_SHIFT)
#define WR_MAX_OSTD_SHIFT 24
#define WR_MAX_OSTD_MASK (0xff << WR_MAX_OSTD_SHIFT)

#define BMU3_EINT_QUARTER_SW_FLAG 1
#define BMU3_EINT_HALF_SW_FLAG 2
#define BMU3_EINT_3QUARTER_SW_FLAG 4
#define BMU3_EINT_FULL_SW_FLAG 8

#define BMU3_DEVID_AXID 0
#define BMU3_DEVID_USR 1

#define BMU3_TRIGGER_CH_READ 0
#define BMU3_TRIGGER_CH_WRITE 1

#define BMU3_TRIGGER_SRC_USER 0
#define BMU3_TRIGGER_SRC_ID 1
#define BMU3_TRIGGER_SRC_ADDR 2
#define BMU3_TRIGGER_SRC_DATA 3
#define BMU3_TRIGGER_SRC_SIDEBAND 4
#define BMU3_TRIGGER_SRC_RESERV 5

// perf events define
// event_num = (event_offset_reg - PMU_STS_REG_BASE) / 4
#define DDR_EVENT_READ_DURATION_CNT 0
#define DDR_EVENT_READ_TRANS_CNT 1
#define DDR_EVENT_READ_BYTES 2
#define DDR_EVENT_READ_DURATION_OVER_THRESH 3
#define DDR_EVENT_WRITE_DURATION_CNT 4
#define DDR_EVENT_WRITE_TRANS_CNT 5
#define DDR_EVENT_WRITE_BYTES 6
#define DDR_EVENT_WRITE_DURATION_OVER_THRESH 7
#define DDR_EVENT_VRD0_TRANS_CNT 8
#define DDR_EVENT_VRD1_TRANS_CNT 9
#define DDR_EVENT_DUMMY1 0xa
#define DDR_EVENT_DUMMY2 0xb
#define DDR_EVENT_VWR0_TRANS_CNT 0xc
#define DDR_EVENT_VWR1_TRANS_CNT 0xd
#define DDR_EVENT_DUMMY3 0xe
#define DDR_EVENT_DUMMY4 0xf
#define DDR_EVENT_DUMMY5 0x10
#define DDR_EVENT_DUMMY6 0x11
#define DDR_EVENT_DUMMY7 0x12
#define DDR_EVENT_DUMMY8 0x13
#define DDR_EVENT_RD_MAX_OSTD 0x14
#define DDR_EVENT_WR_MAX_OSTD 0x15
#define DDR_EVENT_DUMMY9 0x16
#define DDR_EVENT_DUMMY10 0x17
#define DDR_EVENT_RD_DLY_CNT 0x18
#define DDR_EVENT_WR_DLY_CNT 0x19

#define DDR_EVENT_MASK 0x1f

#define DDR_EVENT_AXID_MASK 0x20
#define DDR_EVENT_AXID_READ_DURATION_CNT \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_READ_DURATION_CNT)
#define DDR_EVENT_AXID_READ_TRANS_CNT \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_READ_TRANS_CNT)
#define DDR_EVENT_AXID_READ_BYTES (DDR_EVENT_AXID_MASK | DDR_EVENT_READ_BYTES)
#define DDR_EVENT_AXID_READ_DURATION_OVER_THRESH \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_READ_DURATION_OVER_THRESH)
#define DDR_EVENT_AXID_WRITE_DURATION_CNT \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_WRITE_DURATION_CNT)
#define DDR_EVENT_AXID_WRITE_TRANS_CNT \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_WRITE_TRANS_CNT)
#define DDR_EVENT_AXID_WRITE_BYTES (DDR_EVENT_AXID_MASK | DDR_EVENT_WRITE_BYTES)
#define DDR_EVENT_AXID_WRITE_DURATION_OVER_THRESH \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_WRITE_DURATION_OVER_THRESH)
#define DDR_EVENT_AXID_VRD0_TRANS_CNT \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_VRD0_TRANS_CNT)
#define DDR_EVENT_AXID_VRD1_TRANS_CNT \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_VRD1_TRANS_CNT)
#define DDR_EVENT_AXID_VWR0_TRANS_CNT \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_VWR0_TRANS_CNT)
#define DDR_EVENT_AXID_VWR1_TRANS_CNT \
	(DDR_EVENT_AXID_MASK | DDR_EVENT_VWR1_TRANS_CNT)
#define DDR_EVENT_AXID_RD_MAX_OSTD (DDR_EVENT_AXID_MASK | DDR_EVENT_RD_MAX_OSTD)
#define DDR_EVENT_AXID_WR_MAX_OSTD (DDR_EVENT_AXID_MASK | DDR_EVENT_WR_MAX_OSTD)
#define DDR_EVENT_AXID_RD_DLY_CNT (DDR_EVENT_AXID_MASK | DDR_EVENT_RD_DLY_CNT)
#define DDR_EVENT_AXID_WR_DLY_CNT (DDR_EVENT_AXID_MASK | DDR_EVENT_WR_DLY_CNT)

#define DDR_EVENT_MISC_MASK 0x40
#define DDR_EVENT_PMU_EXEC_TIME (DDR_EVENT_MISC_MASK | 0x0)
#define DDR_EVENT_QUARY_TOTAL_BW (DDR_EVENT_MISC_MASK | 0x1)

#define DDR_EVENT_CAPTURE_MASK 0x60
#define DDR_EVENT_CAPTURE_R_DATA (DDR_EVENT_CAPTURE_MASK | 0x0)
#define DDR_EVENT_CAPTURE_W_DATA (DDR_EVENT_CAPTURE_MASK | 0x1)
#define DDR_EVENT_CAPTURE_ADDR (DDR_EVENT_CAPTURE_MASK | 0x2)
#define DDR_EVENT_CAPTURE_ERROR_RESP_W (DDR_EVENT_CAPTURE_MASK | 0x3)
#define DDR_EVENT_CAPTURE_ERROR_RESP_R (DDR_EVENT_CAPTURE_MASK | 0x4)

#define DDR_EVENT_FILTER_MASK \
	(DDR_EVENT_CAPTURE_MASK | DDR_EVENT_MISC_MASK | DDR_EVENT_AXID_MASK)
#define NUM_TIME_EXPIRED_EVENTS (DDR_EVENT_CAPTURE_MASK - 1)
#define NUM_EVENTS 0x65
#define NUM_INST 2
#define INST_ALL -1
#define INST_MISC -2
#define INST_NULL -3
#define NUM_PFT 3

// # address hit compare mode: 0: write; 1: read
#define CM_WRITE 0
#define CM_READ 1

#define to_ddr_pmu(p) container_of(p, struct ddr_pmu, pmu)

#define DDR_PERF_DEV_NAME "a210_ddr"
#define DDR_CPUHP_CB_NAME DDR_PERF_DEV_NAME "_perf_pmu"

static DEFINE_IDA(ddr_ida);

#define APB_CLK (250 * 1000 * 1000) // 250MHz
#define PMU_PERIOD_CNT 40 //in default 10ms, 40 = 1m

#define TRIGGER_MODE PERIOD_MODE
//#define TRIGGER_MODE SINGLE_MODE

#define ADDRMSB 34
#define FMT_HEX 0
#define FMT_DECIMAL 1

#define CLK_1M (1024 * 1024)

#define DDR_BITWIDTH_32 (32)
#define DDR_BITWIDTH_64 (64)
#define DDR_BITWIDTH DDR_BITWIDTH_64

#define ddr0_sys1_reg_ph_base 0x04861000 //ddr0 slc sysreg
#define ddr0_sys2_reg_ph_base 0x04810000 //ddr0 sysreg
#define ddr0_perf_addr_base 0x04811000 //ddr0 perf
#define ddr1_sys1_reg_ph_base 0x05861000 //ddr1 slc sysreg
#define ddr1_sys2_reg_ph_base 0x05810000 //ddr1 sysreg
#define ddr1_perf_addr_base 0x05811000 //ddr1 perf
#define ddr_reg_length (4096)

#define vp_sys1_reg_ph_base 0x06b20000 //vp sysreg
#define vp_sys1_reg_size (4096)
#define vp_sys2_reg_ph_base 0x06b10000 //vp mt
#define vp_sys2_reg_size (1024)
#define vp_sys3_reg_ph_base 0x06bf0000
#define vp_sys3_reg_size (1024 * 1024)

#define vo_sys1_reg_ph_base 0x06720000 //vo sysreg
#define vo_sys1_reg_size (4096)
#define vo_sys2_reg_ph_base 0x06710000 //vo mt
#define vo_sys2_reg_size (1024)
#define vo_sys3_reg_ph_base 0x067f1000 //vo ss
#define vo_sys3_reg_size (4096)

#define vi_sys1_reg_ph_base 0x063a0000 //vi sysreg
#define vi_sys1_reg_size (4096)
#define vi_sys2_reg_ph_base 0x06370000 //vi mt
#define vi_sys2_reg_size (1024)
#define vi_sys3_reg_ph_base 0x063f0000 //vi pct/bpc
#define vi_sys3_reg_size (64 * 1024)

#define peri_sys1_reg_ph_base 0x02010000 //peri1 sysreg
#define peri_sys1_reg_size (4096)
#define peri_sys2_reg_ph_base 0x02030000 //peri1 mt
#define peri_sys2_reg_size (1024)

#define npu_sys1_reg_ph_base 0x07300000 //npu pctrl
#define npu_sys1_reg_size (4096)
#define npu_sys2_reg_ph_base 0x07100000 //npu mt
#define npu_sys2_reg_size (1024)

#define gpu_sys1_reg_ph_base 0x06e00000 //gpu top pctl/top
#define gpu_sys1_reg_size (8192)
#define gpu_sys2_reg_ph_base 0x06d30000 //gpu cctrl/aclk
#define gpu_sys2_reg_size (4096)
#define gpu_sys3_reg_ph_base 0x06d10000 //gpu mt
#define gpu_sys3_reg_size (1024)

//#define pcie_sys1_reg_ph_base         0x02010000
//#define pcie_sys1_reg_size            (4096)
#define pcie_sys2_reg_ph_base 0x0a010000
#define pcie_sys2_reg_size (1024)

#define usb_sys1_reg_ph_base 0x08004000 //pct
#define usb_sys1_reg_size (8192)
#define usb_sys2_reg_ph_base 0x08020000 //usb mt
#define usb_sys2_reg_size (1024)

#define d2d_sys1_reg_ph_base 0x09000000
#define d2d_sys1_reg_size (4096)
#define d2d_sys2_reg_ph_base 0x09030000
#define d2d_sys2_reg_size (1024)
//#define d2d_sys3_reg_ph_base          0x09000000
//#define d2d_sys3_reg_size             (1024)

#define ddr0_phy_addr_base 0x80000000
#define ddr1_phy_addr_base 0x80000100
#define ddr_half_intlv_base_64B_128B 0x80000080
#define ddr_intlv_256 0x100
#define ddr_intlv_128 0x80

#define calibration_ddr_mem1_pa_addr (0xbc500000) //ddr0
#define calibration_ddr_mem2_pa_addr (0xbc600000) //ddr1
#define calibration_vp_mem_pa_addr (0xbc700000) //vp
#define calibration_vi_mem_pa_addr (0xbc800000) //vi
#define calibration_vo_mem_pa_addr (0xbc900000) //vo
#define calibration_gpu_mem_pa_addr (0xbca00000) //gpu
#define calibration_npu_mem_pa_addr (0xbcb00000) //npu
#define calibration_usb_mem_pa_addr (0xbcc00000) //usb
#define calibration_pcie_mem_pa_addr (0xbcd00000) //pcie
#define calibration_d2d_mem_pa_addr (0xbce00000) //d2d
#define calibration_peri_mem_pa_addr (0xbcf00000) //peri

#define resv_trace_raw_len (20 * 1024 * 1024)
#define resv_trace_dec_len 0
#define tst_mem_len 0
#define TST_MEM_1M_OFFSET (tst_mem_len / 4)

/* mt reg*/
#define MT_REG_CTRL 0x0
#define MT_REG_STATUS 0x4
#define MT_REG_CFG0 0x8
#define MT_REG_CFG1 0xc
#define MT_REG_AXI_ATTR 0x10
#define MT_REG_LOOP 0x14
#define MT_REG_PRBS_SEED 0x18
#define MT_REG_WR_SADDR 0x1c
#define MT_REG_ERR_ADDR 0x20
#define MT_REG_ERR_BIT 0x24
#define MT_REG_LOOP_TIME 0x28
#define MT_REG_ADDR_MASK 0x2c
#define MT_REG_FIFO_CTRL 0x30
#define MT_REG_AXI_STATUS 0x40
#define MT_REG_RDATA1 0x44
#define MT_REG_RDATA2 0x48
#define MT_REG_RDATA3 0x4c
#define MT_REG_RDATA4 0x50

#if defined(A210_95P_V2)
#define addr_map_64b_128B_non_half_non_intlv
#define intlv_offset ddr_intlv_128
#elif defined(A210_95P_V1)
#define addr_map_32b_128B_half_non_intlv
#define intlv_offset 0
#elif defined(A210_100P_V1)
#define addr_map_64b_256B_non_half_intlv
#define intlv_offset ddr_intlv_256
#else
#define addr_map_32b_128B_half_intlv
#endif

#if defined(addr_map_64b_256B_non_half_intlv)
#define bmuaxiw_virt_to_phy(x)                                          \
	(x & ddr_intlv_256) ?                                           \
		((((x - ddr1_phy_addr_base) >> 9) << 8) + (x & 0xff)) : \
		((((x - ddr0_phy_addr_base) >> 9) << 8) + (x & 0xff))
#elif defined(addr_map_32b_128B_half_non_intlv)
#define bmuaxiw_virt_to_phy(x) (x - ddr0_phy_addr_base)
#elif defined(addr_map_64b_128B_non_half_non_intlv)
#define bmuaxiw_virt_to_phy(x)                                      \
	(x & ddr_intlv_128) ?                                       \
		((((x - ddr_half_intlv_base_64B_128B) >> 8) << 7) + \
		 (x & 0x7f)) :                                      \
		((((x - ddr0_phy_addr_base) >> 8) << 7) + (x & 0x7f))
#elif defined(addr_map_32b_128B_half_intlv)
#endif

#define BM_LOG_FILE_PATH "/mnt/trc_"
#define BM_CNT_LOG_FILE_PATH "/mnt/cnt_"
#define BM_CNT_TOTAL_LOG_FILE_PATH "/mnt/tol_"
#define BM_PER_CNT_RECORD_SIZE 60 //100//10ms*100 ->1s
#define BM_PER_CNT_BUF_SIZE \
	(sizeof(struct bm_data_info) * BM_PER_CNT_RECORD_SIZE)
#define BM_LOG_FILE_SECONDS (60 * 60)
#define BM_LOG_FILE_MAX_RECORDS (BM_LOG_FILE_SECONDS * 24)

#define BMU_TRACE_CNT 16384 //16384->1M,163840->10M
#define BMU_TRACE_BUF_SIZE (sizeof(struct bmu_trace_frame) * BMU_TRACE_CNT)
#define AXIW_BUF_SIZE (10 * BMU_TRACE_BUF_SIZE)
#define CAP_TRACE_SHOW_SIZE 512
#define SUB_NUM_BIT 4
#define BMU_CH1 0
#define BMU_CH2 1
#define BMU_CHALL 2
#define TRACE_REUSE_REG_MAP 0x8
#define TRACE_REUSE_DDR_MAP 0x4
#define DMA_EN_CH1 0
#define DMA_EN_CH2 1
#define DMA_EN_CHALL 2
#define AXIW_ADDR_VALID 0xfffffff
#define CAP_FRAME_WIDETH 64
#define IRQ_NUM (NUM_INST * 2)
#define DMA_ENABLE 1
#define DMA_DISABLE 0

#define type_width 3
#define awsize_width 3
#define awburst_width 2
#define awlock_width 1
#define awcache_width 4
#define awprot_width 3
#define awqos_width 4
#define awregion_width 4
#define awvalid_width 1
#define awready_width 1

//counter data type
#define cnt_rtr_type 0x8100000000000000
#define cnt_rdbyte_type 0x8200000000000000
#define cnt_rdu_type 0x8300000000000000
#define cnt_rthd_type 0x8400000000000000
#define cnt_rdly_type 0x8500000000000000
#define cnt_r_max_ot_type 0x8600000000000000
#define cnt_vrd_type 0x8700000000000000
#define cnt_wtr_type 0x8800000000000000
#define cnt_wdbyte_type 0x8900000000000000
#define cnt_wdu_type 0x8a00000000000000
#define cnt_wthd_type 0x8b00000000000000
#define cnt_wdly_type 0x8c00000000000000
#define cnt_w_max_ot_type 0x8d00000000000000
#define cnt_vwr_type 0x8e00000000000000
#define cnt_time_start_type 0x9100000000000000
#define cnt_time_stop_type 0x9200000000000000

#define CHN0 1
#define CHN1 2
#define raw_event_inv 0
#define raw_event_cnt 1
#define raw_event_trc 2
#define raw_event_cnt_trc 3

#define tim_delta_type_invalid 0
#define tim_delta_type_aw_b 1
#define tim_delta_type_ar_r 2

#define BMB (1024 * 1024)
#define RAW_BUF_SIZE (5 * BMB)
#define PROC1_BUF_SIZE (2 * BMB)
#define PROC2_BUF_SIZE (2 * BMB)

#define FRAME_INValid 0x00
#define FRAME_CONF_LOW4 0x01
#define FRAME_CONF_HIGH4 0x10
#define FRAME_AW_LOW4 0x02
#define FRAME_AW_HIGH4 0x20
#define FRAME_W_LOW4 0x03
#define FRAME_W_HIGH4 0x30
#define FRAME_B_LOW4 0x04
#define FRAME_B_HIGH4 0x40
#define FRAME_AR_LOW4 0x05
#define FRAME_AR_HIGH4 0x50
#define FRAME_R_LOW4 0x06
#define FRAME_R_HIGH4 0x60
#define FRAME_T_LOW4 0x07
#define FRAME_T_HIGH4 0x70
#define FRAME_WIDETH_TYPE1 88
#define FRAME_WIDETH_TYPE2 44
#define TYPE_WIDETH 3
#define LATENCY_WIDETH 7
#define TYPE_MASK 0x77
#define PACKET1_MASK 0x003f8000
#define PACKET1_SHIFT 15
#define PACKET2_MASK 0x3FC00000
#define PACKET2_SHIFT 22
#define SUB_NUM_BIT 4
#define type_low 0
#define type_high 1
#define type_fix_shift 7
#define type_wrd_shift 6
#define type_chn_shift 4
#define type_mon_shift 0
#define type_master_shift 0
#define type_fix (1 << type_fix_shift)
#define type_wr_rd (1 << type_wrd_shift)
#define type_chn (1 << type_chn_shift)
#define type_mon_dev(x) (x << type_mon_shift)
#define type_master_event(x) (x << type_master_shift)

#define BUFFER_SIZE (1024 * 640)
#define CAPTURE_SIZE 16

#ifndef __io_ww
#define __io_ww() __asm__ __volatile__("fence w,w" : : : "memory")
#endif

#ifndef __io_wr
#define __io_wr() __asm__ __volatile__("fence w,r" : : : "memory")
#endif

#ifndef sync_is
#define sync_is() asm volatile(".long 0x01a0000b")
#endif

#define bmu_ctl_cnt_en (1 << 0)
#define bmu_ctl_cap_en (1 << 1)
#define bmu_ctl_trc_en (1 << 2)
#define bmu_ctl_trg_en (1 << 3)
#define bmu_ctl_dma_en (1 << 4)
#define bmu_ctl_reuse (1 << 5)
#define bmu_ctl_pause (1 << 6)
#define bmu_ctl_ch0 (1 << 30)
#define bmu_ctl_ch1 (1 << 31)
#define bmu_ctl_chall (bmu_ctl_ch0 | bmu_ctl_ch1)
/*--------------------------- en ---------------------------------------
|31-30|29--24|23--16 |15-8| 7  | 6   |   5  |  4  |  3  | 2  | 1  | 0  |
| chn | resv | resv  |resv|resv|pause| reuse| dma | trg |trc |cap |cnt |
-----------------------------------------------------------------------*/
typedef struct {
	u32 bmu_ctl;
	//u32 bmu_rst;
	//u32 pause_en;
	//u32 reuse_en;
} bmu_control_t;

struct iomem_base {
	void __iomem *base;
	bool enable;
};

struct trace_point {
	unsigned int reg_offset;
	unsigned int value;
};

typedef struct {
	phys_addr_t bmu_resv_start;
	phys_addr_t bmu_resv_end;
	size_t bmu_resv_len;
	struct resource resv_res[10];
} bmu_resouce_t;

struct bmu3_para {
	char *name;
	u8 mod_id;
	u8 addr_len;
	u8 id_len;
	u8 axi_width_len;
	u8 chn_id[NUM_INST];
	u16 axi_data_len;
	u32 mode[NUM_INST];
	u32 userid[NUM_INST];
	bmu_control_t bm_ctl;
	u32 trace_en;
	u32 trace_rst;
	u32 trace_pause_en;
	u32 trace_reuse_en;
	u32 reg_group_num;
	u32 reg1_size;
	u32 reg2_size;
	u32 reg3_size;
	u32 reg4_size;
	u32 reg1_pa[NUM_INST];
	u32 reg2_pa[NUM_INST];
	u32 reg3_pa[NUM_INST];
	u32 reg4_pa[NUM_INST];
	u32 reg_pa[NUM_INST]; //reg base
	phys_addr_t reg_axiw_pa[NUM_INST];
	void __iomem *reg1_va[NUM_INST];
	void __iomem *reg2_va[NUM_INST];
	void __iomem *reg3_va[NUM_INST];
	void __iomem *reg4_va[NUM_INST];
	void __iomem *reg_va[NUM_INST];
	void *reg_axiw_va[NUM_INST];
	bmu_reg_data_t reg_cfg[NUM_INST];
	u32 reg_len[NUM_INST];
	u32 reg_axiw_length[NUM_INST];
	u32 buff_full_num[NUM_INST];
	void *bm_cnt_buf[NUM_INST];
	void *tol_bm_cnt_buf;
	struct resource resv_res;
	u32 cal_cap_frame_cnt;
	Packet_Header *pCap_head;
	u32 capture_data[2][128]; //64*2 frame
	u32 trace_data[2][256]; //16*2 trace
	u32 bm_count_rec_max_len;
	struct event_storage raw_frame_array[NUM_INST];
	cnt_proc1_data_t cnt_proc1_data[NUM_INST];
	cnt_proc2_data_t cnt_proc2_data[NUM_INST];
	pft_event_array_t pft_event_data[NUM_INST];
	u64 g_tim[NUM_INST];
	u32 cali_mod;
};

struct ddr_pmu {
	int id;
	int period;
	int freq_khz;
	int active_events;
	unsigned int cpu;
	int irq[IRQ_NUM]; //modify irq to irq array
	u32 hwc_trace_version[NUM_INST]; //for trace
	u64 hwc_version[NUM_INST]; //for counter
	int hwc_active_events[NUM_INST];
	struct pmu pmu;
	struct miscdevice misc;
	struct iomem_base pmu_base[NUM_INST];
	struct hlist_node node;
	struct device *dev;
	struct perf_event *events[NUM_INST][NUM_EVENTS];
	struct bmu3_para *bmu_private; //private data
	struct task_struct *task1; //ch0 trace process task
	struct task_struct *task2; //ch1 trace process task
	struct completion comp1; //sync signal
	struct completion comp2; //sync signal
	atomic_t bm_stat[NUM_INST]; //trace stm
	atomic_t bm_cnt_stat[NUM_INST]; //cnt stm
	atomic_t event_flag[NUM_INST]; //trace irq flag
	atomic_t cnt_event_flag[NUM_INST]; //cnt irq flag
	spinlock_t bm_lock;
	struct smem_map_list mem_mp;
	struct hrtimer trigger_timer;
	struct timespec64 t_begin;
	//struct resource resv_res;
};

// # for debugfs configuration
struct ddr_pmu_trace {
	unsigned int trace_enable; // 0: disable; 1: enable
	unsigned int trace_period_ms; // should no less than bmu->period
	unsigned int trace_count; // trace count
	unsigned int trace_mode; // 0: uart log; 1: to ddr reserved memory
	unsigned int trace_data_fmt; // 0: hex; 1: decimal
};

//static long DDR_MT_3200 = (long)3200*CLK_1M;
static long DDR_MT_3733 = (long)3733 * CLK_1M;
static struct ddr_pmu_trace pmu_trace = { 0 };

//static struct dentry *ddr_pmu_dir;
//static struct dentry *events_dir;
//static struct dentry *ver_dir;
struct ddr_pmu *pmu_ddr;
bmu_resouce_t bmu_buff;

static struct bmu3_para bmu3_para_data[] = {
	[BMU_DDR] = { .name = "bmu_ddr",
		      .reg_group_num = 2,
		      .reg1_pa = { ddr0_sys1_reg_ph_base,
				   ddr1_sys1_reg_ph_base },
		      .reg2_pa = { ddr0_sys2_reg_ph_base,
				   ddr1_sys2_reg_ph_base },
		      .reg3_pa = { ddr0_perf_addr_base, ddr1_perf_addr_base },
		      .reg1_size = ddr_reg_length,
		      .reg2_size = ddr_reg_length,
		      .reg3_size = ddr_reg_length,
		      .resv_res = { .name = "bmu_ddr",
				    .start = 0,
				    .end = 0,
				    .flags = IORESOURCE_BUSY |
					     IORESOURCE_MEM } },
	[BMU_GPU] = { .name = "bmu_gpu",
		      .reg_group_num = 1,
		      .reg1_pa = { gpu_sys1_reg_ph_base },
		      .reg1_size = gpu_sys1_reg_size,
		      .reg2_pa = { gpu_sys2_reg_ph_base },
		      .reg2_size = gpu_sys2_reg_size,
		      .reg3_pa = { gpu_sys3_reg_ph_base },
		      .reg3_size = gpu_sys3_reg_size,
		      .resv_res = { .name = "bmu_gpu",
				    .start = 0,
				    .end = 0,
				    .flags = IORESOURCE_BUSY |
					     IORESOURCE_MEM } },
	[BMU_NPU] = { .name = "bmu_npu",
		      .reg_group_num = 1,
		      .reg1_pa = { npu_sys1_reg_ph_base },
		      .reg1_size = npu_sys1_reg_size,
		      .reg2_pa = { npu_sys2_reg_ph_base },
		      .reg2_size = npu_sys2_reg_size,
		      .resv_res = { .name = "bmu_npu",
				    .start = 0,
				    .end = 0,
				    .flags = IORESOURCE_BUSY |
					     IORESOURCE_MEM } },
	[BMU_PCIE] = { .name = "bmu_pcie",
		       .reg_group_num = 1,
		       .reg2_pa = { pcie_sys2_reg_ph_base },
		       .reg2_size = pcie_sys2_reg_size,
		       .resv_res = { .name = "bmu_pcie",
				     .start = 0,
				     .end = 0,
				     .flags = IORESOURCE_BUSY |
					      IORESOURCE_MEM } },
	[BMU_USB] = { .name = "bmu_usb",
		      .reg_group_num = 1,
		      .reg1_pa = { usb_sys1_reg_ph_base },
		      .reg1_size = usb_sys1_reg_size,
		      .reg2_pa = { usb_sys2_reg_ph_base },
		      .reg2_size = usb_sys2_reg_size,
		      .resv_res = { .name = "bmu_usb",
				    .start = 0,
				    .end = 0,
				    .flags = IORESOURCE_BUSY |
					     IORESOURCE_MEM } },
	[BMU_VO] = { .name = "bmu_vo",
		     .reg_group_num = 1,
		     .reg1_pa = { vo_sys1_reg_ph_base },
		     .reg1_size = vo_sys1_reg_size,
		     .reg2_pa = { vo_sys2_reg_ph_base },
		     .reg2_size = vo_sys2_reg_size,
		     .reg3_pa = { vo_sys3_reg_ph_base },
		     .reg3_size = vo_sys3_reg_size,
		     .resv_res = { .name = "bmu_vo",
				   .start = 0,
				   .end = 0,
				   .flags = IORESOURCE_BUSY | IORESOURCE_MEM } },
	[BMU_VI] = { .name = "bmu_vi",
		     .reg_group_num = 1,
		     .reg1_pa = { vi_sys1_reg_ph_base },
		     .reg1_size = vi_sys1_reg_size,
		     .reg2_pa = { vi_sys2_reg_ph_base },
		     .reg2_size = vi_sys2_reg_size,
		     .reg3_pa = { vi_sys3_reg_ph_base },
		     .reg3_size = vi_sys3_reg_size,
		     .resv_res = { .name = "bmu_vi",
				   .start = 0,
				   .end = 0,
				   .flags = IORESOURCE_BUSY | IORESOURCE_MEM } },
	[BMU_VP] = { .name = "bmu_vp",
		     .reg_group_num = 1,
		     .reg1_pa = { vp_sys1_reg_ph_base },
		     .reg1_size = vp_sys1_reg_size,
		     .reg2_pa = { vp_sys2_reg_ph_base },
		     .reg2_size = vp_sys2_reg_size,
		     .reg3_pa = { vp_sys3_reg_ph_base },
		     .reg3_size = vp_sys3_reg_size,
		     .resv_res = { .name = "bmu_vp",
				   .start = 0,
				   .end = 0,
				   .flags = IORESOURCE_BUSY | IORESOURCE_MEM } },
	[BMU_PERI] = { .name = "bmu_peri",
		       .reg_group_num = 1,
		       .reg1_pa = { peri_sys1_reg_ph_base },
		       .reg1_size = peri_sys1_reg_size,
		       .reg2_pa = { peri_sys2_reg_ph_base },
		       .reg2_size = peri_sys2_reg_size,
		       .resv_res = { .name = "bmu_peri",
				     .start = 0,
				     .end = 0,
				     .flags = IORESOURCE_BUSY |
					      IORESOURCE_MEM } },
	[BMU_D2D] = { .name = "bmu_d2d",
		      .reg_group_num = 1,
		      .reg1_pa = { d2d_sys1_reg_ph_base },
		      .reg1_size = d2d_sys1_reg_size,
		      .reg2_pa = { d2d_sys2_reg_ph_base },
		      .reg2_size = d2d_sys2_reg_size,
		      .resv_res = { .name = "bmu_d2d",
				    .start = 0,
				    .end = 0,
				    .flags = IORESOURCE_BUSY |
					     IORESOURCE_MEM } },
};

static const struct of_device_id a210_bmu_dt_ids[] = {
	{
		.compatible = "zhihe,a210-ddr-bmu",
		.data = &bmu3_para_data[BMU_DDR],
	},
	{
		.compatible = "zhihe,a210-gpu-bmu",
		.data = &bmu3_para_data[BMU_GPU],
	},
	{
		.compatible = "zhihe,a210-npu-bmu",
		.data = &bmu3_para_data[BMU_NPU],
	},
	{
		.compatible = "zhihe,a210-pcie-bmu",
		.data = &bmu3_para_data[BMU_PCIE],
	},
	{
		.compatible = "zhihe,a210-usb-bmu",
		.data = &bmu3_para_data[BMU_USB],
	},
	{
		.compatible = "zhihe,a210-vo-bmu",
		.data = &bmu3_para_data[BMU_VO],
	},
	{
		.compatible = "zhihe,a210-vi-bmu",
		.data = &bmu3_para_data[BMU_VI],
	},
	{
		.compatible = "zhihe,a210-vp-bmu",
		.data = &bmu3_para_data[BMU_VP],
	},
	{
		.compatible = "zhihe,a210-peri-bmu",
		.data = &bmu3_para_data[BMU_PERI],
	},
	{
		.compatible = "zhihe,a210-d2d-bmu",
		.data = &bmu3_para_data[BMU_D2D],
	},
};

static void bmu_trace_reset(struct ddr_pmu *pmu, u8 ch, u32 enable);
static void bmu_dma_enable(struct ddr_pmu *pmu, u8 ch, u32 enable);
static void bmu_capture_en(struct ddr_pmu *pmu, u8 ch, u8 out_sel, bool enable);
static void bmu3_ext_int_enable(struct ddr_pmu *pmu, u8 ch, u32 enable);
static void bmu3_send_loop_enable(struct ddr_pmu *pmu, u8 ch, bool en);
static void bmu3_sideband_enable(struct ddr_pmu *pmu, u8 ch, bool en);
static void bmu_trace_enable(struct ddr_pmu *pmu, u8 ch, u32 enable);
static u16 decode_ulaw(u8 comp_data) __attribute__((unused));
static int bmu3_get_trig_cnt(struct ddr_pmu *pmu, u8 ch)
	__attribute__((unused));
static int bmu3_get_trig_state(struct ddr_pmu *pmu, u8 ch)
	__attribute__((unused));
static int bmu3_get_cnt_overflow(struct ddr_pmu *pmu, u8 ch)
	__attribute__((unused));
static void bmu_trc_fast_disable(struct ddr_pmu *pmu, u8 ch)
	__attribute__((unused));
static int bmu_trc_fast_enable(struct ddr_pmu *pmu, u8 ch)
	__attribute__((unused));
static int bmu_trace_init(struct ddr_pmu *pmu);
static void bmu3_trigger_enable(struct ddr_pmu *pmu, u8 ch, bool en);
static void show_raw_data(struct bmu3_para *private, u8 ch)
	__attribute__((unused));
static void bmu3_trigger_clr(struct ddr_pmu *pmu, u8 ch, bool en)
	__attribute__((unused));
static void bmu3_trigger_cnt_clr_set(struct ddr_pmu *pmu, u8 ch, bool en)
	__attribute__((unused));
static int get_mod_id(char *name) __attribute__((unused));
static struct bm_time_info get_current_time(void) __attribute__((unused));

static ssize_t ddr_pmu_event_show(struct device *dev,
				  struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

#define A210_DDR_PMU_EVENT_ATTR(_name, _id)                            \
	(&((struct perf_pmu_events_attr[]){ {                          \
		.attr = __ATTR(_name, 0444, ddr_pmu_event_show, NULL), \
		.id = _id,                                             \
	} })[0]                                                        \
		  .attr.attr)

static struct attribute *ddr_perf_events_attrs[] = {
	A210_DDR_PMU_EVENT_ATTR(rd_duration_cnt, DDR_EVENT_READ_DURATION_CNT),
	A210_DDR_PMU_EVENT_ATTR(rd_trans_cnt, DDR_EVENT_READ_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(rd_byte_cnt, DDR_EVENT_READ_BYTES),
	A210_DDR_PMU_EVENT_ATTR(rd_duration_cnt_over_threshold,
				DDR_EVENT_READ_DURATION_OVER_THRESH),
	A210_DDR_PMU_EVENT_ATTR(wr_duration_cnt, DDR_EVENT_WRITE_DURATION_CNT),
	A210_DDR_PMU_EVENT_ATTR(wr_trans_cnt, DDR_EVENT_WRITE_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(wr_byte_cnt, DDR_EVENT_WRITE_BYTES),
	A210_DDR_PMU_EVENT_ATTR(wr_duration_cnt_over_threshold,
				DDR_EVENT_WRITE_DURATION_OVER_THRESH),
	A210_DDR_PMU_EVENT_ATTR(vrd0_trans_cnt, DDR_EVENT_VRD0_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(vrd1_trans_cnt, DDR_EVENT_VRD1_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(vwr0_trans_cnt, DDR_EVENT_VWR0_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(vwr1_trans_cnt, DDR_EVENT_VWR1_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(rd_max_ostd, DDR_EVENT_RD_MAX_OSTD),
	A210_DDR_PMU_EVENT_ATTR(wr_max_ostd, DDR_EVENT_WR_MAX_OSTD),
	A210_DDR_PMU_EVENT_ATTR(rd_dly_cnt, DDR_EVENT_RD_DLY_CNT),
	A210_DDR_PMU_EVENT_ATTR(wr_dly_cnt, DDR_EVENT_WR_DLY_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_rd_duration_cnt,
				DDR_EVENT_AXID_READ_DURATION_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_rd_trans_cnt,
				DDR_EVENT_AXID_READ_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_rd_bytes, DDR_EVENT_AXID_READ_BYTES),
	A210_DDR_PMU_EVENT_ATTR(axid_rd_duration_cnt_over_threshold,
				DDR_EVENT_AXID_READ_DURATION_OVER_THRESH),
	A210_DDR_PMU_EVENT_ATTR(axid_wr_duration_cnt,
				DDR_EVENT_AXID_WRITE_DURATION_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_wr_trans_cnt,
				DDR_EVENT_AXID_WRITE_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_wr_bytes, DDR_EVENT_AXID_WRITE_BYTES),
	A210_DDR_PMU_EVENT_ATTR(axid_wr_duration_cnt_over_threshold,
				DDR_EVENT_AXID_WRITE_DURATION_OVER_THRESH),
	A210_DDR_PMU_EVENT_ATTR(axid_vrd0_trans_cnt,
				DDR_EVENT_AXID_VRD0_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_vrd1_trans_cnt,
				DDR_EVENT_AXID_VRD1_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_vwr0_trans_cnt,
				DDR_EVENT_AXID_VWR0_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_vwr1_trans_cnt,
				DDR_EVENT_AXID_VWR1_TRANS_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_rd_max_ostd, DDR_EVENT_AXID_RD_MAX_OSTD),
	A210_DDR_PMU_EVENT_ATTR(axid_wr_max_ostd, DDR_EVENT_AXID_WR_MAX_OSTD),
	A210_DDR_PMU_EVENT_ATTR(axid_rd_dly_cnt, DDR_EVENT_AXID_RD_DLY_CNT),
	A210_DDR_PMU_EVENT_ATTR(axid_wr_dly_cnt, DDR_EVENT_AXID_WR_DLY_CNT),
	A210_DDR_PMU_EVENT_ATTR(pmu_exec_time, DDR_EVENT_PMU_EXEC_TIME),
	A210_DDR_PMU_EVENT_ATTR(total_bw, DDR_EVENT_QUARY_TOTAL_BW),
	A210_DDR_PMU_EVENT_ATTR(capture_rdata, DDR_EVENT_CAPTURE_R_DATA),
	A210_DDR_PMU_EVENT_ATTR(capture_wdata, DDR_EVENT_CAPTURE_W_DATA),
	A210_DDR_PMU_EVENT_ATTR(capture_addr, DDR_EVENT_CAPTURE_ADDR),
	A210_DDR_PMU_EVENT_ATTR(capture_error_resp_w,
				DDR_EVENT_CAPTURE_ERROR_RESP_W),
	A210_DDR_PMU_EVENT_ATTR(capture_error_resp_r,
				DDR_EVENT_CAPTURE_ERROR_RESP_R),
	NULL,
};

static struct attribute_group ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = ddr_perf_events_attrs,
};

#define ATTR_EVENT_MASK 0xffUL
#define ATTR_INST_SHIFT 8
#define ATTR_INST_MASK (0xffUL << ATTR_INST_SHIFT)
#define ATTR_CHN_EN_SHIFT 8
#define ATTR_CHN_EN_MASK (0x1fUL << ATTR_CHN_EN_SHIFT)
#define ATTR_AXID_SHIFT 0
#define ATTR_AXID_MASK (0xffffffffUL << ATTR_AXID_SHIFT)
#define ATTR_DURA_THRESHOLD_R_SHIFT 48
#define ATTR_DURA_THRESHOLD_R_MASK (0xffffUL << ATTR_DURA_THRESHOLD_R_SHIFT)
#define ATTR_DURA_THRESHOLD_W_SHIFT 32
#define ATTR_DURA_THRESHOLD_W_MASK (0xffffUL << ATTR_DURA_THRESHOLD_W_SHIFT)
#define ATTR_FILTER_START_ADDR_SHIFT 32
#define ATTR_FILTER_START_ADDR_MASK \
	(0xffffffffUL << ATTR_FILTER_START_ADDR_SHIFT)
#define ATTR_FILTER_END_ADDR_SHIFT 0
#define ATTR_FILTER_END_ADDR_MASK (0xffffffffUL << ATTR_FILTER_END_ADDR_SHIFT)
#define ATTR_FILTER_SIZE_SHIFT 16
#define ATTR_FILTER_SIZE_MASK (0xffUL << ATTR_FILTER_SIZE_SHIFT)
#define ATTR_FILTER_LEN_SHIFT 24
#define ATTR_FILTER_LEN_MASK (0xffUL << ATTR_FILTER_LEN_SHIFT)
#define ATTR_FILTER_ALIGN_SHIFT 32
#define ATTR_FILTER_ALIGN_MASK (0xffUL << ATTR_FILTER_ALIGN_SHIFT)
#define ATTR_CAP_DATA_SHIFT 0
#define ATTR_CAP_DATA_MASK (0xffffffffUL << ATTR_CAP_DATA_SHIFT)
#define ATTR_CAP_START_ADDR_SHIFT 0
#define ATTR_CAP_START_ADDR_MASK (0x3ffffffffUL << ATTR_CAP_START_ADDR_SHIFT)
#define ATTR_CAP_END_ADDR_SHIFT 0
#define ATTR_CAP_END_ADDR_MASK (0x3ffffffffUL << ATTR_CAP_END_ADDR_SHIFT)

PMU_FORMAT_ATTR(event, "config:0-7");
PMU_FORMAT_ATTR(inst_id, "config:8-15");
//PMU_FORMAT_ATTR(chn_en, "config:8-12");
PMU_FORMAT_ATTR(axi_id, "config1:16-31");
PMU_FORMAT_ATTR(axi_mask, "config1:0-15");
PMU_FORMAT_ATTR(dura_threshold_r, "config1:48-63");
PMU_FORMAT_ATTR(dura_threshold_w, "config1:32-47");
PMU_FORMAT_ATTR(flt_start_addr, "config2:32-63"); // [33:0] >> 4
PMU_FORMAT_ATTR(flt_end_addr, "config2:0-31"); // [33:0] >> 4
PMU_FORMAT_ATTR(flt_size, "config:16-23");
PMU_FORMAT_ATTR(flt_len, "config:24-31");
PMU_FORMAT_ATTR(flt_align, "config:32-39");
PMU_FORMAT_ATTR(cap_chn_en, "config:8-12");
PMU_FORMAT_ATTR(cap_data, "config1:0-31");
PMU_FORMAT_ATTR(cap_start_addr, "config1:0-31"); // [33:0] >> 4
PMU_FORMAT_ATTR(cap_end_addr, "config2:0-31"); // [33:0] >> 4

static struct attribute *ddr_perf_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_axi_id.attr,
	&format_attr_axi_mask.attr,
	&format_attr_inst_id.attr,
	&format_attr_dura_threshold_r.attr,
	&format_attr_dura_threshold_w.attr,
	&format_attr_flt_start_addr.attr,
	&format_attr_flt_end_addr.attr,
	&format_attr_flt_size.attr,
	&format_attr_flt_len.attr,
	&format_attr_flt_align.attr,
	&format_attr_cap_chn_en.attr,
	&format_attr_cap_data.attr,
	&format_attr_cap_start_addr.attr,
	&format_attr_cap_end_addr.attr,
	NULL,
};

static struct attribute_group ddr_perf_format_attr_group = {
	.name = "format",
	.attrs = ddr_perf_format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&ddr_perf_events_attr_group,
	&ddr_perf_format_attr_group,
	NULL,
};

//add invalid cache func
void dma_inv_range(unsigned long start, unsigned long end)
{
	//unsigned long i asm("a0") = start & ~(L1_CACHE_BYTES - 1);
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);
	asm volatile("mv a0, %0" : "=r"(i));
	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile(".long 0x02a5000b"); /* dcache.ipa a0 */
	sync_is();
}

static long __get_ddr_freq(void)
{
	return (long)DDR_MT_3733;
}

static long __get_ddr_bitwidth(void)
{
	return (long)DDR_BITWIDTH;
}

static bool ddr_perf_is_axid_masked(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_FILTER_MASK;

	return (val == DDR_EVENT_AXID_MASK);
}

static bool ddr_perf_is_misc_masked(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_FILTER_MASK;

	return (val == DDR_EVENT_MISC_MASK);
}

static bool ddr_perf_is_capture_masked(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_FILTER_MASK;

	return (val == DDR_EVENT_CAPTURE_MASK);
}

static bool ddr_perf_contain_threshold(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_MASK;

	if (val == DDR_EVENT_WRITE_DURATION_OVER_THRESH)
		return 0;
	else if (val == DDR_EVENT_READ_DURATION_OVER_THRESH)
		return 1;

	return -1;
}

static int ddr_perf_contains_filtered(struct perf_event *event)
{
	int val = event->attr.config & ATTR_EVENT_MASK & DDR_EVENT_MASK;

	if ((val == DDR_EVENT_VRD0_TRANS_CNT) ||
	    (val == DDR_EVENT_VWR0_TRANS_CNT))
		return 0;
	else if ((val == DDR_EVENT_VRD1_TRANS_CNT) ||
		 (val == DDR_EVENT_VWR1_TRANS_CNT))
		return 1;

	return -1;
}

// enable instance for operating later
static void ddr_pmu_enable_inst(struct ddr_pmu *pmu, int inst)
{
	struct bmu3_para *private;
	int i;

	private = pmu->bmu_private;

	for (i = 0; i < private->reg_group_num; i++) {
		if ((inst == i) || (inst == INST_ALL))
			pmu->pmu_base[i].enable = true;
		else
			pmu->pmu_base[i].enable = false;
	}
}

static void ddr_perf_free_counter(struct ddr_pmu *pmu, int event_id, int inst)
{
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;

	for (i = 0; i < private->reg_group_num; i++) {
		if ((inst == i) || (inst == INST_ALL) || (inst == INST_MISC)) {
			if (pmu->events[i][event_id] != NULL) {
				pmu->events[i][event_id] = NULL;
				pmu->hwc_active_events[i]--;
				pmu->active_events--;
			}
		}
	}
}

static u64 ddr_perf_read_counter(struct ddr_pmu *pmu, int event_id, int inst)
{
	u64 ret = 0;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		ret += readl_relaxed(pmu->pmu_base[i].base + PMU_READ +
				     (event_id & DDR_EVENT_MASK) * 4);
	}

	return ret;
}

static u64 ddr_perf_read_max_counter(struct ddr_pmu *pmu, int event_id,
				     int inst)
{
	u64 ret = 0, val = 0;
	int i;
	struct bmu3_para *private;
	private = pmu->bmu_private;

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_READ +
				    (event_id & DDR_EVENT_MASK) * 4);

		if (val > ret)
			ret = val;
	}

	return ret;
}

static int ddr_perf_event_init(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (event->attr.type != event->pmu->type) {
		dev_err(pmu->dev, "un-support event attr type 0x%x != 0x%x\n",
			event->attr.type, event->pmu->type);
		return -ENOENT;
	}

	if (event->cpu < 0) {
		dev_err(pmu->dev, "Can't provide per-task data!\n");
		return -EOPNOTSUPP;
	}

	event->cpu = pmu->cpu;
	hwc->config = event->attr.config & ATTR_EVENT_MASK;
	hwc->idx = -1;

	return 0;
}

static void ddr_perf_event_update(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	u64 delta, val;
	struct timespec64 t_end;
	int inst = hwc->idx;

	if (ddr_perf_is_misc_masked(event)) {
		if ((cfg & ATTR_EVENT_MASK) == DDR_EVENT_PMU_EXEC_TIME) {
			ktime_get_real_ts64(&t_end);
			delta = (t_end.tv_sec - pmu->t_begin.tv_sec) * 1000 *
					1000 +
				(t_end.tv_nsec - pmu->t_begin.tv_nsec) / 1000;
			local64_set(&event->count, delta);
		} else if ((cfg & ATTR_EVENT_MASK) ==
			   DDR_EVENT_QUARY_TOTAL_BW) {
			delta = __get_ddr_freq() * __get_ddr_bitwidth() / 8;
			local64_set(&event->count, delta);
		}
		return;
	} else if (ddr_perf_is_capture_masked(event)) {
		local64_add(1, &event->count);
		return;
	}

	if ((cfg & ATTR_EVENT_MASK & DDR_EVENT_MASK) == DDR_EVENT_RD_MAX_OSTD) {
		delta = ddr_perf_read_max_counter(pmu, DDR_EVENT_RD_MAX_OSTD,
						  inst);
		val = (delta & RD_MAX_OSTD_MASK) >> RD_MAX_OSTD_SHIFT;
		if (val > local64_read(&event->count)) {
			local64_set(&event->count, val);
			local64_set(&hwc->prev_count, val);
		}
	} else if ((cfg & ATTR_EVENT_MASK & DDR_EVENT_MASK) ==
		   DDR_EVENT_WR_MAX_OSTD) {
		delta = ddr_perf_read_max_counter(pmu, DDR_EVENT_RD_MAX_OSTD,
						  inst);
		val = (delta & WR_MAX_OSTD_MASK) >> WR_MAX_OSTD_SHIFT;
		if (val > local64_read(&event->count)) {
			local64_set(&event->count, val);
			local64_set(&hwc->prev_count, val);
		}
	} else {
		delta = ddr_perf_read_counter(pmu, cfg & ATTR_EVENT_MASK, inst);
		local64_add(delta, &event->count);
		local64_set(&hwc->prev_count, delta);
	}
}

static void ddr_perf_event_update_by_inst(struct perf_event *event, int inst)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	u64 delta, val;

	if ((cfg & ATTR_EVENT_MASK & DDR_EVENT_MASK) == DDR_EVENT_RD_MAX_OSTD) {
		delta = ddr_perf_read_max_counter(pmu, DDR_EVENT_RD_MAX_OSTD,
						  inst);
		val = (delta & RD_MAX_OSTD_MASK) >> RD_MAX_OSTD_SHIFT;
		if (val > local64_read(&event->count)) {
			local64_set(&event->count, val);
			local64_set(&hwc->prev_count, val);
		}
	} else if ((cfg & ATTR_EVENT_MASK & DDR_EVENT_MASK) ==
		   DDR_EVENT_WR_MAX_OSTD) {
		delta = ddr_perf_read_max_counter(pmu, DDR_EVENT_RD_MAX_OSTD,
						  inst);
		val = (delta & WR_MAX_OSTD_MASK) >> WR_MAX_OSTD_SHIFT;
		if (val > local64_read(&event->count)) {
			local64_set(&event->count, val);
			local64_set(&hwc->prev_count, val);
		}
	} else {
		delta = ddr_perf_read_counter(pmu, cfg & ATTR_EVENT_MASK, inst);
		local64_add(delta, &event->count);
		local64_set(&hwc->prev_count, delta);
	}
}

static void ddr_pmu_inst_reset(struct ddr_pmu *pmu, int inst)
{
	int val;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		val &= ~PMU_RESET;
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);

		val |= PMU_RESET;
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);
	}
}

static int ddr_pmu_query_irq_sts(struct ddr_pmu *pmu, int inst)
{
	int val, val2, val3, val4, val5;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_INT_REG);
		val = (val & PMU_IRQ_SRC_MASK) >> PMU_IRQ_SRC_SHIFT;
		val2 = readl_relaxed(pmu->pmu_base[i].base + PMU_TARGET_ADDR);
		val2 = (val2 & IRQ_TARGET_ADDR_MASK) >> IRQ_TARGET_ADDR_SHIFT;
		val3 = readl_relaxed(pmu->pmu_base[i].base + BMU3_STS);
		val3 = (val3 & IRQ_SRC_CFG_MASK) >> IRQ_SRC_CFG_SHIFT;
		val4 = readl_relaxed(pmu->pmu_base[i].base + BMU3_DMA_CSR);
		val4 = (val4 & IRQ_SRC_DMA_MASK) >> IRQ_SRC_DMA_SHIFT;
		val5 = readl_relaxed(pmu->pmu_base[i].base + BMU3_EXT_INT);
		val5 = (val5 & BMU3_EXT_INT_MASK) >> BMU3_EXT_INT_SHIFT;

		return (val | (val2 << IRQ_SRC_COMBINE_SHIFT) |
			(val3 << IRQ_SRC_TRACE_COMBINE_SHIFT) |
			(val4 << IRQ_SRC_DMA_COMBINE_SHIFT) |
			(val5 << IRQ_SRC_SEND_COMBINE_SHIFT));
	}

	return 0;
}

static void ddr_pmu_clear_irq(struct ddr_pmu *pmu, int inst)
{
	int val, val2;
	//int val3,val4;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;

	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_INT_REG);
		val2 = readl_relaxed(pmu->pmu_base[i].base + PMU_TARGET_ADDR);
		val2 = (val2 & IRQ_TARGET_ADDR_MASK) >> IRQ_TARGET_ADDR_SHIFT;
		val |= PMU_CLEAR_INT;
		writel(val, pmu->pmu_base[i].base + PMU_INT_REG);
		val2 = readl_relaxed(pmu->pmu_base[i].base + PMU_INT_REG);
	}
}

static void ddr_pmu_interrupt_enable(struct ddr_pmu *pmu, int inst, int mode,
				     bool enable)
{
	int val;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		val &= ~PMU_SRC_SEL_MASK;
		if (enable) {
			switch (mode) {
			case SRC_ADDR_RANGE_HIT:
				val |= SRC_ADDR_RANGE_HIT;
				break;
			case SRC_PERIOD_EXPIRED:
				val |= SRC_PERIOD_EXPIRED;
				break;
			case SRC_TARGET_WDATA:
				val |= SRC_TARGET_WDATA;
				break;
			case SRC_ERROR_RESP:
				val |= SRC_ERROR_RESP;
				break;
			case SRC_CNT_OVERFLOW:
				val |= SRC_CNT_OVERFLOW;
				break;
			default:
				val |= SRC_PERIOD_EXPIRED;
			}
		}
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);
	}
}

static void ddr_pmu_set_trigge_mode(struct ddr_pmu *pmu, int inst, int mode)
{
	int val;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);
	/*
   * config trigge mode to period,
   * and interrupt source to period expired
   */

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		val &= ~PMU_TRIG_MODE_MASK;
		if (mode == SINGLE_MODE)
			val |= SINGLE_MODE;
		else
			val |= PERIOD_MODE;
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);
	}
}

static void ddr_pmu_config_axid(struct ddr_pmu *pmu, int inst, int axid)
{
	int val;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);
	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		if (axid == -1)
			val = 0;
		else
			val = axid;
		writel(val, pmu->pmu_base[i].base + PMU_MST_ID);
	}
}

static void ddr_pmu_config_threshold(struct ddr_pmu *pmu, int inst,
				     int threshold, bool is_read)
{
	int val;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_DURA_THRESHOLD);
		if (is_read == true) {
			val &= ~PMU_DURA_THRESHOLD_R_MASK;
			val |= threshold << PMU_DURA_THRESHOLD_R_SHIFT;
		} else {
			val &= ~PMU_DURA_THRESHOLD_W_MASK;
			val |= threshold << PMU_DURA_THRESHOLD_W_SHIFT;
		}
		writel(val, pmu->pmu_base[i].base + PMU_DURA_THRESHOLD);
	}
}

static void ddr_pmu_config_filter_addr(struct ddr_pmu *pmu, int inst, int cnt,
				       long start, long end)
{
	int val;
	int i;
	void __iomem *base;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		if (cnt == 0) {
			// config addr in counter0
			// [ADDRMSB-1: 0]
			val = start & 0xffffffff; // start[31:0]
			writel(val, base + PMU_FLT_S_ADDR0); //2c

			val = readl_relaxed(base + PMU_FLT_S_ADDR1);
			val &= ~((1 << (ADDRMSB - 32)) - 1);
			// start[33:32]
			val |= (start >> 32) & ((1 << (ADDRMSB - 32)) - 1);
			writel(val, base + PMU_FLT_S_ADDR1); //30

			val = end & 0xffffffff; // end[31:0]
			writel(val, base + PMU_FLT_E_ADDR0); //18

			val = readl_relaxed(base + PMU_FLT_E_ADDR1);
			val &= ~((1 << (ADDRMSB - 32)) - 1);
			// end[33:32]
			val = (end >> 32) & ((1 << (ADDRMSB - 32)) - 1);
			writel(val, base + PMU_FLT_E_ADDR1); //1c
		} else if (cnt == 1) {
			// config addr in counter1
			// [ADDRMSB*2-1: ADDMSB]
			val = readl_relaxed(base + PMU_FLT_S_ADDR1); //30
			val &= ~((1 << (64 - ADDRMSB)) - 1);
			// start[29:0]
			val |= start & ((1 << (64 - ADDRMSB)) - 1);
			writel(val, base + PMU_FLT_S_ADDR1); //30

			val = start >> (64 - ADDRMSB); // start[33:30]
			writel(val, base + PMU_FLT_S_ADDR2); //34

			val = readl_relaxed(base + PMU_FLT_E_ADDR1); //1c
			val &= ~((1 << (64 - ADDRMSB)) - 1);
			val |= end & ((1 << (64 - ADDRMSB)) - 1); // end[29:0]
			writel(val, base + PMU_FLT_E_ADDR1);

			val = end >> (64 - ADDRMSB); // end[33:30]
			writel(val, base + PMU_FLT_E_ADDR2); //20
		}
	}
}

static void ddr_pmu_enable_filter_addr(struct ddr_pmu *pmu, int inst, int cnt)
{
	int val;
	int i;
	void __iomem *base;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		if (cnt == 0) {
			// enable addr filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= ADDR_FILTER_CNT0_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else if (cnt == 1) {
			// enable addr filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= ADDR_FILTER_CNT1_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else {
			// disable addr filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~(ADDR_FILTER_CNT0_EN | ADDR_FILTER_CNT1_EN);
			writel(val, base + PMU_FLT_CTRL);
		}
	}
}

static void ddr_pmu_filter_size(struct ddr_pmu *pmu, int inst, int cnt,
				int size)
{
	int val;
	int i;
	void __iomem *base;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		if (cnt == 0) {
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~SIZE_FLT_CNT0_MASK;
			val |= (size << SIZE_FLT_CNT0_SHIFT) &
			       SIZE_FLT_CNT0_MASK;
			// enable size filter
			val |= SIZE_FILTER_CNT0_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else if (cnt == 1) {
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~SIZE_FLT_CNT1_MASK;
			val |= (size << SIZE_FLT_CNT1_SHIFT) &
			       SIZE_FLT_CNT1_MASK;
			// enable size filter
			val |= SIZE_FILTER_CNT1_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else {
			// disable size filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~(SIZE_FILTER_CNT0_EN | SIZE_FILTER_CNT1_EN);
			writel(val, base + PMU_FLT_CTRL);
		}
	}
}

static void ddr_pmu_filter_len(struct ddr_pmu *pmu, int inst, int cnt, int len)
{
	int val;
	int i;
	void __iomem *base;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		if (cnt == 0) {
			val = readl_relaxed(base + PMU_FLT_LEN);
			val &= ~LEN_FLT_CNT0_MASK;
			val |= (len << LEN_FLT_CNT0_SHIFT) & LEN_FLT_CNT0_MASK;
			writel(val, base + PMU_FLT_LEN);

			// enable len filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= LEN_FILTER_CNT0_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else if (cnt == 1) {
			val = readl_relaxed(base + PMU_FLT_LEN);
			val &= ~LEN_FLT_CNT1_MASK;
			val |= (len << LEN_FLT_CNT1_SHIFT) & LEN_FLT_CNT1_MASK;
			writel(val, base + PMU_FLT_LEN);
			// enable len filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= LEN_FILTER_CNT1_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else {
			// disable len filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~(LEN_FILTER_CNT0_EN | LEN_FILTER_CNT1_EN);
			writel(val, base + PMU_FLT_CTRL);
		}
	}
}

static void ddr_pmu_filter_align(struct ddr_pmu *pmu, int inst, int cnt,
				 int align)
{
	int val;
	int i;
	int cfg;
	void __iomem *base;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		base = pmu->pmu_base[i].base;
		switch (align) {
		case 16:
			cfg = 1 << 0;
			break;
		case 32:
			cfg = 1 << 1;
			break;
		case 64:
			cfg = 1 << 2;
			break;
		case 128:
			cfg = 1 << 3;
			break;
		default:
			cfg = 0xf;
		}

		if (cnt == 0) {
			val = readl_relaxed(base + PMU_CTRL);
			val &= ~ALIGN_FLT_CNT0_MASK;
			val |= (cfg << ALIGN_FLT_CNT0_SHIFT) &
			       ALIGN_FLT_CNT0_MASK;
			writel(val, base + PMU_CTRL);
			// enable align filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= ALIGN_FILTER_CNT0_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else if (cnt == 1) {
			val = readl_relaxed(base + PMU_CTRL);
			val &= ~ALIGN_FLT_CNT1_MASK;
			val |= (cfg << ALIGN_FLT_CNT1_SHIFT) &
			       ALIGN_FLT_CNT1_MASK;
			writel(val, base + PMU_CTRL);
			// enable align filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val |= ALIGN_FILTER_CNT1_EN;
			writel(val, base + PMU_FLT_CTRL);
		} else {
			// disable align filter
			val = readl_relaxed(base + PMU_FLT_CTRL);
			val &= ~(ALIGN_FILTER_CNT0_EN | ALIGN_FILTER_CNT1_EN);
			writel(val, base + PMU_FLT_CTRL);
		}
	}
}

static void ddr_pmu_counter_period(struct ddr_pmu *pmu, int inst, int period)
{
	long val;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = (long)pmu->freq_khz * period;
		if (val > UINT_MAX) {
			dev_warn(pmu->dev, "%s counter period is overflow\n",
				 __func__);
			val = UINT_MAX;
		}
		writel((unsigned int)val,
		       pmu->pmu_base[i].base + PMU_MON_PERIOD);
	}
}

static void ddr_pmu_set_target_data(struct ddr_pmu *pmu, int inst, int data)
{
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		writel(data, pmu->pmu_base[i].base + PMU_TARGET_WDATA);
	}
}

static void ddr_pmu_set_compare_mode(struct ddr_pmu *pmu, int inst, int mode)
{
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		writel(mode, pmu->pmu_base[i].base + PMU_TARGET_ADDR);
	}
}

static int ddr_pmu_get_compare_mode(struct ddr_pmu *pmu, int inst)
{
	int i;
	int mode;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		mode = readl_relaxed(pmu->pmu_base[i].base + PMU_TARGET_ADDR);
		return mode;
	}
	return -1;
}

static void ddr_pmu_config_wdata_select(struct ddr_pmu *pmu, int inst, int mask)
{
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		writel((mask << WDATA_SEL_SHIFT) & WDATA_SEL_MASK,
		       pmu->pmu_base[i].base + PMU_INT_REG);
	}
}

static void ddr_pmu_counter_enable(struct ddr_pmu *pmu, int inst, bool enable)
{
	int val;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ddr_pmu_enable_inst(pmu, inst);

	for (i = 0; i < private->reg_group_num; i++) {
		if (pmu->pmu_base[i].enable == false)
			continue;
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		if (enable) {
			val |= PMU_RESET;
			val |= PMU_EN;
		} else {
			/* Disable monitor */
			val &= ~PMU_EN;
			val &= ~PMU_RESET;
		}
		writel(val, pmu->pmu_base[i].base + PMU_CTRL);
	}
}

static void ddr_pmu_counter_init(struct ddr_pmu *pmu, int inst)
{
	int val1, val2, val3;

	ddr_pmu_enable_inst(pmu, inst);
	ddr_pmu_inst_reset(pmu, inst);
	ddr_pmu_counter_enable(pmu, inst, false);
	ddr_pmu_config_axid(pmu, inst, -1);
	ddr_pmu_config_threshold(pmu, inst, 1, true);
	ddr_pmu_config_threshold(pmu, inst, 1, false);
	ddr_pmu_config_filter_addr(pmu, inst, -1, 0, 0);
	ddr_pmu_enable_filter_addr(pmu, inst, -1);
	ddr_pmu_filter_size(pmu, inst, -1, 0);
	ddr_pmu_filter_len(pmu, inst, -1, 0);
	ddr_pmu_filter_align(pmu, inst, -1, 0);
	ddr_pmu_counter_period(pmu, inst, 5); //0911

	val1 = readl_relaxed(pmu->pmu_base[inst].base + PMU_CTRL);
	val2 = readl_relaxed(pmu->pmu_base[inst].base + PMU_MON_PERIOD);
	val3 = readl_relaxed(pmu->pmu_base[inst].base + PMU_INT_REG);
}

static u64 ddr_pmu_get_version(struct ddr_pmu *pmu, int inst)
{
	u64 ver = -1;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	ver = readl_relaxed(pmu->pmu_base[inst].base + PMU_VERSION1);
	ver = readl_relaxed(pmu->pmu_base[inst].base + PMU_VERSION0) |
	      (ver << 32);

	return ver;
}

static u32 bmu3_get_version(struct ddr_pmu *pmu, int inst)
{
	u32 ver;

	ver = readl_relaxed(pmu->pmu_base[inst].base + BMU3_STS);

	return ver;
}

static void ddr_perf_event_start(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	unsigned long cfg1 = event->attr.config1;
	unsigned long cfg2 = event->attr.config2;
	int inst, axid = -1;
	int chns = 0;
	int cm_mode = CM_WRITE;
	int threshold;
	int thres_event, flt_cnt;
	long start_addr, end_addr;
	int data;
	int i;
	int event_num = cfg & ATTR_EVENT_MASK;
	struct bmu3_para *private;

	inst = hwc->idx;

	private = pmu->bmu_private;
	if (ddr_perf_is_capture_masked(event)) {
		chns = hwc->idx;
		for (i = 0; i < private->reg_group_num; i++) {
			if (chns & (1 << i)) {
				ddr_pmu_enable_inst(pmu, i);
				ddr_pmu_set_trigge_mode(pmu, i, TRIGGER_MODE);
				/*pmu->period */
				ddr_pmu_counter_period(pmu, i, 5); //5
				switch (event_num) {
				case DDR_EVENT_CAPTURE_R_DATA:
				case DDR_EVENT_CAPTURE_W_DATA:
					if (event_num ==
					    DDR_EVENT_CAPTURE_R_DATA) {
						cm_mode = CM_READ;
					} else if (event_num ==
						   DDR_EVENT_CAPTURE_W_DATA) {
						cm_mode = CM_WRITE;
					}
					data = (cfg1 & ATTR_CAP_DATA_MASK) >>
					       ATTR_CAP_DATA_SHIFT;
					ddr_pmu_set_target_data(pmu, i, data);
					ddr_pmu_config_wdata_select(pmu, i,
								    0xf);
					ddr_pmu_set_compare_mode(pmu, i,
								 cm_mode);
					ddr_pmu_interrupt_enable(
						pmu, i, SRC_TARGET_WDATA, true);
					break;
				case DDR_EVENT_CAPTURE_ADDR:
					start_addr =
						(cfg1 &
						 ATTR_CAP_START_ADDR_MASK) >>
						ATTR_CAP_START_ADDR_SHIFT << 4;
					end_addr = (cfg2 &
						    ATTR_CAP_END_ADDR_MASK) >>
						   ATTR_CAP_END_ADDR_SHIFT << 4;

					if ((start_addr >= 0) &&
					    (end_addr > 0)) {
						ddr_pmu_config_filter_addr(
							pmu, i, 0, start_addr,
							end_addr);
						ddr_pmu_enable_filter_addr(
							pmu, i, 0);
					}
					ddr_pmu_interrupt_enable(
						pmu, i, SRC_ADDR_RANGE_HIT,
						true);
					break;
				case DDR_EVENT_CAPTURE_ERROR_RESP_R:
				case DDR_EVENT_CAPTURE_ERROR_RESP_W:
					ddr_pmu_interrupt_enable(
						pmu, i, SRC_ERROR_RESP, true);
					break;
				default:
					dev_err(pmu->dev,
						"%s un-support capture "
						"event<0x%lx>\n",
						__func__,
						cfg & ATTR_EVENT_MASK);
					break;
				}
				ddr_pmu_counter_enable(pmu, i, true);
				{
					int val1, val2, val3;
					val1 = readl_relaxed(
						pmu->pmu_base[i].base +
						PMU_CTRL);
					val2 = readl_relaxed(
						pmu->pmu_base[i].base +
						PMU_MON_PERIOD);
					val3 = readl_relaxed(
						pmu->pmu_base[i].base +
						PMU_INT_REG);
				}
				hwc->state = 0;
			}
		}
		return;
	}

	ddr_pmu_enable_inst(pmu, inst);
	local64_set(&event->count, 0);
	local64_set(&hwc->prev_count, 0);

	if (ddr_perf_is_misc_masked(event)) {
		if ((cfg & ATTR_EVENT_MASK) == DDR_EVENT_PMU_EXEC_TIME) {
			// record current time as pmu begin time
			// once new an event, it will update the t_begin value
			ktime_get_real_ts64(&pmu->t_begin);
		}
		hwc->state = 0;
		return;
	}

	if (ddr_perf_is_axid_masked(event)) {
		axid = event->attr.config1 & ATTR_AXID_MASK;
		ddr_pmu_config_axid(pmu, inst, axid);
	}
	thres_event = ddr_perf_contain_threshold(event);
	if (thres_event == 0) {
		threshold =
			(event->attr.config1 & ATTR_DURA_THRESHOLD_W_MASK) >>
			ATTR_DURA_THRESHOLD_W_SHIFT;
		ddr_pmu_config_threshold(pmu, inst, threshold, false);
	} else if (thres_event == 1) {
		threshold =
			(event->attr.config1 & ATTR_DURA_THRESHOLD_R_MASK) >>
			ATTR_DURA_THRESHOLD_R_SHIFT;
		ddr_pmu_config_threshold(pmu, inst, threshold, true);
	}
	flt_cnt = ddr_perf_contains_filtered(event);
	if (flt_cnt >= 0) {
		int flt_size, flt_len, flt_align;
		start_addr =
			(event->attr.config2 & ATTR_FILTER_START_ADDR_MASK) >>
			ATTR_FILTER_START_ADDR_SHIFT << 4;
		end_addr = (event->attr.config2 & ATTR_FILTER_END_ADDR_MASK) >>
			   ATTR_FILTER_END_ADDR_SHIFT << 4;
		flt_size = (cfg & ATTR_FILTER_SIZE_MASK) >>
			   ATTR_FILTER_SIZE_SHIFT;
		flt_len = (cfg & ATTR_FILTER_LEN_MASK) >> ATTR_FILTER_LEN_SHIFT;
		flt_align = (cfg & ATTR_FILTER_ALIGN_MASK) >>
			    ATTR_FILTER_ALIGN_SHIFT;
		if ((start_addr >= 0) && (end_addr > 0)) {
			ddr_pmu_config_filter_addr(pmu, inst, flt_cnt,
						   start_addr, end_addr);
			ddr_pmu_enable_filter_addr(pmu, inst, flt_cnt);
		}
		if (flt_size > 0)
			ddr_pmu_filter_size(pmu, inst, flt_cnt, flt_size);
		if (flt_len > 0)
			ddr_pmu_filter_len(pmu, inst, flt_cnt, flt_len);
		if (flt_align > 0)
			ddr_pmu_filter_align(pmu, inst, flt_cnt, flt_align);
	}
	ddr_pmu_set_trigge_mode(pmu, inst, TRIGGER_MODE);
	/*pmu->period */
	ddr_pmu_counter_period(pmu, inst, 5); //
	ddr_pmu_interrupt_enable(pmu, inst, SRC_PERIOD_EXPIRED, true);
	ddr_pmu_counter_enable(pmu, inst, true);

	hwc->state = 0;
}

static int ddr_perf_event_add(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	int inst = INST_NULL;
	int chns = 0;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	if (hwc->idx == -1) { // first initial
		if (ddr_perf_is_axid_masked(event))
			inst = (cfg & ATTR_INST_MASK) >> ATTR_INST_SHIFT;
		else if (ddr_perf_is_misc_masked(event))
			inst = INST_MISC;
		else if (ddr_perf_is_capture_masked(event))
			chns = (cfg & ATTR_CHN_EN_MASK) >> ATTR_CHN_EN_SHIFT;
		else
			inst = INST_ALL;
		hwc->idx = inst;
		if (ddr_perf_is_capture_masked(event)) {
			hwc->idx = chns;
		}
	}

	for (i = 0; i < private->reg_group_num; i++) {
		if ((chns & (1 << i)) || (inst == i) || (inst == INST_ALL) ||
		    (inst == INST_MISC)) {
			pmu->events[i][cfg & ATTR_EVENT_MASK] = event;
			if (pmu->active_events == 0)
				pmu_trace.trace_count = 0;
			pmu->active_events++;
			if (pmu->hwc_active_events[i] == 0) {
				if (inst != INST_MISC)
					ddr_pmu_counter_init(pmu, i);
			}
			pmu->hwc_active_events[i]++;
			if (inst == i)
				break;
		}
	}

	hwc->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		ddr_perf_event_start(event, flags);

	return 0;
}

static void ddr_perf_event_stop(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long cfg = event->attr.config;
	int inst = hwc->idx;
	int chns = 0;
	int i;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	if (ddr_perf_is_capture_masked(event)) {
		chns = hwc->idx;
	}

	for (i = 0; i < private->reg_group_num; i++) {
		if ((chns & (1 << i)) || (inst == i) || (inst == INST_ALL) ||
		    (inst == INST_MISC)) {
			if (inst != INST_MISC) {
				ddr_pmu_enable_inst(pmu, i);
				ddr_pmu_counter_enable(pmu, i, false);
				if (pmu->hwc_active_events[i] == 0) {
					//the last event of one bmu inst stop it
					ddr_pmu_interrupt_enable(pmu, i, 0,
								 false);
				}
				ddr_pmu_clear_irq(pmu, i);
			}
			if (pmu->hwc_active_events[i] == 0) {
				/* the last event of one bmu inst,
	         reinit the configuration */
				ddr_pmu_counter_init(pmu, i);
			}
			ddr_perf_free_counter(pmu, cfg & ATTR_EVENT_MASK, i);
		}
	}
}

static void ddr_perf_event_del(struct perf_event *event, int flags)
{
	//struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	ddr_perf_event_stop(event, PERF_EF_UPDATE);
	if (!ddr_perf_is_capture_masked(event))
		ddr_perf_event_update(event);
	hwc->state |= PERF_HES_STOPPED;
	hwc->idx = -1;
}

static void ddr_perf_pmu_enable(struct pmu *pmu)
{
}

static void ddr_perf_pmu_disable(struct pmu *pmu)
{
}

#define VENCGPU_EFUSE_CFG_ADDR 0x27400000 + 0xE4
static int vp_tee_init(void)
{
	size_t pa = VENCGPU_EFUSE_CFG_ADDR;
	size_t sz = 4;
	volatile u8 *vpss_sys_hwregs = NULL;
	vpss_sys_hwregs = (volatile u8 __force *)ioremap(pa, sz);
	if (vpss_sys_hwregs == NULL) {
		pr_err("%s: vpss_sys_hwregs ioremap failed! pa = %lx, \
			sz = %lx\n",
		       __func__, pa, sz);
		return -1;
	}
	iowrite32(0xFFFFFF00, ((void __iomem *)vpss_sys_hwregs));
	udelay(1);
	iounmap(vpss_sys_hwregs);
	return 0;
}

static int ddr_perf_init(struct ddr_pmu *pmu, void __iomem *base[],
			 struct device *dev)
{
	int i, j;
	u32 sys_reg1, sys_reg2, sys_reg3, sys_reg4, sys_reg5, sys_reg6;
	u32 sys_reg7, sys_reg8, sys_reg9, sys_reg10, sys_reg11, sys_reg12;
	u32 regid;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	pmu->dev = dev;
	pmu->pmu = (struct pmu){
		//.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
		.task_ctx_nr = perf_invalid_context,
		.attr_groups = attr_groups,
		.event_init = ddr_perf_event_init,
		.add = ddr_perf_event_add,
		.del = ddr_perf_event_del,
		.start = ddr_perf_event_start,
		.stop = ddr_perf_event_stop,
		.read = ddr_perf_event_update,
		.pmu_enable = ddr_perf_pmu_enable,
		.pmu_disable = ddr_perf_pmu_disable,
	};

	for (i = 0; i < private->reg_group_num; i++) {
		pmu->pmu_base[i].base = base[i];
		pmu->pmu_base[i].enable = false;
		for (j = 0; j < NUM_EVENTS; j++) {
			pmu->events[i][j] = NULL;
		}
		if (!strcmp(private->name, "bmu_ddr")) {
			private->mod_id = BMU_DDR;
			private->chn_id[0] = CHN0;
			private->chn_id[1] = CHN1;
			size_t pa = 0x04900050;
			size_t sz = 4;
			volatile u8 *ddr_broacast_en_reg = NULL;
			ddr_broacast_en_reg =
				(volatile u8 __force *)ioremap(pa, sz);
			if (ddr_broacast_en_reg == NULL) {
				pr_err("%s: ddr_broacast_en_reg ioremap failed! "
				       "pa = %lx, sz = %lx\n",
				       __func__, pa, sz);
				return -1;
			}
			iowrite32(0x0, ((void __iomem *)ddr_broacast_en_reg));
			/* delay 1ms before access any venc register */
			udelay(100);
			iounmap(ddr_broacast_en_reg);
			regid = BMU_SYSREG_CFG;
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
		} else if (!strcmp(private->name, "bmu_vp")) {
			private->mod_id = BMU_VP;
			private->chn_id[0] = CHN0;
			vp_tee_init();
			// bpc
			writel(0x0, private->reg3_va[0] + 0x3000);
			writel(0x10101, private->reg3_va[0] + 0x3004);
			writel(0x0, private->reg3_va[0] + 0x6000);
			writel(0x10101, private->reg3_va[0] + 0x6004);
			writel(0x0, private->reg3_va[0] + 0x9000);
			writel(0x10101, private->reg3_va[0] + 0x9004);
			// pcu
			writel(0x3f, private->reg3_va[0] + 0x2024);
			writel(0x1f, private->reg3_va[0] + 0x200c);
			writel(0x1, private->reg3_va[0] + 0x2008);
			writel(0x3f, private->reg3_va[0] + 0x5024);
			writel(0x1f, private->reg3_va[0] + 0x500c);
			writel(0x1, private->reg3_va[0] + 0x5008);
			writel(0x3f, private->reg3_va[0] + 0x8024);
			writel(0x1f, private->reg3_va[0] + 0x800c);
			writel(0x1, private->reg3_va[0] + 0x8008);
			regid = BMU_VP_SYSREG_CLK_EN;
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
			sys_reg1 |= 0xffffffff;
			writel(sys_reg1, (private->reg1_va[i] + regid));
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
			regid = BMU_VP_SYSREG_RSTN_EN;
			sys_reg2 = readl_relaxed(private->reg1_va[i] + regid);
			sys_reg2 |= 0xffffffff;
			writel(sys_reg2, (private->reg1_va[i] + regid));
			sys_reg2 = readl_relaxed(private->reg1_va[i] + regid);
			private->mod_id = BMU_VP;
			private->chn_id[0] = CHN0;
		} else if (!strcmp(private->name, "bmu_vi")) {
			int count = 0;
			// bpc
			private->mod_id = BMU_VI;
			private->chn_id[0] = CHN0;
			writel(0x0, private->reg3_va[0] + 0x3000);
			writel(0x10101, private->reg3_va[0] + 0x3004);
			writel(0x0, private->reg3_va[0] + 0x6000);
			writel(0x10101, private->reg3_va[0] + 0x6004);
			writel(0x0, private->reg3_va[0] + 0x9000);
			writel(0x10101, private->reg3_va[0] + 0x9004);
			// pcu
			writel(0x3f, private->reg3_va[0] + 0x2024);
			writel(0x1f, private->reg3_va[0] + 0x200c);
			writel(0x1, private->reg3_va[0] + 0x2008);
			writel(0x3f, private->reg3_va[0] + 0x5024);
			writel(0x1f, private->reg3_va[0] + 0x500c);
			writel(0x1, private->reg3_va[0] + 0x5008);
			writel(0x3f, private->reg3_va[0] + 0x8024);
			writel(0x1f, private->reg3_va[0] + 0x800c);
			writel(0x1, private->reg3_va[0] + 0x8008);
			private->mod_id = BMU_VI;
			private->chn_id[0] = CHN0;
			regid = BMU_VI_SYSREG_CLK_EN1;
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
			sys_reg1 |= 0xffffffff;
			writel(sys_reg1, (private->reg1_va[i] + regid));
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
			do {
				regid = BMU_VI_SYSREG_CLK_EN1;
				sys_reg1 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(1);
			} while (((sys_reg1 & 0x1) != 0x1) && (count++ < 1000));
			regid = BMU_VI_SYSREG_RSTN_EN1;
			sys_reg3 = readl_relaxed(private->reg1_va[i] + regid);
			sys_reg3 |= 0xffffffff;
			writel(sys_reg3, (private->reg1_va[i] + regid));
			/*sys_reg3 = readl_relaxed(private->reg1_va[i] + regid); */
			count = 0;
			do {
				regid = BMU_VI_SYSREG_RSTN_EN1;
				sys_reg3 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(1);
			} while (((sys_reg3 & 0x1) != 0x1) && (count++ < 1000));
		} else if (!strcmp(private->name, "bmu_vo")) {
			private->mod_id = BMU_VO;
			private->chn_id[0] = CHN0;
			regid = BMU_VO_SYSREG_REG13c;
			sys_reg3 = 0x19;
			writel(sys_reg3, (private->reg3_va[i] + regid));
			sys_reg3 = readl_relaxed(private->reg3_va[i] + regid);
			regid = BMU_VO_SYSREG_REG004;
			sys_reg4 = 0x10101;
			writel(sys_reg4, (private->reg3_va[i] + regid));
			sys_reg4 = readl_relaxed(private->reg3_va[i] + regid);
			udelay(10);
			regid = BMU_VO_SYSREG_CLK_EN;
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
			sys_reg1 |= 0xffffffff;
			writel(sys_reg1, (private->reg1_va[i] + regid));
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
			udelay(10);
			regid = BMU_VO_SYSREG_RSTN_EN;
			sys_reg2 = readl_relaxed(private->reg1_va[i] + regid);
			sys_reg2 |= 0xffffffff;
			writel(sys_reg2, (private->reg1_va[i] + regid));
			sys_reg2 = readl_relaxed(private->reg1_va[i] + regid);
		} else if (!strcmp(private->name, "bmu_gpu")) {
			private->mod_id = BMU_GPU;
			private->chn_id[0] = CHN0;
			{
				size_t pa = 0x2025c004;
				size_t sz = 4;
				volatile u8 *gpu_top_ccu_hwregs = NULL;
				gpu_top_ccu_hwregs =
					(volatile u8 __force *)ioremap(pa, sz);
				if (gpu_top_ccu_hwregs == NULL) {
					pr_debug(
						"%s: gpu_top_ccu_hwregs ioremap failed!\
						pa=%lx,sz = %lx\n",
						__func__, pa, sz);
					return -1;
				}
				iowrite32(0x1,
					  ((void __iomem *)gpu_top_ccu_hwregs));
				udelay(1);
				iounmap(gpu_top_ccu_hwregs);
			}
			regid = BMU_GPU_TOP_BPC_REG04;
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
			sys_reg1 &= 0x0;
			writel(sys_reg1, (private->reg1_va[i] + regid));
			udelay(50);
			regid = BMU_GPU_TOP_BPC_REG13C;
			sys_reg2 = 0x18;
			writel(sys_reg2, (private->reg1_va[i] + regid));
			udelay(50);
			regid = BMU_GPU_TOP_CFG_ACLK_CCU_REG28;
			sys_reg3 = 0x1;
			writel(sys_reg3, (private->reg1_va[i] + regid));
			udelay(10);
			regid = BMU_GPU_TOP_ACLK_CCU_REG28;
			sys_reg4 = 0x1;
			writel(sys_reg4, (private->reg1_va[i] + regid));
			udelay(10);
			regid = BMU_GPU_TOP_PCLK_REG28;
			sys_reg5 = 0x1;
			writel(sys_reg5, (private->reg1_va[i] + regid));
			udelay(10);
			regid = BMU_GPU_ACLK_CCU_REG28;
			sys_reg6 = 0x1;
			writel(sys_reg6, (private->reg2_va[i] + regid));
			udelay(10);
			regid = BMU_GPU_CORE_CLK_CCU_REG28;
			sys_reg7 = 0x1;
			writel(sys_reg7, (private->reg2_va[i] + regid));
			udelay(10);
			regid = BMU_GPU_PCLK_CCU_REG28;
			sys_reg8 = 0x1;
			writel(sys_reg8, (private->reg2_va[i] + regid));
			udelay(50);
			regid = BMU_GPU_TOP_PCU_REG24;
			sys_reg9 = 0x3f;
			writel(sys_reg9, (private->reg1_va[i] + regid));
			udelay(50);
			regid = BMU_GPU_TOP_PCA_REG10;
			sys_reg10 = 0x1;
			writel(sys_reg10, (private->reg1_va[i] + regid));
			udelay(50);
			regid = BMU_GPU_TOP_PCU_REG0C;
			sys_reg11 = 0x1f;
			writel(sys_reg11, (private->reg1_va[i] + regid));
			udelay(50);
			regid = BMU_GPU_TOP_PCU_REG08;
			sys_reg12 = 0x1;
			writel(sys_reg12, (private->reg1_va[i] + regid));
			udelay(50);
		} else if (!strcmp(private->name, "bmu_npu")) {
			int count = 0;
			private->mod_id = BMU_NPU;
			private->chn_id[0] = CHN0;
			regid = BMU_NPU_PCTRL_PCU_REG24;
			sys_reg1 = 0x3f;
			writel(sys_reg1, (private->reg1_va[i] + regid));
			sys_reg1 = readl_relaxed(private->reg1_va[i] + regid);
			regid = BMU_NPU_PCTRL_PCA_REG20;
			sys_reg2 = 0x0;
			writel(sys_reg2, (private->reg1_va[i] + regid));
			sys_reg2 = readl_relaxed(private->reg1_va[i] + regid);
			regid = BMU_NPU_PCTRL_BPC_REG00;
			sys_reg3 = 0x0;
			writel(sys_reg3, (private->reg1_va[i] + regid));
			sys_reg3 = readl_relaxed(private->reg1_va[i] + regid);
			regid = BMU_NPU_PCU_SW_LPS_REG0C;
			sys_reg4 = 0x1f;
			writel(sys_reg4, (private->reg1_va[i] + regid));
			sys_reg4 = readl_relaxed(private->reg1_va[i] + regid);
			regid = BMU_NPU_PCU_SW_LPR_REG08;
			sys_reg5 = 0x1;
			writel(sys_reg5, (private->reg1_va[i] + regid));
			sys_reg5 = readl_relaxed(private->reg1_va[i] + regid);
			regid = BMU_NPU_PCU_ICR_REG28;
			sys_reg6 = 0x3f;
			writel(sys_reg6, (private->reg1_va[i] + regid));
			sys_reg6 = readl_relaxed(private->reg1_va[i] + regid);
			do {
				regid = BMU_NPU_PCU_ISR_REG30;
				sys_reg7 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg7 & 0x1) != 0x1) && (count++ < 1000));
			regid = BMU_NPU_CRG_REG200;
			sys_reg8 = 0x7ffff;
			writel(sys_reg8, (private->reg2_va[i] + regid));
			sys_reg8 = readl_relaxed(private->reg2_va[i] + regid);
			regid = BMU_NPU_CRG_REG210;
			sys_reg9 = 0x7ffffff;
			writel(sys_reg9, (private->reg2_va[i] + regid));
			sys_reg9 = readl_relaxed(private->reg2_va[i] + regid);
		} else if (!strcmp(private->name, "bmu_d2d")) {
			int count = 0;
			private->mod_id = BMU_D2D;
			private->chn_id[0] = CHN0;
			do {
				regid = BMU_D2D_PCU_REG24;
				sys_reg3 = 0x3f;
				writel(sys_reg3, (private->reg1_va[i] + regid));
				sys_reg3 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg3 & 0x1) != 0x1) && (count++ < 1000));
			count = 0;
			do {
				regid = BMU_D2D_BPC_REG200;
				sys_reg4 = 0x0;
				writel(sys_reg4, (private->reg1_va[i] + regid));
				sys_reg4 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg4 & 0x1) != 0x0) && (count++ < 1000));
			count = 0;
			do {
				regid = BMU_D2D_PCUEN_REG0C;
				sys_reg1 = 0x1f;
				writel(sys_reg1, (private->reg1_va[i] + regid));
				sys_reg1 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg1 & 0x1) != 0x1) && (count++ < 1000));
			count = 0;
			do {
				regid = BMU_D2D_SW_LPREQ_REG08;
				sys_reg2 = 0x1;
				writel(sys_reg2, (private->reg1_va[i] + regid));
				sys_reg2 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg2 & 0x1) != 0x1) && (count++ < 1000));
		} else if (!strcmp(private->name, "bmu_usb")) {
			int count = 0;
			private->mod_id = BMU_USB;
			private->chn_id[0] = CHN0;
			do {
				regid = BMU_USB_PCU_PCA_REG24;
				sys_reg3 = 0x3f;
				writel(sys_reg3, (private->reg1_va[i] + regid));
				sys_reg3 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg3 & 0x1) != 0x1) && (count++ < 1000));
			count = 0;
			do {
				regid = BMU_USB_BPC_REG0;
				sys_reg4 = 0x0;
				writel(sys_reg4, (private->reg1_va[i] + regid));
				sys_reg4 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg4 & 0x1) != 0x0) && (count++ < 1000));
			count = 0;
			do {
				regid = BMU_USB_PCU_SW_LPS_REG0C;
				sys_reg1 = 0x1f;
				writel(sys_reg1, (private->reg1_va[i] + regid));
				sys_reg1 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg1 & 0x1) != 0x1) && (count++ < 1000));
			count = 0;
			do {
				regid = BMU_USB_PCU_SW_LPREQ_REG08;
				sys_reg2 = 0x1;
				writel(sys_reg2, (private->reg1_va[i] + regid));
				sys_reg2 = readl_relaxed(private->reg1_va[i] +
							 regid);
				udelay(3);
			} while (((sys_reg2 & 0x1) != 0x1) && (count++ < 1000));
		} else if (!strcmp(private->name, "bmu_pcie")) {
			private->mod_id = BMU_PCIE;
			private->chn_id[0] = CHN0;
		} else if (!strcmp(private->name, "bmu_peri")) {
			private->mod_id = BMU_PERI;
			private->chn_id[0] = CHN0;
		}
		pmu->hwc_active_events[i] = 0;
		pmu->hwc_version[i] = ddr_pmu_get_version(pmu, i);
		pmu->hwc_trace_version[i] = bmu3_get_version(pmu, i);
		private->addr_len = (pmu->hwc_version[0]) & 0xff;
		private->id_len = (pmu->hwc_version[0] >> 8) & 0xff;
		private->axi_width_len = (pmu->hwc_version[0] >> 32) & 0xff;
		private->axi_data_len = (pmu->hwc_version[0] >> 48) & 0xffff;
		ddr_pmu_counter_period(pmu, i, PMU_PERIOD_CNT);
	}

	bmu_trace_init(pmu);
	pmu->active_events = 0;
	pmu->period = PMU_PERIOD_CNT;
	pmu->freq_khz = APB_CLK / 1000; // unit KHz
	pmu->id = ida_simple_get(&ddr_ida, 0, 0, GFP_KERNEL);
	pmu_trace.trace_data_fmt = FMT_DECIMAL;

	return pmu->id;
}

static struct bm_time_info get_current_time(void)
{
	struct timespec64 ts;
	struct bm_time_info info;

	ktime_get_real_ts64(&ts);
	info.s = ts.tv_sec;
	info.ns = ts.tv_nsec;
	info.us = ts.tv_nsec / 1000;

	return info;
}

static void bmu_counter_enable(struct ddr_pmu *pmu, u32 enable, u8 ch)
{
	int i;
	//long long cnt_ms;
	struct bmu3_para *private;
	int val1, val3;

	private = pmu->bmu_private;

	if (ch >= private->reg_group_num)
		return;

	i = ch;
	if (enable) {
		atomic_set(&(pmu->bm_cnt_stat[i]), BMU_CNT_START);
		//ddr_pmu_enable_inst(pmu, i);
		ddr_pmu_counter_enable(pmu, i, false);
		ddr_pmu_config_threshold(pmu, i, 0x1f4, true);
		ddr_pmu_config_threshold(pmu, i, 0x1f4, false);
		ddr_pmu_inst_reset(pmu, i);
		ddr_pmu_set_trigge_mode(pmu, i, TRIGGER_MODE);
		//ddr_pmu_counter_period(pmu, i, PMU_PERIOD_CNT);
		ddr_pmu_interrupt_enable(pmu, i, SRC_PERIOD_EXPIRED, true);
		ddr_pmu_counter_enable(pmu, i, true);
	} else {
		atomic_set(&(pmu->bm_cnt_stat[i]), BMU_CNT_STOP);
		ddr_pmu_counter_enable(pmu, i, false);
	}

	val1 = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
	val3 = readl_relaxed(pmu->pmu_base[i].base + PMU_MON_PERIOD);
}

static void bmu_trace_interrupt_enable(struct ddr_pmu *pmu, u8 ch, u32 enable)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;

	if (ch > private->reg_group_num) {
		return;
	}

	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;

	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);

		if (enable) {
			val |= (BMU3_CFG_FULL_AXI_INT_EN |
				BMU3_CFG_AXI_RESP_ERR_INT_EN);
		} else {
			val &= ~(BMU3_CFG_FULL_AXI_INT_EN |
				 BMU3_CFG_AXI_RESP_ERR_INT_EN |
				 BMU3_CFG_BUFF_FULL_INT_EN);
		}
		writel(val, pmu->pmu_base[i].base + BMU3_CFG);
	}
}

static void bmu_trace_enable(struct ddr_pmu *pmu, u8 ch, u32 enable)
{
	int val1, val2, val3, val4, val5, val6;
	int val10;
	int val11, val12;
	int val = 0;
	int i = 0;
	u32 bmu3_status;
	char *bmu_name;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	bmu_name = private->name;

	if (ch == BMU_CHALL) {
		for (i = 0; i < private->reg_group_num; i++) {
			if (enable) {
				private->g_tim[i] = 0;
				ddr_pmu_clear_irq(pmu, i); //2024/09/04
				bmu3_ext_int_enable(pmu, ch, 0);

				//set bmu3 axiw addr lenth
				val3 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val3 &= (~(BMU3_CFG_BUFF_PAUSE_EN |
					   BMU3_CFG_REG_REUSE_EN |
					   BMU3_CFG_BUFF_FULL_INT_EN |
					   BMU3_CFG_EN));
				val3 |= (BMU3_CFG_FULL_AXI_INT_EN);
				writel(val3, pmu->pmu_base[i].base + BMU3_CFG);
				val5 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);

				val3 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val3 |= BMU3_CFG_RESET;
				writel(val3, pmu->pmu_base[i].base +
						     BMU3_CFG); //reset

				val5 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val3 &= ~BMU3_CFG_RESET;
				val3 &= ~BMU3_CFG_EN;
				writel(val3, pmu->pmu_base[i].base + BMU3_CFG);

				val = (val & 0xf0000000) |
				      (private->reg_axiw_pa[i]);
				writel(val,
				       pmu->pmu_base[i].base + PMU_FLT_E_ADDR3);

				val1 = private->reg_axiw_length[i];
				writel(val1,
				       pmu->pmu_base[i].base + PMU_FLT_S_ADDR3);

				val2 = readl_relaxed(pmu->pmu_base[i].base +
						     PMU_CTRL);
				val2 &= ~PMU_TRIG_MODE_MASK;
				val2 |= PERIOD_MODE;
				writel(val2, pmu->pmu_base[i].base + PMU_CTRL);

				__io_ww();
				val3 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val3 |= BMU3_CFG_EN;
				writel(val3, pmu->pmu_base[i].base + BMU3_CFG);

				val3 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val3 &= ~BMU3_CFG_RESET;
				writel(val3, pmu->pmu_base[i].base + BMU3_CFG);

				//mb();
				val4 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val4 &= ~BMU3_CFG_EN;
				writel(val4, pmu->pmu_base[i].base + BMU3_CFG);

				val4 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val4 |= BMU3_CFG_EN;
				writel(val4, pmu->pmu_base[i].base + BMU3_CFG);

				val4 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				bmu3_status = readl_relaxed(
					pmu->pmu_base[i].base + BMU3_STS);
				if (bmu3_status & BMU3_STS_BUSY) {
					val4 = readl_relaxed(
						pmu->pmu_base[i].base +
						BMU3_CFG);
					val4 &= ~BMU3_CFG_EN;
					writel(val4, pmu->pmu_base[i].base +
							     BMU3_CFG);
					__io_ww();
					val4 = readl_relaxed(
						pmu->pmu_base[i].base +
						BMU3_CFG);
					val4 |= BMU3_CFG_EN;
					writel(val4, pmu->pmu_base[i].base +
							     BMU3_CFG);
				}
			} else {
				bmu3_ext_int_enable(pmu, i, 0);
				bmu3_send_loop_enable(pmu, i, 0);
				bmu3_sideband_enable(pmu, i, 0);

				/* Disable monitor */
				val3 &= ~(BMU3_CFG_FULL_AXI_INT_EN |
					  BMU3_CFG_AXI_RESP_ERR_INT_EN |
					  BMU3_CFG_BUFF_FULL_INT_EN |
					  BMU3_CFG_BUFF_PAUSE_EN |
					  BMU3_CFG_REG_REUSE_EN);
				val3 |= (BMU3_CFG_RESET | BMU3_CFG_EN);
				writel(val3, pmu->pmu_base[i].base + BMU3_CFG);
				__io_ww();
				val3 &= ~BMU3_CFG_EN;
				writel(val3, pmu->pmu_base[i].base + BMU3_CFG);
				__io_ww();
			}
			private->trace_en = val3 & BMU3_CFG_EN;
			private->trace_rst = val3 & BMU3_CFG_RESET;
		}
	} else {
		if (ch >= private->reg_group_num) {
			return;
		}
		bmu3_ext_int_enable(pmu, ch, 0);
		ddr_pmu_clear_irq(pmu, ch);

		mb();
		val1 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
		val1 |= BMU3_CFG_RESET;
		writel(val1, pmu->pmu_base[ch].base + BMU3_CFG); //reset

		mb();
		val1 &= ~BMU3_CFG_EN;
		writel(val1, pmu->pmu_base[ch].base + BMU3_CFG); //disable en

		mb();
		val = (val & 0xf0000000) | (private->reg_axiw_pa[ch]);
		writel(val, pmu->pmu_base[ch].base + PMU_FLT_E_ADDR3);
		val4 = private->reg_axiw_length[ch];
		writel(6400, pmu->pmu_base[ch].base + PMU_FLT_S_ADDR3);

		mb();
		val5 = readl_relaxed(pmu->pmu_base[ch].base + PMU_CTRL);
		val5 &= ~PMU_TRIG_MODE_MASK;
		val5 |= PERIOD_MODE;
		writel(val5, pmu->pmu_base[ch].base + PMU_CTRL);

		mb();
		if (enable) {
			private->g_tim[ch] = 0;
			//set bmu3 axiw addr lenth
			val1 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
			val1 &= (~(BMU3_CFG_BUFF_PAUSE_EN |
				   BMU3_CFG_REG_REUSE_EN |
				   BMU3_CFG_BUFF_FULL_INT_EN | BMU3_CFG_EN));
			val1 |= (BMU3_CFG_FULL_AXI_INT_EN);
			writel(val1, pmu->pmu_base[ch].base + BMU3_CFG);

			val6 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
			val6 &= ~BMU3_CFG_RESET;
			writel(val6, pmu->pmu_base[ch].base + BMU3_CFG);
			mb();

			val6 |= BMU3_CFG_EN;
			writel(val6, pmu->pmu_base[ch].base + BMU3_CFG);
			mb();

			val6 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
			val6 &= ~BMU3_CFG_RESET;
			writel(val6, pmu->pmu_base[ch].base + BMU3_CFG);
			mb();

			val6 |= BMU3_CFG_EN;
			writel(val6, pmu->pmu_base[ch].base + BMU3_CFG);

			mb();
			val10 = readl_relaxed(pmu->pmu_base[ch].base +
					      BMU3_CFG);
			bmu3_status = readl_relaxed(pmu->pmu_base[ch].base +
						    BMU3_STS);
			if (bmu3_status & BMU3_STS_BUSY) {
				val11 = readl_relaxed(pmu->pmu_base[ch].base +
						      BMU3_CFG);
				val11 &= ~BMU3_CFG_EN;
				writel(val11,
				       pmu->pmu_base[ch].base + BMU3_CFG);
				mb();
				val12 = readl_relaxed(pmu->pmu_base[ch].base +
						      BMU3_CFG);
				val12 |= BMU3_CFG_EN;
				writel(val12,
				       pmu->pmu_base[ch].base + BMU3_CFG);
			}
		} else {
			//diaable bmu3 ext int 20240819 for new function
			bmu3_ext_int_enable(pmu, ch, 0);
			bmu3_send_loop_enable(pmu, ch, 0);
			bmu3_sideband_enable(pmu, ch, 0);

			/* Disable monitor */
			val3 &= ~(BMU3_CFG_FULL_AXI_INT_EN |
				  BMU3_CFG_AXI_RESP_ERR_INT_EN |
				  BMU3_CFG_BUFF_FULL_INT_EN |
				  BMU3_CFG_BUFF_PAUSE_EN |
				  BMU3_CFG_REG_REUSE_EN);
			val3 |= (BMU3_CFG_RESET | BMU3_CFG_EN);
			writel(val3, pmu->pmu_base[ch].base + BMU3_CFG);
			val3 &= ~BMU3_CFG_EN;
			writel(val3, pmu->pmu_base[ch].base + BMU3_CFG);
		}
		private->trace_en = val3 & BMU3_CFG_EN;
		private->trace_rst = val3 & BMU3_CFG_RESET;
	}
}

static void bmu_trace_reset(struct ddr_pmu *pmu, u8 ch, u32 enable)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}

	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);

		if (enable) {
			/*release soft reset */
			val &= ~BMU3_CFG_RESET;
		} else {
			/* soft reset monitor */
			val |= BMU3_CFG_RESET;
		}
		private->trace_rst = val & BMU3_CFG_RESET;
		writel(val, pmu->pmu_base[i].base + BMU3_CFG);
	}
}

static void bmu_dma_enable(struct ddr_pmu *pmu, u8 ch, u32 enable)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}
	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_DMA_CSR);
		if (enable) {
			/* dma enable */
			val |= BMU3_DMA_CSR_EN;
		} else {
			/* dma disable */
			val &= ~BMU3_DMA_CSR_EN;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_DMA_CSR);
	}
}

static void bmu3_id_sel(struct ddr_pmu *pmu, u8 ch, bool id_sel)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}
	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
		if (id_sel) {
			val |= BMU3_ID_SRC_SEL;
		} else {
			val &= ~BMU3_ID_SRC_SEL;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_CFG);
	}
}

static void bmu3_send_loop_enable(struct ddr_pmu *pmu, u8 ch, bool en)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}
	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
		if (en) {
			val |= BMU3_SEND_LOOP_EN;
		} else {
			val &= ~BMU3_SEND_LOOP_EN;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_CFG);
	}
}

static void bmu3_sideband_enable(struct ddr_pmu *pmu, u8 ch, bool en)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}
	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
		if (en) {
			val |= BMU3_SIDEBAND_EN;
		} else {
			val &= ~BMU3_SIDEBAND_EN;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_CFG);
	}
}

static void bmu3_ext_int_enable(struct ddr_pmu *pmu, u8 ch, u32 enable)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}
	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_EXT_INT_EN);
		if (enable & BMU3_EXT_INT_EN_MASK) {
			val |= (enable & BMU3_EXT_INT_EN_MASK);
		} else {
			val &= ~BMU3_EXT_INT_EN_MASK;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_EXT_INT_EN);
	}
}

static void bmu3_trigger_enable(struct ddr_pmu *pmu, u8 ch, bool en)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}
	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
		if (en) {
			private->g_tim[i] = 0;
			val |= BMU3_TRIGGER_EN;
		} else {
			val &= ~BMU3_TRIGGER_EN;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
	}
}

static void bmu3_trigger_clr(struct ddr_pmu *pmu, u8 ch, bool en)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}
	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
		if (en) {
			val |= BMU3_TRIGGER_CLR;
		} else {
			val &= ~BMU3_TRIGGER_CLR;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
	}
}

static void bmu3_trigger_cnt_clr_set(struct ddr_pmu *pmu, u8 ch, bool en)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}
	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;
	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
		if (en) {
			val |= BMU3_TRIGGER_CNT_CLR;
		} else {
			val &= ~BMU3_TRIGGER_CNT_CLR;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
	}
}

static void bmu3_trigger_src_sel(struct ddr_pmu *pmu, u8 ch, u8 src)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}

	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;

	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
		val &= (~BMU3_TRIGGER_SRC_SEL);
		val |= (src & BMU3_TRIGGER_SRC_SEL);
		writel(val, pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
	}
}

static void bmu3_trigger_ch_sel(struct ddr_pmu *pmu, u8 ch, bool sel)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}

	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;

	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
		if (sel) {
			val |= BMU3_TRIGGER_CH_SEL;
		} else {
			val &= ~BMU3_TRIGGER_CH_SEL;
		}
		writel(val, pmu->pmu_base[i].base + BMU3_TRIGGER_CTRL);
	}
}

static void bmu3_trigger_cond_set(struct ddr_pmu *pmu, u8 ch, u32 value)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}

	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;

	for (i = start; i < end; i++) {
		val = readl(pmu->pmu_base[i].base + BMU3_TRIGGER_COND_CFG);
		val &= (~BMU3_TRIGGER_COND);
		val |= (value & BMU3_TRIGGER_COND);
		writel(val, pmu->pmu_base[i].base + BMU3_TRIGGER_COND_CFG);
	}
}

static void bmu3_trigger_cond_mask_set(struct ddr_pmu *pmu, u8 ch, u32 value)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}

	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;

	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base +
				    BMU3_TRIGGER_COND_MASK_CFG);
		val &= (~BMU3_TRIGGER_COND_MASK);
		val |= (value & BMU3_TRIGGER_COND_MASK);
		writel(val, pmu->pmu_base[i].base + BMU3_TRIGGER_COND_MASK_CFG);
	}
}

static void bmu3_devid_set(struct ddr_pmu *pmu, u8 ch, u32 value)
{
	struct bmu3_para *private;
	int start, end;
	int val;
	int i;

	private = pmu->bmu_private;
	if (ch > private->reg_group_num) {
		return;
	}

	start = (ch == BMU_CHALL) ? 0 : ch;
	end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;

	for (i = start; i < end; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base +
				    BMU3_DEV_ID_FILTER_CFG);
		val &= (~BMU3_DEVID_MASK);
		val |= (value & BMU3_DEVID_MASK);
		writel(val, pmu->pmu_base[i].base + BMU3_DEV_ID_FILTER_CFG);
	}
}

static int bmu3_get_cnt_overflow(struct ddr_pmu *pmu, u8 ch)
{
	int val;
	bool over_flag;
	struct bmu3_para *private;

	private = pmu->bmu_private;

	if (ch >= private->reg_group_num) {
		return -1;
	}
	val = readl_relaxed(pmu->pmu_base[ch].base + BMU3_TRIGGER_STAT);

	over_flag = (val & 0x200) ? true : false;

	return over_flag;
}

static int bmu3_get_trig_state(struct ddr_pmu *pmu, u8 ch)
{
	int val;
	bool trig_stat;
	struct bmu3_para *private;

	private = pmu->bmu_private;

	if (ch >= private->reg_group_num) {
		return -1;
	}
	val = readl_relaxed(pmu->pmu_base[ch].base + BMU3_TRIGGER_STAT);

	trig_stat = (val & 0x1) ? true : false;

	return trig_stat;
}

static int bmu3_get_trig_cnt(struct ddr_pmu *pmu, u8 ch)
{
	int val;
	struct bmu3_para *private;

	private = pmu->bmu_private;

	if (ch >= private->reg_group_num) {
		return -1;
	}
	val = readl_relaxed(pmu->pmu_base[ch].base + BMU3_TRIGGER_STAT);
	val = (val >> 1) & 0xff;

	return val;
}

static int bmu_trace_init(struct ddr_pmu *pmu)
{
	int i;
	u32 val, val1, val3, val4;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	if (!private) {
		dev_err(pmu->dev, "%s, private is null\n", __func__);
		return -1;
	}

	//config axiw addr,length
	for (i = 0; i < private->reg_group_num; i++) {
		//bmu3 interrupt disable, reuse, pause diaable bmu en , reset en
		val3 = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
		val3 &= (~(BMU3_CFG_BUFF_PAUSE_EN | BMU3_CFG_REG_REUSE_EN |
			   BMU3_CFG_BUFF_FULL_INT_EN |
			   BMU3_CFG_FULL_AXI_INT_EN |
			   BMU3_CFG_AXI_RESP_ERR_INT_EN));
		val3 |= BMU3_CFG_RESET;
		val3 &= ~BMU3_CFG_EN;
		writel(val3, pmu->pmu_base[i].base + BMU3_CFG);

		//disable dma en
		if (!strcmp(private->name, "bmu_ddr")) {
			val4 = readl_relaxed(pmu->pmu_base[i].base +
					     BMU3_DMA_CSR);
			val4 &= ~BMU3_DMA_CSR_EN;
			writel(val4, pmu->pmu_base[i].base + BMU3_DMA_CSR);
		}

		//set bmu3 axiw addr lenth
		val = readl_relaxed(pmu->pmu_base[i].base + PMU_FLT_E_ADDR3);
		val = (val & 0xf0000000) | (private->reg_axiw_pa[i]);
		writel(val, pmu->pmu_base[i].base + PMU_FLT_E_ADDR3);
		val1 = private->reg_axiw_length[i];
		writel(val1, pmu->pmu_base[i].base + PMU_FLT_S_ADDR3);
		bmu3_send_loop_enable(pmu, i, 0);
	}
	private->bm_count_rec_max_len = BM_LOG_FILE_MAX_RECORDS;

	return 0;
}

static void bmu_trace_pause(struct ddr_pmu *pmu, u32 enable)
{
	int val;
	int i;
	struct bmu3_para *private;
	private = pmu->bmu_private;

	for (i = 0; i < private->reg_group_num; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
		if (enable) {
			val |= BMU3_CFG_BUFF_PAUSE_EN;
		} else {
			val &= ~BMU3_CFG_BUFF_PAUSE_EN;
		}
		private->trace_pause_en = val & BMU3_CFG_BUFF_PAUSE_EN;
		writel(val, pmu->pmu_base[i].base + BMU3_CFG);
	}
}

static void bmu_trace_reuse(struct ddr_pmu *pmu, u32 enable)
{
	int val;
	int i;
	struct bmu3_para *private;
	private = pmu->bmu_private;

	for (i = 0; i < private->reg_group_num; i++) {
		val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
		if (enable) {
			val |= BMU3_CFG_REG_REUSE_EN;
		} else {
			val &= ~BMU3_CFG_REG_REUSE_EN;
		}
		private->trace_reuse_en = val & BMU3_CFG_REG_REUSE_EN;
		writel(val, pmu->pmu_base[i].base + BMU3_CFG);
	}
}

static void bmu_capture_en(struct ddr_pmu *pmu, u8 ch, u8 out_sel, bool enable)
{
	u32 bmu3_status;
	u32 dma_status;
	u32 val, val1, val2, val3, val4, val5;
	u32 count;
	u16 i, j;
	u32 *cap;
	u32 *pframe;
	int start, end;

	struct bmu3_para *private;
	private = pmu->bmu_private;

	if (enable) {
		if (ch > private->reg_group_num) {
			return;
		}

		start = (ch == BMU_CHALL) ? 0 : ch;
		end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;

		for (count = 0, i = start; i < end; i++) {
			//trigger mode en
			val3 = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
			val3 &= ~PMU_CLK_EN;
			val3 |= PMU_CLK_EN;
			writel(val3, pmu->pmu_base[i].base + PMU_CTRL);

			//reg reuse enable release soft reset
			val5 = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
			val5 |= BMU3_CFG_REG_REUSE_EN;
			writel(val5, pmu->pmu_base[i].base + BMU3_CFG);

			val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
			val |= BMU3_CFG_RESET;
			val &= ~BMU3_CFG_EN;
			writel(val, pmu->pmu_base[i].base + BMU3_CFG);

			val = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
			val &= ~BMU3_CFG_RESET;
			val &= ~BMU3_CFG_EN;
			writel(val, pmu->pmu_base[i].base + BMU3_CFG);

			//read bmu3 work status idle or count >=1000 exit loop
			do {
				bmu3_status = readl_relaxed(
					pmu->pmu_base[i].base + BMU3_STS);
				if (count++ >= 1000) {
					break;
				}
			} while (bmu3_status & BMU3_STS_BUSY);
			count = 0;

			//read dma work status idle or count >=1000 exit loop
			do {
				dma_status = readl_relaxed(
					pmu->pmu_base[i].base + BMU3_DMA_CSR);
				if (count++ >= 1000) {
					break;
				}
			} while (dma_status & BMU3_DMA_CSR_BUSY);
			count = 0;

			//config axiw addr:0xfffffff,length=64B(512bit)
			if (out_sel & TRACE_REUSE_REG_MAP) {
				val1 = readl_relaxed(pmu->pmu_base[i].base +
						     PMU_FLT_E_ADDR3);
				val1 = (val1 & 0xf0000000) | AXIW_ADDR_VALID;
				writel(val1,
				       pmu->pmu_base[i].base + PMU_FLT_E_ADDR3);
				val2 = 5; //4
				writel(val2,
				       pmu->pmu_base[i].base + PMU_FLT_S_ADDR3);
			} else if (out_sel & TRACE_REUSE_DDR_MAP) {
				bmu_trace_interrupt_enable(pmu, i, 1);
				val2 = readl_relaxed(pmu->pmu_base[i].base +
						     PMU_FLT_E_ADDR3);
				val2 = (val2 & 0xf0000000) |
				       private->reg_axiw_pa[i];
				writel(val2,
				       pmu->pmu_base[i].base + PMU_FLT_E_ADDR3);
				val2 = 5; //4
				writel(val2,
				       pmu->pmu_base[i].base + PMU_FLT_S_ADDR3);
				val5 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val5 &= ~BMU3_CFG_REG_REUSE_EN;
				writel(val5, pmu->pmu_base[i].base + BMU3_CFG);
			}

			//pause en
			val2 = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
			val2 |= BMU3_CFG_BUFF_PAUSE_EN;
			private->trace_pause_en = val2 & BMU3_CFG_BUFF_PAUSE_EN;
			writel(val2, pmu->pmu_base[i].base + BMU3_CFG);

			//trigger mode en
			val3 = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
			val3 &= ~PMU_TRIG_MODE_MASK;
			val3 |= SINGLE_MODE;
			writel(val3, pmu->pmu_base[i].base + PMU_CTRL);

			//bmu3 en
			val4 = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
			val4 |= BMU3_CFG_EN;
			writel(val4, pmu->pmu_base[i].base + BMU3_CFG);

			//read bmu3 work status busy or count >=1000 exit loop
			do {
				bmu3_status = readl_relaxed(
					pmu->pmu_base[i].base + BMU3_STS);
				if (count++ >= 1000) {
					break;
				}
			} while (!((bmu3_status & BMU3_STS_BUSY) ==
				   BMU3_STS_BUSY));
			count = 0;

			if (out_sel & TRACE_REUSE_REG_MAP) {
				u8 cap_len;
				//reg reuse enable
				val5 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val5 |= BMU3_CFG_REG_REUSE_EN;
				writel(val5, pmu->pmu_base[i].base + BMU3_CFG);

				//read capture data from reg[0x40-0x7c]
				cap_len = CAPTURE_SIZE;
				for (j = 0; j < cap_len; j++) {
					private->capture_data
						[i][j +
						    private->cal_cap_frame_cnt] =
						readl_relaxed(
							pmu->pmu_base[i].base +
							PMU_STS_REG_BASE +
							(j << 2));
				}
			} else if (out_sel & TRACE_REUSE_DDR_MAP) {
				//cap trace to ddr mem
				if ((intlv_offset) &&
				    (!strcmp(private->name, "bmu_ddr"))) {
					for (j = 0; j < (CAP_TRACE_SHOW_SIZE /
							 intlv_offset);
					     j++) {
						pframe =
							(u32 *)(private->reg_axiw_va
									[i]) +
							(i * intlv_offset / 4) +
							(j * 2 * intlv_offset /
							 4);
						memcpy((u8 *)(&(private->capture_data
									[i][0]) +
							      j * intlv_offset),
						       (u8 *)pframe,
						       intlv_offset);
					}
				} else {
					cap = (u32 *)(private->reg_axiw_va[i]);
					for (j = 0; j < 128; j++) {
						private->capture_data[i][j] =
							cap[j];
					}
				}
			}

			{
				//release pause en
				val2 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val2 &= ~BMU3_CFG_BUFF_PAUSE_EN;
				writel(val2, pmu->pmu_base[i].base + BMU3_CFG);
				val4 = readl_relaxed(pmu->pmu_base[i].base +
						     BMU3_CFG);
				val4 &= ~BMU3_CFG_EN;
				writel(val4, pmu->pmu_base[i].base + BMU3_CFG);
			}
		}
	} else {
		if (ch > private->reg_group_num) {
			return;
		}

		start = (ch == BMU_CHALL) ? 0 : ch;
		end = (ch == BMU_CHALL) ? private->reg_group_num : ch + 1;

		for (count = 0, i = start; i < end; i++) {
			//set trace mode
			val3 = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
			val3 &= ~PMU_TRIG_MODE_MASK;
			val3 |= PERIOD_MODE;
			writel(val3, pmu->pmu_base[i].base + PMU_CTRL);
			val3 = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
			val3 &= ~PMU_CLK_EN;
			writel(val3, pmu->pmu_base[i].base + PMU_CTRL);
			//disable reuse/pause
			bmu_trace_reuse(pmu, enable);
			bmu_trace_pause(pmu, enable);
			bmu_trace_enable(pmu, i, enable);
			bmu_trace_reset(pmu, i, BMU3_IRQ_RESTORE_RST);
		}
	}
}

static void show_raw_data(struct bmu3_para *private, u8 ch)
{
	int i;
	u32 *ptraceout;

	ptraceout = (u32 *)(private->raw_frame_array[ch].bufvi);
	for (i = 0; i < 16; i++) {
		pr_info("%#x, %#x, %#x, %#x\n", ptraceout[i * 4],
			ptraceout[i * 4 + 1], ptraceout[i * 4 + 2],
			ptraceout[i * 4 + 3]);
	}

	return;
}

static void bmu_trc_fast_disable(struct ddr_pmu *pmu, u8 ch)
{
	int val1, val2;

	val1 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val1 |= 0x01;
	writel(val1, pmu->pmu_base[ch].base + BMU3_CFG); //reset
	val1 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val2 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_STS);

	val1 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val1 &= ~0x02;
	writel(val1, pmu->pmu_base[ch].base + BMU3_CFG); //disable en
	val1 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val2 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_STS);

	return;
}

static int bmu_trc_fast_enable(struct ddr_pmu *pmu, u8 ch)
{
	int val4, val5;

	local_irq_disable();
	val4 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val4 |= 0x01;
	writel(val4, pmu->pmu_base[ch].base + BMU3_CFG); //reset
	val4 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val5 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_STS);

	val4 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val4 &= ~0x02;
	writel(val4, pmu->pmu_base[ch].base + BMU3_CFG); //disable en
	val4 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val5 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_STS);

	val4 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val4 &= ~0x01;
	writel(val4, pmu->pmu_base[ch].base + BMU3_CFG); //release reset
	val4 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val5 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_STS);

	val4 = readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	val4 |= 0x02;
	writel(val4, pmu->pmu_base[ch].base + BMU3_CFG); //disable en
	local_irq_enable();

	return 0;
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	ssize_t len = 0;
	u32 ret = 0;
	u16 val[2];
	u8 i;

	private = pmu->bmu_private;

	for (i = 0; i < private->reg_group_num; i++) {
		u32 temp1, temp2, temp3, temp4;
		temp1 = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		temp2 = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
		temp3 = readl_relaxed(pmu->pmu_base[i].base + BMU3_DMA_CSR);
		temp4 = readl_relaxed(pmu->pmu_base[i].base +
				      BMU3_TRIGGER_CTRL);
		val[i] = (temp1 & 0x3) | (temp2 & 0x3) << 2 |
			 (temp3 & 0x1) << 4 | (temp4 & 0x10) << 1;
		ret |= val[i] << (i * 16);
	}

	len += snprintf(buf + len, 32, "%d\n", ret);

	if (len >= PAGE_SIZE) {
		return -1;
	}

	return len;
}

static void bmu_control(struct ddr_pmu *pmu, u32 para)
{
	struct bmu3_para *private;
	u8 i;
	u8 cnt_ch, trc_ch;
	u8 ctl_en;
	//int val;
	bool cnt_en, trg_en, trc_en, cap_en;

	private = pmu->bmu_private;

	cnt_en = para & 0x1;
	ctl_en = (para >> 8) & 0x7;
	cap_en = (para >> 8) & 0x1;
	trc_en = (para >> 9) & 0x1;
	trg_en = (para >> 10) & 0x1;
	cnt_ch = (para >> 4) & 0x3;
	trc_ch = (para >> 12) & 0x3;

	memset(private->capture_data, 0, sizeof(private->capture_data));
	memset(private->trace_data, 0x0, sizeof(private->trace_data));
	memset(private->reg_axiw_va[0], 0x0, private->reg_axiw_length[0]);
	if (private->reg_axiw_va[1])
		memset(private->reg_axiw_va[1], 0x0,
		       private->reg_axiw_length[1]);
	memset(private->raw_frame_array[0].bufvi, 0x0,
	       private->raw_frame_array[0].cnt);
	memset(private->raw_frame_array[1].bufvi, 0x0,
	       private->raw_frame_array[1].cnt);

	while (hrtimer_cancel(&pmu->trigger_timer)) {
		pr_debug("try to cancel hrtimer...\n");
	}

	if (ctl_en | cnt_en) {
		if (((ctl_en & 0x7)) && (!strcmp(private->name, "bmu_ddr"))) {
			//capture,trace,counter
			hrtimer_start(&(pmu->trigger_timer), ms_to_ktime(1000),
				      HRTIMER_MODE_REL);
		}
	} else {
		private->bm_ctl.bmu_ctl = private->bm_ctl.bmu_ctl &
					  (~bmu_ctl_cap_en);
		private->bm_ctl.bmu_ctl = private->bm_ctl.bmu_ctl &
					  (~bmu_ctl_trc_en);
		bmu_trc_fast_disable(pmu, BMU_CH1);
		bmu_counter_enable(pmu, 0, BMU_CH1);
		if (!strcmp(private->name, "bmu_ddr")) {
			bmu_trc_fast_disable(pmu, BMU_CH2);
			bmu_counter_enable(pmu, 0, BMU_CH2);
		}
		bmu_capture_en(pmu, BMU_CHALL, TRACE_REUSE_REG_MAP, 0);
		bmu3_trigger_enable(pmu, BMU_CHALL, 0);
	}

	if (cnt_ch & 0x3) {
		for (i = 0; i < private->reg_group_num; i++) {
			memset(&(private->cnt_proc1_data[i]), 0,
			       sizeof(cnt_proc1_data_t));
			memset(&(private->cnt_proc2_data[i]), 0,
			       sizeof(cnt_proc1_data_t));
			memset(&(private->pft_event_data[i].count_event), 0,
			       (3 * sizeof(cnt_proc1_data_t)));
			memset((u8 *)(private->bm_cnt_buf[i]), 0,
			       BM_PER_CNT_BUF_SIZE);
			memset((u8 *)(private->tol_bm_cnt_buf), 0,
			       BM_PER_CNT_BUF_SIZE);
		}

		if (cnt_en) {
			u8 chn;
			if ((cnt_ch == 3) &&
			    !strcmp(private->name, "bmu_ddr")) {
				bmu_counter_enable(pmu, cnt_en, 0);
				bmu_counter_enable(pmu, cnt_en, 1);
			} else {
				chn = cnt_ch & 0x2 ? CHN1 - 1 : CHN0 - 1;
				ddr_pmu_counter_period(pmu, chn,
						       PMU_PERIOD_CNT);
				ddr_pmu_clear_irq(pmu, BMU_CH1);
				ddr_pmu_clear_irq(pmu, BMU_CH2);
				bmu_counter_enable(pmu, cnt_en, chn);
			}
		} else {
			for (i = 0; i < private->reg_group_num; i++) {
				writel(0, pmu->pmu_base[i].base + PMU_FLT_CTRL);
				writel(0, pmu->pmu_base[i].base +
						  PMU_TARGET_WDATA);
				writel(0, pmu->pmu_base[i].base + PMU_CTRL);
				memset(&(private->cnt_proc1_data[i]), 0,
				       sizeof(cnt_proc1_data_t));
				memset(&(private->cnt_proc2_data[i]), 0,
				       sizeof(cnt_proc1_data_t));
				memset(&(private->pft_event_data[i].count_event),
				       0, (3 * sizeof(cnt_proc1_data_t)));
			}
		}
	}

	if (trc_ch & 0x3) {
		u8 chn;
		chn = (trc_ch == 0x3) ? BMU_CHALL : trc_ch - 1;
		private->bm_ctl.bmu_ctl =
			(private->bm_ctl.bmu_ctl & (~bmu_ctl_chall)) |
			(trc_ch << 30);
		switch (ctl_en & 0x7) {
		case 1: { //capture on
			private->bm_ctl.bmu_ctl =
				(private->bm_ctl.bmu_ctl & (~bmu_ctl_cap_en)) |
				bmu_ctl_cap_en;
			if (chn == BMU_CHALL) {
				private->cal_cap_frame_cnt = 0;
				bmu_capture_en(pmu, BMU_CHALL,
					       TRACE_REUSE_REG_MAP, 1);
				private->cal_cap_frame_cnt += CAPTURE_SIZE;
				bmu_capture_en(pmu, BMU_CHALL,
					       TRACE_REUSE_REG_MAP, 1);

			} else {
				private->cal_cap_frame_cnt = 0;
				bmu_capture_en(pmu, chn, TRACE_REUSE_REG_MAP,
					       1);
				private->cal_cap_frame_cnt += CAPTURE_SIZE;
				bmu_capture_en(pmu, chn, TRACE_REUSE_REG_MAP,
					       1);
			}
		} break;
		case 2: {
			//trace on
			private->bm_ctl.bmu_ctl =
				(private->bm_ctl.bmu_ctl & (~bmu_ctl_trc_en)) |
				bmu_ctl_trc_en;
			bmu_trace_enable(pmu, chn, 1);
		} break;
		case 4: {
			//axi trigger on
			u8 trg_ch;
			trg_ch = (private->reg_cfg[0].trg_sel_regfc >> 3) & 0x1;
			if (chn == BMU_CHALL) {
				//bmu_trace_reset(pmu,BMU_CHALL,BMU3_IRQ_RESTORE_RST);
				//bmu3_trigger_enable(pmu,BMU_CHALL,0);
				ddr_pmu_set_trigge_mode(pmu, INST_ALL,
							PERIOD_MODE);
				bmu3_devid_set(pmu, BMU_CHALL, 0);
				bmu3_ext_int_enable(pmu, BMU_CHALL, 0);
				bmu3_send_loop_enable(pmu, BMU_CHALL, 0);
				bmu3_sideband_enable(pmu, BMU_CHALL, 0);
				bmu3_trigger_ch_sel(pmu, BMU_CHALL, trg_ch);
				bmu_trace_interrupt_enable(pmu, BMU_CHALL, 1);
				bmu3_trigger_cnt_clr_set(pmu, BMU_CHALL, 1);
				bmu_trace_reset(pmu, BMU_CHALL,
						BMU3_IRQ_RESTORE_ACTIVE);
				bmu3_trigger_enable(pmu, BMU_CHALL, trg_en);
				if (!trg_en)
					bmu3_trigger_clr(pmu, BMU_CHALL, 1);
			} else {
				ddr_pmu_set_trigge_mode(pmu, chn, PERIOD_MODE);
				bmu3_devid_set(pmu, chn, 0);
				bmu3_ext_int_enable(pmu, chn, 0);
				bmu3_send_loop_enable(pmu, chn, 0);
				bmu3_sideband_enable(pmu, chn, 0);
				bmu3_trigger_ch_sel(pmu, chn, trg_ch);
				bmu_trace_interrupt_enable(pmu, chn, 1);
				bmu3_trigger_cnt_clr_set(pmu, chn, 1);
				bmu_trace_reset(pmu, chn,
						BMU3_IRQ_RESTORE_ACTIVE);
				bmu3_trigger_enable(pmu, chn, trg_en);
				if (!trg_en)
					bmu3_trigger_clr(pmu, chn, 1);
			}
		} break;
		default:
			break;
		}
	}
	return;
}

/* ====================== trace/cnt en ==========================
|31---16|15--14|13--12|11  | 10  | 9   | 8 |7--6|5---4|3---1| 0 |
| resv  | resv |ch1-0 |resv| trig|trace|cap|resv|ch1-0|resv | en|
|                        trace             |      count         |
on:
cd /sys/class/misc/bmu_xxx/count
echo 0x00000011 > enable(ch0 on) //counter en
echo 0x00000021 > enable(ch1 on) //counter en for bmu_ddr

cd /sys/class/misc/bmu_xxx/trace
echo 0x00001400 > enable(ch0 on) //trig en
echo 0x00001200 > enable(ch0 on) //trace en
echo 0x00001100 > enable(ch0 on) //capture en
echo 0x00002400 > enable(ch1 on) //trig en for bmu_ddr
echo 0x00002200 > enable(ch1 on) //trace en for bmu_ddr
echo 0x00002100 > enable(ch1 on) //capture en for bmu_ddr

off:
echo 0x00000000 > enable
================================================================*/
static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	int ret;
	u32 para;

	private = pmu->bmu_private;

	ret = sscanf(buf, "%x", &para);
	if (ret != 1) {
		dev_err(dev, "Invalid parameters, err value : %d\n", ret);
		return -EINVAL;
	}

	//para = 0x1200;//0x11,0x00001100
	bmu_control(pmu, para);

	return count;
}

static DEVICE_ATTR_RW(enable);

/*
bytes 64bit : wr(high32)|rd(low32)
*/
static ssize_t bytes_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	ssize_t len = 0;
	u64 val[2] = { 0 };
	u8 i, num;

	private = pmu->bmu_private;
	num = private->reg_group_num;
	for (i = 0; i < num; i++) {
		u32 temp;
		temp = ddr_perf_read_counter(
			pmu, DDR_EVENT_READ_BYTES & ATTR_EVENT_MASK, i);
		val[i] = ddr_perf_read_counter(
			pmu, DDR_EVENT_WRITE_BYTES & ATTR_EVENT_MASK, i);
		val[i] = (val[i] << 32) | (temp & 0xffffffff);
	}

	len += snprintf(buf + len, PAGE_SIZE,
			"CH0 rd bytes: 0x%x, wr bytes: 0x%#x\n"
			"CH1 rd bytes: 0x%x, wr bytes: 0x%#x\n",
			(u32)(val[0] & 0xffffffff),
			(u32)((val[0] >> 32) & 0xffffffff),
			(u32)(val[1] & 0xffffffff),
			(u32)((val[1] >> 32) & 0xffffffff));

	if (len >= PAGE_SIZE) {
		return -1;
	}

	return len;
}

static DEVICE_ATTR_RO(bytes);

/*
cycle 64bit : wr(high32)|rd(low32)
*/
static ssize_t cycle_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	ssize_t len = 0;
	u64 val[2] = { 0 };
	u8 i, num;

	private = pmu->bmu_private;
	num = private->reg_group_num;
	for (i = 0; i < num; i++) {
		u32 temp;
		val[i] = ddr_perf_read_counter(
			pmu, DDR_EVENT_WRITE_DURATION_CNT & ATTR_EVENT_MASK, i);
		temp = ddr_perf_read_counter(
			pmu, DDR_EVENT_READ_DURATION_CNT & ATTR_EVENT_MASK, i);
		val[i] = (val[i] << 32) | (temp & 0xffffffff);
	}

	len += snprintf(buf + len, PAGE_SIZE,
			"CH0 rd cycles: 0x%x, wr cycles: 0x%#x\n"
			"CH1 rd cycles: 0x%x, wr cycles: 0x%#x\n",
			(u32)(val[0] & 0xffffffff),
			(u32)((val[0] >> 32) & 0xffffffff),
			(u32)(val[1] & 0xffffffff),
			(u32)((val[1] >> 32) & 0xffffffff));

	if (len >= PAGE_SIZE) {
		return -1;
	}

	return len;
}

static DEVICE_ATTR_RO(cycle);

/*
trans 64bit : wr(high32)|rd(low32)
*/
static ssize_t trans_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	ssize_t len = 0;
	u64 val[2] = { 0 };
	u8 i, num;

	private = pmu->bmu_private;
	num = private->reg_group_num;
	for (i = 0; i < num; i++) {
		u32 temp;
		val[i] = ddr_perf_read_counter(
			pmu, DDR_EVENT_WRITE_TRANS_CNT & ATTR_EVENT_MASK, i);
		temp = ddr_perf_read_counter(
			pmu, DDR_EVENT_READ_TRANS_CNT & ATTR_EVENT_MASK, i);
		val[i] = (val[i] << 32) | (temp & 0xffffffff);
	}

	len += snprintf(buf + len, PAGE_SIZE,
			"CH0 rd trans: 0x%x, wr trans: 0x%#x\n"
			"CH1 rd trans: 0x%x, wr trans: 0x%#x\n",
			(u32)(val[0] & 0xffffffff),
			(u32)((val[0] >> 32) & 0xffffffff),
			(u32)(val[1] & 0xffffffff),
			(u32)((val[1] >> 32) & 0xffffffff));

	if (len >= PAGE_SIZE) {
		return -1;
	}

	return len;
}

static DEVICE_ATTR_RO(trans);

static ssize_t frame_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	ssize_t len = 0;
	size_t append_size, output_size;
	//u64 val[2];
	u8 i, j;
	u8 num;

	output_size = append_size = 0;
	private = pmu->bmu_private;
	num = private->reg_group_num;
	len = sizeof(private->capture_data);

	for (i = 0; i < num; i++) {
		append_size = snprintf(buf + output_size,
				       PAGE_SIZE - output_size, "Group %d:\n",
				       i);
		output_size += append_size;

		for (j = 0; j < 8; j++) {
			append_size = snprintf(
				buf + output_size, PAGE_SIZE - output_size,
				"ch[%d], %#x, %#x, %#x, %#x\n", i,
				private->capture_data[i][j * 4],
				private->capture_data[i][j * 4 + 1],
				private->capture_data[i][j * 4 + 2],
				private->capture_data[i][j * 4 + 3]);

			output_size += append_size;

			if (output_size >= PAGE_SIZE)
				break;
		}

		if (output_size >= PAGE_SIZE)
			break;
	}

	return output_size;
}

static DEVICE_ATTR_RO(frame);

static ssize_t trace_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	ssize_t len;
	size_t append_size, output_size;
	u32 *ptrc;
	u8 i, j;
	u8 num;

	output_size = append_size = 0;
	private = pmu->bmu_private;
	num = private->reg_group_num;
	len = 256;

	for (i = 0; i < num; i++) {
		output_size += append_size;

		ptrc = (u32 *)private->raw_frame_array[i].bufvi;

		for (j = 0; j < 16; j++) {
			append_size = snprintf(buf + output_size,
					       PAGE_SIZE - output_size,
					       "ch[%d], %#x, %#x, %#x, %#x\n",
					       i, ptrc[j * 4], ptrc[j * 4 + 1],
					       ptrc[j * 4 + 2],
					       ptrc[j * 4 + 3]);

			output_size += append_size;

			if (output_size >= PAGE_SIZE)
				break;
		}

		if (output_size >= PAGE_SIZE)
			break;
	}
	return output_size;
}

static DEVICE_ATTR_RO(trace);

static void bmu_reg_get(struct ddr_pmu *pmu, u8 ch)
{
	struct bmu3_para *private;
	u64 temp = 0;

	private = pmu->bmu_private;
	private->reg_cfg[ch].align_reg0 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_CTRL);
	private->reg_cfg[ch].dura_thresh_reg04 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_DURA_THRESHOLD);
	private->reg_cfg[ch].master_id_reg08 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_MST_ID);
	private->reg_cfg[ch].period_cnt_reg0c =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_MON_PERIOD);
	private->reg_cfg[ch].vr_cnt_flt_reg10 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_FLT_CTRL);
	private->reg_cfg[ch].vr_cnt_len_reg14 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_FLT_LEN);
	private->reg_cfg[ch].vr_up_addr_reg18 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_FLT_E_ADDR0);
	temp = readl_relaxed(pmu->pmu_base[ch].base + PMU_FLT_E_ADDR1);
	private->reg_cfg[ch].vr_up_addr_reg18 |= temp << 32;
	private->reg_cfg[ch].vr_low_addr_reg2c =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_FLT_S_ADDR0);
	temp = readl_relaxed(pmu->pmu_base[ch].base + PMU_FLT_S_ADDR1);
	private->reg_cfg[ch].vr_low_addr_reg2c |= temp << 32;
	private->reg_cfg[ch].cmd_dura_thresh_reg28 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_FLT_CMD);
	private->reg_cfg[ch].target_data_reg3c =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_TARGET_WDATA);
	private->reg_cfg[ch].target_comp_reg80 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_INT_REG);
	private->reg_cfg[ch].ostd_cnt_reg94 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_OSTD_CFG);
	private->reg_cfg[ch].comp_mod_reg98 =
		readl_relaxed(pmu->pmu_base[ch].base + PMU_TARGET_ADDR);
	private->reg_cfg[ch].loop_sband_regac =
		readl_relaxed(pmu->pmu_base[ch].base + BMU3_CFG);
	private->reg_cfg[ch].devid_flt_regf0 =
		readl_relaxed(pmu->pmu_base[ch].base + BMU3_DEV_ID_FILTER_CFG);
	private->reg_cfg[ch].ext_int_regf8 =
		readl_relaxed(pmu->pmu_base[ch].base + BMU3_EXT_INT_EN);
	private->reg_cfg[ch].trg_sel_regfc =
		readl_relaxed(pmu->pmu_base[ch].base + BMU3_TRIGGER_CTRL);
	private->reg_cfg[ch].trg_cond_reg100 =
		readl_relaxed(pmu->pmu_base[ch].base + BMU3_TRIGGER_COND_CFG);
	private->reg_cfg[ch].trg_cond_mask_reg104 = readl_relaxed(
		pmu->pmu_base[ch].base + BMU3_TRIGGER_COND_MASK_CFG);

	return;
}

static ssize_t config_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	ssize_t len = 0;
	u8 i;

	private = pmu->bmu_private;

	for (i = 0; i < private->reg_group_num; i++) {
		bmu_reg_get(pmu, i);
	}

	//ch0
	len += snprintf(buf + len, PAGE_SIZE - len,
			"Channel 0:\n"
			"align_reg0: 0x%x\n"
			"dura_thresh_reg04: 0x%x\n"
			"master_id_reg08: 0x%x\n"
			"period_cnt_reg0c: 0x%x\n"
			"vr_cnt_flt_reg10: 0x%x\n"
			"vr_cnt_len_reg14: 0x%x\n"
			"vr_up_addr_reg18: 0x%llx\n"
			"vr_low_addr_reg2c: 0x%llx\n"
			"cmd_dura_thresh_reg28: 0x%x\n"
			"target_data_reg3c: 0x%x\n"
			"target_comp_reg80: 0x%x\n"
			"ostd_cnt_reg94: 0x%x\n"
			"comp_mod_reg98: 0x%x\n"
			"loop_sband_regac: 0x%x\n"
			"devid_flt_regf0: 0x%x\n"
			"ext_int_regf8: 0x%x\n"
			"trg_sel_regfc: 0x%x\n"
			"trg_cond_reg100: 0x%x\n"
			"trg_cond_mask_reg104: 0x%x\n",
			private->reg_cfg[0].align_reg0,
			private->reg_cfg[0].dura_thresh_reg04,
			private->reg_cfg[0].master_id_reg08,
			private->reg_cfg[0].period_cnt_reg0c,
			private->reg_cfg[0].vr_cnt_flt_reg10,
			private->reg_cfg[0].vr_cnt_len_reg14,
			private->reg_cfg[0].vr_up_addr_reg18,
			private->reg_cfg[0].vr_low_addr_reg2c,
			private->reg_cfg[0].cmd_dura_thresh_reg28,
			private->reg_cfg[0].target_data_reg3c,
			private->reg_cfg[0].target_comp_reg80,
			private->reg_cfg[0].ostd_cnt_reg94,
			private->reg_cfg[0].comp_mod_reg98,
			private->reg_cfg[0].loop_sband_regac,
			private->reg_cfg[0].devid_flt_regf0,
			private->reg_cfg[0].ext_int_regf8,
			private->reg_cfg[0].trg_sel_regfc,
			private->reg_cfg[0].trg_cond_reg100,
			private->reg_cfg[0].trg_cond_mask_reg104);

	// ch1
	len += snprintf(buf + len, PAGE_SIZE - len,
			"\nChannel 1:\n"
			"align_reg0: 0x%x\n"
			"dura_thresh_reg04: 0x%x\n"
			"master_id_reg08: 0x%x\n"
			"period_cnt_reg0c: 0x%x\n"
			"vr_cnt_flt_reg10: 0x%x\n"
			"vr_cnt_len_reg14: 0x%x\n"
			"vr_up_addr_reg18: 0x%llx\n"
			"vr_low_addr_reg2c: 0x%llx\n"
			"cmd_dura_thresh_reg28: 0x%x\n"
			"target_data_reg3c: 0x%x\n"
			"target_comp_reg80: 0x%x\n"
			"ostd_cnt_reg94: 0x%x\n"
			"comp_mod_reg98: 0x%x\n"
			"loop_sband_regac: 0x%x\n"
			"devid_flt_regf0: 0x%x\n"
			"ext_int_regf8: 0x%x\n"
			"trg_sel_regfc: 0x%x\n"
			"trg_cond_reg100: 0x%x\n"
			"trg_cond_mask_reg104: 0x%x\n",
			private->reg_cfg[1].align_reg0,
			private->reg_cfg[1].dura_thresh_reg04,
			private->reg_cfg[1].master_id_reg08,
			private->reg_cfg[1].period_cnt_reg0c,
			private->reg_cfg[1].vr_cnt_flt_reg10,
			private->reg_cfg[1].vr_cnt_len_reg14,
			private->reg_cfg[1].vr_up_addr_reg18,
			private->reg_cfg[1].vr_low_addr_reg2c,
			private->reg_cfg[1].cmd_dura_thresh_reg28,
			private->reg_cfg[1].target_data_reg3c,
			private->reg_cfg[1].target_comp_reg80,
			private->reg_cfg[1].ostd_cnt_reg94,
			private->reg_cfg[1].comp_mod_reg98,
			private->reg_cfg[1].loop_sband_regac,
			private->reg_cfg[1].devid_flt_regf0,
			private->reg_cfg[1].ext_int_regf8,
			private->reg_cfg[1].trg_sel_regfc,
			private->reg_cfg[1].trg_cond_reg100,
			private->reg_cfg[1].trg_cond_mask_reg104);

	if (len >= PAGE_SIZE) {
		return -1;
	}

	return len;
}

/*======================================================================
| 64bit| 64bit| 64bit| 64bit| 64bit| 64bit |
| val0 | val1 | val2 | val3 | val4 | val5  |
| cmd  | para1| para2| para3| para4| para5 |
========================================================================
cmd
|63--40|39--32|31--8 |7---0|
| resv | cmd  | resv | cmd |
|     ch1     |     ch0    |
=======================================================================*/
static ssize_t config_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev->parent);
	struct bmu3_para *private;
	u32 ch0_action, ch1_action;
	u64 val[6];
	u32 ret;

	private = pmu->bmu_private;

	ret = sscanf(buf, "%llx %llx %llx %llx %llx %llx", &val[0], &val[1],
		     &val[2], &val[3], &val[4], &val[5]);
	if (ret != 6) {
		dev_err(dev, "Invalid number of parameters, \
			expected 5, err value : %d\n",
			ret);
		return -EINVAL;
	}

	return strnlen(buf, count);

	ch0_action = val[0] & 0xffffffff;
	ch1_action = (val[0] >> 32) & 0xffffffff;
	switch (ch0_action) {
	case DEVICE_ID_CFG: {
		private->reg_cfg[0].devid_flt_regf0 = (u32)(val[1] & 0xffff);
		private->userid[0] = private->reg_cfg[0].devid_flt_regf0 >> 8;
		bmu3_devid_set(pmu, 0, private->reg_cfg[0].devid_flt_regf0);
		bmu3_id_sel(pmu, 0, BMU3_DEVID_USR);
		if (!strcmp(private->name, "bmu_ddr")) {
			private->reg_cfg[1].devid_flt_regf0 =
				(u32)((val[1] & 0xffff0000) >> 16);
			bmu3_devid_set(pmu, 1,
				       private->reg_cfg[1].devid_flt_regf0);
			bmu3_id_sel(pmu, 1, BMU3_DEVID_USR);
			private->userid[1] =
				private->reg_cfg[1].devid_flt_regf0 >> 8;
		}
	} break;
	case MASTER_ID_CFG: {
		private->reg_cfg[0].master_id_reg08 =
			(u32)((val[1] & 0xffff) << 16 | 0xffff);
		ddr_pmu_config_axid(pmu, 0,
				    private->reg_cfg[0].master_id_reg08);
		if (!strcmp(private->name, "bmu_ddr")) {
			private->reg_cfg[1].master_id_reg08 =
				(u32)((val[1] & 0xffff) << 16 | 0xffff);
			ddr_pmu_config_axid(
				pmu, 1, private->reg_cfg[1].master_id_reg08);
		}
	} break;
	case PERIOD_CNT_CFG: {
		private->reg_cfg[0].period_cnt_reg0c =
			(u32)(val[1] & 0xffffffff);
		ddr_pmu_counter_period(pmu, 0,
				       private->reg_cfg[0].period_cnt_reg0c);
		if (!strcmp(private->name, "bmu_ddr")) {
			private->reg_cfg[1].period_cnt_reg0c =
				(u32)(val[1] & 0xffffffff);
			ddr_pmu_counter_period(
				pmu, 1, private->reg_cfg[1].period_cnt_reg0c);
		}
	} break;
	case DURA_THRESH_CFG: {
		u16 rd_thresh, wr_thresh;
		private->reg_cfg[0].dura_thresh_reg04 =
			(u32)(val[1] & 0xffffffff);
		rd_thresh = private->reg_cfg[0].dura_thresh_reg04 >> 16;
		wr_thresh = private->reg_cfg[0].dura_thresh_reg04 & 0xffff;
		ddr_pmu_config_threshold(pmu, 0, rd_thresh, true);
		ddr_pmu_config_threshold(pmu, 0, wr_thresh, false);
		if (!strcmp(private->name, "bmu_ddr")) {
			private->reg_cfg[1].dura_thresh_reg04 =
				(u32)(val[1] & 0xffffffff);
			ddr_pmu_config_threshold(pmu, 1, rd_thresh, true);
			ddr_pmu_config_threshold(pmu, 1, wr_thresh, false);
		}
	} break;
	case VR_FLT_CFG: {
		u16 flt_size, flt_size_en, flt_len_en;
		u16 flt_len, flt_align_en, flt_align, flt_addr_en;
		u64 start_addr, end_addr;

		if (val[1] != 0xffffffffffffffff) {
			private->reg_cfg[0].vr_cnt_flt_reg10 =
				(u32)(val[1] & 0xffffffff);
			private->reg_cfg[0].vr_cnt_len_reg14 =
				(u32)(val[1] & 0xffffffff);
			private->reg_cfg[0].align_reg0 |=
				(u32)(((val[1] >> 16) & 0xf) << 16);
		}
		private->reg_cfg[0].vr_up_addr_reg18 = val[3] << 32 | val[2];
		private->reg_cfg[0].vr_low_addr_reg2c = val[5] << 32 | val[4];

		if (!strcmp(private->name, "bmu_ddr")) {
			if (val[1] != 0xffffffffffffffff) {
				private->reg_cfg[1].vr_cnt_flt_reg10 =
					(u32)(val[1] & 0xffffffff);
				private->reg_cfg[1].vr_cnt_len_reg14 =
					(u32)(val[1] & 0xffffffff);
				private->reg_cfg[1].align_reg0 |=
					(u32)(((val[1] >> 16) & 0xf) << 16);
			}
			private->reg_cfg[1].vr_up_addr_reg18 = val[3] << 32 |
							       val[2];
			private->reg_cfg[1].vr_low_addr_reg2c = val[5] << 32 |
								val[4];
		}
		flt_size = (private->reg_cfg[0].vr_cnt_flt_reg10 >> 16) & 0x7;
		flt_size_en = (private->reg_cfg[0].vr_cnt_flt_reg10 >> 8) & 0x1;
		flt_len_en = (private->reg_cfg[0].vr_cnt_flt_reg10 >> 12) & 0x1;
		flt_len = private->reg_cfg[0].vr_cnt_len_reg14 & 0xff;
		flt_align = (private->reg_cfg[0].align_reg0 >> 16) & 0xf;
		flt_align_en = private->reg_cfg[0].vr_cnt_flt_reg10 & 0x1;
		flt_addr_en = (private->reg_cfg[0].vr_cnt_flt_reg10 >> 4) & 0x1;

		start_addr = private->reg_cfg[0].vr_low_addr_reg2c;
		end_addr = private->reg_cfg[0].vr_up_addr_reg18;

		if (flt_size_en) {
			ddr_pmu_filter_size(pmu, 0, 0, flt_size);
			if (!strcmp(private->name, "bmu_ddr")) {
				ddr_pmu_filter_size(pmu, 1, 0, flt_size);
			}
		}

		if (flt_len_en) {
			ddr_pmu_filter_len(pmu, 0, 0, flt_len);
			if (!strcmp(private->name, "bmu_ddr")) {
				ddr_pmu_filter_len(pmu, 1, 0, flt_len);
			}
		}

		if (flt_align_en) {
			ddr_pmu_filter_align(pmu, 0, 0, flt_align);
			if (!strcmp(private->name, "bmu_ddr")) {
				ddr_pmu_filter_align(pmu, 1, 0, flt_align);
			}
		}

		if (flt_addr_en) {
			ddr_pmu_config_filter_addr(pmu, 0, 0, start_addr,
						   end_addr);
			ddr_pmu_enable_filter_addr(pmu, 0, 0);
			if (!strcmp(private->name, "bmu_ddr")) {
				ddr_pmu_config_filter_addr(
					pmu, 1, 0, start_addr, end_addr);
				ddr_pmu_enable_filter_addr(pmu, 1, 0);
			}
		}
	} break;
	case DURA_CMD_THRESH_CFG:
		break;
	case TARGET_DATA_CFG: {
		u32 tgt_data;
		u8 cm_mode;
		private->reg_cfg[0].target_data_reg3c = val[1] & 0xffffffff;
		private->reg_cfg[0].target_comp_reg80 = (val[1] >> 33) & 0xf;
		private->reg_cfg[0].comp_mod_reg98 = (val[1] >> 32) & 0x1;
		tgt_data = private->reg_cfg[0].target_data_reg3c;
		cm_mode = private->reg_cfg[0].comp_mod_reg98;
		ddr_pmu_set_target_data(pmu, 0, tgt_data);
		ddr_pmu_config_wdata_select(pmu, 0, 0xf);
		ddr_pmu_set_compare_mode(pmu, 0, cm_mode);
		ddr_pmu_interrupt_enable(pmu, 0, SRC_TARGET_WDATA, true);
		if (!strcmp(private->name, "bmu_ddr")) {
			private->reg_cfg[1].target_data_reg3c = val[1] &
								0xffffffff;
			private->reg_cfg[1].target_comp_reg80 = (val[1] >> 33) &
								0xf;
			private->reg_cfg[1].comp_mod_reg98 = (val[1] >> 32) &
							     0x1;
			ddr_pmu_set_target_data(pmu, 1, tgt_data);
			ddr_pmu_config_wdata_select(pmu, 1, 0xf);
			ddr_pmu_set_compare_mode(pmu, 1, cm_mode);
			ddr_pmu_interrupt_enable(pmu, 1, SRC_TARGET_WDATA,
						 true);
		}
	} break;
	case SIDEBAND_LOOP_CFG: {
		bool loop_en, sideband_en;
		private->reg_cfg[0].loop_sband_regac = (val[1] >> 7) & 0x3;
		sideband_en = private->reg_cfg[0].loop_sband_regac & 0x1;
		loop_en = (private->reg_cfg[0].loop_sband_regac >> 1) & 0x1;
		bmu3_send_loop_enable(pmu, 0, loop_en);
		if (!strcmp(private->name, "bmu_gpu")) {
			bmu3_sideband_enable(pmu, BMU_CHALL, sideband_en);
		}
		if (!strcmp(private->name, "bmu_ddr")) {
			private->reg_cfg[1].loop_sband_regac = (val[1] >> 7) &
							       0x3;
			bmu3_sideband_enable(pmu, BMU_CHALL, sideband_en);
			bmu3_send_loop_enable(pmu, 1, loop_en);
		}
	} break;
	case EXT_INT_CFG: {
		u8 eint_val;
		private->reg_cfg[0].ext_int_regf8 = val[1] & 0x7;
		eint_val = private->reg_cfg[0].ext_int_regf8;
		bmu3_ext_int_enable(pmu, 0, eint_val);
		if (!strcmp(private->name, "bmu_ddr")) {
			private->reg_cfg[1].ext_int_regf8 = val[1] & 0x7;
			bmu3_ext_int_enable(pmu, 1, eint_val);
		}
	} break;
	case TRIG_CFG: {
		u8 trg_src, trg_ch;
		u32 trg_cond;
		u32 trg_mask;
		private->reg_cfg[0].trg_cond_reg100 = val[1] & 0xffffffff;
		private->reg_cfg[0].trg_cond_mask_reg104 = (val[1] >> 32) &
							   0xffffffff;
		private->reg_cfg[0].trg_sel_regfc = val[2] & 0xf;
		trg_cond = private->reg_cfg[0].trg_cond_reg100;
		trg_mask = private->reg_cfg[0].trg_cond_mask_reg104;
		trg_ch = (private->reg_cfg[0].trg_sel_regfc >> 3) & 0x1;
		trg_src = private->reg_cfg[0].trg_sel_regfc & 0x7;
		bmu_trace_reset(pmu, 0, BMU3_IRQ_RESTORE_RST); //bmu rest;
		bmu3_trigger_enable(pmu, 0, 0);
		bmu3_trigger_ch_sel(pmu, 0, trg_ch); //0->read;1->write
		bmu3_trigger_src_sel(pmu, 0, trg_src); //3->tgt data
		bmu3_trigger_cond_set(pmu, 0, trg_cond);
		bmu3_trigger_cond_mask_set(pmu, 0, trg_mask);
		//bmu_trace_reset(pmu,0,BMU3_IRQ_RESTORE_ACTIVE);
		//bmu3_trigger_enable(pmu,0,1);
		if (!strcmp(private->name, "bmu_ddr")) {
			private->reg_cfg[1].trg_cond_reg100 = val[1] &
							      0xffffffff;
			private->reg_cfg[1].trg_cond_mask_reg104 =
				(val[1] >> 32) & 0xffffffff;
			private->reg_cfg[1].trg_sel_regfc = val[2] & 0xf;
			bmu_trace_reset(pmu, 1,
					BMU3_IRQ_RESTORE_RST); //bmu rest;
			bmu3_trigger_enable(pmu, 1, 0);
			bmu3_trigger_ch_sel(pmu, 1, trg_ch); //0->read;1->write
			bmu3_trigger_src_sel(pmu, 1, trg_src); //3->tgt data
			bmu3_trigger_cond_set(pmu, 1, trg_cond);
			bmu3_trigger_cond_mask_set(pmu, 1, trg_mask);
		}
	} break;
	case CAP_TRIG:
		pr_debug("%s, val[1] is %#llx\n", __func__, val[1]);
		break;
	default:
		break;
	}

	return strnlen(buf, count);
}

static DEVICE_ATTR_RW(config);

static struct attribute *bmu_counter_attrs[] = {
	&dev_attr_enable.attr, &dev_attr_bytes.attr,  &dev_attr_cycle.attr,
	&dev_attr_trans.attr,  &dev_attr_config.attr, NULL,
};

static struct attribute_group bmu_counter_group = {
	.name = "count",
	.attrs = bmu_counter_attrs,
};

static struct attribute *bmu_trace_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_frame.attr,
	&dev_attr_trace.attr,
	&dev_attr_config.attr,
	NULL,
};

static struct attribute_group bmu_trace_group = {
	.name = "trace",
	.attrs = bmu_trace_attrs,
};

static const struct attribute_group *bmu_attr_groups[] = {
	&bmu_counter_group,
	&bmu_trace_group,
	NULL,
};

/*decompress an original 11-bit value into a 7-bit value*/
static u16 decode_ulaw(u8 comp_data)
{
	u8 segment_code;
	u16 base;
	u16 num_code;
	u16 origin_data;

	segment_code = num_code = base = origin_data = 0;
	segment_code = (comp_data & 0x70) >> 4;

	if (segment_code == 0) {
		origin_data = comp_data & 0xf;
	} else {
		base = 1 << (segment_code - 1 + SUB_NUM_BIT);
		num_code = (comp_data & 0xf) * (1 << (segment_code - 1));
		origin_data = base + num_code;
	}

	return origin_data;
}

static int get_mod_id(char *name)
{
	int ret = -1;

	if (!strcmp(name, "bmu_ddr")) {
		ret = BMU_DDR;
	} else if (!strcmp(name, "bmu_gpu")) {
		ret = BMU_GPU;
	} else if (!strcmp(name, "bmu_npu")) {
		ret = BMU_NPU;
	} else if (!strcmp(name, "bmu_pcie")) {
		ret = BMU_PCIE;
	} else if (!strcmp(name, "bmu_usb")) {
		ret = BMU_USB;
	} else if (!strcmp(name, "bmu_vo")) {
		ret = BMU_VO;
	} else if (!strcmp(name, "bmu_vi")) {
		ret = BMU_VI;
	} else if (!strcmp(name, "bmu_vp")) {
		ret = BMU_VP;
	} else if (!strcmp(name, "bmu_peri")) {
		ret = BMU_PERI;
	} else if (!strcmp(name, "bmu_d2d")) {
		ret = BMU_D2D;
	}

	return ret;
}

static int bmu_ch0_rawdata_proc0(struct ddr_pmu *pmu, u8 chn)
{
	//int ret;
	int cnt;
	int j;
	u8 *src_va;
	u8 *pframe, *ptraceout;
	phys_addr_t src_pa, end_pa;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	src_pa = private->reg_axiw_pa[0];
	end_pa = private->reg_axiw_pa[0] + 100 * 1024;
	dma_inv_range(src_pa, end_pa); //iv cache

	cnt = 10 * 1024; /*private->raw_frame_array[0].cnt */
	;
	src_va = (u8 *)(private->reg_axiw_va[0]);
	if ((intlv_offset) && (!strcmp(private->name, "bmu_ddr"))) {
		for (j = 0; j < (cnt / intlv_offset); j++) {
			pframe = src_va + (j * 2 * intlv_offset);
			ptraceout = (u8 *)(private->raw_frame_array[0].bufvi) +
				    (j * intlv_offset);
			memcpy((u8 *)ptraceout, (u8 *)pframe, intlv_offset);
		}
	} else {
		pframe = src_va;
		ptraceout = (u8 *)(private->raw_frame_array[0].bufvi);
		memcpy((u8 *)ptraceout, (u8 *)pframe, cnt);
	}

	return 0;
}

static int bmu_ch1_rawdata_proc0(struct ddr_pmu *pmu, u8 chn)
{
	//int ret;
	int cnt;
	int j;
	u8 *src_va;
	u8 *pframe, *ptraceout;
	phys_addr_t src_pa, end_pa;
	struct bmu3_para *private;

	private = pmu->bmu_private;
	if ((intlv_offset) && (!strcmp(private->name, "bmu_ddr"))) {
		src_pa = private->reg_axiw_pa[1];
		end_pa = private->reg_axiw_pa[1] +
			 100 * 1024 /*resv_trace_raw_len/2 */;
		dma_inv_range(src_pa, end_pa); //iv cache
		src_va = (u8 *)(private->reg_axiw_va[1]) + intlv_offset;
		cnt = 10 * 1024 /*private->raw_frame_array[1].cnt */;
		for (j = 0; j < (cnt / intlv_offset); j++) {
			pframe = src_va + (j * 2 * intlv_offset);
			ptraceout = (u8 *)(private->raw_frame_array[1].bufvi) +
				    (j * intlv_offset);
			memcpy((u8 *)ptraceout, (u8 *)pframe, intlv_offset);
		}
	}

	return 0;
}

static enum hrtimer_restart hrtimer_handler(struct hrtimer *timer)
{
	u8 i;
	u32 val1, val2;
	u32 val3, val4, val5, val6;
	u32 rd_byte, wr_byte;
	struct ddr_pmu *pmu;
	struct bmu3_para *private;
	int ret = HRTIMER_RESTART /*HRTIMER_NORESTART */;

	pmu = container_of(timer, struct ddr_pmu, trigger_timer);
	private = pmu->bmu_private;

	for (i = 0; i < private->reg_group_num; i++) {
		val1 = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		val2 = readl_relaxed(pmu->pmu_base[i].base + PMU_INT_REG);
		val3 = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
		val4 = readl_relaxed(pmu->pmu_base[i].base + BMU3_STS);
		val5 = readl_relaxed(pmu->pmu_base[i].base + BMU3_DBG_STS);
		val6 = readl_relaxed(pmu->pmu_base[i].base + BMU3_DBG_WADDR);
		rd_byte = readl_relaxed(pmu->pmu_base[i].base + PMU_RD_STS2);
		wr_byte = readl_relaxed(pmu->pmu_base[i].base + PMU_WR_STS2);
		pr_debug("%s, ch[%d], cnt_ctl<%#x>, int<%#x>, "
			 "trc_cfg<%#x>, sts<%#x>, dbgsts<%#x>, dbgwadr<%#x> "
			 "rd_byte<%u>, wr_byte<%u>\n",
			 __func__, i, val1, val2, val3, val4, val5, val6,
			 rd_byte, wr_byte);
	}

	hrtimer_forward_now(timer, ms_to_ktime(1000));

	return ret;
}

static irqreturn_t bmu_irq_handler(int irq, void *p)
{
	int i, j;
	struct ddr_pmu *pmu = (struct ddr_pmu *)p;
	struct perf_event *event;
	struct hw_perf_event *hwc;
	long long ms;
	int status;
	char tracelog[1024];
	struct trace_point tp;
	struct bmu3_para *private;
	//u32 val1,val2;
	u32 val3, val4;
	//u32 val5,val6,val7, val8;
	u32 ctl, period;
	//int stat,stat1,irq_flag;

	private = pmu->bmu_private;

	if (pmu_trace.trace_enable == 1) {
		ms = ktime_to_ms(ktime_get_boottime());
		sprintf(tracelog, "{pmu_trace<%d> [ms]:%lld",
			pmu_trace.trace_count, ms);
	}

	for (i = 0; i < private->reg_group_num; i++) {
		status = ddr_pmu_query_irq_sts(pmu, i);
		if (status == 0)
			continue;
		ctl = readl_relaxed(pmu->pmu_base[i].base + PMU_CTRL);
		period = readl_relaxed(pmu->pmu_base[i].base + PMU_MON_PERIOD);
		if ((status & IRQ_SRC_TIME_EXPIRED) && ((ctl & 0x3) == 0x3)) {
			private->cnt_proc1_data[i].rd_cycle =
				ddr_perf_read_counter(
					pmu,
					DDR_EVENT_READ_DURATION_CNT &
						ATTR_EVENT_MASK,
					0);
			private->cnt_proc1_data[i].rd_trans =
				ddr_perf_read_counter(pmu,
						      DDR_EVENT_READ_TRANS_CNT &
							      ATTR_EVENT_MASK,
						      0);
			private->cnt_proc1_data[i].rd_bytes =
				ddr_perf_read_counter(pmu,
						      DDR_EVENT_READ_BYTES &
							      ATTR_EVENT_MASK,
						      0);
			private->cnt_proc1_data[i].wr_cycle =
				ddr_perf_read_counter(
					pmu,
					DDR_EVENT_WRITE_DURATION_CNT &
						ATTR_EVENT_MASK,
					0);
			private->cnt_proc1_data[i].wr_trans =
				ddr_perf_read_counter(
					pmu,
					DDR_EVENT_WRITE_TRANS_CNT &
						ATTR_EVENT_MASK,
					0);
			private->cnt_proc1_data[i].wr_bytes =
				ddr_perf_read_counter(pmu,
						      DDR_EVENT_WRITE_BYTES &
							      ATTR_EVENT_MASK,
						      0);

			for (j = 0; j < NUM_TIME_EXPIRED_EVENTS; j++) {
				if (!pmu->events[i][j])
					continue;
				event = pmu->events[i][j];
				if (ddr_perf_is_misc_masked(event))
					continue;
				hwc = &event->hw;
				ddr_perf_event_update_by_inst(event, i);
				if (pmu_trace.trace_enable == 1) {
					char tmp[30];
					tp.reg_offset =
						(unsigned int)((event->attr
									.config &
								DDR_EVENT_MASK) *
							       4) +
						PMU_STS_REG_BASE;
					tp.value = (unsigned int)local64_read(
						&hwc->prev_count);
					if (pmu_trace.trace_data_fmt == 1)
						sprintf(tmp, " [BMU%d_0x%x]:%d",
							i, tp.reg_offset,
							tp.value);
					else // in hex format
						sprintf(tmp,
							" [BMU%d_0x%x]:0x%x", i,
							tp.reg_offset,
							tp.value);
					strcat(tracelog, tmp);
				}
			}
		}

		if (status & IRQ_SRC_TARGET_DATA_OCCUR) {
			if (ddr_pmu_get_compare_mode(pmu, i) == 1) {
				event = pmu->events[i][DDR_EVENT_CAPTURE_R_DATA];
				private->cnt_proc1_data[i].vrd_tgtdat_cnt++;
				if (event) {
					ddr_perf_event_update(event);
				} else {
					dev_err(pmu->dev, "%s bmu inst<%d>: \
					found null event CAP_RDATA\n",
						__func__, i);
				}
			} else {
				event = pmu->events[i][DDR_EVENT_CAPTURE_W_DATA];
				private->cnt_proc1_data[i].vwr_tgtdat_cnt++;
				if (event) {
					ddr_perf_event_update(event);
				} else {
					dev_err(pmu->dev, "%s bmu inst<%d>: \
				found null event CAP_WDATA\n",
						__func__, i);
				}
			}
		}

		if ((status & IRQ_SRC_TARGET_ADDR_R_OCCUR) ||
		    (status & IRQ_SRC_TARGET_ADDR_W_OCCUR)) {
			event = pmu->events[i][DDR_EVENT_CAPTURE_ADDR];
			if (event) {
				ddr_perf_event_update(event);
				if (status & IRQ_SRC_TARGET_ADDR_R_OCCUR) {
					private->cnt_proc1_data[i]
						.vrd_addr_cnt++;
				} else {
					private->cnt_proc1_data[i]
						.vwr_addr_cnt++;
				}
			} else {
				dev_err(pmu->dev, "%s bmu inst<%d>: \
					found null event CAP_ADDR\n",
					__func__, i);
			}
		}

		if (status & IRQ_SRC_READ_ERROR_RESP_OCCUR) {
			event = pmu->events[i][DDR_EVENT_CAPTURE_ERROR_RESP_R];
			if (event) {
				ddr_perf_event_update(event);
			} else
				dev_err(pmu->dev, "%s bmu inst<%d>: \
					found null event ERROR_RESP_R\n",
					__func__, i);
		}

		if (status & IRQ_SRC_WRITE_ERROR_RESP_OCCUR) {
			event = pmu->events[i][DDR_EVENT_CAPTURE_ERROR_RESP_W];
			if (event) {
				ddr_perf_event_update(event);
			} else
				dev_err(pmu->dev, "%s bmu inst<%d>: \
					found null event ERROR_RESP_W\n",
					__func__, i);
		}

		//interupt flag clear start
		if (status & IRQ_SRC_CFG_AXI_RESP_ERR) {
			if (status & IRQ_SRC_CFG_BUFF_FULL) {
				private->buff_full_num[i]++;
				return 0;
			}
			val3 = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
			val3 |= BMU3_CFG_RESET;
			writel(val3, pmu->pmu_base[i].base + BMU3_CFG);
			val3 &= ~BMU3_CFG_RESET;
			writel(val3, pmu->pmu_base[i].base + BMU3_CFG);
			val4 = readl_relaxed(pmu->pmu_base[i].base + BMU3_CFG);
			val4 &= ~BMU3_CFG_EN;
			writel(val4, pmu->pmu_base[i].base + BMU3_CFG);
		} else if (status & IRQ_SRC_CFG_DMA_RESP_ERR) {
			bmu_dma_enable(pmu, i, DMA_DISABLE);
		}

		if (((status & IRQ_SRC_CFG_BUSY_FULL) !=
		     IRQ_SRC_CFG_BUSY_FULL) ||
		    ((status & IRQ_SRC_CFG_BUFF_FULL) !=
		     IRQ_SRC_CFG_BUFF_FULL)) {
			ddr_pmu_clear_irq(pmu, i);
		}

		if (((status & IRQ_SRC_CFG_AXI_FULL) == IRQ_SRC_CFG_AXI_FULL) ||
		    ((status & IRQ_SRC_SEND_HALF_EINT) ==
		     IRQ_SRC_SEND_HALF_EINT) ||
		    ((status & IRQ_SRC_SEND_QUARTER_EINT) ==
		     IRQ_SRC_SEND_QUARTER_EINT) ||
		    ((status & IRQ_SRC_SEND_3QUARTER_EINT) ==
		     IRQ_SRC_SEND_3QUARTER_EINT)) {
			private->chn_id[i] = (i == 1 ? CHN1 : CHN0);
			ddr_pmu_clear_irq(pmu, i);
			bmu_trc_fast_disable(pmu, i);

			if (status & IRQ_SRC_CFG_AXI_FULL) {
				atomic_set(&(pmu->bm_stat[i]), BMU_INT_TRIG);
				atomic_set(&(pmu->event_flag[i]),
					   TRACE_FULL_OCCUR);
			}

			if (i) {
				complete(&pmu->comp2);
			} else {
				complete(&pmu->comp1);
			}
		}

		if (((status & IRQ_SRC_TIME_EXPIRED) == IRQ_SRC_TIME_EXPIRED) &&
		    ((ctl & 0x3) == 0x3)) {
			ddr_pmu_clear_irq(pmu, i);
			atomic_set(&(pmu->bm_cnt_stat[i]), BMU_INT_TRIG);
			atomic_set(&(pmu->cnt_event_flag[i]), CNT_TIME_EXPIRED);
			if (i) {
				complete(&pmu->comp2);
			} else {
				complete(&pmu->comp1);
			}
		}
	}

	if (pmu_trace.trace_enable == 1) {
		strcat(tracelog, " }:");
		if (pmu_trace.trace_mode == 0) {
			// via uart log
			dev_info(pmu->dev, "%s\n", tracelog);
		}
		pmu_trace.trace_count++;
	}

	return IRQ_HANDLED;
}

static int bmu3_monitor_thread(void *data)
{
	struct ddr_pmu *pmu = (struct ddr_pmu *)data;
	struct bmu3_para *private;
	char *bmu_name;
	//u32 val1,val2,val3,val4,val5,val6,val7;
	int stat, stat1, irq_flag, cnt_irq_flag;
	int ret1, ret2;
	unsigned long time;
	u32 t_cnt;

	while (!kthread_should_stop()) {
		time = wait_for_completion_timeout(&pmu->comp1,
						   msecs_to_jiffies(5000));
		if (!time) {
			if (t_cnt++ > 100) {
				t_cnt = 0;
			}
			continue;
		}
		private = pmu->bmu_private;
		bmu_name = private->name;
		ret1 = ret2 = 0;
		stat = atomic_read(&pmu->bm_stat[0]);
		irq_flag = atomic_read(&pmu->event_flag[0]);
		if ((stat == BMU_INT_TRIG) &&
		    (irq_flag == TRACE_HALF_OCCUR ||
		     irq_flag == TRACE_FULL_OCCUR ||
		     irq_flag == TRACE_TIMER_OVEROUT)) {
			bmu_ch0_rawdata_proc0(pmu, BMU_CH1);
			if (private->bm_ctl.bmu_ctl & bmu_ctl_trc_en) {
				//bmu_trace_enable(pmu, 0, 1);
				bmu_trace_interrupt_enable(pmu, 0, 1);
				bmu_trc_fast_enable(pmu, 0);
			}
		}

		stat1 = atomic_read(&pmu->bm_cnt_stat[0]);
		cnt_irq_flag = atomic_read(&pmu->cnt_event_flag[0]);
		if ((stat1 == BMU_INT_TRIG) &&
		    (cnt_irq_flag == CNT_TIME_EXPIRED)) {
			memcpy(&(private->cnt_proc2_data[0]),
			       &(private->cnt_proc1_data[0]),
			       sizeof(cnt_proc1_data_t));
		}
	}

	return 0;
}

static int bmu3_monitor_thread2(void *data)
{
	struct ddr_pmu *pmu = (struct ddr_pmu *)data;
	struct bmu3_para *private;
	const char *bmu_name;
	//u32 val1,val2,val3,val4,val5,val6,val7;
	int stat, stat1, irq_flag, cnt_irq_flag;
	unsigned long time;
	u32 t_cnt = 0;

	while (!kthread_should_stop()) {
		time = wait_for_completion_timeout(&pmu->comp2,
						   msecs_to_jiffies(5000));
		if (!time) {
			if (t_cnt++ > 100) {
				t_cnt = 0;
			}
			continue;
		}

		private = pmu->bmu_private;
		bmu_name = private->name;

		stat = atomic_read(&pmu->bm_stat[1]);
		irq_flag = atomic_read(&pmu->event_flag[1]);
		if ((stat == BMU_INT_TRIG) && (irq_flag == TRACE_HALF_OCCUR ||
					       irq_flag == TRACE_FULL_OCCUR)) {
			bmu_ch1_rawdata_proc0(pmu, BMU_CH2);
			if (private->bm_ctl.bmu_ctl & bmu_ctl_trc_en) {
				bmu_trace_interrupt_enable(pmu, 1, 1);
				bmu_trc_fast_enable(pmu, 1);
			}
		}

		stat1 = atomic_read(&pmu->bm_cnt_stat[1]);
		cnt_irq_flag = atomic_read(&pmu->cnt_event_flag[1]);
		if ((stat1 == BMU_INT_TRIG) &&
		    (cnt_irq_flag == CNT_TIME_EXPIRED)) {
			memcpy(&(private->cnt_proc2_data[1]),
			       &(private->cnt_proc1_data[1]),
			       sizeof(cnt_proc1_data_t));
		}
	}

	return 0;
}

static void *shmem_ram_vmap_nocache(phys_addr_t start, size_t size,
				    int noncached, struct ddr_pmu *pmu)
{
	struct page **pages;
	phys_addr_t page_start;
	phys_addr_t addr;
	pgprot_t prot;
	unsigned int i;
	unsigned int page_count;
	void *vaddr;
	struct smem_map_list *smem = &(pmu->mem_mp);

	if (!smem->inited)
		return NULL;

	page_start = start - offset_in_page(start);
	page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);
	if (noncached)
		prot = pgprot_noncached(PAGE_KERNEL);
	else
		prot = PAGE_KERNEL;

	pages = kmalloc_array(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		pr_err("%s: Failed to allocate array for %u pages\n", __func__,
		       page_count);
		return NULL;
	}

	for (i = 0; i < page_count; i++) {
		addr = page_start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}

	vaddr = vmap(pages, page_count, VM_MAP, prot);
	kfree(pages);

	return vaddr + offset_in_page(start);
}

static void shmem_ram_unmap(const void *mem)
{
	vunmap(mem - offset_in_page(mem));
}

static void __init reserve_bmu(struct resource *res, u32 len)
{
	int ret = 0;
	unsigned long long bmu_base = 0;
	unsigned long long bmu_size = 0;

	bmu_size = len;
	bmu_size = PAGE_ALIGN(bmu_size);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
	{
		unsigned long search_start = memblock_start_of_DRAM();
		unsigned long search_end = memblock_end_of_DRAM();
		bmu_base = memblock_find_in_range(search_start, search_end,
						  bmu_size, PMD_SIZE);
	}
#else
	bmu_base = memblock_phys_alloc_range(bmu_size, PAGE_SIZE, 0, 0);
#endif

	if (bmu_base == 0) {
		pr_warn("bmu: couldn't allocate %lldKB\n", bmu_size >> 10);
		return;
	}

	ret = memblock_reserve(bmu_base, bmu_size);
	if (ret < 0) {
		pr_warn("bmu reservation failed - memory is in use (0x%lx)\n",
			(unsigned long)bmu_base);
		return;
	}

	res->start = bmu_base;
	res->end = bmu_base + bmu_size - 1;

	insert_resource(&iomem_resource, res);
}

static int bmu_parse_dt(struct platform_device *pdev, struct ddr_pmu *pmu)
{
	//u32 len;
	u32 reg_size;
	int i, irq, ret;
	struct device_node *np;
	struct resource res;
	struct bmu3_para *private;
	void __iomem *base[NUM_INST];
	void *base_axiw[NUM_INST];
	struct device_node *memnp;
	const char *bmu_name;
	char *irq_name;
	phys_addr_t raw_pt;
	phys_addr_t trace_raw_addr;

	private = pmu->bmu_private;
	bmu_name = private->name;

	/* get bmu num */
	if (of_property_read_u32_index(pdev->dev.of_node, "zhihe,bm-num", 0,
				       &(private->reg_group_num))) {
		dev_err(&pdev->dev, "%s, Error: Can't get port number!\n",
			__func__);
		return -EINVAL;
	}

	// to init with some individual pmu instances
	for (i = 0; i < private->reg_group_num; i++) {
		ret = of_address_to_resource(pdev->dev.of_node, i, &res);
		if (ret) {
			dev_err(&pdev->dev, "%s, \
				unsupported reg<%d> in dts\n",
				__func__, i);
			return ret;
		}

		reg_size = res.end - res.start + 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
		base[i] = devm_ioremap_nocache(&pdev->dev, res.start, reg_size);
#else
		base[i] = devm_ioremap(&pdev->dev, res.start, reg_size);
#endif
		if (IS_ERR(base[i])) {
			dev_err(&pdev->dev, "%s, iomap failed, \
				phy_addr[%#llx]\n",
				__func__, res.start);
			return PTR_ERR(base[i]);
		}
		private->reg_pa[i] = res.start;
		private->reg_len[i] = reg_size;
		private->reg_va[i] = base[i];
	}

	//sys reg for control
	for (i = 0; i < private->reg_group_num; i++) {
		if (private->reg1_pa[i] == 0) {
			continue;
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
		private->reg1_va[i] = devm_ioremap_nocache(
			&pdev->dev, private->reg1_pa[i], private->reg1_size);
#else
		private->reg1_va[i] = devm_ioremap(
			&pdev->dev, private->reg1_pa[i], private->reg1_size);
#endif
		if (IS_ERR(private->reg1_va)) {
			dev_err(&pdev->dev, "%s, iomap phy_addr[%#x]\n",
				__func__, private->reg1_pa[i]);
			return PTR_ERR(private->reg1_va[i]);
		}
	}

	for (i = 0; i < private->reg_group_num; i++) {
		if (private->reg2_pa[i] == 0) {
			continue;
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
		private->reg2_va[i] = devm_ioremap_nocache(
			&pdev->dev, private->reg2_pa[i], private->reg2_size);
#else
		private->reg2_va[i] = devm_ioremap(
			&pdev->dev, private->reg2_pa[i], private->reg2_size);
#endif
		if (IS_ERR(private->reg2_va[i])) {
			dev_err(&pdev->dev, "%s, iomap ddr sysreg \
				phy addr[%#x]\n",
				__func__, private->reg2_pa[i]);
			return PTR_ERR(private->reg2_va[i]);
		}
	}

	for (i = 0; i < private->reg_group_num; i++) {
		if (private->reg3_pa[i] == 0) {
			continue;
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
		private->reg3_va[i] = devm_ioremap_nocache(
			&pdev->dev, private->reg3_pa[i], private->reg3_size);
#else
		private->reg3_va[i] = devm_ioremap(
			&pdev->dev, private->reg3_pa[i], private->reg3_size);
#endif
		if (IS_ERR(private->reg3_va[i])) {
			dev_err(&pdev->dev, "%s, iomap error, \
					ddr sysreg phy addr[%#x]\n",
				__func__, private->reg3_pa[i]);
			return PTR_ERR(private->reg3_va[i]);
		}
	}

	for (i = 0; i < 10; i++) {
		if (bmu_buff.resv_res[i].flags == 0)
			break;
	}
	bmu_buff.resv_res[i].flags = IORESOURCE_BUSY | IORESOURCE_MEM;
	bmu_buff.resv_res[i].name = bmu_name;
	private->resv_res.start = bmu_buff.resv_res[i].start;
	memnp = NULL;
	for (i = 0, reg_size = 0; i < private->reg_group_num; i++) {
		reg_size = resv_trace_raw_len / 2;
		trace_raw_addr = private->resv_res.start;
		res.start = trace_raw_addr + i * reg_size;
		base_axiw[i] =
			shmem_ram_vmap_nocache(res.start, reg_size, 1, pmu);
		if (IS_ERR(base_axiw[i])) {
			dev_err(&pdev->dev, "%s, axiw vmap nocache failed, \
				phy_addr[%#llx]\n",
				__func__, res.start);
			return PTR_ERR(base_axiw[i]);
		}

		if (!strcmp(bmu_name, "bmu_ddr")) {
			//private->reg_axiw_pa[i]=(res.start>>12)&BMU3_AXIW_WR_ADDR_28BIT;
			reg_size = 200 * 1024;
			private->reg_axiw_pa[i] =
				bmuaxiw_virt_to_phy(res.start) >> 12;
			private->reg_axiw_length[i] =
				((reg_size >> 1) & BMU3_AXIW_WR_LEN_32BIT) >> 4;
			private->reg_axiw_va[i] = base_axiw[i];
		} else if ((!strcmp(bmu_name, "bmu_vp")) ||
			   (!strcmp(bmu_name, "bmu_vi")) ||
			   (!strcmp(bmu_name, "bmu_vo")) ||
			   (!strcmp(bmu_name, "bmu_gpu")) ||
			   (!strcmp(bmu_name, "bmu_peri")) ||
			   (!strcmp(bmu_name, "bmu_npu")) ||
			   (!strcmp(bmu_name, "bmu_pcie")) ||
			   (!strcmp(bmu_name, "bmu_usb")) ||
			   (!strcmp(bmu_name, "bmu_d2d"))) {
			private->reg_axiw_pa[i] = res.start >> 12;
			private->reg_axiw_length[i] =
				(reg_size >> 1) & BMU3_AXIW_WR_LEN_32BIT >> 4;
			private->reg_axiw_va[i] = base_axiw[i];
		}
	}

	//create buffer for counter and trace
	for (i = 0; i < private->reg_group_num; i++) {
		private->bm_cnt_buf[i] = devm_kzalloc(
			&pdev->dev, (BM_PER_CNT_BUF_SIZE * 2), GFP_KERNEL);
		if (!private->bm_cnt_buf[i]) {
			dev_err(&pdev->dev, "%s, CH[%d], bm_cnt_buf[%px], \
				kzalloc is failed\n",
				__func__, i, private->bm_cnt_buf[i]);
			return -EINVAL;
		}
	}

	private->tol_bm_cnt_buf =
		devm_kzalloc(&pdev->dev, (BM_PER_CNT_BUF_SIZE * 2), GFP_KERNEL);
	if (!private->tol_bm_cnt_buf) {
		dev_err(&pdev->dev, "%s, tol_bm_cnt_buf[%px] \
			kzalloc is failed\n",
			__func__, private->tol_bm_cnt_buf);
		return -EINVAL;
	}

	if (!strcmp(private->name, "bmu_vp")) {
		private->pCap_head = (Packet_Header *)kzalloc(
			sizeof(Packet_Header), GFP_KERNEL);
		if (!private->pCap_head) {
			dev_err(&pdev->dev, "%s, \
				cap_data kzalloc is failed\n",
				__func__);
			return -EINVAL;
		}
	}

	private->resv_res.start = private->resv_res.start + resv_trace_raw_len;
	for (i = 0, reg_size = 0; i < private->reg_group_num; i++) {
		raw_pt = private->resv_res.start +
			 i * (RAW_BUF_SIZE + 7 * PROC1_BUF_SIZE);
		private->raw_frame_array[i].cnt = RAW_BUF_SIZE;
		private->raw_frame_array[i].pa = raw_pt;
		private->raw_frame_array[i].bufvi =
			shmem_ram_vmap_nocache(raw_pt, RAW_BUF_SIZE, 0, pmu);
		if (IS_ERR(private->raw_frame_array[i].bufvi)) {
			dev_err(&pdev->dev, "%s, raw vmap nocache failed, \
				phy_addr[%#llx]\n",
				__func__, raw_pt);
			return PTR_ERR(private->raw_frame_array[i].bufvi);
		}
	}

	/* Request irq */
	np = pdev->dev.of_node;
	for (i = 0; i < private->reg_group_num; i++) {
		irq = of_irq_get(np, i * 2);
		if (irq < 0) {
			dev_err(&pdev->dev, "%s, \
				Failed to get irq: %d, %d\n",
				__func__, irq, i * 2);
			ret = irq;
			return -EINVAL;
		}
        	irq_name = devm_kmalloc(&pdev->dev, 30, GFP_KERNEL);
		if (!irq_name) {
			dev_err(&pdev->dev, "%s, \
				kmalloc irq_name failed\n", __func__);
			return -ENOMEM;
			return -ENOMEM;
		}
		sprintf(irq_name, "%s%d", bmu_name, i * 2);
		ret = devm_request_irq(&pdev->dev, irq, bmu_irq_handler,
				       IRQF_NO_THREAD, irq_name, pmu);
		if (ret < 0) {
			dev_err(&pdev->dev, "%s, Request irq failed:%d,%d\n",
				__func__, ret, i * 2);
			return -EINVAL;
		}
		pmu->irq[i * 2] = irq;

		if (!strcmp(bmu_name, "bmu_ddr")) {
			irq = of_irq_get(np, i * 2 + 1);
			if (irq < 0) {
				dev_err(&pdev->dev, "%s, \
					Failed to get irq: %d, %d\n",
					__func__, irq, i * 2 + 1);
				ret = irq;
				return -EINVAL;
			}
			irq_name = devm_kmalloc(&pdev->dev, 30, GFP_KERNEL);
			if (!irq_name) {
				dev_err(&pdev->dev, "%s, \
					kmalloc irq_name failed\n", __func__);
				return -ENOMEM;
			}
			sprintf(irq_name, "%s%d", bmu_name, i * 2 + 1);
			ret = devm_request_irq(&pdev->dev, irq, bmu_irq_handler,
					       IRQF_NO_THREAD, irq_name, pmu);
			if (ret < 0) {
				dev_err(&pdev->dev, "%s, \
					Request irq failed: %d, %d\n",
					__func__, ret, i * 2 + 1);
				return -EINVAL;
			}
			pmu->irq[i * 2 + 1] = irq;
		}
	}

	return 0;
}

static int bmu_perf_probe(struct platform_device *pdev)
{
	struct ddr_pmu *pmu;
	struct bmu3_para *bmu3_data;
	char thread_name[30];
	const char *bmu_name;
	const struct of_device_id *match;
	struct smem_map_list *smem;
	char *name;
	int num;
	int ret;
	int i;
	if (!pdev) {
		pr_err("%s, pdev null\n", __func__);
		return -EINVAL;
	}

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu) {
		dev_err(&pdev->dev, "%s, Error: devm kzalloc faild!\n",
			__func__);
		return -ENOMEM;
	}

	//match node private data
	match = of_match_node(a210_bmu_dt_ids, pdev->dev.of_node);
	if (!match) {
		dev_err(&pdev->dev, "%s, Error: Can't fount match table!\n",
			__func__);
		return -EINVAL;
	}

	bmu3_data = (struct bmu3_para *)match->data;
	if (!bmu3_data) {
		dev_err(&pdev->dev, "%s, Error: get match->data is null!\n",
			__func__);
		return -EINVAL;
	}
	pmu->bmu_private = bmu3_data;

	smem = &(pmu->mem_mp);
	if (!smem) {
		dev_err(&pdev->dev, "%s, Error: smem is null\n", __func__);
		return -EINVAL;
	}
	smem->inited = 1;

	/*get bmu name */
	if (of_property_read_string_index(pdev->dev.of_node, "zhihe,bm-name", 0,
					  &bmu_name)) {
		dev_err(&pdev->dev, "%s, Error: Read bmu name\n", __func__);
		return -EINVAL;
	}

	if (strcmp(bmu_name, bmu3_data->name)) {
		dev_err(&pdev->dev, "%s, Error: find dt not same device\n",
			__func__);
		return -EINVAL;
	}
	//parse device tree
	ret = bmu_parse_dt(pdev, pmu);
	if (ret) {
		dev_err(&pdev->dev, "%s, Error: dt error\n", __func__);
		return -EINVAL;
	}

	num = ddr_perf_init(pmu, bmu3_data->reg_va, &pdev->dev);
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, DDR_PERF_DEV_NAME "%d",
			      num);
	if (!name)
		return -ENOMEM;

	//regiter perf pmu
	ret = perf_pmu_register(&pmu->pmu, bmu_name, -1);
	if (ret)
		goto ddr_perf_err;

	pmu->misc.name = bmu_name;
	pmu->misc.parent = &pdev->dev;
	pmu->misc.minor = MISC_DYNAMIC_MINOR;
	ret = misc_register(&pmu->misc);
	if (ret) {
		dev_err(&pdev->dev,
			"%s, Error: Unable to "
			"register misc dev\n",
			__func__);
		return ret;
	}
	ret = sysfs_create_group(&pmu->misc.this_device->kobj,
				 bmu_attr_groups[0]);
	if (ret) {
		dev_err(&pdev->dev,
			"%s, Error: Unable to export "
			"bmu sysfs\n",
			__func__);
		misc_deregister(&pmu->misc);
		return ret;
	}
	ret = sysfs_create_group(&pmu->misc.this_device->kobj,
				 bmu_attr_groups[1]);
	if (ret) {
		dev_err(&pdev->dev,
			"%s, Error: Unable to "
			"export bmu sysfs\n",
			__func__);
		misc_deregister(&pmu->misc);
		return ret;
	}
	platform_set_drvdata(pdev, pmu);

	if (!strcmp(bmu_name, "bmu_ddr")) {
		pmu_ddr = pmu;
	}

	//init timer
	{
		hrtimer_init(&(pmu->trigger_timer), CLOCK_MONOTONIC,
			     HRTIMER_MODE_REL);
		pmu->trigger_timer.function = hrtimer_handler;
	}

	for (i = 0; i < bmu3_data->reg_group_num; i++) {
		atomic_set(&(pmu->bm_stat[i]), BMU_IDLE);
		atomic_set(&(pmu->bm_cnt_stat[i]), BMU_IDLE);
		atomic_set(&(pmu->event_flag[i]), EVENT_NONE);
		atomic_set(&(pmu->cnt_event_flag[i]), EVENT_NONE);
	}

	spin_lock_init(&pmu->bm_lock);
	init_completion(&pmu->comp1);
	sprintf(thread_name, "%s0_thread\n", bmu_name);
	pmu->task1 = kthread_create(bmu3_monitor_thread, pmu, thread_name);
	if (!pmu->task1)
		dev_err(&pdev->dev, "%s, Failed to create print thread[%s]\n",
			__func__, thread_name);
	else {
		wake_up_process(pmu->task1);
	}

	if (bmu3_data->reg_group_num == 2) {
		init_completion(&pmu->comp2);
		sprintf(thread_name, "%s1_thread\n", bmu_name);
		pmu->task2 =
			kthread_create(bmu3_monitor_thread2, pmu, thread_name);
		if (!pmu->task2)
			dev_err(&pdev->dev, "%s, Failed to create \
				 print thread[%s]\n",
				__func__, thread_name);
		else {
			wake_up_process(pmu->task2);
		}
	}

	return 0;

ddr_perf_err:
	ida_simple_remove(&ddr_ida, pmu->id);
	dev_err(&pdev->dev, " DDR Perf PMU failed (%d), disabled\n", ret);
	return ret;
}

static int bmu_perf_remove(struct platform_device *pdev)
{
	struct ddr_pmu *pmu = platform_get_drvdata(pdev);
	struct bmu3_para *private;
	int i;

	private = pmu->bmu_private;

	if (pmu->task1)
		kthread_stop(pmu->task1);
	if (pmu->task2)
		kthread_stop(pmu->task2);

	for (i = 0; i < private->reg_group_num; i++) {
		if (private->raw_frame_array[i].bufvi)
			shmem_ram_unmap(private->raw_frame_array[i].bufvi);
		if (private->reg_axiw_va[i])
			shmem_ram_unmap(private->reg_axiw_va[i]);
		if (private->bm_cnt_buf[i])
			devm_kfree(&pdev->dev, private->bm_cnt_buf[i]);
		if (private->tol_bm_cnt_buf)
			devm_kfree(&pdev->dev, private->tol_bm_cnt_buf);
		if (private->pCap_head)
			kfree(private->pCap_head);
	}

	sysfs_remove_group(pdev->dev.kobj.parent, bmu_attr_groups[0]);
	sysfs_remove_group(pdev->dev.kobj.parent, bmu_attr_groups[1]);

	misc_deregister(&pmu->misc);
	perf_pmu_unregister(&pmu->pmu);
	ida_simple_remove(&ddr_ida, pmu->id);

	return 0;
}

static struct platform_driver a210_bmu_driver = {
  .driver = {
	     .name = "zhihe-bmu",
	     .of_match_table = a210_bmu_dt_ids,
	     },
  .probe = bmu_perf_probe,
  .remove = bmu_perf_remove,
};

int __init zhihe_bmu_probe(void)
{
	u32 len1, len2;
	u8 i;

	if (!bmu_buff.resv_res[BMU_DDR].start) {
		len1 = (RAW_BUF_SIZE + 7 * PROC1_BUF_SIZE) * 2 +
		       resv_trace_raw_len;
		len2 = resv_trace_raw_len + RAW_BUF_SIZE + 7 * PROC1_BUF_SIZE;
		reserve_bmu(&(bmu_buff.resv_res[BMU_DDR]), len1);
		for (i = BMU_GPU; i < BMU_CATE_MAX; i++) {
			reserve_bmu(&(bmu_buff.resv_res[i]), len2);
		}
	}

	return platform_driver_register(&a210_bmu_driver);
}

device_initcall(zhihe_bmu_probe);
