/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#ifndef __DRV_CLK_MTK_H
#define __DRV_CLK_MTK_H

#include <linux/regmap.h>
#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

/* hw voter timeout configures */
#define MTK_WAIT_HWV_PREPARE_CNT	200
#define MTK_WAIT_HWV_PREPARE_US		1
#define MTK_WAIT_HWV_DONE_CNT		500
#define MTK_WAIT_HWV_DONE_US		1
#define MTK_WAIT_HWV_STA_CNT		100
#define MTK_HWV_ID_OFS			(0x8)
#define MTK_HWV_PREPARE_TMROUT		(200000)
#define MTK_HWV_DONE_TMROUT		(500000)
#define MTK_HWV_STA_TMROUT		(100000)
#define MTK_WAIT_SET_PARENT_CNT		(1000)
#define MTK_WAIT_SET_PARENT_US		(1)

struct clk;
struct clk_onecell_data;

#define MAX_MUX_GATE_BIT	31
#define INVALID_MUX_GATE_BIT	(MAX_MUX_GATE_BIT + 1)

#define MHZ (1000 * 1000)

enum clk_evt_type {
	CLK_EVT_HWV_CG_TIMEOUT = 0,
	CLK_EVT_HWV_CG_CHK_PWR = 1,
	CLK_EVT_LONG_BUS_LATENCY = 2,
	CLK_EVT_HWV_PLL_TIMEOUT = 3,
	CLK_EVT_CLK_TRACE = 4,
	CLK_EVT_TRIGGER_TRACE_DUMP = 5,
	CLK_EVT_IPI_CG_TIMEOUT = 6,
	CLK_EVT_SET_PARENT_TIMEOUT = 7,
	CLK_EVT_BYPASS_PLL = 8,
	CLK_EVT_NUM,
};

struct clk_event_data {
	struct regmap *regmap;
	struct regmap *hwv_regmap;
	int event_type;
	const char *name;
	u32 ofs;
	u32 id;
	u32 shift;
};

struct mtk_fixed_clk {
	int id;
	const char *name;
	const char *parent;
	unsigned long rate;
};

#define FIXED_CLK(_id, _name, _parent, _rate) {		\
		.id = _id,				\
		.name = _name,				\
		.parent = _parent,			\
		.rate = _rate,				\
	}

void mtk_clk_register_fixed_clks(const struct mtk_fixed_clk *clks,
		int num, struct clk_onecell_data *clk_data);

struct mtk_fixed_factor {
	int id;
	const char *name;
	const char *parent_name;
	int mult;
	int div;
};

#define FACTOR(_id, _name, _parent, _mult, _div) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.mult = _mult,				\
		.div = _div,				\
	}

void mtk_clk_register_factors(const struct mtk_fixed_factor *clks,
		int num, struct clk_onecell_data *clk_data);

struct mtk_composite {
	int id;
	const char *name;
	const char * const *parent_names;
	const char *parent;
	unsigned flags;

	uint32_t mux_reg;
	uint32_t divider_reg;
	uint32_t gate_reg;

	signed char mux_shift;
	signed char mux_width;
	signed char gate_shift;

	signed char divider_shift;
	signed char divider_width;

	u8 mux_flags;

	signed char num_parents;
};

#define MUX_GATE_FLAGS_2(_id, _name, _parents, _reg, _shift,		\
				_width, _gate, _flags, _muxflags) {	\
		.id = _id,						\
		.name = _name,						\
		.mux_reg = _reg,					\
		.mux_shift = _shift,					\
		.mux_width = _width,					\
		.gate_reg = _reg,					\
		.gate_shift = _gate,					\
		.divider_shift = -1,					\
		.parent_names = _parents,				\
		.num_parents = ARRAY_SIZE(_parents),			\
		.flags = _flags,					\
		.mux_flags = _muxflags,					\
	}

/*
 * In case the rate change propagation to parent clocks is undesirable,
 * this macro allows to specify the clock flags manually.
 */
#define MUX_GATE_FLAGS(_id, _name, _parents, _reg, _shift, _width,	\
			_gate, _flags)					\
		MUX_GATE_FLAGS_2(_id, _name, _parents, _reg,		\
					_shift, _width, _gate, _flags, 0)

/*
 * Unless necessary, all MUX_GATE clocks propagate rate changes to their
 * parent clock by default.
 */
#define MUX_GATE(_id, _name, _parents, _reg, _shift, _width, _gate)	\
	MUX_GATE_FLAGS(_id, _name, _parents, _reg, _shift, _width,	\
		_gate, 0)

#define MUX(_id, _name, _parents, _reg, _shift, _width)			\
	MUX_FLAGS(_id, _name, _parents, _reg,				\
		  _shift, _width, 0)

#define MUX_FLAGS(_id, _name, _parents, _reg, _shift, _width, _flags) {	\
		.id = _id,						\
		.name = _name,						\
		.mux_reg = _reg,					\
		.mux_shift = _shift,					\
		.mux_width = _width,					\
		.gate_shift = -1,					\
		.divider_shift = -1,					\
		.parent_names = _parents,				\
		.num_parents = ARRAY_SIZE(_parents),			\
		.flags = _flags,					\
	}

