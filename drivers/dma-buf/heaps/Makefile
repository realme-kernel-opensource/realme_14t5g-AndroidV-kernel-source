# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_DMABUF_HEAPS_DEFERRED_FREE) += deferred-free-helper.o
obj-$(CONFIG_DMABUF_HEAPS_PAGE_POOL)	+= page_pool.o
obj-$(CONFIG_DMABUF_HEAPS_SYSTEM)	+= system_heap.o
obj-$(CONFIG_DMABUF_HEAPS_CMA)		+= cma_heap.o

ccflags-y += -I$(srctree)/drivers/iommu/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/trusted_mem/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/monitor_hang/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/
ccflags-y += -I$(srctree)/mm/oplus_mm/
obj-$(CONFIG_MTK_TRUSTED_MEMORY_SUBSYSTEM)	+= mtk_sec_heap.o
obj-$(CONFIG_DMABUF_HEAPS_DEBUG) += mtk_heap_debug.o
ifneq ($(MEM_LIGHTOS), yes)
obj-$(CONFIG_OPLUS_FEATURE_MM_BOOSTPOOL) += ../../../mm/oplus_mm/mm_boost_pool/oplus_boost_pool_mtk.o
else
ccflags-y += -DOPLUS_FEATURE_MEM_4G_DEVICE
endif
