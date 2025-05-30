# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2021 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

ccflags-y += -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2 \
             -I$(srctree)/drivers/devfreq \
             -I$(srctree)/drivers/dma-buf/heaps/ \
             -I$(srctree)/drivers/misc/mediatek/mmdvfs \
             -I$(srctree)/drivers/misc/mediatek/smi \
             -I$(srctree)/drivers/misc/mediatek/include \
             -I$(srctree)/drivers/misc/mediatek/include/mt-plat \
             -I$(srctree)/drivers/iommu \
             -I$(srctree)/drivers/misc/mediatek/mtk-interconnect \
             -I$(srctree)/drivers/misc/mediatek/cmdq/mailbox \
             -I$(srctree)/drivers/misc/mediatek/slbc \
             -I$(srctree)/drivers/misc/mediatek/mmp \
             -I$(srctree)/drivers/misc/mediatek/tinysys_scmi

obj-$(CONFIG_MTK_MML_LEGACY) := mtk-mml-legacy.o
obj-$(CONFIG_MTK_MML) := mtk-mml.o

mtk-mml-legacy-objs := mtk-mml.o

ifeq ($(strip $(CONFIG_MTK_MML_DEBUG)),y)
mtk-mml-objs += mtk-mml-monitor.o
endif

mtk-mml-objs += mtk-mml-driver.o
mtk-mml-objs += mtk-mml-core.o
mtk-mml-objs += mtk-mml-tile.o
mtk-mml-objs += mtk-mml-pq-core.o
mtk-mml-objs += mtk-mml-drm-adaptor.o
mtk-mml-objs += mtk-mml-dle-adaptor.o
mtk-mml-objs += mtk-mml-buf.o
mtk-mml-objs += mtk-mml-sys.o
mtk-mml-objs += tile_core.o
mtk-mml-objs += tile_mdp_func.o
mtk-mml-objs += DpTileScaler.o
# device components
mtk-mml-objs += mtk-mml-aal.o
mtk-mml-objs += mtk-mml-color.o
mtk-mml-objs += mtk-mml-fg.o
mtk-mml-objs += mtk-mml-hdr.o
mtk-mml-objs += mtk-mml-mutex.o
mtk-mml-objs += mtk-mml-rdma.o
mtk-mml-objs += mtk-mml-rsz.o
mtk-mml-objs += mtk-mml-rsz-fw.o
mtk-mml-objs += mtk-mml-pq-rdma.o
mtk-mml-objs += mtk-mml-pq-birsz.o
mtk-mml-objs += mtk-mml-pq-birsz-fw.o
mtk-mml-objs += mtk-mml-tcc.o
mtk-mml-objs += mtk-mml-tdshp.o
mtk-mml-objs += mtk-mml-wrot.o

# mml call sspm support
mtk-mml-objs += mtk-mml-scmi.o

ifeq ($(strip $(CONFIG_MTK_MML_DEBUG)),y)
ifneq (,$(strip $(CONFIG_MMPROFILE)))
mtk-mml-objs += mtk-mml-mmp.o
endif
endif

ifneq ($(wildcard $(srctree)/$(src)/mtk-mml-tp-mt6983.c),)
obj-$(CONFIG_MTK_MML) += mtk-mml-mt6983.o
mtk-mml-mt6983-objs := mtk-mml-tp-mt6983.o
endif

ifneq ($(wildcard $(srctree)/$(src)/mtk-mml-tp-mt6985.c),)
obj-$(CONFIG_MTK_MML) += mtk-mml-mt6985.o
mtk-mml-mt6985-objs := mtk-mml-tp-mt6985.o
endif

ifneq ($(wildcard $(srctree)/$(src)/mtk-mml-tp-mt6886.c),)
obj-$(CONFIG_MTK_MML) += mtk-mml-mt6886.o
mtk-mml-mt6886-objs := mtk-mml-tp-mt6886.o
endif

# for unit test
ifeq ($(strip $(CONFIG_MTK_MML_DEBUG)),y)
mtk-mml-objs += mtk-mml-test.o
endif