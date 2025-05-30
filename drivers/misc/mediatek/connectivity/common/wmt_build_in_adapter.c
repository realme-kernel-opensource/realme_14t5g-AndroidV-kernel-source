// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#include <linux/kernel.h>

#include "wmt_build_in_adapter.h"
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/cdev.h>
#include <linux/sched/clock.h>
#include <linux/sysfs.h>
#include "conn_dbg.h"

/*device tree mode*/
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/irqreturn.h>
#include <linux/of_address.h>
#endif

#include <linux/interrupt.h>
#include <linux/ratelimit.h>

#define CONNADP_LOG_LOUD    4
#define CONNADP_LOG_DBG     3
#define CONNADP_LOG_INFO    2
#define CONNADP_LOG_WARN    1
#define CONNADP_LOG_ERR     0

/*******************************************************************************
 * Connsys adaptation layer logging utility
 ******************************************************************************/
static unsigned int gConnAdpDbgLvl = CONNADP_LOG_INFO;

#define CONNADP_LOUD_FUNC(fmt, arg...) \
do { \
	if (gConnAdpDbgLvl >= CONNADP_LOG_LOUD) \
		pr_info("[L]%s:"  fmt, __func__, ##arg); \
} while (0)
#define CONNADP_DBG_FUNC(fmt, arg...) \
do { \
	if (gConnAdpDbgLvl >= CONNADP_LOG_DBG) \
		pr_info("[D]%s:"  fmt, __func__, ##arg); \
} while (0)
#define CONNADP_INFO_FUNC(fmt, arg...)  \
do { \
	if (gConnAdpDbgLvl >= CONNADP_LOG_INFO) \
		pr_info("[I]%s:"  fmt, __func__, ##arg); \
} while (0)
#define CONNADP_WARN_FUNC(fmt, arg...) \
do { \
	if (gConnAdpDbgLvl >= CONNADP_LOG_WARN) \
		pr_info("[W]%s:"  fmt, __func__, ##arg); \
} while (0)
#define CONNADP_ERR_FUNC(fmt, arg...) \
do { \
	if (gConnAdpDbgLvl >= CONNADP_LOG_ERR) \
		pr_info("[E]%s(%d):"  fmt, __func__, __LINE__, ##arg); \
} while (0)

/* device node related macro */
#define CONN_DBG_DEV_NUM 1
#define CONN_DBG_DRVIER_NAME "conn_dbg_drv"
#define CONN_DBG_DEVICE_NAME "conn_dbg_dev"
#define CONN_DBG_DEV_MAJOR 156

struct conn_dbg_record {
	char *log;
	u64 num;
	u64 first_sec;
	unsigned long first_nsec;
	u64 last_sec;
	unsigned long last_nsec;
	struct conn_dbg_record *next;
};

/* device node related */
static int gConnDbgMajor = CONN_DBG_DEV_MAJOR;
static struct class *pConnDbgClass;
static struct device *pConnDbgDev;
static struct cdev gConnDbgdev;

/*******************************************************************************
 * Bridging from platform -> wmt_drv.ko
 ******************************************************************************/
static struct wmt_platform_bridge bridge;
static struct wmt_platform_dbg_bridge g_dbg_bridge;

#define CONN_DBG_LOG_BUF_SIZE PAGE_SIZE
#define CONN_DBG_MAX_LOG_LEN 128
#define CONN_DBG_MAX_REC_NUM 128
static spinlock_t conn_dbg_log_lock;
static struct conn_dbg_record *g_conn_dbg_record_p;
static int conn_dbg_record_num;

static void conn_dbg_dump_log(char *buf)
{
	struct conn_dbg_record *rec;
	char temp[64];
	unsigned long flag;

	buf[0] = '\0';
	buf[CONN_DBG_LOG_BUF_SIZE - 1] = '\0';
	spin_lock_irqsave(&conn_dbg_log_lock, flag);
	rec = g_conn_dbg_record_p;
	while(rec != NULL) {
		if (snprintf(temp, sizeof(temp), "[%llu.%06lu~%llu.%06lu][%llu]",
			rec->first_sec, rec->first_nsec,
			rec->last_sec, rec->last_nsec, rec->num) > 0) {
			strncat(buf, temp, CONN_DBG_LOG_BUF_SIZE - strlen(buf) - 1);
			strncat(buf, rec->log, CONN_DBG_LOG_BUF_SIZE - strlen(buf) - 1);
			strncat(buf, "\n", CONN_DBG_LOG_BUF_SIZE - strlen(buf) - 1);
		}
		rec = rec->next;
	}
	spin_unlock_irqrestore(&conn_dbg_log_lock, flag);
	buf[CONN_DBG_LOG_BUF_SIZE - 1] = '\0';
}

static ssize_t hw_monitor_show(
	struct kobject *kobj,
	struct kobj_attribute *attr,
	char *buf)
{
	conn_dbg_dump_log(buf);
	return strlen(buf);
}

static struct kobject *conn_kobj;
static struct kobj_attribute hw_monitor_attr
	= __ATTR(hw_monitor, 0664, hw_monitor_show, NULL);

static void conn_dbg_get_local_time(u64 *sec, unsigned long *nsec)
{
	if (sec != NULL && nsec != NULL) {
		*sec = local_clock();
		*nsec = do_div(*sec, 1000000000)/1000;
	} else
		pr_info("The input parameters error when get local time\n");
}

int conn_dbg_add_log(enum conn_dbg_log_type type, const char *buf)
{
	unsigned long flag;
	u64 sec;
	unsigned long nsec;
	struct conn_dbg_record *rec;
	int ret = 0;
	unsigned int len;
	char *envp[] = { "ATTR=hw_monitor", NULL };
	int notify = 0;

	if (type >= CONN_DBG_LOG_TYPE_NUM || buf == NULL || strnlen(buf, CONN_DBG_MAX_LOG_LEN) == 0) {
		pr_notice("%s type %d or buf %p is invalid\n", __func__, type, buf);
		return -1;
	}

	pr_info("%s type = %d, log = %s\n", __func__, type, buf);
	conn_dbg_get_local_time(&sec, &nsec);

	spin_lock_irqsave(&conn_dbg_log_lock, flag);
	/* search for an existing record */
	rec = g_conn_dbg_record_p;
	while (rec != NULL && rec->log != NULL) {
		if (strncmp(rec->log, buf, CONN_DBG_MAX_LOG_LEN) == 0)
			break;
		rec = rec->next;
	}

	/* create a new record for a new log */
	if (rec == NULL) {
		if (conn_dbg_record_num >= CONN_DBG_MAX_REC_NUM) {
			pr_notice("%s number of log record is over maximum.\n", __func__);
			ret = -3;
			goto exit;
		}
		rec = kmalloc(sizeof(struct conn_dbg_record), GFP_ATOMIC);
		if (rec == NULL) {
			ret = -4;
			pr_notice("%s failed to allocate record\n", __func__);
			goto exit;
		}

		len = strnlen(buf, CONN_DBG_MAX_LOG_LEN);
		rec->log = kmalloc(len + 1, GFP_ATOMIC);
		if (rec->log == NULL) {
			kfree(rec);
			ret = -5;
			pr_notice("%s failed to allocate log buffer\n", __func__);
			goto exit;
		}
		strncpy(rec->log, buf, len);
		rec->log[len] = '\0';
		rec->num = 0;
		rec->first_sec = sec;
		rec->first_nsec = nsec;
		rec->next = g_conn_dbg_record_p;
		g_conn_dbg_record_p = rec;
		conn_dbg_record_num++;
	}
	rec->num++;
	rec->last_sec = sec;
	rec->last_nsec = nsec;
	notify = 1;

exit:
	spin_unlock_irqrestore(&conn_dbg_log_lock, flag);
	if (notify && pConnDbgDev) {
		ret = kobject_uevent_env(&(pConnDbgDev->kobj), KOBJ_CHANGE, envp);
		pr_info("%s kobject_uevent_env ret = %d\n", __func__, ret);
	}
	return ret;
}
EXPORT_SYMBOL(conn_dbg_add_log);

static int conn_dbg_log_init(void)
{
	int ret;

	spin_lock_init(&conn_dbg_log_lock);

	conn_kobj = kobject_create_and_add("conn", NULL);
	kobject_get(conn_kobj);
	kobject_uevent(conn_kobj, KOBJ_ADD);

	ret = sysfs_create_file(conn_kobj, &hw_monitor_attr.attr);
	if (ret)
		pr_notice("%s sysfs_create_file failed. (%d)\n", __func__, ret);

	return ret;
}

static void conn_dbg_log_deinit(void)
{
	struct conn_dbg_record *rec;
	unsigned long flag;

	spin_lock_irqsave(&conn_dbg_log_lock, flag);
	rec = g_conn_dbg_record_p;
	while (rec != NULL) {
		g_conn_dbg_record_p = rec->next;
		if (rec->log != NULL)
			kfree(rec->log);
		kfree(rec);
		rec = g_conn_dbg_record_p;
	}
	conn_dbg_record_num = 0;
	spin_unlock_irqrestore(&conn_dbg_log_lock, flag);

	sysfs_remove_file(conn_kobj, &hw_monitor_attr.attr);
	kobject_put(conn_kobj);
	conn_kobj = NULL;
}

ssize_t conn_dbg_dev_write(struct file *filp, const char __user *buffer,
				size_t count, loff_t *f_pos)
{
	if (g_dbg_bridge.write_cb)
		return g_dbg_bridge.write_cb(filp, buffer, count, f_pos);

	return 0;
}

ssize_t conn_dbg_dev_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *f_pos)
{
	ssize_t ret = 0;

	if (g_dbg_bridge.read_cb)
		ret = g_dbg_bridge.read_cb(filp, buffer, count, f_pos);

	return ret;
}

static const struct file_operations gConnDbgDevFops = {
	.read = conn_dbg_dev_read,
	.write = conn_dbg_dev_write,
};

static int conn_dbg_dev_init(void)
{
	dev_t dev_id = MKDEV(gConnDbgMajor, 0);
	int ret = 0;

	ret = register_chrdev_region(dev_id, CONN_DBG_DEV_NUM, CONN_DBG_DRVIER_NAME);
	if (ret) {
		pr_info("%s fail to register chrdev.(%d)\n", __func__, ret);
		return -1;
	}

	cdev_init(&gConnDbgdev, &gConnDbgDevFops);
	gConnDbgdev.owner = THIS_MODULE;

	ret = cdev_add(&gConnDbgdev, dev_id, CONN_DBG_DEV_NUM);
	if (ret) {
		pr_info("cdev_add() fails (%d)\n", ret);
		goto err1;
	}

	pConnDbgClass = class_create(THIS_MODULE, CONN_DBG_DEVICE_NAME);
	if (IS_ERR(pConnDbgClass)) {
		pr_info("class create fail, error code(%ld)\n", PTR_ERR(pConnDbgClass));
		goto err2;
	}

	pConnDbgDev = device_create(pConnDbgClass, NULL, dev_id, NULL, CONN_DBG_DEVICE_NAME);
	if (IS_ERR(pConnDbgDev)) {
		pr_info("device create fail, error code(%ld)\n", PTR_ERR(pConnDbgDev));
		goto err3;
	}

	return 0;
err3:

	pr_info("[%s] err3", __func__);
	if (pConnDbgClass) {
		class_destroy(pConnDbgClass);
		pConnDbgClass = NULL;
	}
err2:
	pr_info("[%s] err2", __func__);
	cdev_del(&gConnDbgdev);

err1:
	pr_info("[%s] err1", __func__);
	unregister_chrdev_region(dev_id, CONN_DBG_DEV_NUM);

	return -1;
}

static int conn_dbg_dev_deinit(void)
{
	dev_t dev_id = MKDEV(gConnDbgMajor, 0);

	if (pConnDbgDev) {
		device_destroy(pConnDbgClass, dev_id);
		pConnDbgDev = NULL;
	}

	if (pConnDbgClass) {
		class_destroy(pConnDbgClass);
		pConnDbgClass = NULL;
	}

	cdev_del(&gConnDbgdev);
	unregister_chrdev_region(dev_id, CONN_DBG_DEV_NUM);

	return 0;
}

void wmt_export_platform_bridge_register(struct wmt_platform_bridge *cb)
{
	if (unlikely(!cb))
		return;
	bridge.thermal_query_cb = cb->thermal_query_cb;
	bridge.trigger_assert_cb = cb->trigger_assert_cb;
	bridge.clock_fail_dump_cb = cb->clock_fail_dump_cb;
	bridge.conninfra_reg_readable_cb = cb->conninfra_reg_readable_cb;
	bridge.conninfra_reg_is_bus_hang_cb = cb->conninfra_reg_is_bus_hang_cb;

	conn_dbg_log_init();
	CONNADP_INFO_FUNC("\n");
}
EXPORT_SYMBOL(wmt_export_platform_bridge_register);

void wmt_export_platform_bridge_unregister(void)
{
	memset(&bridge, 0, sizeof(struct wmt_platform_bridge));
	CONNADP_INFO_FUNC("\n");
}
EXPORT_SYMBOL(wmt_export_platform_bridge_unregister);

void wmt_export_platform_dbg_bridge_register(const struct wmt_platform_dbg_bridge *cb)
{
	if (unlikely(!cb))
		return;
	if (cb->write_cb != NULL && cb->read_cb != NULL) {
		g_dbg_bridge.write_cb = cb->write_cb;
		g_dbg_bridge.read_cb = cb->read_cb;
		conn_dbg_dev_init();
	}
}
EXPORT_SYMBOL(wmt_export_platform_dbg_bridge_register);

void wmt_export_platform_dbg_bridge_unregister(void)
{
	if (g_dbg_bridge.write_cb && g_dbg_bridge.read_cb)
		conn_dbg_dev_deinit();
	memset(&g_dbg_bridge, 0, sizeof(struct wmt_platform_dbg_bridge));
	conn_dbg_log_deinit();
}
EXPORT_SYMBOL(wmt_export_platform_dbg_bridge_unregister);

int mtk_wcn_cmb_stub_query_ctrl(void)
{
	CONNADP_DBG_FUNC("\n");
	if (unlikely(!bridge.thermal_query_cb)) {
		CONNADP_WARN_FUNC("Thermal query not registered\n");
		return -1;
	} else
		return bridge.thermal_query_cb();
}

int mtk_wcn_cmb_stub_trigger_assert(void)
{
	CONNADP_DBG_FUNC("\n");
	/* dump backtrace for checking assert reason */
	dump_stack();
	if (unlikely(!bridge.trigger_assert_cb)) {
		CONNADP_WARN_FUNC("Trigger assert not registered\n");
		return -1;
	} else
		return bridge.trigger_assert_cb();
}

void mtk_wcn_cmb_stub_clock_fail_dump(void)
{
	CONNADP_DBG_FUNC("\n");
	if (unlikely(!bridge.clock_fail_dump_cb))
		CONNADP_WARN_FUNC("Clock fail dump not registered\n");
	else
		bridge.clock_fail_dump_cb();
}

int mtk_wcn_conninfra_reg_readable(void)
{
	static DEFINE_RATELIMIT_STATE(_rs, 5*HZ, 1);

	if (unlikely(!bridge.conninfra_reg_readable_cb)) {
		if (__ratelimit(&_rs))
			CONNADP_WARN_FUNC("reg_readable not registered\n");
		return -1;
	} else
		return bridge.conninfra_reg_readable_cb();
}

int mtk_wcn_conninfra_is_bus_hang(void)
{
	static DEFINE_RATELIMIT_STATE(_rs, 5*HZ, 1);

	if (unlikely(!bridge.conninfra_reg_is_bus_hang_cb)) {
		if (__ratelimit(&_rs))
			CONNADP_WARN_FUNC("is_bus_hang not registered\n");
		return -1;
	} else
		return bridge.conninfra_reg_is_bus_hang_cb();
}

/*******************************************************************************
 * SDIO integration with platform MMC driver
 ******************************************************************************/
static void mtk_wcn_cmb_sdio_request_eirq(msdc_sdio_irq_handler_t irq_handler,
					  void *data);
static void mtk_wcn_cmb_sdio_enable_eirq(void);
static void mtk_wcn_cmb_sdio_disable_eirq(void);
static void mtk_wcn_cmb_sdio_register_pm(pm_callback_t pm_cb, void *data);

struct sdio_ops mt_sdio_ops[4] = {
	{NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL},
	{mtk_wcn_cmb_sdio_request_eirq, mtk_wcn_cmb_sdio_enable_eirq,
		mtk_wcn_cmb_sdio_disable_eirq, mtk_wcn_cmb_sdio_register_pm},
	{mtk_wcn_cmb_sdio_request_eirq, mtk_wcn_cmb_sdio_enable_eirq,
		mtk_wcn_cmb_sdio_disable_eirq, mtk_wcn_cmb_sdio_register_pm}
};

static atomic_t sdio_claim_irq_enable_flag;
static atomic_t irq_enable_flag;

static msdc_sdio_irq_handler_t mtk_wcn_cmb_sdio_eirq_handler;
static void *mtk_wcn_cmb_sdio_eirq_data;

unsigned int wifi_irq = 0xffffffff;
EXPORT_SYMBOL(wifi_irq);

pm_callback_t mtk_wcn_cmb_sdio_pm_cb;
EXPORT_SYMBOL(mtk_wcn_cmb_sdio_pm_cb);

void *mtk_wcn_cmb_sdio_pm_data;
EXPORT_SYMBOL(mtk_wcn_cmb_sdio_pm_data);

static int _mtk_wcn_sdio_irq_flag_set(int flag)
{
	if (flag != 0)
		atomic_set(&sdio_claim_irq_enable_flag, 1);
	else
		atomic_set(&sdio_claim_irq_enable_flag, 0);

	CONNADP_DBG_FUNC("sdio_claim_irq_enable_flag:%d\n",
			atomic_read(&sdio_claim_irq_enable_flag));

	return atomic_read(&sdio_claim_irq_enable_flag);
}

int wmt_export_mtk_wcn_sdio_irq_flag_set(int flag)
{
	return _mtk_wcn_sdio_irq_flag_set(flag);
}
EXPORT_SYMBOL(wmt_export_mtk_wcn_sdio_irq_flag_set);

static irqreturn_t mtk_wcn_cmb_sdio_eirq_handler_stub(int irq, void *data)
{
	if ((mtk_wcn_cmb_sdio_eirq_handler != NULL) &&
	    (atomic_read(&sdio_claim_irq_enable_flag) != 0))
		mtk_wcn_cmb_sdio_eirq_handler(mtk_wcn_cmb_sdio_eirq_data);
	return IRQ_HANDLED;
}

static void mtk_wcn_cmb_sdio_request_eirq(msdc_sdio_irq_handler_t irq_handler,
					  void *data)
{
#ifdef CONFIG_OF
	struct device_node *node;
	int ret = -EINVAL;

	CONNADP_INFO_FUNC("enter\n");
	_mtk_wcn_sdio_irq_flag_set(0);
	atomic_set(&irq_enable_flag, 1);
	mtk_wcn_cmb_sdio_eirq_data = data;
	mtk_wcn_cmb_sdio_eirq_handler = irq_handler;

	node = (struct device_node *)of_find_compatible_node(NULL, NULL,
					"mediatek,connectivity-combo");
	if (node) {
		wifi_irq = irq_of_parse_and_map(node, 0);/* get wifi eint num */
		ret = request_irq(wifi_irq, mtk_wcn_cmb_sdio_eirq_handler_stub,
				IRQF_TRIGGER_LOW, "WIFI-eint", NULL);
		CONNADP_DBG_FUNC("WIFI EINT irq %d !!\n", wifi_irq);

		if (ret)
			CONNADP_WARN_FUNC("WIFI EINT LINE NOT AVAILABLE!!\n");
		else
			mtk_wcn_cmb_sdio_disable_eirq();/*state:power off*/
	} else
		CONNADP_WARN_FUNC("can't find connectivity compatible node\n");

	CONNADP_INFO_FUNC("exit\n");
#else
	CONNADP_ERR_FUNC("not implemented\n");
#endif
}

static void mtk_wcn_cmb_sdio_register_pm(pm_callback_t pm_cb, void *data)
{
	CONNADP_DBG_FUNC("cmb_sdio_register_pm (0x%p, 0x%p)\n", pm_cb, data);
	/* register pm change callback */
	mtk_wcn_cmb_sdio_pm_cb = pm_cb;
	mtk_wcn_cmb_sdio_pm_data = data;
}

static void mtk_wcn_cmb_sdio_enable_eirq(void)
{
	if (atomic_read(&irq_enable_flag))
		CONNADP_DBG_FUNC("wifi eint has been enabled\n");
	else {
		atomic_set(&irq_enable_flag, 1);
		if (wifi_irq != 0xfffffff) {
			enable_irq(wifi_irq);
			CONNADP_DBG_FUNC(" enable WIFI EINT %d!\n", wifi_irq);
		}
	}
}

static void mtk_wcn_cmb_sdio_disable_eirq(void)
{
	if (!atomic_read(&irq_enable_flag))
		CONNADP_DBG_FUNC("wifi eint has been disabled!\n");
	else {
		if (wifi_irq != 0xfffffff) {
			disable_irq_nosync(wifi_irq);
			CONNADP_DBG_FUNC("disable WIFI EINT %d!\n", wifi_irq);
		}
		atomic_set(&irq_enable_flag, 0);
	}
}

void wmt_export_mtk_wcn_cmb_sdio_disable_eirq(void)
{
	mtk_wcn_cmb_sdio_disable_eirq();
}
EXPORT_SYMBOL(wmt_export_mtk_wcn_cmb_sdio_disable_eirq);
