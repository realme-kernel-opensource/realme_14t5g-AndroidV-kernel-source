obj-$(CONFIG_MTK_VIDEO_LM3643)     += lm3643.o
obj-$(CONFIG_MTK_VIDEO_LM3644)     += lm3644.o
obj-$(CONFIG_MTK_VIDEO_SY7806)     += sy7806.o
obj-$(CONFIG_MTK_VIDEO_AW36515)    += aw36515.o
obj-$(CONFIG_MTK_VIDEO_AW36518_23660)    += aw36518_23660.o
obj-$(CONFIG_MTK_VIDEO_AW36515_23083)    += aw36515_23083.o
MTK_CUSTOM_PATH := $(srctree)/drivers/misc/mediatek
subdir-ccflags-y += \
	-I$(MTK_CUSTOM_PATH)/flashlight

ifneq ($(wildcard $(MTK_CUSTOM_PATH)/flashlight/v4l2/k6983v1_64_alpha),)
obj-$(CONFIG_MTK_VIDEO_SY7806)     += k6983v1_64_alpha/sy7806.o
endif
