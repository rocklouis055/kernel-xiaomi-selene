// SPDX-License-Identifier: GPL-2.0
/*
 * FPC1022 fingerprint sensor driver for selene (Redmi 10), ported from the
 * selene 4.14 fpc1022_tee.c.
 *
 * The sensor is driven by the FPC trusted app in the Microtrust TEE; this
 * driver only checks that the sensor is present and exposes reset, SPI clock
 * and IRQ controls in sysfs on the "fingerprint" platform device, which is
 * where the vendor fingerprint HAL (fpc_tac) looks for them.
 *
 * On selene the SPI device node is shared with Goodix sensors
 * ("goodix,goodix-fp" on spi2). The Goodix driver is a 4.14 binary blob, so
 * only FPC units are supported here.
 */

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/hqsysfs.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "teei_fp.h"
#include "tee_client_api.h"

#define FPC1022_RESET_LOW_US		5000
#define FPC1022_RESET_HIGH1_US		100
#define FPC_TTW_HOLD_TIME		2000
#define FP_UNLOCK_REJECTION_TIMEOUT	1500

#define FPC102X_REG_HWID		252
#define FPC1022_CHIP			0x1000
#define FPC1022_CHIP_MASK_SENSOR_TYPE	0xf000
#define FPC_SPI_SPEED_HZ		1000000

extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
extern int mdss_prim_panel_fb_unblank(int timeout);

static struct TEEC_UUID uuid_ta_fpc = { 0x7778c03f, 0xc30c, 0x4dd0,
	{ 0xa3, 0x19, 0xea, 0x29, 0x64, 0x3d, 0x4d, 0x4b }
};

struct fpc1022_data {
	struct device *dev;
	struct spi_device *spi;
	int irq_gpio;
	int irq_num;
	struct pinctrl *pinctrl;
	struct pinctrl_state *st_rst_l;
	struct pinctrl_state *st_rst_h;
	struct input_dev *idev;
	char idev_name[32];
	bool wakeup_enabled;
	struct wakeup_source *ttw_wl;
	struct notifier_block fb_notifier;
	bool fb_black;
	bool wait_finger_down;
	struct work_struct work;
};

static struct spi_device *fpc_spi;
static bool fpc1022_fp_exist;

static void hw_reset(struct fpc1022_data *fpc)
{
	pinctrl_select_state(fpc->pinctrl, fpc->st_rst_h);
	usleep_range(FPC1022_RESET_HIGH1_US, FPC1022_RESET_HIGH1_US + 100);
	pinctrl_select_state(fpc->pinctrl, fpc->st_rst_l);
	usleep_range(FPC1022_RESET_LOW_US, FPC1022_RESET_LOW_US + 100);
	pinctrl_select_state(fpc->pinctrl, fpc->st_rst_h);
	usleep_range(FPC1022_RESET_HIGH1_US, FPC1022_RESET_HIGH1_US + 100);

	dev_info(fpc->dev, "IRQ after reset %d\n",
		 gpio_get_value(fpc->irq_gpio));
}

static int spi_read_hwid(struct spi_device *spi, u16 *hwid)
{
	u8 tx[3] = { FPC102X_REG_HWID, 0, 0 };
	u8 rx[2] = { 0 };
	struct spi_transfer xfer[2] = {
		{
			.tx_buf = tx,
			.len = 1,
			.delay_usecs = 5,
			.speed_hz = FPC_SPI_SPEED_HZ,
		}, {
			.tx_buf = tx + 1,
			.rx_buf = rx,
			.len = 2,
			.delay_usecs = 5,
			.speed_hz = FPC_SPI_SPEED_HZ,
		},
	};
	int ret;

	ret = spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));
	if (ret)
		return ret;

	*hwid = (rx[0] << 8) | rx[1];
	return 0;
}

static int check_hwid(struct fpc1022_data *fpc)
{
	u16 hwid = 0;
	int i, ret = -ENODEV;

	mt_spi_enable_master_clk(fpc->spi);
	for (i = 0; i < 2; i++) {
		if (spi_read_hwid(fpc->spi, &hwid))
			continue;
		if ((hwid & FPC1022_CHIP_MASK_SENSOR_TYPE) == FPC1022_CHIP) {
			ret = 0;
			break;
		}
	}
	mt_spi_disable_master_clk(fpc->spi);

	if (ret)
		dev_info(fpc->dev, "no FPC1022 sensor (hwid 0x%04x)\n", hwid);
	else
		dev_info(fpc->dev, "FPC1022 detected, hwid 0x%04x\n", hwid);
	return ret;
}

static ssize_t hw_reset_set(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fpc1022_data *fpc = dev_get_drvdata(dev);

	if (strncmp(buf, "reset", strlen("reset")))
		return -EINVAL;
	hw_reset(fpc);
	return count;
}
static DEVICE_ATTR(hw_reset, 0200, NULL, hw_reset_set);

