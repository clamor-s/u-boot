// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <common.h>
#include <clk.h>
#include <dm.h>
#include <i2c.h>
#include <log.h>
#include <misc.h>
#include <mipi_display.h>
#include <mipi_dsi.h>
#include <backlight.h>
#include <panel.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <power/regulator.h>

#include <asm/gpio.h>

/* Global (16-bit addressable) */
#define TC358768_CHIPID			0x0000
#define TC358768_SYSCTL			0x0002
#define TC358768_CONFCTL		0x0004
#define TC358768_VSDLY			0x0006
#define TC358768_DATAFMT		0x0008
#define TC358768_GPIOEN			0x000E
#define TC358768_GPIODIR		0x0010
#define TC358768_GPIOIN			0x0012
#define TC358768_GPIOOUT		0x0014
#define TC358768_PLLCTL0		0x0016
#define TC358768_PLLCTL1		0x0018
#define TC358768_CMDBYTE		0x0022
#define TC358768_PP_MISC		0x0032
#define TC358768_DSITX_DT		0x0050
#define TC358768_FIFOSTATUS		0x00F8

/* Debug (16-bit addressable) */
#define TC358768_VBUFCTRL		0x00E0
#define TC358768_DBG_WIDTH		0x00E2
#define TC358768_DBG_VBLANK		0x00E4
#define TC358768_DBG_DATA		0x00E8

/* TX PHY (32-bit addressable) */
#define TC358768_CLW_DPHYCONTTX		0x0100
#define TC358768_D0W_DPHYCONTTX		0x0104
#define TC358768_D1W_DPHYCONTTX		0x0108
#define TC358768_D2W_DPHYCONTTX		0x010C
#define TC358768_D3W_DPHYCONTTX		0x0110
#define TC358768_CLW_CNTRL		0x0140
#define TC358768_D0W_CNTRL		0x0144
#define TC358768_D1W_CNTRL		0x0148
#define TC358768_D2W_CNTRL		0x014C
#define TC358768_D3W_CNTRL		0x0150

/* TX PPI (32-bit addressable) */
#define TC358768_STARTCNTRL		0x0204
#define TC358768_DSITXSTATUS		0x0208
#define TC358768_LINEINITCNT		0x0210
#define TC358768_LPTXTIMECNT		0x0214
#define TC358768_TCLK_HEADERCNT		0x0218
#define TC358768_TCLK_TRAILCNT		0x021C
#define TC358768_THS_HEADERCNT		0x0220
#define TC358768_TWAKEUP		0x0224
#define TC358768_TCLK_POSTCNT		0x0228
#define TC358768_THS_TRAILCNT		0x022C
#define TC358768_HSTXVREGCNT		0x0230
#define TC358768_HSTXVREGEN		0x0234
#define TC358768_TXOPTIONCNTRL		0x0238
#define TC358768_BTACNTRL1		0x023C

/* TX CTRL (32-bit addressable) */
#define TC358768_DSI_CONTROL		0x040C
#define TC358768_DSI_STATUS		0x0410
#define TC358768_DSI_INT		0x0414
#define TC358768_DSI_INT_ENA		0x0418
#define TC358768_DSICMD_RDFIFO		0x0430
#define TC358768_DSI_ACKERR		0x0434
#define TC358768_DSI_ACKERR_INTENA	0x0438
#define TC358768_DSI_ACKERR_HALT	0x043c
#define TC358768_DSI_RXERR		0x0440
#define TC358768_DSI_RXERR_INTENA	0x0444
#define TC358768_DSI_RXERR_HALT		0x0448
#define TC358768_DSI_ERR		0x044C
#define TC358768_DSI_ERR_INTENA		0x0450
#define TC358768_DSI_ERR_HALT		0x0454
#define TC358768_DSI_CONFW		0x0500
#define TC358768_DSI_LPCMD		0x0500
#define TC358768_DSI_RESET		0x0504
#define TC358768_DSI_INT_CLR		0x050C
#define TC358768_DSI_START		0x0518

