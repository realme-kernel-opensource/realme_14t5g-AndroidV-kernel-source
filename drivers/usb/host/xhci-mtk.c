// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek xHCI Host Controller Driver
 *
 * Copyright (c) 2015 MediaTek Inc.
 * Author:
 *  Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "xhci.h"
#include "xhci-mtk.h"

/* ip_pw_ctrl0 register */
#define CTRL0_IP_SW_RST	BIT(0)

/* ip_pw_ctrl1 register */
#define CTRL1_IP_HOST_PDN	BIT(0)

/* ip_pw_ctrl2 register */
#define CTRL2_IP_DEV_PDN	BIT(0)

/* ip_pw_sts1 register */
#define STS1_IP_SLEEP_STS	BIT(30)
#define STS1_U3_MAC_RST	BIT(16)
#define STS1_XHCI_RST		BIT(11)
#define STS1_SYS125_RST	BIT(10)
#define STS1_REF_RST		BIT(8)
#define STS1_SYSPLL_STABLE	BIT(0)

/* ip_xhci_cap register */
#define CAP_U3_PORT_NUM(p)	((p) & 0xff)
#define CAP_U2_PORT_NUM(p)	(((p) >> 8) & 0xff)

/* u3_ctrl_p register */
#define CTRL_U3_PORT_HOST_SEL	BIT(2)
#define CTRL_U3_PORT_PDN	BIT(1)
#define CTRL_U3_PORT_DIS	BIT(0)

/* u2_ctrl_p register */
#define CTRL_U2_PORT_HOST_SEL	BIT(2)
#define CTRL_U2_PORT_PDN	BIT(1)
#define CTRL_U2_PORT_DIS	BIT(0)

/* u2_phy_pll register */
#define CTRL_U2_FORCE_PLL_STB	BIT(28)

/* xHCI CSR */
#define LS_EOF_CFG		0x930
#define LSEOF_OFFSET		0x89

#define FS_EOF_CFG		0x934
#define FSEOF_OFFSET		0x2e

#define SS_GEN1_EOF_CFG		0x93c
#define SSG1EOF_OFFSET		0x78

#define HFCNTR_CFG		0x944
#define ITP_DELTA_CLK		(0xa << 1)
#define ITP_DELTA_CLK_MASK	GENMASK(5, 1)
#define FRMCNT_LEV1_RANG	(0x12b << 8)
#define FRMCNT_LEV1_RANG_MASK	GENMASK(19, 8)

#define HSCH_CFG1		0x960
#define SCH3_RXFIFO_DEPTH_MASK	GENMASK(21, 20)

#define SS_GEN2_EOF_CFG		0x990
#define SSG2EOF_OFFSET		0x3c

#define XSEOF_OFFSET_MASK	GENMASK(11, 0)

/* testmode*/
#define HOST_CMD_STOP               0x0
#define HOST_CMD_TEST_J             0x1
#define HOST_CMD_TEST_K             0x2
#define HOST_CMD_TEST_SE0_NAK       0x3
#define HOST_CMD_TEST_PACKET        0x4
#define PMSC_PORT_TEST_CTRL_OFFSET  28

#define PROC_MTK_USB "mtk_usb"
#define PROC_TEST_MODE "testmode"

/* usb remote wakeup registers in syscon */

/* mt8173 etc */
#define PERI_WK_CTRL1	0x4
#define WC1_IS_C(x)	(((x) & 0xf) << 26)  /* cycle debounce */
#define WC1_IS_EN	BIT(25)
#define WC1_IS_P	BIT(6)  /* polarity for ip sleep */

/* mt8183 */
#define PERI_WK_CTRL0	0x0
#define WC0_IS_C(x)	((u32)(((x) & 0xf) << 28))  /* cycle debounce */
#define WC0_IS_P	BIT(12)	/* polarity */
#define WC0_IS_EN	BIT(6)

/* mt8192 */
#define WC0_SSUSB0_CDEN		BIT(6)
#define WC0_IS_SPM_EN		BIT(1)

/* mt2712 etc */
#define PERI_SSUSB_SPM_CTRL	0x0
#define SSC_IP_SLEEP_EN	BIT(4)
#define SSC_SPM_INT_EN		BIT(1)

#define SCH_FIFO_TO_KB(x)	((x) >> 10)

enum ssusb_uwk_vers {
	SSUSB_UWK_V1 = 1,
	SSUSB_UWK_V2,
	SSUSB_UWK_V1_1 = 101,	/* specific revision 1.01 */
	SSUSB_UWK_V1_2,		/* specific revision 1.2 */
};