static ssize_t wakeup_enable_set(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct fpc1022_data *fpc = dev_get_drvdata(dev);

	if (!strncmp(buf, "enable", strlen("enable")))
		fpc->wakeup_enabled = true;
	else if (!strncmp(buf, "disable", strlen("disable")))
		fpc->wakeup_enabled = false;
	else
		return -EINVAL;
	smp_wmb();
	return count;
}
static DEVICE_ATTR(wakeup_enable, 0200, NULL, wakeup_enable_set);

static ssize_t do_wakeup_set(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct fpc1022_data *fpc = dev_get_drvdata(dev);

	if (!count)
		return -EINVAL;
	input_report_key(fpc->idev, KEY_POWER, 1);
	input_report_key(fpc->idev, KEY_POWER, 0);
	input_sync(fpc->idev);
	return count;
}
static DEVICE_ATTR(do_wakeup, 0200, NULL, do_wakeup_set);

/* Like stock, returns 1 rather than count; the HAL only writes one byte. */
static ssize_t clk_enable_set(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fpc1022_data *fpc = dev_get_drvdata(dev);

	if (*buf == '1')
		mt_spi_enable_master_clk(fpc->spi);
	else if (*buf == '0')
		mt_spi_disable_master_clk(fpc->spi);
	return 1;
}
static DEVICE_ATTR(clk_enable, 0200, NULL, clk_enable_set);

static ssize_t irq_get(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct fpc1022_data *fpc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%i\n", gpio_get_value(fpc->irq_gpio));
}

static ssize_t irq_ack(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	return count;
}
static DEVICE_ATTR(irq, 0600, irq_get, irq_ack);

static ssize_t fpid_get(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%i\n", fpc1022_fp_exist);
}
static DEVICE_ATTR(fpid_get, 0400, fpid_get, NULL);

static ssize_t fingerdown_wait_set(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct fpc1022_data *fpc = dev_get_drvdata(dev);

	if (!strncmp(buf, "enable", strlen("enable")))
		fpc->wait_finger_down = true;
	else if (!strncmp(buf, "disable", strlen("disable")))
		fpc->wait_finger_down = false;
	else
		return -EINVAL;
	return count;
}
static DEVICE_ATTR(fingerdown_wait, 0200, NULL, fingerdown_wait_set);

static struct attribute *fpc1022_attributes[] = {
	&dev_attr_hw_reset.attr,
	&dev_attr_wakeup_enable.attr,
	&dev_attr_do_wakeup.attr,
	&dev_attr_clk_enable.attr,
	&dev_attr_irq.attr,
	&dev_attr_fpid_get.attr,
	&dev_attr_fingerdown_wait.attr,
	NULL
};

static const struct attribute_group fpc1022_attribute_group = {
	.attrs = fpc1022_attributes,
};

/* Finger down while the screen is off: unblank early to speed up unlock. */
static void notification_work(struct work_struct *work)
{
	mdss_prim_panel_fb_unblank(FP_UNLOCK_REJECTION_TIMEOUT);
}

static irqreturn_t fpc1022_irq_handler(int irq, void *handle)
{
	struct fpc1022_data *fpc = handle;

	smp_rmb();
	__pm_wakeup_event(fpc->ttw_wl, FPC_TTW_HOLD_TIME);

	if (fpc->wait_finger_down && fpc->fb_black) {
		fpc->wait_finger_down = false;
		schedule_work(&fpc->work);
	}

	sysfs_notify(&fpc->dev->kobj, NULL, dev_attr_irq.attr.name);
	return IRQ_HANDLED;
}

static int fpc_fb_notifier_callback(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	struct fpc1022_data *fpc = container_of(nb, struct fpc1022_data,
						fb_notifier);
	struct fb_event *evdata = data;

	if (event != FB_EVENT_BLANK || !evdata || !evdata->data)
		return 0;

	switch (*(int *)evdata->data) {
	case FB_BLANK_POWERDOWN:
		fpc->fb_black = true;
		break;
	case FB_BLANK_UNBLANK:
		fpc->fb_black = false;
		break;
	}
	return 0;
}

