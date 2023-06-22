// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2013 NVIDIA Corporation
 * Copyright (c) 2023 Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <common.h>
#include <dm.h>
#include <edid.h>
#include <i2c.h>
#include <log.h>
#include <misc.h>
#include <panel.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <power/regulator.h>

#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/display.h>
#include <asm/arch-tegra30/hdmi.h>

#define USEC_PER_SEC		1000000L

#define HDMI_EDID_I2C_ADDR	0x50
#define HDMI_REKEY_DEFAULT	56

struct tmds_config {
	unsigned int pclk;
	u32 pll0;
	u32 pll1;
	u32 pe_current;
	u32 drive_current;
	u32 peak_current;
};

struct tegra_hdmi_config {
	const struct tmds_config *tmds;
	unsigned int num_tmds;

	bool has_sor_io_peak_current;
};

struct tegra_hdmi_priv {
	struct hdmi_ctlr *hdmi_regmap;

	/* power supply */
	struct udevice *hdmi;
	struct udevice *pll;
	struct udevice *vdd;

	/* clocks */
	int hdmi_clk[2];

	/* monitor data */
	struct udevice *hdmi_ddc;
	struct display_timing timing;
	int panel_bits_per_colourp;

	const struct tegra_hdmi_config *config;
};

static const struct tmds_config tegra30_tmds_config[] = {
	{ /* 480p modes */
		.pclk = 27000000,
		.pll0 = SOR_PLL_BG_V17_S(3) | SOR_PLL_ICHPMP(1) |
			SOR_PLL_RESISTORSEL | SOR_PLL_VCOCAP(0) |
			SOR_PLL_TX_REG_LOAD(0),
		.pll1 = SOR_PLL_TMDS_TERM_ENABLE,
		.pe_current = PE_CURRENT0(PE_CURRENT_0_0_mA) |
			PE_CURRENT1(PE_CURRENT_0_0_mA) |
			PE_CURRENT2(PE_CURRENT_0_0_mA) |
			PE_CURRENT3(PE_CURRENT_0_0_mA),
		.drive_current = DRIVE_CURRENT_LANE0(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE1(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE2(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE3(DRIVE_CURRENT_5_250_mA),
	}, { /* 720p modes */
		.pclk = 74250000,
		.pll0 = SOR_PLL_BG_V17_S(3) | SOR_PLL_ICHPMP(1) |
			SOR_PLL_RESISTORSEL | SOR_PLL_VCOCAP(1) |
			SOR_PLL_TX_REG_LOAD(0),
		.pll1 = SOR_PLL_TMDS_TERM_ENABLE | SOR_PLL_PE_EN,
		.pe_current = PE_CURRENT0(PE_CURRENT_5_0_mA) |
			PE_CURRENT1(PE_CURRENT_5_0_mA) |
			PE_CURRENT2(PE_CURRENT_5_0_mA) |
			PE_CURRENT3(PE_CURRENT_5_0_mA),
		.drive_current = DRIVE_CURRENT_LANE0(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE1(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE2(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE3(DRIVE_CURRENT_5_250_mA),
	}, { /* 1080p modes */
		.pclk = UINT_MAX,
		.pll0 = SOR_PLL_BG_V17_S(3) | SOR_PLL_ICHPMP(1) |
			SOR_PLL_RESISTORSEL | SOR_PLL_VCOCAP(3) |
			SOR_PLL_TX_REG_LOAD(0),
		.pll1 = SOR_PLL_TMDS_TERM_ENABLE | SOR_PLL_PE_EN,
		.pe_current = PE_CURRENT0(PE_CURRENT_5_0_mA) |
			PE_CURRENT1(PE_CURRENT_5_0_mA) |
			PE_CURRENT2(PE_CURRENT_5_0_mA) |
			PE_CURRENT3(PE_CURRENT_5_0_mA),
		.drive_current = DRIVE_CURRENT_LANE0(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE1(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE2(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE3(DRIVE_CURRENT_5_250_mA),
	},
};

static void tegra_dc_enable_controller(struct udevice *dev)
{
	struct tegra_dc_plat *dc_plat = dev_get_plat(dev);
	struct dc_ctlr *dc = dc_plat->dc;
	u32 value;

	value = readl(&dc->disp.disp_win_opt);
	value |= HDMI_ENABLE;
	writel(value, &dc->disp.disp_win_opt);

	writel(GENERAL_UPDATE, &dc->cmd.state_ctrl);
	writel(GENERAL_ACT_REQ, &dc->cmd.state_ctrl);
}

static void tegra_hdmi_setup_tmds(struct tegra_hdmi_priv *priv,
				  const struct tmds_config *tmds)
{
	struct hdmi_ctlr *hdmi = priv->hdmi_regmap;
	u32 value;

	writel(tmds->pll0, &hdmi->nv_pdisp_sor_pll0);
	writel(tmds->pll1, &hdmi->nv_pdisp_sor_pll1);
	writel(tmds->pe_current, &hdmi->nv_pdisp_pe_current);

	writel(tmds->drive_current, &hdmi->nv_pdisp_sor_lane_drive_current);

	value = readl(&hdmi->nv_pdisp_sor_lane_drive_current);
	value |= BIT(31);
	writel(value, &hdmi->nv_pdisp_sor_lane_drive_current);

	if (priv->config->has_sor_io_peak_current)
		writel(tmds->peak_current, &hdmi->nv_pdisp_sor_io_peak_current);
}

static int tegra_hdmi_encoder_enable(struct udevice *dev)
{
	struct tegra_dc_plat *dc_plat = dev_get_plat(dev);
	struct tegra_hdmi_priv *priv = dev_get_priv(dev);
	struct dc_ctlr *dc = dc_plat->dc;
	struct display_timing *dt = &priv->timing;
	struct hdmi_ctlr *hdmi = priv->hdmi_regmap;
	unsigned long rate, div82;
	unsigned int pulse_start, rekey;
	int retries = 1000;
	u32 value;
	int i;

	/* power up sequence */
	value = readl(&hdmi->nv_pdisp_sor_pll0);
	value &= ~SOR_PLL_PDBG;
	writel(value, &hdmi->nv_pdisp_sor_pll0);

	udelay(20);

	value = readl(&hdmi->nv_pdisp_sor_pll0);
	value &= ~SOR_PLL_PWR;
	writel(value, &hdmi->nv_pdisp_sor_pll0);

	writel(VSYNC_H_POSITION(1),
	       &dc->disp.disp_timing_opt);
	writel(DITHER_CONTROL_DISABLE | BASE_COLOR_SIZE_888,
	       &dc->disp.disp_color_ctrl);

	/* video_preamble uses h_pulse2 */
	pulse_start = 1 + dt->hsync_len.typ + dt->hback_porch.typ - 10;

	writel(H_PULSE2_ENABLE, &dc->disp.disp_signal_opt0);

	value = PULSE_MODE_NORMAL | PULSE_POLARITY_HIGH |
		PULSE_QUAL_VACTIVE | PULSE_LAST_END_A;
	writel(value, &dc->disp.h_pulse[H_PULSE2].h_pulse_ctrl);

	value = PULSE_START(pulse_start) | PULSE_END(pulse_start + 8);
	writel(value, &dc->disp.h_pulse[H_PULSE2].h_pulse_pos[H_PULSE0_POSITION_A]);

	value = VSYNC_WINDOW_END(0x210) | VSYNC_WINDOW_START(0x200) |
		VSYNC_WINDOW_ENABLE;
	writel(value, &hdmi->nv_pdisp_hdmi_vsync_window);

	if (dc_plat->pipe)
		value = HDMI_SRC_DISPLAYB;
	else
		value = HDMI_SRC_DISPLAYA;

	if ((dt->hactive.typ == 720) && ((dt->vactive.typ == 480) ||
					 (dt->vactive.typ == 576)))
		writel(value | ARM_VIDEO_RANGE_FULL,
		       &hdmi->nv_pdisp_input_control);
	else
		writel(value | ARM_VIDEO_RANGE_LIMITED,
		       &hdmi->nv_pdisp_input_control);

	rate = clock_get_periph_rate(priv->hdmi_clk[0], priv->hdmi_clk[1]);
	div82 = rate / USEC_PER_SEC * 4;
	value = SOR_REFCLK_DIV_INT(div82 >> 2) | SOR_REFCLK_DIV_FRAC(div82);
	writel(value, &hdmi->nv_pdisp_sor_refclk);

	rekey = HDMI_REKEY_DEFAULT;
	value = HDMI_CTRL_REKEY(rekey);
	value |= HDMI_CTRL_MAX_AC_PACKET((dt->hsync_len.typ + dt->hback_porch.typ +
					  dt->hfront_porch.typ - rekey - 18) / 32);

	writel(value, &hdmi->nv_pdisp_hdmi_ctrl);

	/* TMDS CONFIG */
	for (i = 0; i < priv->config->num_tmds; i++) {
		if (dt->pixelclock.typ <= priv->config->tmds[i].pclk) {
			tegra_hdmi_setup_tmds(priv, &priv->config->tmds[i]);
			break;
		}
	}

	writel(SOR_SEQ_PU_PC(0) | SOR_SEQ_PU_PC_ALT(0) | SOR_SEQ_PD_PC(8) |
	       SOR_SEQ_PD_PC_ALT(8), &hdmi->nv_pdisp_sor_seq_ctl);

	value = SOR_SEQ_INST_WAIT_TIME(1) | SOR_SEQ_INST_WAIT_UNITS_VSYNC |
		SOR_SEQ_INST_HALT | SOR_SEQ_INST_PIN_A_LOW |
		SOR_SEQ_INST_PIN_B_LOW | SOR_SEQ_INST_DRIVE_PWM_OUT_LO;

	writel(value, &hdmi->nv_pdisp_sor_seq_inst0);
	writel(value, &hdmi->nv_pdisp_sor_seq_inst8);

	value = readl(&hdmi->nv_pdisp_sor_cstm);

	value &= ~SOR_CSTM_ROTCLK(~0);
	value |= SOR_CSTM_ROTCLK(2);
	value |= SOR_CSTM_PLLDIV;
	value &= ~SOR_CSTM_LVDS_ENABLE;
	value &= ~SOR_CSTM_MODE_MASK;
	value |= SOR_CSTM_MODE_TMDS;

	writel(value, &hdmi->nv_pdisp_sor_cstm);

	/* start SOR */
	writel(SOR_PWR_NORMAL_STATE_PU | SOR_PWR_NORMAL_START_NORMAL |
	       SOR_PWR_SAFE_STATE_PD | SOR_PWR_SETTING_NEW_TRIGGER,
	       &hdmi->nv_pdisp_sor_pwr);
	writel(SOR_PWR_NORMAL_STATE_PU | SOR_PWR_NORMAL_START_NORMAL |
	       SOR_PWR_SAFE_STATE_PD | SOR_PWR_SETTING_NEW_DONE,
	       &hdmi->nv_pdisp_sor_pwr);

	do {
		if (--retries < 0)
			return -ETIME;
		value = readl(&hdmi->nv_pdisp_sor_pwr);
	} while (value & SOR_PWR_SETTING_NEW_PENDING);

	value = SOR_STATE_ASY_CRCMODE_COMPLETE |
		SOR_STATE_ASY_OWNER_HEAD0 |
		SOR_STATE_ASY_SUBOWNER_BOTH |
		SOR_STATE_ASY_PROTOCOL_SINGLE_TMDS_A |
		SOR_STATE_ASY_DEPOL_POS;

	/* setup sync polarities */
	if (dt->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		value |= SOR_STATE_ASY_HSYNCPOL_POS;

	if (dt->flags & DISPLAY_FLAGS_HSYNC_LOW)
		value |= SOR_STATE_ASY_HSYNCPOL_NEG;

	if (dt->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		value |= SOR_STATE_ASY_VSYNCPOL_POS;

	if (dt->flags & DISPLAY_FLAGS_VSYNC_LOW)
		value |= SOR_STATE_ASY_VSYNCPOL_NEG;

	writel(value, &hdmi->nv_pdisp_sor_state2);

	value = SOR_STATE_ASY_HEAD_OPMODE_AWAKE | SOR_STATE_ASY_ORMODE_NORMAL;
	writel(value, &hdmi->nv_pdisp_sor_state1);

	writel(0, &hdmi->nv_pdisp_sor_state0);
	writel(SOR_STATE_UPDATE, &hdmi->nv_pdisp_sor_state0);
	writel(value | SOR_STATE_ATTACHED,
	       &hdmi->nv_pdisp_sor_state1);
	writel(0, &hdmi->nv_pdisp_sor_state0);

	value = readl(&dc->win.win_opt);
	value |= HDMI_ENABLE;
	writel(value, &dc->win.win_opt);

	tegra_dc_enable_controller(dev);

	return 0;
}

static int tegra_hdmi_set_connector(struct udevice *dev, int percent)
{
	/* Is not used in tegra dc */
	return 0;
}

static int tegra_hdmi_timings(struct udevice *dev,
			      struct display_timing *timing)
{
	struct tegra_hdmi_priv *priv = dev_get_priv(dev);

	memcpy(timing, &priv->timing, sizeof(*timing));

	return 0;
}

static void tegra_hdmi_init_clocks(struct udevice *dev)
{
	struct tegra_hdmi_priv *priv = dev_get_priv(dev);
	u32 n = priv->timing.pixelclock.typ * 2 / USEC_PER_SEC; // 148.5 * 2

	switch (clock_get_osc_freq()) {
	case CLOCK_OSC_FREQ_12_0: /* OSC is 12Mhz */
	case CLOCK_OSC_FREQ_48_0: /* OSC is 48Mhz */
		clock_set_rate(priv->hdmi_clk[1], n, 12, 0, 8);
		break;

	case CLOCK_OSC_FREQ_26_0: /* OSC is 26Mhz */
		clock_set_rate(priv->hdmi_clk[1], n, 26, 0, 8);
		break;

	case CLOCK_OSC_FREQ_13_0: /* OSC is 13Mhz */
	case CLOCK_OSC_FREQ_16_8: /* OSC is 16.8Mhz */
		clock_set_rate(priv->hdmi_clk[1], n, 13, 0, 8);
		break;

	case CLOCK_OSC_FREQ_19_2:
	case CLOCK_OSC_FREQ_38_4:
	default:
		/*
		 * These are not supported.
		 */
		break;
	}

	clock_start_periph_pll(priv->hdmi_clk[0], priv->hdmi_clk[1],
			       priv->timing.pixelclock.typ);
}

#if 0
static int tegra_hdmi_decode_edid(struct udevice *dev)
{
	struct tegra_hdmi_priv *priv = dev_get_priv(dev);
	struct udevice *hdmi_edid;
	uchar edid_buf[256] = { 0 };
	int ret;

	ret = dm_i2c_probe(priv->hdmi_ddc, HDMI_EDID_I2C_ADDR, 0, &hdmi_edid);
	if (ret) {
		log_err("cannot probe EDID: error %d\n", ret);
		return ret;
	}

	ret = dm_i2c_read(hdmi_edid, 0, edid_buf, sizeof(edid_buf));
	if (ret) {
		log_err("cannot dump EDID buffer: error %d\n", ret);
		return ret;
	}

	ret = edid_get_timing(edid_buf, sizeof(edid_buf),
			      &priv->timing, &priv->panel_bits_per_colourp);
	if (ret) {
		log_err("cannot decode EDID info: error %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

static const struct tegra_hdmi_config tegra30_hdmi_config = {
	.tmds = tegra30_tmds_config,
	.num_tmds = ARRAY_SIZE(tegra30_tmds_config),
	.has_sor_io_peak_current = false,
};

/* pre-defined P1801-T panel timings */
static struct display_timing default_timing = {
	.pixelclock.typ		= 148500000,
	.hactive.typ		= 1080,
	.hfront_porch.typ	= 88,
	.hback_porch.typ	= 148,
	.hsync_len.typ		= 44,
	.vactive.typ		= 1920,
	.vfront_porch.typ	= 4,
	.vback_porch.typ	= 36,
	.vsync_len.typ		= 5,
	.flags			= DISPLAY_FLAGS_HSYNC_HIGH |
				  DISPLAY_FLAGS_VSYNC_HIGH,
};

static int tegra_hdmi_probe(struct udevice *dev)
{
	struct tegra_hdmi_priv *priv = dev_get_priv(dev);
	int ret;

	priv->hdmi_regmap = (struct hdmi_ctlr *)dev_read_addr_ptr(dev);
	if (!priv->hdmi_regmap) {
		log_err("no display controller address\n");
		return -EINVAL;
	}

	priv->config = (struct tegra_hdmi_config *)dev_get_driver_data(dev);

	ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
					   "hdmi-supply", &priv->hdmi);
	if (ret)
		log_err("cannot get hdmi-supply: error %d\n", ret);

	ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
					   "pll-supply", &priv->pll);
	if (ret)
		log_err("cannot get pll-supply: error %d\n", ret);

	ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
					   "vdd-supply", &priv->vdd);
	if (ret)
		log_err("cannot get vdd-supply: error %d\n", ret);

	ret = regulator_set_enable_if_allowed(priv->hdmi, true);
	if (ret && ret != -ENOSYS) {
		log_err("cannot enable hdmi-supply: error %d\n", ret);
		return ret;
	}

	ret = regulator_set_enable_if_allowed(priv->pll, true);
	if (ret && ret != -ENOSYS) {
		log_err("cannot enable pll-supply: error %d\n", ret);
		return ret;
	}

	ret = regulator_set_enable_if_allowed(priv->vdd, true);
	if (ret && ret != -ENOSYS) {
		log_err("cannot enable vdd-supply: error %d\n", ret);
		return ret;
	}

	/* Pass pre-defined timings for now since edid is broken */
	memcpy(&priv->timing, &default_timing,
	       sizeof(default_timing));

#if 0
	ret = uclass_get_device_by_phandle(UCLASS_I2C, dev,
					   "nvidia,ddc-i2c-bus",
					   &priv->hdmi_ddc);
	if (ret) {
		log_err("cannot get hdmi ddc i2c bus: error %d\n", ret);
		return ret;
	}

	ret = tegra_hdmi_decode_edid(dev);
	if (ret)
		return ret;
#endif

	ret = clock_decode_pair(dev, priv->hdmi_clk);
	if (ret < 0) {
		log_err("cannot decode clocks for '%s' (ret = %d)\n",
			 dev->name, ret);
		return ret;
	}

	tegra_hdmi_init_clocks(dev);

	return 0;
}

static const struct panel_ops tegra_hdmi_ops = {
	.enable_backlight	= tegra_hdmi_encoder_enable,
	.set_backlight		= tegra_hdmi_set_connector,
	.get_display_timing	= tegra_hdmi_timings,
};

static const struct udevice_id tegra_hdmi_ids[] = {
	{ .compatible = "nvidia,tegra30-hdmi", .data = (ulong)&tegra30_hdmi_config },
	{ }
};

U_BOOT_DRIVER(tegra_hdmi) = {
	.name		= "tegra_hdmi",
	.id		= UCLASS_PANEL,
	.of_match	= tegra_hdmi_ids,
	.ops		= &tegra_hdmi_ops,
	.probe		= tegra_hdmi_probe,
	.plat_auto	= sizeof(struct tegra_dc_plat),
	.priv_auto	= sizeof(struct tegra_hdmi_priv),
};