/*
 * MT8195 has 4 controllers, the controller1~3's default SOF/ITP interval
 * is calculated from the frame counter clock 24M, but in fact, the clock
 * is 48M, add workaround for it.
 */
static void xhci_mtk_set_frame_interval(struct xhci_hcd_mtk *mtk)
{
	struct device *dev = mtk->dev;
	struct usb_hcd *hcd = mtk->hcd;
	u32 value;

	if (!of_device_is_compatible(dev->of_node, "mediatek,mt8195-xhci"))
		return;

	value = readl(hcd->regs + HFCNTR_CFG);
	value &= ~(ITP_DELTA_CLK_MASK | FRMCNT_LEV1_RANG_MASK);
	value |= (ITP_DELTA_CLK | FRMCNT_LEV1_RANG);
	writel(value, hcd->regs + HFCNTR_CFG);

	value = readl(hcd->regs + LS_EOF_CFG);
	value &= ~XSEOF_OFFSET_MASK;
	value |= LSEOF_OFFSET;
	writel(value, hcd->regs + LS_EOF_CFG);

	value = readl(hcd->regs + FS_EOF_CFG);
	value &= ~XSEOF_OFFSET_MASK;
	value |= FSEOF_OFFSET;
	writel(value, hcd->regs + FS_EOF_CFG);

	value = readl(hcd->regs + SS_GEN1_EOF_CFG);
	value &= ~XSEOF_OFFSET_MASK;
	value |= SSG1EOF_OFFSET;
	writel(value, hcd->regs + SS_GEN1_EOF_CFG);

	value = readl(hcd->regs + SS_GEN2_EOF_CFG);
	value &= ~XSEOF_OFFSET_MASK;
	value |= SSG2EOF_OFFSET;
	writel(value, hcd->regs + SS_GEN2_EOF_CFG);
}

static int xhci_mtk_halt(struct xhci_hcd *xhci)
{
	u32 result;
	int ret;
	u32 halted;
	u32 cmd;
	u32 mask;

	mask = ~(XHCI_IRQS);
	halted = readl(&xhci->op_regs->status) & STS_HALT;
	if (!halted)
		mask &= ~CMD_RUN;

	cmd = readl(&xhci->op_regs->command);
	cmd &= mask;
	writel(cmd, &xhci->op_regs->command);

	ret = readl_poll_timeout_atomic(&xhci->op_regs->status, result,
					(result & STS_HALT) == STS_HALT ||
					result == U32_MAX,
					1, XHCI_MAX_HALT_USEC);
	if (result == U32_MAX)		/* card removed */
		ret = -ENODEV;

	if (ret) {
		xhci_warn(xhci, "Host halt failed, %d\n", ret);
		return ret;
	}
	xhci->xhc_state |= XHCI_STATE_HALTED;
	xhci->cmd_ring_state = CMD_RING_STATE_STOPPED;
	return ret;
}

static int xhci_mtk_testmode_show(struct seq_file *s, void *unused)
{
	struct xhci_hcd_mtk *mtk = s->private;
	struct xhci_hcd	*xhci = hcd_to_xhci(mtk->hcd);

	switch (xhci->test_mode) {
	case HOST_CMD_STOP:
		seq_puts(s, "0\n");
		break;
	case HOST_CMD_TEST_J:
		seq_puts(s, "test J\n");
		break;
	case HOST_CMD_TEST_K:
		seq_puts(s, "test K\n");
		break;
	case HOST_CMD_TEST_SE0_NAK:
		seq_puts(s, "test SE0 NAK\n");
		break;
	case HOST_CMD_TEST_PACKET:
		seq_puts(s, "test packet\n");
		break;
	default:
		break;
	}

	return 0;
}

static int xhci_mtk_testmode_open(struct inode *inode, struct file *file)
{
	return single_open(file, xhci_mtk_testmode_show, PDE_DATA(inode));
}