/* DSITX CTRL (16-bit addressable) */
#define TC358768_DSICMD_TX		0x0600
#define TC358768_DSICMD_TYPE		0x0602
#define TC358768_DSICMD_WC		0x0604
#define TC358768_DSICMD_WD0		0x0610
#define TC358768_DSICMD_WD1		0x0612
#define TC358768_DSICMD_WD2		0x0614
#define TC358768_DSICMD_WD3		0x0616
#define TC358768_DSI_EVENT		0x0620
#define TC358768_DSI_VSW		0x0622
#define TC358768_DSI_VBPR		0x0624
#define TC358768_DSI_VACT		0x0626
#define TC358768_DSI_HSW		0x0628
#define TC358768_DSI_HBPR		0x062A
#define TC358768_DSI_HACT		0x062C

/* TC358768_DSI_CONTROL (0x040C) register */
#define TC358768_DSI_CONTROL_DIS_MODE	BIT(15)
#define TC358768_DSI_CONTROL_TXMD	BIT(7)
#define TC358768_DSI_CONTROL_HSCKMD	BIT(5)
#define TC358768_DSI_CONTROL_EOTDIS	BIT(0)

/* TC358768_DSI_CONFW (0x0500) register */
#define TC358768_DSI_CONFW_MODE_SET		(5 << 29)
#define TC358768_DSI_CONFW_MODE_CLR		(6 << 29)
#define TC358768_DSI_CONFW_ADDR_DSI_CONTROL	(0x3 << 24)

struct tc358768_priv {
	struct mipi_dsi_host host;
	struct mipi_dsi_device device;

	struct udevice *panel;
	struct display_timing timing;

	struct udevice *vddc;
	struct udevice *vddmipi;
	struct udevice *vddio;

	struct clk *refclk;

	struct gpio_desc reset_gpio;

	u32 pd_lines;	/* number of Parallel Port Input Data Lines */
	u32 dsi_lanes;	/* number of DSI Lanes */

	/* Parameters for PLL programming */
	u32 fbd;	/* PLL feedback divider */
	u32 prd;	/* PLL input divider */
	u32 frs;	/* PLL Freqency range for HSCK (post divider) */

	u32 dsiclk;	/* pll_clk / 2 */
};

static int tc358768_read16(struct udevice *dev, u32 reg, u16 *val)
{
        struct i2c_msg msg[2];
        u8 buf[4];
	int ret;

	msg[0].addr = 0x07;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = buf;

	/* high byte goes out first */
	buf[0] = (u8) (reg >> 8);
	buf[1] = (u8) (reg & 0xff);

	msg[1].addr = 0x07;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = buf + 2;

	ret = dm_i2c_xfer(dev, msg, 2);
	*val = (buf[2] << 8) | buf[3];
	printf("%s 0x%04x<<0x%04x\n", __func__, reg, *val);

	return ret;
}

static int tc358768_write16(struct udevice *dev, u32 reg, u16 val)
{
	struct i2c_msg msg;
	u8 buf[4];

        buf[0] = (u8) (reg >> 8);
        buf[1] = (u8) (reg & 0xff);
        buf[2] = (u8) (val >> 8);
        buf[3] = (u8) (val & 0xff);
	
	msg.addr = 0x07;
	msg.flags = 0;
	msg.len = 4;
	msg.buf = buf;

	printf("%s 0x%04x>>0x%04x\n", __func__, reg, val);
	return dm_i2c_xfer(dev, &msg, 1);
}

static void tc358768_read(struct udevice *dev, u32 reg, u32 *val)
{
	u16 out1 = 0, out2 = 0;

	/* 16-bit register? */
	if (reg < 0x100 || reg >= 0x600) {
		tc358768_read16(dev, reg, &out1);
	} else {
		tc358768_read16(dev, reg, &out1);
		tc358768_read16(dev, reg + 2, &out2);
	}

	*val = (out2 << 16) | out1;
}