#define DIV_GATE(_id, _name, _parent, _gate_reg, _gate_shift, _div_reg,	\
					_div_width, _div_shift) {	\
		.id = _id,						\
		.parent = _parent,					\
		.name = _name,						\
		.divider_reg = _div_reg,				\
		.divider_shift = _div_shift,				\
		.divider_width = _div_width,				\
		.gate_reg = _gate_reg,					\
		.gate_shift = _gate_shift,				\
		.mux_shift = -1,					\
		.flags = 0,						\
	}

struct clk *mtk_clk_register_composite(const struct mtk_composite *mc,
		void __iomem *base, spinlock_t *lock);

void mtk_clk_register_composites(const struct mtk_composite *mcs,
		int num, void __iomem *base, spinlock_t *lock,
		struct clk_onecell_data *clk_data);

struct mtk_gate_regs {
	u32 sta_ofs;
	u32 clr_ofs;
	u32 set_ofs;
};

struct mtk_gate {
	int id;
	const char *name;
	const char *parent_name;
	const struct mtk_gate_regs *regs;
	const struct mtk_gate_regs *hwv_regs;
	int shift;
	const struct clk_ops *ops;
	unsigned long flags;
};

int mtk_clk_register_gates(struct device_node *node,
			const struct mtk_gate *clks, int num,
			struct clk_onecell_data *clk_data);

int mtk_clk_register_gates_with_dev(struct device_node *node,
		const struct mtk_gate *clks,
		int num, struct clk_onecell_data *clk_data,
		struct device *dev);

struct mtk_clk_divider {
	int id;
	const char *name;
	const char *parent_name;
	unsigned long flags;

	u32 div_reg;
	unsigned char div_shift;
	unsigned char div_width;
	unsigned char clk_divider_flags;
	const struct clk_div_table *clk_div_table;
};

#define DIV_ADJ(_id, _name, _parent, _reg, _shift, _width) {	\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.div_reg = _reg,				\
		.div_shift = _shift,				\
		.div_width = _width,				\
}

void mtk_clk_register_dividers(const struct mtk_clk_divider *mcds,
			int num, void __iomem *base, spinlock_t *lock,
				struct clk_onecell_data *clk_data);

struct clk_onecell_data *mtk_alloc_clk_data(unsigned int clk_num);
void mtk_free_clk_data(struct clk_onecell_data *clk_data);

#define HAVE_RST_BAR			BIT(0)
#define PLL_AO				BIT(1)
#define CLK_USE_HW_VOTER		BIT(2)
#define HWV_CHK_FULL_STA		BIT(3)
#define CLK_ENABLE_QUICK_SWITCH		BIT(4)
#define CLK_IPI_EN_DEFAULT_OPP		BIT(5)
#define MUX_ROUND_CLOSEST		BIT(6)

struct mtk_pll_div_table {
	u32 div;
	unsigned long freq;
};

struct mtk_pll_data {
	int id;
	const char *name;
	uint32_t reg;
	uint32_t pwr_reg;
	uint32_t en_reg;
	uint32_t en_mask;
	uint32_t hwv_set_ofs;
	uint32_t hwv_clr_ofs;
	uint32_t hwv_sta_ofs;
	uint32_t hwv_done_ofs;
	uint32_t hwv_set_sta_ofs;
	uint32_t hwv_clr_sta_ofs;
	int hwv_shift;
	uint32_t pd_reg;
	uint32_t tuner_reg;
	uint32_t tuner_en_reg;
	uint8_t tuner_en_bit;
	int pd_shift;
	unsigned int flags;
	const struct clk_ops *ops;
	u32 rst_bar_mask;
	unsigned long fmin;
	unsigned long fmax;
	int pcwbits;
	int pcwibits;
	uint32_t pcw_reg;
	int pcw_shift;
	uint32_t pcw_chg_reg;
	const struct mtk_pll_div_table *div_table;
	const char *parent_name;
	uint8_t pll_en_bit;
};

struct ipi_callbacks {
	int (*clk_enable)(const u8 clk_idx);
	int (*clk_disable)(const u8 clk_idx);
};

void mtk_clk_register_plls(struct device_node *node,
		const struct mtk_pll_data *plls, int num_plls,
		struct clk_onecell_data *clk_data);

struct clk *mtk_clk_register_ref2usb_tx(const char *name,
			const char *parent_name, void __iomem *reg);

void mtk_register_reset_controller(struct device_node *np,
			unsigned int num_regs, int regofs);

void mtk_register_reset_controller_set_clr(struct device_node *np,
	unsigned int num_regs, int regofs);

extern bool (*mtk_fh_set_rate)(const char *name, unsigned long dds, int postdiv);

struct mtk_clk_desc {
	const struct mtk_gate *clks;
	size_t num_clks;
};

int mtk_clk_simple_probe(struct platform_device *pdev);
extern int register_mtk_clk_notifier(struct notifier_block *nb);
extern int unregister_mtk_clk_notifier(struct notifier_block *nb);
extern int mtk_clk_notify(struct regmap *regmap, struct regmap *hwv_regmap,
		const char *name, u32 ofs, u32 id, u32 shift, int event_type);
extern void mtk_clk_register_ipi_callback(struct ipi_callbacks *clk_cb);
extern struct ipi_callbacks *mtk_clk_get_ipi_cb(void);
extern int mtk_hwv_pll_on(struct clk_hw *hw);
extern void mtk_hwv_pll_off(struct clk_hw *hw);
extern bool mtk_hwv_pll_is_on(struct clk_hw *hw);

#endif /* __DRV_CLK_MTK_H */