static ssize_t xhci_mtk_testmode_write(struct file *file,  const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct xhci_hcd_mtk *mtk = s->private;
	struct xhci_hcd	*xhci = hcd_to_xhci(mtk->hcd);
	int ports = HCS_MAX_PORTS(xhci->hcs_params1);
	char buf[32];
	unsigned long flags;
	u8 testmode = HOST_CMD_STOP;
	u32 temp;
	u32 __iomem *addr;
	int i;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "test packet", 10))
		testmode = HOST_CMD_TEST_PACKET;
	else if (!strncmp(buf, "test K", 6))
		testmode = HOST_CMD_TEST_K;
	else if (!strncmp(buf, "test J", 6))
		testmode = HOST_CMD_TEST_J;
	else if (!strncmp(buf, "test SE0 NAK", 12))
		testmode = HOST_CMD_TEST_SE0_NAK;

	if (testmode >= HOST_CMD_STOP && testmode <= HOST_CMD_TEST_PACKET) {
		xhci_info(xhci, "set test mode %d\n", testmode);

		spin_lock_irqsave(&xhci->lock, flags);

		/* set the Run/Stop in USBCMD to 0 */
		addr = &xhci->op_regs->command;
		temp = readl(addr);
		temp &= ~CMD_RUN;
		writel(temp, addr);

		/*  wait for HCHalted */
		xhci_mtk_halt(xhci);

		/* test mode */
		for (i = 0; i < ports; i++) {
			addr = &xhci->op_regs->port_power_base +
				NUM_PORT_REGS * (i & 0xff);
			temp = readl(addr);
			temp &= ~(0xf << PMSC_PORT_TEST_CTRL_OFFSET);
			temp |= (testmode << PMSC_PORT_TEST_CTRL_OFFSET);
			writel(temp, addr);
		}

		xhci->test_mode = testmode;
		spin_unlock_irqrestore(&xhci->lock, flags);
	} else {
		pr_info("%s: invalid value\n", __func__);
		return -EINVAL;
	}

	return count;
}