static void tc358768_write(struct udevice *dev, u32 reg, u32 val)
{
	/* 16-bit register? */
	if (reg < 0x100 || reg >= 0x600) {
		tc358768_write16(dev, reg, val);
	} else {
		u16 cmd1, cmd2;

		cmd1 = val & 0xFFFF;
		cmd2 = val >> 16;

		tc358768_write16(dev, reg, cmd1);
		tc358768_write16(dev, reg + 2, cmd2);
	}

	u32 dump;
	tc358768_read(dev, reg, &dump);
	printf("%s: reg 0x%x, dump 0x%x\n",__func__, reg, dump);
}

static void tc358768_update_bits(struct udevice *dev, u32 reg, u32 mask,
				 u32 val)
{
	u32 tmp, orig;

	tc358768_read(dev, reg, &orig);
	tmp = orig & ~mask;
	tmp |= val & mask;
	if (tmp != orig)
		tc358768_write(dev, reg, tmp);
}

static ssize_t tc358768_dsi_host_transfer(struct mipi_dsi_host *host,
					  const struct mipi_dsi_msg *msg)
{
	struct udevice *dev = (struct udevice *)host->dev;
	struct mipi_dsi_packet packet;
	int ret;

	if (msg->rx_len) {
		printf("%s: MIPI rx is not supported\n", __func__);
		return -ENOTSUPP;
	}

	if (msg->tx_len > 8) {
		printf("%s: Maximum 8 byte MIPI tx is supported\n", __func__);
		return -ENOTSUPP;
	}

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret)
		return ret;

	if (mipi_dsi_packet_format_is_short(msg->type)) {
		tc358768_write(dev, TC358768_DSICMD_TYPE,
			       (0x10 << 8) | (packet.header[0] & 0x3f));
		tc358768_write(dev, TC358768_DSICMD_WC, 0);
		tc358768_write(dev, TC358768_DSICMD_WD0,
			       (packet.header[2] << 8) | packet.header[1]);
	} else {
		int i;

		tc358768_write(dev, TC358768_DSICMD_TYPE,
			       (0x40 << 8) | (packet.header[0] & 0x3f));
		tc358768_write(dev, TC358768_DSICMD_WC, packet.payload_length);
		for (i = 0; i < packet.payload_length; i += 2) {
			u16 val = packet.payload[i];

			if (i + 1 < packet.payload_length)
				val |= packet.payload[i + 1] << 8;

			tc358768_write(dev, TC358768_DSICMD_WD0 + i, val);
		}
	}

	/* start transfer */
	tc358768_write(dev, TC358768_DSICMD_TX, 1);

	return packet.size;
}

static const struct mipi_dsi_host_ops tc358768_dsi_host_ops = {
	.transfer = tc358768_dsi_host_transfer,
};

static void tc358768_hw_enable(struct tc358768_priv *priv)
{
	int ret;

	ret = clk_prepare_enable(priv->refclk);
	if (ret)
		printf("%s: error enabling refclk (%d)\n", __func__, ret);

	ret = regulator_set_enable_if_allowed(priv->vddc, true);
	if (ret)
		printf("%s: error enabling vddc (%d)\n", __func__, ret);

	ret = regulator_set_enable_if_allowed(priv->vddmipi, true);
	if (ret)
		printf("%s: error enabling vddmipi (%d)\n", __func__, ret);

	mdelay(10);

	ret = regulator_set_enable_if_allowed(priv->vddio, true);
	if (ret)
		printf("%s: error enabling vddio (%d)\n", __func__, ret);

	mdelay(2);

	/*
	 * The RESX is active low (GPIO_ACTIVE_LOW).
	 * DEASSERT (value = 0) the reset_gpio to enable the chip
	 */
	ret = dm_gpio_set_value(&priv->reset_gpio, 0);
	if (ret)
		printf("%s: error changing reset-gpio (%d)\n", __func__, ret);

	/* wait for encoder clocks to stabilize */
	udelay(2000);
}

