/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/io.h>
#include <dt-bindings/reset/a210-reset.h>


#define A210_RST_NAME_SIZE 20

/* reset subsys enumeration */
enum {
	VP_RST,
	VI_RST,
	NPU_RST,
	VO_RST,
	PERI0_RST,
	PERI1_RST,
	PERI2_RST,
	PERI3_RST,
	PCIE_RST,
	USB_RST,
	TEE_RST,
	GPU_RST,
	A210_RESET_SUBSYS_MAX,
};

/* one single reset signal*/
struct a210_rst_signal {
	unsigned int offset, bit;
};

/* reset info within a subsys */
struct a210_rst_subsys {
	void __iomem *base;
	const struct a210_rst_signal *signals;
	u32 num_signals;
	char name[A210_RST_NAME_SIZE];
};

struct a210_reset {
	struct device *dev;
	struct reset_controller_dev reset;
	struct a210_rst_subsys *subsys;
	u32 num_subsys;
	spinlock_t lock;
};

static const struct a210_rst_signal vp_rst_signals[] = {
	[VP_VDEC_PRST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[VP_VDEC_CRST & RST_SIGNAL_MASK] = {0, BIT(1)},
	[VP_VDEC_ARST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[VP_VENC_PRST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[VP_VENC_CRST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[VP_VENC_ARST & RST_SIGNAL_MASK] = {0, BIT(5)},
	[VP_G2D_PRST & RST_SIGNAL_MASK] = {0, BIT(6)},
	[VP_G2D_CRST & RST_SIGNAL_MASK] = {0, BIT(7)},
	[VP_G2D_ARST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[VP_COMP_PRST & RST_SIGNAL_MASK] = {0, BIT(9)},
	[VP_COMP_CRST & RST_SIGNAL_MASK] = {0, BIT(10)},
	[VP_COMP_ARST & RST_SIGNAL_MASK] = {0, BIT(11)},
	[VP_DECOMP_PRST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[VP_DECOMP_CRST & RST_SIGNAL_MASK] = {0, BIT(13)},
	[VP_DECOMP_ARST & RST_SIGNAL_MASK] = {0, BIT(14)},
	[VP_DFMU_PRST & RST_SIGNAL_MASK] = {0, BIT(15)},
	[VP_DFMU_ARST & RST_SIGNAL_MASK] = {0, BIT(16)},
	[VP_ARB_PRST & RST_SIGNAL_MASK] = {0, BIT(18)},
	[VP_ARB_ARST & RST_SIGNAL_MASK] = {0, BIT(19)},
	[VP_VDEC_RS_ARST & RST_SIGNAL_MASK] = {0, BIT(20)},
	[VP_VENC_RS_ARST & RST_SIGNAL_MASK] = {0, BIT(21)},
	[VP_COMP_EXTPRST & RST_SIGNAL_MASK] = {0, BIT(22)},
	[VP_DECOMP_EXTPRST & RST_SIGNAL_MASK] = {0, BIT(23)},
};

static const struct a210_rst_signal vi_rst_signals[] = {
	[VI_MIPI0_CSI0_PRST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[VI_MIPI0_CSI1_PRST & RST_SIGNAL_MASK] = {0, BIT(1)},
	[VI_MIPI0_FIFO_RST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[VI_MIPI1_CSI0_PRST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[VI_MIPI1_CSI1_PRST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[VI_MIPI1_FIFO_RST & RST_SIGNAL_MASK] = {0, BIT(5)},
	[VI_VIPRE_PRST & RST_SIGNAL_MASK] = {0, BIT(6)},
	[VI_VIPRE_I0_PIX_RST & RST_SIGNAL_MASK] = {0, BIT(7)},
	[VI_VIPRE_I1_PIX_RST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[VI_VIPRE_ISPIF_RST & RST_SIGNAL_MASK] = {0, BIT(9)},
	[VI_VIPRE_ARST & RST_SIGNAL_MASK] = {0, BIT(10)},
	[VI_ISP_RST & RST_SIGNAL_MASK] = {0, BIT(11)},
	[VI_COMP_PRST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[VI_COMP_ARST & RST_SIGNAL_MASK] = {0, BIT(13)},
	[VI_COMP_ISPOUT_RST & RST_SIGNAL_MASK] = {0, BIT(14)},
	[VI_COMP_DECOUT_RST & RST_SIGNAL_MASK] = {0, BIT(15)},
	[VI_COMP_VSEOUT_RST & RST_SIGNAL_MASK] = {0, BIT(16)},
	[VI_COMP0_RST & RST_SIGNAL_MASK] = {0, BIT(17)},
	[VI_COMP1_RST & RST_SIGNAL_MASK] = {0, BIT(18)},
	[VI_DECOMP_RST & RST_SIGNAL_MASK] = {0, BIT(19)},
	[VI_DW200_RST & RST_SIGNAL_MASK] = {0, BIT(20)},
	[VI_DFMU_PRST & RST_SIGNAL_MASK] = {0, BIT(21)},
	[VI_DFMU_ARST & RST_SIGNAL_MASK] = {0, BIT(22)},
	[VI_ARB_PRST & RST_SIGNAL_MASK] = {0, BIT(23)},
	[VI_ARB_ARST & RST_SIGNAL_MASK] = {0, BIT(24)},
	[VI_X2H0_HRST & RST_SIGNAL_MASK] = {0, BIT(26)},
	[VI_X2H1_HRST & RST_SIGNAL_MASK] = {0, BIT(27)},
	[VI_X2H2_HRST & RST_SIGNAL_MASK] = {0, BIT(28)},
	[VI_REC_PRST & RST_SIGNAL_MASK] = {0, BIT(29)},
	[VI_REC_ARST & RST_SIGNAL_MASK] = {0, BIT(30)},
};

static const struct a210_rst_signal npu_rst_signals[] = {
	[NPU_AAB_MST_RST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[NPU_AFENCE_RST & RST_SIGNAL_MASK] = {0, BIT(1)},
	[NPU_AXI_CCU_PRST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[NPU_AXI_CCU_RST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[NPU_AXI_M2S1_RST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[NPU_AXI_RST & RST_SIGNAL_MASK] = {0, BIT(5)},
	[NPU_CLK_CALC_RST & RST_SIGNAL_MASK] = {0, BIT(6)},
	[NPU_DFMU_ARST & RST_SIGNAL_MASK] = {0, BIT(7)},
	[NPU_DFMU_PRST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[NPU_LPC_C_RST & RST_SIGNAL_MASK] = {0, BIT(9)},
	[NPU_LPE_C_PRST & RST_SIGNAL_MASK] = {0, BIT(10)},
	[NPU_LPE_C_RST & RST_SIGNAL_MASK] = {0, BIT(11)},
	[NPU_ARST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[NPU_CRST & RST_SIGNAL_MASK] = {0, BIT(13)},
	[NPU_HRST & RST_SIGNAL_MASK] = {0, BIT(14)},
	[NPU_PCTRL_PB_RST & RST_SIGNAL_MASK] = {0, BIT(15)},
	[NPU_PCTRL_PRST & RST_SIGNAL_MASK] = {0, BIT(16)},
	[NPU_PCTRL_RST & RST_SIGNAL_MASK] = {0, BIT(17)},
	[NPU_PTW_CCU_PRST & RST_SIGNAL_MASK] = {0, BIT(18)},
	[NPU_PTW_CCU_RST & RST_SIGNAL_MASK] = {0, BIT(19)},
	[NPU_SEMA_ARST & RST_SIGNAL_MASK] = {0, BIT(20)},
	[NPU_SEMA_PRST & RST_SIGNAL_MASK] = {0, BIT(21)},
	[NPU_DROOP_CCTRL_PRST & RST_SIGNAL_MASK] = {0, BIT(22)},
	[NPU_DROOP_CCTRL_RST & RST_SIGNAL_MASK] = {0, BIT(23)},
	[NPU_X2H_RST & RST_SIGNAL_MASK] = {0, BIT(24)},
	[NPU_X2P_RST & RST_SIGNAL_MASK] = {0, BIT(25)},
	[NPU_LDIP_RST & RST_SIGNAL_MASK] = {0, BIT(26)},
	[NPU_SRAM_RST & RST_SIGNAL_MASK] = {0, BIT(27)},
};

static const struct a210_rst_signal vo_rst_signals[] = {
	[VO_X2H0_RST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[VO_X2H1_RST & RST_SIGNAL_MASK] = {0, BIT(1)},
	[VO_DPU_HRST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[VO_DPU_CRST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[VO_DPU_ARST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[VO_AUXDISP_PRST & RST_SIGNAL_MASK] = {0, BIT(5)},
	[VO_AUXDISP_PIX_RST & RST_SIGNAL_MASK] = {0, BIT(6)},
	[VO_AUXDISP_ARST & RST_SIGNAL_MASK] = {0, BIT(7)},
	[VO_HDMI_PRST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[VO_HDMI_MAIN_RST & RST_SIGNAL_MASK] = {0, BIT(9)},
	[VO_MIPI_PRST & RST_SIGNAL_MASK] = {0, BIT(10)},
	[VO_DECOMP_PRST & RST_SIGNAL_MASK] = {0, BIT(11)},
	[VO_DECOMP0_CRST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[VO_DECOMP1_CRST & RST_SIGNAL_MASK] = {0, BIT(13)},
	[VO_DECOMP_ARST & RST_SIGNAL_MASK] = {0, BIT(14)},
	[VO_ARB_PRST & RST_SIGNAL_MASK] = {0, BIT(15)},
	[VO_ARB_ARST & RST_SIGNAL_MASK] = {0, BIT(16)},
	[VO_DFMU_PRST & RST_SIGNAL_MASK] = {0, BIT(17)},
	[VO_DFMU_ARST & RST_SIGNAL_MASK] = {0, BIT(18)},
};

static const struct a210_rst_signal peri0_rst_signals[] = {
	[PERI0_TIMER0_CRST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[PERI0_TIMER0_PRST & RST_SIGNAL_MASK] = {0, BIT(1)},
	[PERI0_TIMER1_CRST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[PERI0_TIMER1_PRST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[PERI0_WDT0_PRST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[PERI0_MBOX0_PRST & RST_SIGNAL_MASK] = {0, BIT(5)},
	[PERI0_MBOX1_PRST & RST_SIGNAL_MASK] = {0, BIT(6)},
};

static const struct a210_rst_signal peri1_rst_signals[] = {
	[PERI1_GMAC0_ARST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[PERI1_GMAC0_HRST & RST_SIGNAL_MASK] = {0, BIT(1)},
	[PERI1_GMAC1_ARST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[PERI1_GMAC1_HRST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[PERI1_GPIO0_DBRST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[PERI1_GPIO0_PRST & RST_SIGNAL_MASK] = {0, BIT(5)},
	[PERI1_GPIO1_DBRST & RST_SIGNAL_MASK] = {0, BIT(6)},
	[PERI1_GPIO1_PRST & RST_SIGNAL_MASK] = {0, BIT(7)},
	[PERI1_I2C0_IC_RST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[PERI1_I2C0_PRST & RST_SIGNAL_MASK] = {0, BIT(9)},
	[PERI1_I2C1_IC_RST & RST_SIGNAL_MASK] = {0, BIT(10)},
	[PERI1_I2C1_PRST & RST_SIGNAL_MASK] = {0, BIT(11)},
	[PERI1_I2C2_IC_RST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[PERI1_I2C2_PRST & RST_SIGNAL_MASK] = {0, BIT(13)},
	[PERI1_I2S0_PRST & RST_SIGNAL_MASK] = {0, BIT(14)},
	[PERI1_MST_BUS_ARST & RST_SIGNAL_MASK] = {0, BIT(15)},
	[PERI1_MST_BUS_PRST & RST_SIGNAL_MASK] = {0, BIT(16)},
	[PERI1_PWM0_CRST & RST_SIGNAL_MASK] = {0, BIT(17)},
	[PERI1_PWM0_PRST & RST_SIGNAL_MASK] = {0, BIT(18)},
	[PERI1_QSPI0_PRST & RST_SIGNAL_MASK] = {0, BIT(19)},
	[PERI1_QSPI0_SSI_RST & RST_SIGNAL_MASK] = {0, BIT(20)},
	[PERI1_SPI0_PRST & RST_SIGNAL_MASK] = {0, BIT(21)},
	[PERI1_SPI0_SSI_RST & RST_SIGNAL_MASK] = {0, BIT(22)},
	[PERI1_UART0_PRST & RST_SIGNAL_MASK] = {0, BIT(23)},
	[PERI1_UART0_S_RST & RST_SIGNAL_MASK] = {0, BIT(24)},
	[PERI1_UART1_PRST & RST_SIGNAL_MASK] = {0, BIT(25)},
	[PERI1_UART1_S_RST & RST_SIGNAL_MASK] = {0, BIT(26)},
	[PERI1_UART2_PRST & RST_SIGNAL_MASK] = {0, BIT(27)},
	[PERI1_UART2_S_RST & RST_SIGNAL_MASK] = {0, BIT(28)},
	[PERI1_UART3_PRST & RST_SIGNAL_MASK] = {0, BIT(29)},
	[PERI1_UART3_S_RST & RST_SIGNAL_MASK] = {0, BIT(30)},
	[PERI1_X2H_GMAC0_ARST & RST_SIGNAL_MASK] = {0, BIT(31)},
	[PERI1_X2H_GMAC0_HRST & RST_SIGNAL_MASK] = {0x4, BIT(0)},
	[PERI1_X2H_GMAC1_ARST & RST_SIGNAL_MASK] = {0x4, BIT(1)},
	[PERI1_X2H_GMAC1_HRST & RST_SIGNAL_MASK] = {0x4, BIT(2)},
	[PERI1_DFMU_ARESET & RST_SIGNAL_MASK] = {0x4, BIT(3)},
	[PERI1_DFMU_PRESET & RST_SIGNAL_MASK] = {0x4, BIT(4)},
	[PERI1_PAD_CTRL_PRST & RST_SIGNAL_MASK] = {0x4, BIT(5)},
	[PERI1_PDM0_MRST & RST_SIGNAL_MASK] = {0x4, BIT(6)},
	[PERI1_PDM0_PRST & RST_SIGNAL_MASK] = {0x4, BIT(7)},
	[PERI1_TDM0_RST & RST_SIGNAL_MASK] = {0x4, BIT(8)},
	[PERI1_CAN0_IPG_PE_RST & RST_SIGNAL_MASK] = {0x4, BIT(9)},
	[PERI1_CAN0_IPG_RST & RST_SIGNAL_MASK] = {0x4, BIT(10)},
	[PERI1_CAN0_IPG_SOFT_RST & RST_SIGNAL_MASK] = {0x4, BIT(11)},
	[PERI1_CAN0_IPG_TS_RST & RST_SIGNAL_MASK] = {0x4, BIT(12)},
	[PERI1_CAN0_PRST & RST_SIGNAL_MASK] = {0x4, BIT(13)},
	[PERI1_CAN1_IPG_PE_RST & RST_SIGNAL_MASK] = {0x4, BIT(14)},
	[PERI1_CAN1_IPG_RST & RST_SIGNAL_MASK] = {0x4, BIT(15)},
	[PERI1_CAN1_IPG_SOFT_RST & RST_SIGNAL_MASK] = {0x4, BIT(16)},
	[PERI1_CAN1_IPG_TS_RST & RST_SIGNAL_MASK] = {0x4, BIT(17)},
	[PERI1_CAN1_PRST & RST_SIGNAL_MASK] = {0x4, BIT(18)},
	[PERI1_CHIP_DBG_ARST & RST_SIGNAL_MASK] = {0x4, BIT(19)},
	[PERI1_CHIP_DBG_CRST & RST_SIGNAL_MASK] = {0x4, BIT(20)},
	[PERI1_CHIP_DBG_PRST & RST_SIGNAL_MASK] = {0x4, BIT(21)},
	[PERI1_GMAC_CRST & RST_SIGNAL_MASK] = {0x4, BIT(22)},
	[PERI1_X2H_GMAC2_ARST & RST_SIGNAL_MASK] = {0x4, BIT(23)},
	[PERI1_X2H_GMAC2_HRST & RST_SIGNAL_MASK] = {0x4, BIT(24)},
	[PERI1_ZGMAC_X2X_ARST & RST_SIGNAL_MASK] = {0x4, BIT(25)},
};

static const struct a210_rst_signal peri2_rst_signals[] = {
	[PERI2_CAN2_IPG_PE_RST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[PERI2_CAN2_IPG_RST & RST_SIGNAL_MASK] = {0, BIT(1)},
	[PERI2_CAN2_IPG_SOFT_RST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[PERI2_CAN2_IPG_TS_RST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[PERI2_CAN2_PRST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[PERI2_GPIO2_DBRST & RST_SIGNAL_MASK] = {0, BIT(5)},
	[PERI2_GPIO2_PRST & RST_SIGNAL_MASK] = {0, BIT(6)},
	[PERI2_I2C4_IC_RST & RST_SIGNAL_MASK] = {0, BIT(7)},
	[PERI2_I2C4_PRST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[PERI2_I2S2_PRST & RST_SIGNAL_MASK] = {0, BIT(9)},
	[PERI2_SPI1_PRST & RST_SIGNAL_MASK] = {0, BIT(10)},
	[PERI2_SPI1_SSI_RST & RST_SIGNAL_MASK] = {0, BIT(11)},
	[PERI2_UART4_PRST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[PERI2_UART4_S_RST & RST_SIGNAL_MASK] = {0, BIT(13)},
	[PERI2_UART5_PRST & RST_SIGNAL_MASK] = {0, BIT(14)},
	[PERI2_UART5_S_RST & RST_SIGNAL_MASK] = {0, BIT(15)},
	[PERI2_UART6_PRST & RST_SIGNAL_MASK] = {0, BIT(16)},
	[PERI2_UART6_S_RST & RST_SIGNAL_MASK] = {0, BIT(17)},
	[PERI2_PAD_CTRL_PRST & RST_SIGNAL_MASK] = {0, BIT(18)},
	[PERI2_GPIO3_DBRST & RST_SIGNAL_MASK] = {0, BIT(19)},
	[PERI2_GPIO3_PRST & RST_SIGNAL_MASK] = {0, BIT(20)},
	[PERI2_I2C3_IC_RST & RST_SIGNAL_MASK] = {0, BIT(21)},
	[PERI2_I2C3_PRST & RST_SIGNAL_MASK] = {0, BIT(22)},
	[PERI2_I2C5_IC_RST & RST_SIGNAL_MASK] = {0, BIT(23)},
	[PERI2_I2C5_PRST & RST_SIGNAL_MASK] = {0, BIT(24)},
	[PERI2_I2C6_IC_RST & RST_SIGNAL_MASK] = {0, BIT(25)},
	[PERI2_I2C6_PRST & RST_SIGNAL_MASK] = {0, BIT(26)},
	[PERI2_I2C7_IC_RST & RST_SIGNAL_MASK] = {0, BIT(27)},
	[PERI2_I2C7_PRST & RST_SIGNAL_MASK] = {0, BIT(28)},
	[PERI2_I2S1_PRST & RST_SIGNAL_MASK] = {0, BIT(29)},
	[PERI2_I2S3_PRST & RST_SIGNAL_MASK] = {0, BIT(30)},
	[PERI2_UART7_PRST & RST_SIGNAL_MASK] = {0, BIT(31)},
	[PERI2_UART7_S_RST & RST_SIGNAL_MASK] = {0x4, BIT(0)},
	[PERI2_UART8_PRST & RST_SIGNAL_MASK] = {0x4, BIT(1)},
	[PERI2_UART8_S_RST & RST_SIGNAL_MASK] = {0x4, BIT(2)},
	[PERI2_UART9_PRST & RST_SIGNAL_MASK] = {0x4, BIT(3)},
	[PERI2_UART9_S_RST & RST_SIGNAL_MASK] = {0x4, BIT(4)},
	[PERI2_QSPI1_PRST & RST_SIGNAL_MASK] = {0x4, BIT(5)},
	[PERI2_QSPI1_SSI_RST & RST_SIGNAL_MASK] = {0x4, BIT(6)},
	[PERI2_PWM1_CRST & RST_SIGNAL_MASK] = {0x4, BIT(7)},
	[PERI2_PWM1_PRST & RST_SIGNAL_MASK] = {0x4, BIT(8)},
	[PERI2_PWM2_CRST & RST_SIGNAL_MASK] = {0x4, BIT(9)},
	[PERI2_PWM2_PRST & RST_SIGNAL_MASK] = {0x4, BIT(10)},
};

static const struct a210_rst_signal peri3_rst_signals[] = {
	[PERI3_DMAC_ARESET & RST_SIGNAL_MASK] = {0, BIT(0)},
	[PERI3_DMAC_HRESET & RST_SIGNAL_MASK] = {0, BIT(1)},
	[PERI3_EMMC_SDIO_CLKGEN_RST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[PERI3_EMMC_RST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[PERI3_EMMC_X2X_ARST_M & RST_SIGNAL_MASK] = {0, BIT(4)},
	[PERI3_EMMC_X2X_ARST_S & RST_SIGNAL_MASK] = {0, BIT(5)},
	[PERI3_SDIO_RST & RST_SIGNAL_MASK] = {0, BIT(6)},
	[PERI3_SDIO_X2X_ARST_M & RST_SIGNAL_MASK] = {0, BIT(7)},
	[PERI3_SDIO_X2X_ARST_S & RST_SIGNAL_MASK] = {0, BIT(8)},
	[PERI3_AXI_MST_ARST & RST_SIGNAL_MASK] = {0, BIT(9)},
	[PERI3_AXI_MST_PRST & RST_SIGNAL_MASK] = {0, BIT(10)},
	[PERI3_ADC_PRST & RST_SIGNAL_MASK] = {0, BIT(11)},
	[PERI3_TEE_X2X_ARST_M & RST_SIGNAL_MASK] = {0, BIT(12)},
	[PERI3_TEE_X2X_ARST_S & RST_SIGNAL_MASK] = {0, BIT(13)},
	[PERI3_TEE_H2H_HRST & RST_SIGNAL_MASK] = {0, BIT(14)},
	[PERI3_GPIO4_DBRST & RST_SIGNAL_MASK] = {0, BIT(15)},
	[PERI3_GPIO4_PRST & RST_SIGNAL_MASK] = {0, BIT(16)},
	[PERI3_PAD_CTRL_PRST & RST_SIGNAL_MASK] = {0, BIT(17)},
};

static const struct a210_rst_signal pcie_rst_signals[] = {
	[PCIE_X2X_PERI_SLV_ARST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[PCIE_X2X_PERI_MST_ARST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[PCIE_AXI4_PCIE_MST_ARST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[PCIE_AXI4_PCIE_MST_PRST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[PCIE_DFMU_PRST & RST_SIGNAL_MASK] = {0x4, BIT(0)},
	[PCIE_DFMU_ARST & RST_SIGNAL_MASK] = {0x4, BIT(4)},
	[PCIE_E16PHY_PHY_RST & RST_SIGNAL_MASK] = {0x8, BIT(0)},
	[PCIE_E16PHY_APBS_PRST & RST_SIGNAL_MASK] = {0x8, BIT(4)},
	[PCIE_X2X_SATA_MST_ARST & RST_SIGNAL_MASK] = {0x10, BIT(0)},
	[PCIE_X2X_SATA_SLV_ARST & RST_SIGNAL_MASK] = {0x10, BIT(4)},
	[PCIE_SATA_ARESET & RST_SIGNAL_MASK] = {0x10, BIT(8)},
	[PCIE_SATA_RST_PMALIVE & RST_SIGNAL_MASK] = {0x10, BIT(12)},
	[PCIE_SATA_SLV_AFENCE_ARST & RST_SIGNAL_MASK] = {0x10, BIT(16)},
	[PCIE_SATA_MST_AFENCE_ARST & RST_SIGNAL_MASK] = {0x10, BIT(20)},
	[PCIE_SATA_RST_ASIC0 & RST_SIGNAL_MASK] = {0x14, BIT(0)},
	[PCIE_SATA_RST_ASIC1 & RST_SIGNAL_MASK] = {0x14, BIT(4)},
	[PCIE_SATA_RST_RXOOB0 & RST_SIGNAL_MASK] = {0x14, BIT(8)},
	[PCIE_SATA_RST_RXOOB1 & RST_SIGNAL_MASK] = {0x14, BIT(12)},
	[PCIE_DM_GEN3X4_APBS_PRST & RST_SIGNAL_MASK] = {0x20, BIT(0)},
	[PCIE_DM_GEN3X4_POWER_UP_RST & RST_SIGNAL_MASK] = {0x20, BIT(4)},
	[PCIE_DM_SLV_AFENCE_ARST & RST_SIGNAL_MASK] = {0x20, BIT(16)},
	[PCIE_DM_MST_AFENCE_ARST & RST_SIGNAL_MASK] = {0x20, BIT(20)},
	[PCIE_RP_GEN3X1_APBS_PRST & RST_SIGNAL_MASK] = {0x24, BIT(0)},
	[PCIE_RP_GEN3X1_POWER_UP_RST & RST_SIGNAL_MASK] = {0x24, BIT(4)},
	[PCIE_RP_SLV_AFENCE_ARST & RST_SIGNAL_MASK] = {0x24, BIT(16)},
	[PCIE_RP_MST_AFENCE_ARST & RST_SIGNAL_MASK] = {0x24, BIT(20)},
};

static const struct a210_rst_signal usb_rst_signals[] = {
	[USB_DFMU_PRST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[USB_DFMU_ARST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[USB_AXI4_MST_ARST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[USB_AXI4_MST_PRST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[USB_DPTX_APBS_PRST & RST_SIGNAL_MASK] = {0x4, BIT(0)},
	[USB_DPTX_VCC_RST & RST_SIGNAL_MASK] = {0x4, BIT(4)},
	[USB_USB31_APBS_PRST & RST_SIGNAL_MASK] = {0x8, BIT(0)},
	[USB_USB31_VCC_RST & RST_SIGNAL_MASK] = {0x8, BIT(4)},
	[USB_USB31_PHY_RST & RST_SIGNAL_MASK] = {0x8, BIT(8)},
	[USB_USB31_SLV_AFENCE_ARST & RST_SIGNAL_MASK] = {0x8, BIT(12)},
	[USB_C10PHY_PHY_RST & RST_SIGNAL_MASK] = {0xc, BIT(0)},
	[USB_C10PHY_APBS_PRST & RST_SIGNAL_MASK] = {0xc, BIT(4)},
	[USB_USB20_BLK_X2H_HRST & RST_SIGNAL_MASK] = {0x10, BIT(0)},
	[USB_USB20_BLK_X2H_ARST & RST_SIGNAL_MASK] = {0x10, BIT(4)},
	[USB_USB20_SLV_AFENCE_ARST & RST_SIGNAL_MASK] = {0x10, BIT(8)},
	[USB_USB20_BLK_H2P_HRST & RST_SIGNAL_MASK] = {0x10, BIT(16)},
	[USB_USB20_BLK_AHB_SLV_HRST & RST_SIGNAL_MASK] = {0x10, BIT(20)},
	[USB_USB20_BLK_AXI_MST_ARST & RST_SIGNAL_MASK] = {0x10, BIT(24)},
	[USB_USB20_BLK_USB2_SYSREG_PRST & RST_SIGNAL_MASK] = {0x10, BIT(28)},
	[USB_USB20_BLK_USB2_WRAP0_PRST & RST_SIGNAL_MASK] = {0x14, BIT(0)},
	[USB_USB20_BLK_USB2_WRAP0_HRST & RST_SIGNAL_MASK] = {0x14, BIT(4)},
	[USB_USB20_BLK_USB2_WRAP1_PRST & RST_SIGNAL_MASK] = {0x14, BIT(8)},
	[USB_USB20_BLK_USB2_WRAP1_HRST & RST_SIGNAL_MASK] = {0x14, BIT(12)},
	[USB_USB20_BLK_A2X0_HRST & RST_SIGNAL_MASK] = {0x14, BIT(16)},
	[USB_USB20_BLK_A2X0_ARST & RST_SIGNAL_MASK] = {0x14, BIT(20)},
	[USB_USB20_BLK_A2X1_HRST & RST_SIGNAL_MASK] = {0x14, BIT(24)},
	[USB_USB20_BLK_A2X1_ARST & RST_SIGNAL_MASK] = {0x14, BIT(28)},
	[USB_USB20_BLK_USB0_PHY_PON_RESET & RST_SIGNAL_MASK] = {0x18, BIT(0)},
	[USB_USB20_BLK_USB1_PHY_PON_RESET & RST_SIGNAL_MASK] = {0x18, BIT(4)},
	[USB_PERI2_SS_RST & RST_SIGNAL_MASK] = {0x20, BIT(0)},
	[USB_PERI2_SLV_AFENCE_ARST & RST_SIGNAL_MASK] = {0x20, BIT(4)},
};

static const struct a210_rst_signal tee_rst_signals[] = {
	[TEE_KEYRAM_PRST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[TEE_DS_PRST & RST_SIGNAL_MASK] = {0, BIT(1)},
	[TEE_EFUSE_PRST & RST_SIGNAL_MASK] = {0, BIT(2)},
	[TEE_OCRAM_HRST & RST_SIGNAL_MASK] = {0, BIT(3)},
	[TEE_SYSREG_PRST & RST_SIGNAL_MASK] = {0, BIT(4)},
	[TEE_CCU_PRST & RST_SIGNAL_MASK] = {0, BIT(5)},
	[TEE_CCU_CRST & RST_SIGNAL_MASK] = {0, BIT(6)},
	[TEE_EIP150B_HRST & RST_SIGNAL_MASK] = {0, BIT(8)},
	[TEE_EIP120SIII_HRST & RST_SIGNAL_MASK] = {0, BIT(9)},
	[TEE_EIP120SIII_ARST & RST_SIGNAL_MASK] = {0, BIT(10)},
	[TEE_EIP120SII_HRST & RST_SIGNAL_MASK] = {0, BIT(11)},
	[TEE_EIP120SII_ARST & RST_SIGNAL_MASK] = {0, BIT(12)},
	[TEE_EIP120SI_HRST & RST_SIGNAL_MASK] = {0, BIT(13)},
	[TEE_EIP120SI_ARST & RST_SIGNAL_MASK] = {0, BIT(14)},
	[TEE_DMAC_HRST & RST_SIGNAL_MASK] = {0, BIT(15)},
	[TEE_DMAC_ARST & RST_SIGNAL_MASK] = {0, BIT(16)},
	[TEE_X2P_TEESYS_PRST & RST_SIGNAL_MASK] = {0, BIT(24)},
	[TEE_X2P_TEESYS_ARST & RST_SIGNAL_MASK] = {0, BIT(25)},
	[TEE_AXI4_TEESYS_ARST & RST_SIGNAL_MASK] = {0, BIT(26)},
	[TEE_APB3_TEESYS_PRST & RST_SIGNAL_MASK] = {0, BIT(27)},
	[TEE_APB3_TEESYS_HRST & RST_SIGNAL_MASK] = {0, BIT(28)},
	[TEE_AHB2_TEESYS_HRST & RST_SIGNAL_MASK] = {0, BIT(29)},
};

static const struct a210_rst_signal gpu_rst_signals[] = {
	[GPU_PWR_WRAP_RGX_HOOD_RST & RST_SIGNAL_MASK] = {0, BIT(0)},
	[GPU_PWR_WRAP_DFMU_RST & RST_SIGNAL_MASK] = {0, BIT(1)},
};

static struct a210_rst_subsys subsys[] = {
	[VP_RST] = {
		.signals = vp_rst_signals,
		.num_signals = ARRAY_SIZE(vp_rst_signals),
		.name = "VP_RST",
	},
	[VI_RST] = {
		.signals = vi_rst_signals,
		.num_signals = ARRAY_SIZE(vi_rst_signals),
		.name = "VI_RST",
	},
	[NPU_RST] = {
		.signals = npu_rst_signals,
		.num_signals = ARRAY_SIZE(npu_rst_signals),
		.name = "NPU_RST",
	},
	[VO_RST] = {
		.signals = vo_rst_signals,
		.num_signals = ARRAY_SIZE(vo_rst_signals),
		.name = "VO_RST",
	},
	[PERI0_RST] = {
		.signals = peri0_rst_signals,
		.num_signals = ARRAY_SIZE(peri0_rst_signals),
		.name = "PERI0_RST",
	},
	[PERI1_RST] = {
		.signals = peri1_rst_signals,
		.num_signals = ARRAY_SIZE(peri1_rst_signals),
		.name = "PERI1_RST",
	},
	[PERI2_RST] = {
		.signals = peri2_rst_signals,
		.num_signals = ARRAY_SIZE(peri2_rst_signals),
		.name = "PERI2_RST",
	},
	[PERI3_RST] = {
		.signals = peri3_rst_signals,
		.num_signals = ARRAY_SIZE(peri3_rst_signals),
		.name = "PERI3_RST",
	},
	[PCIE_RST] = {
		.signals = pcie_rst_signals,
		.num_signals = ARRAY_SIZE(pcie_rst_signals),
		.name = "PCIE_RST",
	},
	[USB_RST] = {
		.signals = usb_rst_signals,
		.num_signals = ARRAY_SIZE(usb_rst_signals),
		.name = "USB_RST",
	},
	[TEE_RST] = {
		.signals = tee_rst_signals,
		.num_signals = ARRAY_SIZE(tee_rst_signals),
		.name = "TEE_RST",
	},
	[GPU_RST] = {
		.signals = gpu_rst_signals,
		.num_signals = ARRAY_SIZE(gpu_rst_signals),
		.name = "GPU_RST",
	},
};

static int a210_reset_parse_regbase(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a210_reset *priv = dev_get_drvdata(dev);
	int ret = 0;

	for (int i = 0; i < A210_RESET_SUBSYS_MAX; i++) {
		priv->subsys[i].base = devm_platform_ioremap_resource_byname(pdev, priv->subsys[i].name);
		if (WARN_ON(IS_ERR(priv->subsys[i].base))) {
			return PTR_ERR(priv->subsys[i].base);
		}
	}

	return ret;
}

static inline struct a210_reset *to_a210_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct a210_reset, reset);
}

static int a210_reset_subsys_signal_lookup(struct a210_reset *priv, unsigned long id,
               struct a210_rst_subsys **subsys, const struct a210_rst_signal **signal)
{
	u32 subsys_id = id >> 8;
	u32 signal_id = id & RST_SIGNAL_MASK;

	if (subsys_id > GPU_RST)
		return -ENODEV;

	*subsys = &priv->subsys[subsys_id];

	if (signal_id >= (*subsys)->num_signals)
		return -ENODEV;

	*signal = &(*subsys)->signals[signal_id];

	return 0;
}

static int a210_reset_update(struct reset_controller_dev *rcdev,
			     unsigned long id, bool assert)
{
	u32 reg;
	struct a210_reset *priv = to_a210_reset(rcdev);
	struct a210_rst_subsys *subsys;
	const struct a210_rst_signal *signal;
	unsigned long flags;
	int ret;

	ret = a210_reset_subsys_signal_lookup(priv, id, &subsys, &signal);
	if (ret) {
		return ret;
	}

	spin_lock_irqsave(&priv->lock, flags);

	reg = readl(subsys->base + signal->offset);
	if (assert == true)
		reg &= ~signal->bit;
	else
		reg |= signal->bit;
	writel(reg, subsys->base + signal->offset);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int a210_reset_assert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	return a210_reset_update(rcdev, id, true);
}

static int a210_reset_deassert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	return a210_reset_update(rcdev, id, false);
}

static int a210_reset_status(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct a210_reset *priv = to_a210_reset(rcdev);
	struct a210_rst_subsys *subsys;
	const struct a210_rst_signal *signal;
	int ret;

	ret = a210_reset_subsys_signal_lookup(priv, id, &subsys, &signal);
	if (ret)
		return ret;

	return !!(readl(subsys->base + signal->offset) & signal->bit);
}

static const struct reset_control_ops a210_reset_ops = {
	.assert = a210_reset_assert,
	.deassert = a210_reset_deassert,
	.status = a210_reset_status,
};

static void a210_register_reset(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a210_reset *priv = dev_get_drvdata(dev);

	priv->reset.ops = &a210_reset_ops;
	priv->reset.owner = THIS_MODULE;
	priv->reset.of_node = dev->of_node;
	priv->reset.of_reset_n_cells = 1;
	priv->reset.nr_resets = A210_RESETS_MAX;

	spin_lock_init(&priv->lock);

	reset_controller_register(&priv->reset);
}

static int a210_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a210_reset *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(dev, priv);

	priv->subsys = subsys;
	priv->num_subsys = ARRAY_SIZE(subsys);
	priv->dev = dev;

	ret = a210_reset_parse_regbase(pdev);
	if (ret)
		goto fail;

	a210_register_reset(pdev);

	dev_info(dev, "succeed to register a210 reset driver\n");

	return ret;

fail:
	devm_kfree(dev, priv);
	return ret;
}

static const struct of_device_id a210_reset_of_match[] = {
	{.compatible = "zhihe,a210-reset-controller"},
	{ /* Sentinel */ },
};

struct platform_driver a210_reset_driver = {
	.probe = a210_reset_probe,
	.driver = {
		.name = "a210-reset",
		.of_match_table = of_match_ptr(a210_reset_of_match),
	},
};

static int a210_reset_init(void)
{
	return platform_driver_register(&a210_reset_driver);
}

arch_initcall(a210_reset_init);

MODULE_AUTHOR("dong.yan <yand@zhcomputing.com>");
MODULE_DESCRIPTION("Zhihe P100 reset driver");
MODULE_LICENSE("GPL v2");