static const struct  proc_ops testmode_fops = {
	.proc_open = xhci_mtk_testmode_open,
	.proc_write = xhci_mtk_testmode_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void xhci_mtk_procfs_init(struct xhci_hcd_mtk *mtk)
{
	struct proc_dir_entry *root = NULL;
	struct device_node *np = mtk->dev->of_node;
	char name[32];

	snprintf(name, sizeof(name), PROC_MTK_USB "/%s", np->name);
	root = proc_mkdir(name, NULL);
	if (!root) {
		dev_info(mtk->dev, "%s, failed to create root\n", __func__);
		return;
	}

	mtk->testmode_file = proc_create_data(PROC_TEST_MODE, 0644,
		root, &testmode_fops, mtk);
	if (!mtk->testmode_file) {
		dev_info(mtk->dev, "%s: fail to create testmode node\n",
			__func__);
		proc_remove(root);
		return;
	}

	mtk->root = root;
}

static void xhci_mtk_procfs_exit(struct xhci_hcd_mtk *mtk)
{
	proc_remove(mtk->testmode_file);
	proc_remove(mtk->root);
}

/*
 * workaround: usb3.2 gen1 isoc rx hw issue
 * host send out unexpected ACK afer device fininsh a burst transfer with
 * a short packet.
 */
static void xhci_mtk_rxfifo_depth_set(struct xhci_hcd_mtk *mtk)
{
	struct usb_hcd *hcd = mtk->hcd;
	u32 value;

	if (!mtk->rxfifo_depth)
		return;

	value = readl(hcd->regs + HSCH_CFG1);
	value &= ~SCH3_RXFIFO_DEPTH_MASK;
	value |= FIELD_PREP(SCH3_RXFIFO_DEPTH_MASK,
			    SCH_FIFO_TO_KB(mtk->rxfifo_depth) - 1);
	writel(value, hcd->regs + HSCH_CFG1);
}

static void xhci_mtk_init_quirk(struct xhci_hcd_mtk *mtk)
{
	/* workaround only for mt8195 */
	xhci_mtk_set_frame_interval(mtk);

	/* workaround for SoCs using SSUSB about before IPM v1.6.0 */
	xhci_mtk_rxfifo_depth_set(mtk);
}

static int xhci_mtk_host_enable(struct xhci_hcd_mtk *mtk)
{
	struct mu3c_ippc_regs __iomem *ippc = mtk->ippc_regs;
	u32 value, check_val;
	int u3_ports_disabled = 0;
	int ret;
	int i;

	if (!mtk->has_ippc)
		return 0;

	/* power on host ip */
	value = readl(&ippc->ip_pw_ctr1);
	value &= ~CTRL1_IP_HOST_PDN;
	writel(value, &ippc->ip_pw_ctr1);

	/* power on and enable u3 ports except skipped ones */
	for (i = 0; i < mtk->num_u3_ports; i++) {
		if ((0x1 << i) & mtk->u3p_dis_msk) {
			u3_ports_disabled++;
			continue;
		}

		value = readl(&ippc->u3_ctrl_p[i]);
		value &= ~(CTRL_U3_PORT_PDN | CTRL_U3_PORT_DIS);
		value |= CTRL_U3_PORT_HOST_SEL;
		writel(value, &ippc->u3_ctrl_p[i]);
	}

	/* power on and enable all u2 ports except skipped ones */
	for (i = 0; i < mtk->num_u2_ports; i++) {
		if (BIT(i) & mtk->u2p_dis_msk)
			continue;

		value = readl(&ippc->u2_ctrl_p[i]);
		value &= ~(CTRL_U2_PORT_PDN | CTRL_U2_PORT_DIS);
		value |= CTRL_U2_PORT_HOST_SEL;
		writel(value, &ippc->u2_ctrl_p[i]);
	}

	/*
	 * wait for clocks to be stable, and clock domains reset to
	 * be inactive after power on and enable ports
	 */
	check_val = STS1_SYSPLL_STABLE | STS1_REF_RST |
			STS1_SYS125_RST | STS1_XHCI_RST;

	if (mtk->num_u3_ports > u3_ports_disabled)
		check_val |= STS1_U3_MAC_RST;

	ret = readl_poll_timeout(&ippc->ip_pw_sts1, value,
			  (check_val == (value & check_val)), 100, 20000);
	if (ret) {
		dev_err(mtk->dev, "clocks are not stable (0x%x)\n", value);
		return ret;
	}

	return 0;
}

static int xhci_mtk_host_disable(struct xhci_hcd_mtk *mtk)
{
	struct mu3c_ippc_regs __iomem *ippc = mtk->ippc_regs;
	u32 value;
	int ret;
	int i;

	if (!mtk->has_ippc)
		return 0;

	/* power down u3 ports except skipped ones */
	for (i = 0; i < mtk->num_u3_ports; i++) {
		if ((0x1 << i) & mtk->u3p_dis_msk)
			continue;

		value = readl(&ippc->u3_ctrl_p[i]);
		value |= CTRL_U3_PORT_PDN;
		writel(value, &ippc->u3_ctrl_p[i]);
	}

	/* power down all u2 ports except skipped ones */
	for (i = 0; i < mtk->num_u2_ports; i++) {
		if (BIT(i) & mtk->u2p_dis_msk)
			continue;

		value = readl(&ippc->u2_ctrl_p[i]);
		value |= CTRL_U2_PORT_PDN;
		writel(value, &ippc->u2_ctrl_p[i]);
	}

	/* power down host ip */
	value = readl(&ippc->ip_pw_ctr1);
	value |= CTRL1_IP_HOST_PDN;
	writel(value, &ippc->ip_pw_ctr1);

	/* wait for host ip to sleep */
	ret = readl_poll_timeout(&ippc->ip_pw_sts1, value,
			  (value & STS1_IP_SLEEP_STS), 100, 100000);
	if (ret) {
		dev_err(mtk->dev, "ip sleep failed!!!\n");
		return ret;
	}
	return 0;
}

static int xhci_mtk_ssusb_config(struct xhci_hcd_mtk *mtk)
{
	struct mu3c_ippc_regs __iomem *ippc = mtk->ippc_regs;
	u32 value;

	if (!mtk->has_ippc)
		return 0;

	/* reset whole ip */
	value = readl(&ippc->ip_pw_ctr0);
	value |= CTRL0_IP_SW_RST;
	writel(value, &ippc->ip_pw_ctr0);
	udelay(1);
	value = readl(&ippc->ip_pw_ctr0);
	value &= ~CTRL0_IP_SW_RST;
	writel(value, &ippc->ip_pw_ctr0);

	/*
	 * device ip is default power-on in fact
	 * power down device ip, otherwise ip-sleep will fail
	 */
	value = readl(&ippc->ip_pw_ctr2);
	value |= CTRL2_IP_DEV_PDN;
	writel(value, &ippc->ip_pw_ctr2);

	value = readl(&ippc->ip_xhci_cap);
	mtk->num_u3_ports = CAP_U3_PORT_NUM(value);
	mtk->num_u2_ports = CAP_U2_PORT_NUM(value);
	dev_dbg(mtk->dev, "%s u2p:%d, u3p:%d\n", __func__,
			mtk->num_u2_ports, mtk->num_u3_ports);

	return xhci_mtk_host_enable(mtk);
}

/* only clocks can be turn off for ip-sleep wakeup mode */
static void usb_wakeup_ip_sleep_set(struct xhci_hcd_mtk *mtk, bool enable)
{
	u32 reg, msk, val;

	switch (mtk->uwk_vers) {
	case SSUSB_UWK_V1:
		reg = mtk->uwk_reg_base + PERI_WK_CTRL1;
		msk = WC1_IS_EN | WC1_IS_C(0xf) | WC1_IS_P;
		val = enable ? (WC1_IS_EN | WC1_IS_C(0x8)) : 0;
		break;
	case SSUSB_UWK_V1_1:
		reg = mtk->uwk_reg_base + PERI_WK_CTRL0;
		msk = WC0_IS_EN | WC0_IS_C(0xf) | WC0_IS_P;
		val = enable ? (WC0_IS_EN | WC0_IS_C(0x8)) : 0;
		break;
	case SSUSB_UWK_V1_2:
		reg = mtk->uwk_reg_base + PERI_WK_CTRL0;
		msk = WC0_SSUSB0_CDEN | WC0_IS_SPM_EN;
		val = enable ? msk : 0;
		break;
	case SSUSB_UWK_V2:
		reg = mtk->uwk_reg_base + PERI_SSUSB_SPM_CTRL;
		msk = SSC_IP_SLEEP_EN | SSC_SPM_INT_EN;
		val = enable ? msk : 0;
		break;
	default:
		return;
	}
	regmap_update_bits(mtk->uwk, reg, msk, val);
}

static int usb_wakeup_of_property_parse(struct xhci_hcd_mtk *mtk,
				struct device_node *dn)
{
	struct of_phandle_args args;
	int ret;

	/* Wakeup function is optional */
	mtk->uwk_en = of_property_read_bool(dn, "wakeup-source");
	if (!mtk->uwk_en)
		return 0;

	ret = of_parse_phandle_with_fixed_args(dn,
				"mediatek,syscon-wakeup", 2, 0, &args);
	if (ret)
		return ret;

	mtk->uwk_reg_base = args.args[0];
	mtk->uwk_vers = args.args[1];
	mtk->uwk = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	dev_info(mtk->dev, "uwk - reg:0x%x, version:%d\n",
			mtk->uwk_reg_base, mtk->uwk_vers);

	return PTR_ERR_OR_ZERO(mtk->uwk);
}

static void usb_wakeup_set(struct xhci_hcd_mtk *mtk, bool enable)
{
	if (mtk->uwk_en)
		usb_wakeup_ip_sleep_set(mtk, enable);
}

static int xhci_mtk_clks_get(struct xhci_hcd_mtk *mtk)
{
	struct clk_bulk_data *clks = mtk->clks;

	clks[0].id = "sys_ck";
	clks[1].id = "xhci_ck";
	clks[2].id = "ref_ck";
	clks[3].id = "mcu_ck";
	clks[4].id = "dma_ck";

	return devm_clk_bulk_get_optional(mtk->dev, BULK_CLKS_NUM, clks);
}

static int xhci_mtk_ldos_enable(struct xhci_hcd_mtk *mtk)
{
	int ret;

	ret = regulator_enable(mtk->vbus);
	if (ret) {
		dev_err(mtk->dev, "failed to enable vbus\n");
		return ret;
	}

	ret = regulator_enable(mtk->vusb33);
	if (ret) {
		dev_err(mtk->dev, "failed to enable vusb33\n");
		regulator_disable(mtk->vbus);
		return ret;
	}
	return 0;
}

static void xhci_mtk_ldos_disable(struct xhci_hcd_mtk *mtk)
{
	regulator_disable(mtk->vbus);
	regulator_disable(mtk->vusb33);
}

static void xhci_mtk_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	struct usb_hcd *hcd = xhci_to_hcd(xhci);
	struct xhci_hcd_mtk *mtk = hcd_to_mtk(hcd);

	/*
	 * As of now platform drivers don't provide MSI support so we ensure
	 * here that the generic code does not try to make a pci_dev from our
	 * dev struct in order to setup MSI
	 */
	xhci->quirks |= XHCI_PLAT;
	xhci->quirks |= XHCI_MTK_HOST;
	/*
	 * MTK host controller gives a spurious successful event after a
	 * short transfer. Ignore it.
	 */
	xhci->quirks |= XHCI_SPURIOUS_SUCCESS;
	if (mtk->lpm_support)
		xhci->quirks |= XHCI_LPM_SUPPORT;
	if (mtk->u2_lpm_disable)
		xhci->quirks |= XHCI_HW_LPM_DISABLE;

	/*
	 * MTK xHCI 0.96: PSA is 1 by default even if doesn't support stream,
	 * and it's 3 when support it.
	 */
	if (xhci->hci_version < 0x100 && HCC_MAX_PSA(xhci->hcc_params) == 4)
		xhci->quirks |= XHCI_BROKEN_STREAMS;
}