static void tc358768_sw_reset(struct udevice *dev)
{
	/* Assert Reset */
	tc358768_write(dev, TC358768_SYSCTL, 1);
	/* Release Reset, Exit Sleep */
	tc358768_write(dev, TC358768_SYSCTL, 0);
}

static u32 tc358768_pclk_to_pll(struct tc358768_priv *priv, u32 pclk)
{
	return (u32)div_u64((u64)pclk * priv->pd_lines, priv->dsi_lanes);
}

static int tc358768_calc_pll(struct udevice *dev)
{
	struct tc358768_priv *priv = dev_get_priv(dev);
	struct display_timing *dt = &priv->timing;
	static const u32 frs_limits[] = {
		1000000000,
		500000000,
		250000000,
		125000000,
		62500000
	};
	unsigned long refclk;
	u32 prd, target_pll, i, max_pll, min_pll;
	u32 frs, best_diff, best_pll, best_prd, best_fbd;

	target_pll = tc358768_pclk_to_pll(priv, dt->pixelclock.typ);

	/* pll_clk = RefClk * [(FBD + 1)/ (PRD + 1)] * [1 / (2^FRS)] */

	for (i = 0; i < ARRAY_SIZE(frs_limits); i++)
		if (target_pll >= frs_limits[i])
			break;

	if (i == ARRAY_SIZE(frs_limits) || i == 0)
		return -EINVAL;

	frs = i - 1;
	max_pll = frs_limits[i - 1];
	min_pll = frs_limits[i];

	refclk = clk_get_rate(priv->refclk);

	best_diff = UINT_MAX;
	best_pll = 0;
	best_prd = 0;
	best_fbd = 0;

	for (prd = 0; prd < 16; ++prd) {
		u32 divisor = (prd + 1) * (1 << frs);
		u32 fbd;

		for (fbd = 0; fbd < 512; ++fbd) {
			u32 pll, diff;

			pll = (u32)div_u64((u64)refclk * (fbd + 1), divisor);

			if (pll >= max_pll || pll < min_pll)
				continue;

			diff = max(pll, target_pll) - min(pll, target_pll);

			if (diff < best_diff) {
				best_diff = diff;
				best_pll = pll;
				best_prd = prd;
				best_fbd = fbd;

				if (best_diff == 0)
					goto found;
			}
		}
	}

	if (best_diff == UINT_MAX) {
		printf("%s: could not find suitable PLL setup\n", __func__);
		return -EINVAL;
	}

found:
	priv->fbd = best_fbd;
	priv->prd = best_prd;
	priv->frs = frs;
	priv->dsiclk = best_pll / 2;

	return 0;
}

static void tc358768_setup_pll(struct udevice *dev)
{
	struct tc358768_priv *priv = dev_get_priv(dev);
	u32 fbd, prd, frs;
	int ret;

	ret = tc358768_calc_pll(dev);
	if (ret)
		printf("%s: PLL calculation failed: %d\n", __func__, ret);

	fbd = priv->fbd;
	prd = priv->prd;
	frs = priv->frs;

	/* PRD[15:12] FBD[8:0] */
	tc358768_write(dev, TC358768_PLLCTL0, (prd << 12) | fbd);

	/* FRS[11:10] LBWS[9:8] CKEN[4] RESETB[1] EN[0] */
	tc358768_write(dev, TC358768_PLLCTL1,
		       (frs << 10) | (0x2 << 8) | BIT(1) | BIT(0));

	/* wait for lock */
	udelay(1000);

	/* FRS[11:10] LBWS[9:8] CKEN[4] PLL_CKEN[4] RESETB[1] EN[0] */
	tc358768_write(dev, TC358768_PLLCTL1,
		       (frs << 10) | (0x2 << 8) | BIT(4) | BIT(1) | BIT(0));
}

#define TC358768_PRECISION	1000
static u32 tc358768_ns_to_cnt(u32 ns, u32 period_nsk)
{
	return (ns * TC358768_PRECISION + period_nsk) / period_nsk;
}

