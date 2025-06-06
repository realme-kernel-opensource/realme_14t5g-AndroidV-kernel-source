/********************************************
 ** Copyright (C) 2023 OPLUS Mobile Comm Corp. Ltd.
 ** OPLUS_EDIT
 ** File: oplus23700_hkc_ili9883c_fhd_dsi_vdo.h
 ** Description: Source file for LCD driver
 **                  To Control LCD driver
 ** Version : 1.0
 ** Date : 2023/11/28
 ** ---------------- Revision History: --------------------------
 ** <version: >        <date>                  < author >                          <desc>
 ********************************************/
#ifndef _OPLUS23706_HKC_ILI9883C_HD_DSI_VDO_H_
#define _OPLUS23706_HKC_ILI9883C_HD_DSI_VDO_H_

/*LCM_DEGREE default value*/
#define PROBE_FROM_DTS 0

struct jdi {
        struct device *dev;
        struct drm_panel panel;
        struct backlight_device *backlight;
        struct gpio_desc *reset_gpio;
        struct gpio_desc *bias_pos;
        struct gpio_desc *bias_neg;
        struct gpio_desc *esd_te_gpio;

        bool prepared;
        bool enabled;

        unsigned int lcm_degree;

        int error;
        struct notifier_block lcd_vsn_reset_nb;
};

#endif /* _OPLUS23706_HKC_ILI9883C_HD_DSI_VDO_H_ */