/* called during probe() after chip reset completes */
static int xhci_mtk_setup(struct usb_hcd *hcd)
{
	struct xhci_hcd_mtk *mtk = hcd_to_mtk(hcd);
	int ret;

	if (usb_hcd_is_primary_hcd(hcd)) {
		ret = xhci_mtk_ssusb_config(mtk);
		if (ret)
			return ret;

		xhci_mtk_init_quirk(mtk);
	}

	ret = xhci_gen_setup(hcd, xhci_mtk_quirks);
	if (ret)
		return ret;

	if (usb_hcd_is_primary_hcd(hcd)) {
		ret = xhci_mtk_sch_init(mtk);
		if (ret)
			return ret;
	}

	return ret;
}

static const struct xhci_driver_overrides xhci_mtk_overrides __initconst = {
	.reset = xhci_mtk_setup,
	.add_endpoint = xhci_mtk_add_ep,
	.drop_endpoint = xhci_mtk_drop_ep,
	.check_bandwidth = xhci_mtk_check_bandwidth,
	.reset_bandwidth = xhci_mtk_reset_bandwidth,
};

static struct hc_driver __read_mostly xhci_mtk_hc_driver;

static int xhci_mtk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct xhci_hcd_mtk *mtk;
	const struct hc_driver *driver;
	struct xhci_hcd *xhci;
	struct resource *res;
	struct usb_hcd *hcd;
	int ret = -ENODEV;
	int wakeup_irq;
	int irq;

	if (usb_disabled())
		return -ENODEV;

	driver = &xhci_mtk_hc_driver;
	mtk = devm_kzalloc(dev, sizeof(*mtk), GFP_KERNEL);
	if (!mtk)
		return -ENOMEM;

	mtk->dev = dev;
	mtk->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(mtk->vbus)) {
		dev_err(dev, "fail to get vbus\n");
		return PTR_ERR(mtk->vbus);
	}

	mtk->vusb33 = devm_regulator_get(dev, "vusb33");
	if (IS_ERR(mtk->vusb33)) {
		dev_err(dev, "fail to get vusb33\n");
		return PTR_ERR(mtk->vusb33);
	}

	ret = xhci_mtk_clks_get(mtk);
	if (ret)
		return ret;

	irq = platform_get_irq_byname_optional(pdev, "host");
	if (irq < 0) {
		if (irq == -EPROBE_DEFER)
			return irq;

		/* for backward compatibility */
		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;
	}

	wakeup_irq = platform_get_irq_byname_optional(pdev, "wakeup");
	if (wakeup_irq == -EPROBE_DEFER)
		return wakeup_irq;

	mtk->lpm_support = of_property_read_bool(node, "usb3-lpm-capable");
	mtk->u2_lpm_disable = of_property_read_bool(node, "usb2-lpm-disable");
	/* optional property, ignore the error if it does not exist */
	of_property_read_u32(node, "mediatek,u3p-dis-msk",
			     &mtk->u3p_dis_msk);
	of_property_read_u32(node, "mediatek,u2p-dis-msk",
			     &mtk->u2p_dis_msk);

	/* keep ref_ck on when suspend on some platform */
	mtk->keep_clk_on = of_property_read_bool(node, "mediatek,keep-clock-on");

	of_property_read_u32(node, "rx-fifo-depth", &mtk->rxfifo_depth);

	ret = usb_wakeup_of_property_parse(mtk, node);
	if (ret) {
		dev_err(dev, "failed to parse uwk property\n");
		return ret;
	}

	pm_runtime_set_active(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 4000);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	ret = xhci_mtk_ldos_enable(mtk);
	if (ret)
		goto disable_pm;

	ret = clk_bulk_prepare_enable(BULK_CLKS_NUM, mtk->clks);
	if (ret)
		goto disable_ldos;

	hcd = usb_create_hcd(driver, dev, dev_name(dev));
	if (!hcd) {
		ret = -ENOMEM;
		goto disable_clk;
	}

	/*
	 * USB 2.0 roothub is stored in the platform_device.
	 * Swap it with mtk HCD.
	 */
	mtk->hcd = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, mtk);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mac");
	hcd->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto put_usb2_hcd;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ippc");
	if (res) {	/* ippc register is optional */
		mtk->ippc_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(mtk->ippc_regs)) {
			ret = PTR_ERR(mtk->ippc_regs);
			goto put_usb2_hcd;
		}
		mtk->has_ippc = true;
	}

	device_init_wakeup(dev, true);
	dma_set_max_seg_size(dev, UINT_MAX);

	xhci = hcd_to_xhci(hcd);
	xhci->main_hcd = hcd;

	/*
	 * imod_interval is the interrupt moderation value in nanoseconds.
	 * The increment interval is 8 times as much as that defined in
	 * the xHCI spec on MTK's controller.
	 */
	xhci->imod_interval = 5000;
	device_property_read_u32(dev, "imod-interval-ns", &xhci->imod_interval);

	xhci->shared_hcd = usb_create_shared_hcd(driver, dev,
			dev_name(dev), hcd);
	if (!xhci->shared_hcd) {
		ret = -ENOMEM;
		goto disable_device_wakeup;
	}

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto put_usb3_hcd;

	if (HCC_MAX_PSA(xhci->hcc_params) >= 4 &&
	    !(xhci->quirks & XHCI_BROKEN_STREAMS))
		xhci->shared_hcd->can_do_streams = 1;

	ret = usb_add_hcd(xhci->shared_hcd, irq, IRQF_SHARED);
	if (ret)
		goto dealloc_usb2_hcd;

	if (wakeup_irq > 0) {
		ret = dev_pm_set_dedicated_wake_irq(dev, wakeup_irq);
		if (ret) {
			dev_err(dev, "set wakeup irq %d failed\n", wakeup_irq);
			goto dealloc_usb3_hcd;
		}
		dev_info(dev, "wakeup irq %d\n", wakeup_irq);
	}

	xhci_mtk_procfs_init(mtk);

	device_enable_async_suspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	pm_runtime_forbid(dev);

	return 0;

