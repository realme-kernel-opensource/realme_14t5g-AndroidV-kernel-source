# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/drivers/usb/typec
ifndef CONFIG_OPLUS_PD_EXT_SUPPORT
ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
else
ccflags-y += -I$(srctree)/drivers/power/oplus/pd_ext/inc
endif
obj-$(CONFIG_MTK_USB_TYPEC_MUX) += mux_switch.o
obj-$(CONFIG_TYPEC_MUX_FUSB304)	+= fusb304.o
obj-$(CONFIG_TYPEC_MUX_PTN36241G) += ptn36241g.o
obj-$(CONFIG_TYPEC_MUX_PS5169) += ps5169.o
obj-$(CONFIG_TYPEC_MUX_PS5170) += ps5170.o
obj-$(CONFIG_TYPEC_MUX_PI3DPX1205A) += pi3dpx1205a.o
obj-$(CONFIG_TYPEC_MUX_DP_SELECTOR) += usb_dp_selector.o
ifneq ($(wildcard $(srctree)/$(src)/k6985v1_64_flip_alpha),)
obj-$(CONFIG_TYPEC_MUX_FLIP_ALPHA_FSA4480) += k6985v1_64_flip_alpha/
endif