static int fpc1022_platform_probe(struct platform_device *pldev)
{
	struct device *dev = &pldev->dev;
	struct device_node *np = dev->of_node;
	struct pinctrl_state *st;
	struct fpc1022_data *fpc;
	int irqf, ret;

	if (!fpc_spi)
		return -EPROBE_DEFER;

	fpc = devm_kzalloc(dev, sizeof(*fpc), GFP_KERNEL);
	if (!fpc)
		return -ENOMEM;

	fpc->dev = dev;
	fpc->spi = fpc_spi;
	dev_set_drvdata(dev, fpc);

	fpc->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(fpc->pinctrl))
		return PTR_ERR(fpc->pinctrl);

	fpc->st_rst_h = pinctrl_lookup_state(fpc->pinctrl, "reset_high");
	fpc->st_rst_l = pinctrl_lookup_state(fpc->pinctrl, "reset_low");
	if (IS_ERR(fpc->st_rst_h) || IS_ERR(fpc->st_rst_l)) {
		dev_err(dev, "missing reset pinctrl states\n");
		return -EINVAL;
	}
	/* "default" is the IRQ pin, "spi_mode" muxes the SPI2 pins */
	st = pinctrl_lookup_state(fpc->pinctrl, "spi_mode");
	if (!IS_ERR(st))
		pinctrl_select_state(fpc->pinctrl, st);

	fpc->irq_gpio = of_get_named_gpio(np, "fpc,gpio_irq", 0);
	if (!gpio_is_valid(fpc->irq_gpio)) {
		dev_err(dev, "invalid fpc,gpio_irq\n");
		return -EINVAL;
	}
	fpc->irq_num = irq_of_parse_and_map(np, 0);
	if (!fpc->irq_num)
		fpc->irq_num = gpio_to_irq(fpc->irq_gpio);

	hw_reset(fpc);
	ret = check_hwid(fpc);
	if (ret)
		return ret;
	fpc1022_fp_exist = true;
	memcpy(&uuid_fp, &uuid_ta_fpc, sizeof(uuid_fp));

	fpc->idev = devm_input_allocate_device(dev);
	if (!fpc->idev)
		return -ENOMEM;
	snprintf(fpc->idev_name, sizeof(fpc->idev_name), "fpc_irq@%s",
		 dev_name(dev));
	fpc->idev->name = fpc->idev_name;
	input_set_capability(fpc->idev, EV_MSC, MSC_SCAN);
	input_set_capability(fpc->idev, EV_KEY, KEY_WAKEUP);
	input_set_capability(fpc->idev, EV_KEY, KEY_POWER);
	ret = input_register_device(fpc->idev);
	if (ret)
		return ret;

	fpc->ttw_wl = wakeup_source_register(dev, "fpc_ttw_wl");
	if (!fpc->ttw_wl)
		return -ENOMEM;
	INIT_WORK(&fpc->work, notification_work);

	irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
	if (of_property_read_bool(fpc_spi->dev.of_node, "fpc,enable-wakeup")) {
		irqf |= IRQF_NO_SUSPEND;
		device_init_wakeup(dev, true);
	}
	ret = devm_request_threaded_irq(dev, fpc->irq_num, NULL,
					fpc1022_irq_handler, irqf,
					dev_name(dev), fpc);
	if (ret) {
		dev_err(dev, "could not request irq %d\n", fpc->irq_num);
		goto err_wl;
	}
	enable_irq_wake(fpc->irq_num);

	fpc->fb_notifier.notifier_call = fpc_fb_notifier_callback;
	fb_register_client(&fpc->fb_notifier);

	ret = sysfs_create_group(&dev->kobj, &fpc1022_attribute_group);
	if (ret)
		goto err_fb;

	hq_regiser_hw_info(HWID_FP, "FPC");
	dev_info(dev, "FPC1022 ready, irq %d (gpio %d)\n", fpc->irq_num,
		 fpc->irq_gpio);
	return 0;

err_fb:
	fb_unregister_client(&fpc->fb_notifier);
err_wl:
	wakeup_source_unregister(fpc->ttw_wl);
	return ret;
}

static int fpc1022_platform_remove(struct platform_device *pldev)
{
	struct fpc1022_data *fpc = dev_get_drvdata(&pldev->dev);

	sysfs_remove_group(&pldev->dev.kobj, &fpc1022_attribute_group);
	fb_unregister_client(&fpc->fb_notifier);
	cancel_work_sync(&fpc->work);
	wakeup_source_unregister(fpc->ttw_wl);
	return 0;
}

static const struct of_device_id fpc1022_of_match[] = {
	{ .compatible = "mediatek,fpc1022_irq", },
	{}
};
MODULE_DEVICE_TABLE(of, fpc1022_of_match);

static struct platform_driver fpc1022_platform_driver = {
	.driver = {
		.name = "fpc1022_irq",
		.of_match_table = fpc1022_of_match,
	},
	.probe = fpc1022_platform_probe,
	.remove = fpc1022_platform_remove,
};

/* The SPI side only hands the spi_device over to the platform driver. */
static int fpc1022_spi_probe(struct spi_device *spi)
{
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	fpc_spi = spi;
	return 0;
}

static int fpc1022_spi_remove(struct spi_device *spi)
{
	fpc_spi = NULL;
	return 0;
}

static const struct of_device_id fpc1022_spi_of_match[] = {
	{ .compatible = "goodix,goodix-fp", },
	{}
};
MODULE_DEVICE_TABLE(of, fpc1022_spi_of_match);

static struct spi_driver fpc1022_spi_driver = {
	.driver = {
		.name = "fpc1022_spi",
		.of_match_table = fpc1022_spi_of_match,
	},
	.probe = fpc1022_spi_probe,
	.remove = fpc1022_spi_remove,
};

static int __init fpc1022_init(void)
{
	int ret;

	ret = spi_register_driver(&fpc1022_spi_driver);
	if (ret)
		return ret;
	ret = platform_driver_register(&fpc1022_platform_driver);
	if (ret)
		spi_unregister_driver(&fpc1022_spi_driver);
	return ret;
}
late_initcall(fpc1022_init);

static void __exit fpc1022_exit(void)
{
	platform_driver_unregister(&fpc1022_platform_driver);
	spi_unregister_driver(&fpc1022_spi_driver);
}
module_exit(fpc1022_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("FPC1022 fingerprint sensor driver for selene");