dealloc_usb3_hcd:
	usb_remove_hcd(xhci->shared_hcd);

dealloc_usb2_hcd:
	usb_remove_hcd(hcd);

put_usb3_hcd:
	xhci_mtk_sch_exit(mtk);
	usb_put_hcd(xhci->shared_hcd);

disable_device_wakeup:
	device_init_wakeup(dev, false);

put_usb2_hcd:
	usb_put_hcd(hcd);

disable_clk:
	clk_bulk_disable_unprepare(BULK_CLKS_NUM, mtk->clks);

disable_ldos:
	xhci_mtk_ldos_disable(mtk);

disable_pm:
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	return ret;
}

static int xhci_mtk_remove(struct platform_device *pdev)
{
	struct xhci_hcd_mtk *mtk = platform_get_drvdata(pdev);
	struct usb_hcd	*hcd = mtk->hcd;
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	struct usb_hcd  *shared_hcd = xhci->shared_hcd;
	struct device *dev = &pdev->dev;

	pm_runtime_get_sync(dev);
	xhci->xhc_state |= XHCI_STATE_REMOVING;
	dev_pm_clear_wake_irq(dev);
	device_init_wakeup(dev, false);

	usb_remove_hcd(shared_hcd);
	xhci->shared_hcd = NULL;
	usb_remove_hcd(hcd);
	usb_put_hcd(shared_hcd);
	usb_put_hcd(hcd);
	xhci_mtk_sch_exit(mtk);
	clk_bulk_disable_unprepare(BULK_CLKS_NUM, mtk->clks);
	xhci_mtk_ldos_disable(mtk);
	xhci_mtk_procfs_exit(mtk);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

	return 0;
}