static u32 tc358768_to_ns(u32 nsk)
{
	return (nsk / TC358768_PRECISION);
}

static int tc358768_attach(struct udevice *dev)
{
	struct tc358768_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_device *device = &priv->device;
	struct display_timing *dt = &priv->timing;
	u32 val, val2, lptxcnt, hact, data_type;
	u32 dsibclk_nsk, dsiclk_nsk, ui_nsk, phy_delay_nsk;
	u32 dsiclk, dsibclk, video_start;
	const u32 internal_delay = 40;
	int ret, i;

	if (device->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS) {
		debug("%s: Non-continuous mode unimplemented, falling back to continuous\n", __func__);
		device->mode_flags &= ~MIPI_DSI_CLOCK_NON_CONTINUOUS;
	}

	tc358768_hw_enable(priv);
	tc358768_sw_reset(dev);

	tc358768_setup_pll(dev);

	dsiclk = priv->dsiclk;
	dsibclk = dsiclk / 4;

	/* Data Format Control Register */
	val = BIT(2) | BIT(1) | BIT(0); /* rdswap_en | dsitx_en | txdt_en */
	switch (device->format) {
	case MIPI_DSI_FMT_RGB888:
		val |= (0x3 << 4);
		hact = dt->hactive.typ * 3;
		video_start = (dt->hback_porch.typ + dt->hsync_len.typ) * 3;
		data_type = MIPI_DSI_PACKED_PIXEL_STREAM_24;
		break;
	case MIPI_DSI_FMT_RGB666:
		val |= (0x4 << 4);
		hact = dt->hactive.typ * 3;
		video_start = (dt->hback_porch.typ + dt->hsync_len.typ) * 3;
		data_type = MIPI_DSI_PACKED_PIXEL_STREAM_18;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		val |= (0x4 << 4) | BIT(3);
		hact = dt->hactive.typ * 18 / 8;
		video_start = (dt->hback_porch.typ + dt->hsync_len.typ) * 18 / 8;
		data_type = MIPI_DSI_PIXEL_STREAM_3BYTE_18;
		break;
	case MIPI_DSI_FMT_RGB565:
		val |= (0x5 << 4);
		hact = dt->hactive.typ * 2;
		video_start = (dt->hback_porch.typ + dt->hsync_len.typ) * 2;
		data_type = MIPI_DSI_PACKED_PIXEL_STREAM_16;
		break;
	default:
		printf("%s: Invalid data format (%u)\n",
			__func__, device->format);
		return -EINVAL;
	}

	/* VSDly[9:0] */
	video_start = max(video_start, internal_delay + 1) - internal_delay;
	tc358768_write(dev, TC358768_VSDLY, video_start);

	tc358768_write(dev, TC358768_DATAFMT, val);
	tc358768_write(dev, TC358768_DSITX_DT, data_type);

	/* Enable D-PHY (HiZ->LP11) */
	tc358768_write(dev, TC358768_CLW_CNTRL, 0x0000);
	/* Enable lanes */
	for (i = 0; i < device->lanes; i++)
		tc358768_write(dev, TC358768_D0W_CNTRL + i * 4, 0x0000);

	/* DSI Timings */
	dsibclk_nsk = (u32)div_u64((u64)1000000000 * TC358768_PRECISION,
				  dsibclk);
	dsiclk_nsk = (u32)div_u64((u64)1000000000 * TC358768_PRECISION, dsiclk);
	ui_nsk = dsiclk_nsk / 2;
	phy_delay_nsk = dsibclk_nsk + 2 * dsiclk_nsk;
	debug("%s: dsiclk_nsk: %u\n", __func__, dsiclk_nsk);
	debug("%s: ui_nsk: %u\n", __func__, ui_nsk);
	debug("%s: dsibclk_nsk: %u\n", __func__, dsibclk_nsk);
	debug("%s: phy_delay_nsk: %u\n", __func__, phy_delay_nsk);

	/* LP11 > 100us for D-PHY Rx Init */
	val = tc358768_ns_to_cnt(100 * 1000, dsibclk_nsk) - 1;
	debug("%s: LINEINITCNT: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_LINEINITCNT, val);

	/* LPTimeCnt > 50ns */
	val = tc358768_ns_to_cnt(50, dsibclk_nsk) - 1;
	lptxcnt = val;
	debug("%s: LPTXTIMECNT: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_LPTXTIMECNT, val);

	/* 38ns < TCLK_PREPARE < 95ns */
	val = tc358768_ns_to_cnt(65, dsibclk_nsk) - 1;
	/* TCLK_PREPARE > 300ns */
	val2 = tc358768_ns_to_cnt(300 + tc358768_to_ns(3 * ui_nsk),
				  dsibclk_nsk);
	val |= (val2 - tc358768_to_ns(phy_delay_nsk - dsibclk_nsk)) << 8;
	debug("%s: TCLK_HEADERCNT: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_TCLK_HEADERCNT, val);

	/* TCLK_TRAIL > 60ns + 3*UI */
	val = 60 + tc358768_to_ns(3 * ui_nsk);
	val = tc358768_ns_to_cnt(val, dsibclk_nsk) - 5;
	debug("%s: TCLK_TRAILCNT: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_TCLK_TRAILCNT, val);

	/* 40ns + 4*UI < THS_PREPARE < 85ns + 6*UI */
	val = 50 + tc358768_to_ns(4 * ui_nsk);
	val = tc358768_ns_to_cnt(val, dsibclk_nsk) - 1;
	/* THS_ZERO > 145ns + 10*UI */
	val2 = tc358768_ns_to_cnt(145 - tc358768_to_ns(ui_nsk), dsibclk_nsk);
	val |= (val2 - tc358768_to_ns(phy_delay_nsk)) << 8;
	debug("%s: THS_HEADERCNT: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_THS_HEADERCNT, val);

	/* TWAKEUP > 1ms in lptxcnt steps */
	val = tc358768_ns_to_cnt(1020000, dsibclk_nsk);
	val = val / (lptxcnt + 1) - 1;
	debug("%s: TWAKEUP: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_TWAKEUP, val);

	/* TCLK_POSTCNT > 60ns + 52*UI */
	val = tc358768_ns_to_cnt(60 + tc358768_to_ns(52 * ui_nsk),
				 dsibclk_nsk) - 3;
	debug("%s: TCLK_POSTCNT: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_TCLK_POSTCNT, val);

	/* 60ns + 4*UI < THS_PREPARE < 105ns + 12*UI */
	val = tc358768_ns_to_cnt(60 + tc358768_to_ns(15 * ui_nsk),
				 dsibclk_nsk) - 5;
	debug("%s: THS_TRAILCNT: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_THS_TRAILCNT, val);

	val = BIT(0);
	for (i = 0; i < device->lanes; i++)
		val |= BIT(i + 1);
	tc358768_write(dev, TC358768_HSTXVREGEN, val);

	if (!(device->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS))
		tc358768_write(dev, TC358768_TXOPTIONCNTRL, 0x1);

	/* TXTAGOCNT[26:16] RXTASURECNT[10:0] */
	val = tc358768_to_ns((lptxcnt + 1) * dsibclk_nsk * 4);
	val = tc358768_ns_to_cnt(val, dsibclk_nsk) - 1;
	val2 = tc358768_ns_to_cnt(tc358768_to_ns((lptxcnt + 1) * dsibclk_nsk),
				  dsibclk_nsk) - 2;
	val = val << 16 | val2;
	debug("%s: BTACNTRL1: 0x%x\n", __func__, val);
	tc358768_write(dev, TC358768_BTACNTRL1, val);

	/* START[0] */
	tc358768_write(dev, TC358768_STARTCNTRL, 1);

	if (device->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
		/* Set pulse mode */
		tc358768_write(dev, TC358768_DSI_EVENT, 0);

		/* vact */
		tc358768_write(dev, TC358768_DSI_VACT, dt->vactive.typ);
		/* vsw */
		tc358768_write(dev, TC358768_DSI_VSW, dt->vsync_len.typ);
		/* vbp */
		tc358768_write(dev, TC358768_DSI_VBPR, dt->vback_porch.typ);

		/* hsw * byteclk * ndl / pclk */
		val = (u32)div_u64(dt->hsync_len.typ *
				   ((u64)priv->dsiclk / 4) * priv->dsi_lanes,
				   dt->pixelclock.typ);
		tc358768_write(dev, TC358768_DSI_HSW, val);

		/* hbp * byteclk * ndl / pclk */
		val = (u32)div_u64(dt->vback_porch.typ *
				   ((u64)priv->dsiclk / 4) * priv->dsi_lanes,
				   dt->pixelclock.typ);
		tc358768_write(dev, TC358768_DSI_HBPR, val);
	} else {
		/* Set event mode */
		tc358768_write(dev, TC358768_DSI_EVENT, 1);

		/* vact */
		tc358768_write(dev, TC358768_DSI_VACT, dt->vactive.typ);

		/* vsw (+ vbp) */
		tc358768_write(dev, TC358768_DSI_VSW,
			       dt->vsync_len.typ + dt->vback_porch.typ);
		/* vbp (not used in event mode) */
		tc358768_write(dev, TC358768_DSI_VBPR, 0);

		/* (hsw + hbp) * byteclk * ndl / pclk */
		val = (u32)div_u64((dt->hsync_len.typ + dt->vback_porch.typ) *
				   ((u64)priv->dsiclk / 4) * priv->dsi_lanes,
				   dt->pixelclock.typ);
		tc358768_write(dev, TC358768_DSI_HSW, val);

		/* hbp (not used in event mode) */
		tc358768_write(dev, TC358768_DSI_HBPR, 0);
	}

	/* hact (bytes) */
	tc358768_write(dev, TC358768_DSI_HACT, hact);

	/* VSYNC polarity */
	if (!(dt->flags & DISPLAY_FLAGS_VSYNC_HIGH))
		tc358768_update_bits(dev, TC358768_CONFCTL, BIT(5), BIT(5));
	/* HSYNC polarity */
	if (dt->flags & DISPLAY_FLAGS_HSYNC_LOW)
		tc358768_update_bits(dev, TC358768_PP_MISC, BIT(0), BIT(0));

	/* Start DSI Tx */
	tc358768_write(dev, TC358768_DSI_START, 0x1);

	/* Configure DSI_Control register */
	val = TC358768_DSI_CONFW_MODE_CLR | TC358768_DSI_CONFW_ADDR_DSI_CONTROL;
	val |= TC358768_DSI_CONTROL_TXMD | TC358768_DSI_CONTROL_HSCKMD |
	       0x3 << 1 | TC358768_DSI_CONTROL_EOTDIS;
	tc358768_write(dev, TC358768_DSI_CONFW, val);

	val = TC358768_DSI_CONFW_MODE_SET | TC358768_DSI_CONFW_ADDR_DSI_CONTROL;
	val |= (device->lanes - 1) << 1;

	if (!(device->mode_flags & MIPI_DSI_MODE_LPM))
		val |= TC358768_DSI_CONTROL_TXMD;

	if (!(device->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS))
		val |= TC358768_DSI_CONTROL_HSCKMD;

	if (device->mode_flags & MIPI_DSI_MODE_EOT_PACKET)
		val |= TC358768_DSI_CONTROL_EOTDIS;

	tc358768_write(dev, TC358768_DSI_CONFW, val);

	val = TC358768_DSI_CONFW_MODE_CLR |
	      TC358768_DSI_CONFW_ADDR_DSI_CONTROL |
	      TC358768_DSI_CONTROL_DIS_MODE; /* DSI mode */
	tc358768_write(dev, TC358768_DSI_CONFW, val);

	/* Perform panel HW setup */
	ret = panel_enable_backlight(priv->panel);
	if (ret)
		return ret;

	/* clear FrmStop and RstPtr */
	tc358768_update_bits(dev, TC358768_PP_MISC, 0x3 << 14, 0);

	/* set PP_en */
	tc358768_update_bits(dev, TC358768_CONFCTL, BIT(6), BIT(6));

	/* Set up SW panel configuration */
	ret = panel_set_backlight(priv->panel, BACKLIGHT_DEFAULT);
	if (ret)
		return ret;

	return 0;
}

static int tc358768_set_backlight(struct udevice *dev, int percent)
{
	return 0;
}

static int tc358768_panel_timings(struct udevice *dev,
				  struct display_timing *timing)
{
	struct tc358768_priv *priv = dev_get_priv(dev);

	memcpy(timing, &priv->timing, sizeof(*timing));

	return 0;
}

static int tc358768_setup(struct udevice *dev)
{
	struct tc358768_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_device *device = &priv->device;
	struct mipi_dsi_panel_plat *mipi_plat;
	int ret;

	ret = uclass_get_device_by_phandle(UCLASS_PANEL, dev,
					   "panel", &priv->panel);
	if (ret) {
		printf("%s: Cannot get panel: ret=%d\n", __func__, ret);
		return log_ret(ret);
	}

	panel_get_display_timing(priv->panel, &priv->timing);

	mipi_plat = dev_get_plat(priv->panel);
	mipi_plat->device = device;

	priv->host.dev = (struct device *)dev;
	priv->host.ops = &tc358768_dsi_host_ops;

	device->host = &priv->host;
	device->lanes = mipi_plat->lanes;
	device->format = mipi_plat->format;
	device->mode_flags = mipi_plat->mode_flags;

	priv->pd_lines = mipi_dsi_pixel_format_to_bpp(device->format);
	priv->dsi_lanes = device->lanes;

	/* get regulators */
	ret = device_get_supply_regulator(dev, "vddc-supply", &priv->vddc);
	if (ret) {
		printf("%s: vddc regulator error: %d\n", __func__, ret);
//		if (ret != -ENOENT)
//			return log_ret(ret);
	}

	ret = device_get_supply_regulator(dev, "vddmipi-supply", &priv->vddmipi);
	if (ret) {
		printf("%s: vddmipi regulator error: %d\n", __func__, ret);
//		if (ret != -ENOENT)
//			return log_ret(ret);
	}

	ret = device_get_supply_regulator(dev, "vddio-supply", &priv->vddio);
	if (ret) {
		printf("%s: vddio regulator error: %d\n", __func__, ret);
//		if (ret != -ENOENT)
//			return log_ret(ret);
	}

	/* get clk */
	priv->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(priv->refclk)) {
		printf("%s: Could not get refclk: %ld\n",
			__func__, PTR_ERR(priv->refclk));
		return PTR_ERR(priv->refclk);
	}

	/* get gpios */
	ret = gpio_request_by_name(dev, "reset-gpios", 0,
				   &priv->reset_gpio, GPIOD_IS_OUT);
	if (ret) {
		printf("%s: Could not decode reset-gpios (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int tc358768_probe(struct udevice *dev)
{
	if (device_get_uclass_id(dev->parent) != UCLASS_I2C)
		return -EPROTONOSUPPORT;

	return tc358768_setup(dev);
}

struct panel_ops tc358768_ops = {
	.enable_backlight	= tc358768_attach,
	.set_backlight		= tc358768_set_backlight,
	.get_display_timing	= tc358768_panel_timings,
};

static const struct udevice_id tc358768_ids[] = {
	{ .compatible = "toshiba,tc358768" },
	{ }
};

U_BOOT_DRIVER(tc358768) = {
	.name		= "tc358768",
	.id		= UCLASS_PANEL,
	.of_match 	= tc358768_ids,
	.ops		= &tc358768_ops,
	.probe		= tc358768_probe,
	.priv_auto	= sizeof(struct tc358768_priv),
};