static int __maybe_unused xhci_mtk_suspend(struct device *dev)
{
	struct xhci_hcd_mtk *mtk = dev_get_drvdata(dev);
	struct usb_hcd *hcd = mtk->hcd;
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	int ret;

	xhci_dbg(xhci, "%s: stop port polling\n", __func__);
	clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	del_timer_sync(&hcd->rh_timer);
	clear_bit(HCD_FLAG_POLL_RH, &xhci->shared_hcd->flags);
	del_timer_sync(&xhci->shared_hcd->rh_timer);

	ret = xhci_mtk_host_disable(mtk);
	if (ret)
		goto restart_poll_rh;

	if (!mtk->keep_clk_on)
		clk_bulk_disable_unprepare(BULK_CLKS_NUM, mtk->clks);
	usb_wakeup_set(mtk, true);
	return 0;

restart_poll_rh:
	xhci_dbg(xhci, "%s: restart port polling\n", __func__);
	set_bit(HCD_FLAG_POLL_RH, &xhci->shared_hcd->flags);
	usb_hcd_poll_rh_status(xhci->shared_hcd);
	set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	usb_hcd_poll_rh_status(hcd);
	return ret;
}

static int __maybe_unused xhci_mtk_resume(struct device *dev)
{
	struct xhci_hcd_mtk *mtk = dev_get_drvdata(dev);
	struct usb_hcd *hcd = mtk->hcd;
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	int ret;

	usb_wakeup_set(mtk, false);
	if (!mtk->keep_clk_on) {
		ret = clk_bulk_prepare_enable(BULK_CLKS_NUM, mtk->clks);
		if (ret)
			goto enable_wakeup;
	}

	ret = xhci_mtk_host_enable(mtk);
	if (ret)
		goto disable_clks;

	xhci_dbg(xhci, "%s: restart port polling\n", __func__);
	set_bit(HCD_FLAG_POLL_RH, &xhci->shared_hcd->flags);
	usb_hcd_poll_rh_status(xhci->shared_hcd);
	set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	usb_hcd_poll_rh_status(hcd);
	return 0;

disable_clks:
	clk_bulk_disable_unprepare(BULK_CLKS_NUM, mtk->clks);
enable_wakeup:
	usb_wakeup_set(mtk, true);
	return ret;
}

static int __maybe_unused xhci_mtk_runtime_suspend(struct device *dev)
{
	struct xhci_hcd_mtk  *mtk = dev_get_drvdata(dev);
	struct xhci_hcd *xhci = hcd_to_xhci(mtk->hcd);
	int ret = 0;

	if (xhci->xhc_state)
		return -ESHUTDOWN;

	if (device_may_wakeup(dev))
		ret = xhci_mtk_suspend(dev);

	/* -EBUSY: let PM automatically reschedule another autosuspend */
	return ret ? -EBUSY : 0;
}

static int __maybe_unused xhci_mtk_runtime_resume(struct device *dev)
{
	struct xhci_hcd_mtk  *mtk = dev_get_drvdata(dev);
	struct xhci_hcd *xhci = hcd_to_xhci(mtk->hcd);
	int ret = 0;

	if (xhci->xhc_state)
		return -ESHUTDOWN;

	if (device_may_wakeup(dev))
		ret = xhci_mtk_resume(dev);

	return ret;
}

static const struct dev_pm_ops xhci_mtk_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xhci_mtk_suspend, xhci_mtk_resume)
	SET_RUNTIME_PM_OPS(xhci_mtk_runtime_suspend,
			   xhci_mtk_runtime_resume, NULL)
};

#define DEV_PM_OPS (IS_ENABLED(CONFIG_PM) ? &xhci_mtk_pm_ops : NULL)


static const struct of_device_id mtk_xhci_p1_of_match[] = {
	{ .compatible = "mediatek,mtk-xhci-p1"},
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_xhci_p1_of_match);

static struct platform_driver mtk_xhci_p1_driver = {
	.probe	= xhci_mtk_probe,
	.remove	= xhci_mtk_remove,
	.driver	= {
		.name = "xhci-mtk-p1",
		.pm = DEV_PM_OPS,
		.of_match_table = mtk_xhci_p1_of_match,
	},
};

static const struct of_device_id mtk_xhci_of_match[] = {
	{ .compatible = "mediatek,mt8173-xhci"},
	{ .compatible = "mediatek,mt8195-xhci"},
	{ .compatible = "mediatek,mtk-xhci"},
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_xhci_of_match);

static struct platform_driver mtk_xhci_driver = {
	.probe	= xhci_mtk_probe,
	.remove	= xhci_mtk_remove,
	.driver	= {
		.name = "xhci-mtk",
		.pm = DEV_PM_OPS,
		.of_match_table = mtk_xhci_of_match,
	},
};

static int __init xhci_mtk_init(void)
{
	int ret;

	xhci_init_driver(&xhci_mtk_hc_driver, &xhci_mtk_overrides);
	ret = platform_driver_register(&mtk_xhci_p1_driver);
	if (ret < 0)
		return ret;
	return platform_driver_register(&mtk_xhci_driver);
}
module_init(xhci_mtk_init);

static void __exit xhci_mtk_exit(void)
{
	platform_driver_unregister(&mtk_xhci_p1_driver);
	platform_driver_unregister(&mtk_xhci_driver);
}
module_exit(xhci_mtk_exit);

MODULE_AUTHOR("Chunfeng Yun <chunfeng.yun@mediatek.com>");
MODULE_DESCRIPTION("MediaTek xHCI Host Controller Driver");
MODULE_LICENSE("GPL v2");
