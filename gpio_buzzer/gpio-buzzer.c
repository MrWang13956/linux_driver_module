/*
 * buzzers driver for GPIOs
 *
 * Copyright (C) 2021 weiye.
 * Raphael Assenat <wwytxjy@163.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>


#define MISCBUZZER_NAME 			"buzzer"
#define BUZZEROFF 					0
#define BUZZERON 					1
#define GPIO_OUTPUT 				1
#define GPIO_INPUT 					0
#define GPIO_HIGH 					1
#define GPIO_LOW 					0
#define DEV_BUSY 					1
#define DEV_FREE 					0


struct miscbuzzer_dev {
	struct device *device;
	int buzzer_gpio;
	int buzzer_stats;
	int dev_stats;
	spinlock_t lock;
};

struct miscbuzzer_dev miscbuzzer;

static int miscbuzzer_open(struct inode *inode, struct file *filp)
{
	unsigned long flags;
	int ret = 0;

	filp->private_data = &miscbuzzer;

	spin_lock_irqsave(&miscbuzzer.lock, flags);
	if (miscbuzzer.dev_stats == DEV_BUSY) {
		ret = -EBUSY;
		goto unlock;
	}
	miscbuzzer.dev_stats = DEV_BUSY;

unlock:
	spin_unlock_irqrestore(&miscbuzzer.lock, flags);

	return ret;
}

static ssize_t miscbuzzer_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	int retvalue;
	unsigned char databuf[1];
	unsigned char buzzerstat;
	struct miscbuzzer_dev *dev = filp->private_data;

	retvalue = copy_from_user(databuf, buf, cnt);
	if (retvalue < 0) {
		dev_err(dev->device, "miscbuzzer_write failed!\n");
		return -EFAULT;
	}

	buzzerstat = databuf[0];
	if (buzzerstat == BUZZERON) {
		gpio_set_value(dev->buzzer_gpio, GPIO_LOW);
		dev->buzzer_stats = BUZZERON;
	} else if (buzzerstat == BUZZEROFF) {
		gpio_set_value(dev->buzzer_gpio, GPIO_HIGH);
		dev->buzzer_stats = BUZZEROFF;
	}

	return 0;
}

static int miscbuzzer_release(struct inode *inode, struct file *filp)
{
	unsigned long flags;
	struct miscbuzzer_dev *dev = filp->private_data;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->dev_stats == DEV_BUSY) {
		dev->dev_stats = DEV_FREE;
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

static struct file_operations miscbuzzer_fops = {
	.owner = THIS_MODULE,
	.open = miscbuzzer_open,
	.write = miscbuzzer_write,
	.release = miscbuzzer_release,
};

static struct miscdevice buzzer_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MISCBUZZER_NAME,
	.fops = &miscbuzzer_fops,
};

static ssize_t buzzer_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", miscbuzzer.buzzer_stats);
}

static ssize_t buzzer_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long flags;
	unsigned long state;
	ssize_t ret;

	spin_lock_irqsave(&miscbuzzer.lock, flags);

	if (miscbuzzer.dev_stats == DEV_BUSY) {
		ret = -EBUSY;
		goto unlock;
	}
	miscbuzzer.dev_stats = DEV_BUSY;

	ret = kstrtoul(buf, 10, &state);

	if (state == BUZZERON) {
		gpio_set_value(miscbuzzer.buzzer_gpio, GPIO_LOW);
		miscbuzzer.buzzer_stats = BUZZERON;
	} else if (state == BUZZEROFF) {
		gpio_set_value(miscbuzzer.buzzer_gpio, GPIO_HIGH);
		miscbuzzer.buzzer_stats = BUZZEROFF;
	}

	miscbuzzer.dev_stats = DEV_FREE;

	ret = size;

unlock:
	spin_unlock_irqrestore(&miscbuzzer.lock, flags);

	return ret;
}

static struct device_attribute buzzer_addr = {
	.attr = {
		.name = "buzz",
		.mode = 0666,
	},
	.show = buzzer_show,
	.store = buzzer_store,
};

static const struct of_device_id of_gpio_buzzer_match[] = {
	{ .compatible = "gpio-buzzer", },
	{},
};

MODULE_DEVICE_TABLE(of, of_gpio_buzzer_match);

static int gpio_buzzer_probe(struct platform_device *pdev)
{
	const char *state = NULL;
	int ret;

	miscbuzzer.buzzer_gpio = of_get_named_gpio(pdev->dev.of_node, "gpios", 0);
	if (miscbuzzer.buzzer_gpio < 0) {
		dev_err(&pdev->dev, "failed get buzzer gpio!\n");
		return -EINVAL;
	}

	ret = misc_register(&buzzer_miscdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "misc device register failed!\n");
		return -EFAULT;
	}

	ret = device_create_file(buzzer_miscdev.this_device, &buzzer_addr);
	if (ret) {
		dev_err(&pdev->dev, "Unable to create sysfs entry: '%s'\n", buzzer_addr.attr.name);
		return -EFAULT;
	}

	miscbuzzer.dev_stats = DEV_FREE;
	spin_lock_init(&miscbuzzer.lock);

	ret = gpio_direction_output(miscbuzzer.buzzer_gpio, GPIO_OUTPUT);
	if (ret) {
		dev_err(&pdev->dev, "failed set buzzer gpio!\n");
		return -EFAULT;
	}

	if(!of_property_read_string(pdev->dev.of_node, "default-state", &state)) {
		if (!strcmp(state, "on"))
			gpio_set_value(miscbuzzer.buzzer_gpio, GPIO_LOW);
		else
			gpio_set_value(miscbuzzer.buzzer_gpio, GPIO_HIGH);
	}

	miscbuzzer.device = &pdev->dev;

	return ret;
}

static int gpio_buzzer_remove(struct platform_device *pdev)
{
	gpio_set_value(miscbuzzer.buzzer_gpio, GPIO_HIGH);
	miscbuzzer.buzzer_stats = BUZZEROFF;

	device_remove_file(buzzer_miscdev.this_device, &buzzer_addr);

	misc_deregister(&buzzer_miscdev);

	return 0;
}

static struct platform_driver gpio_buzzer_driver = {
	.probe		= gpio_buzzer_probe,
	.remove		= gpio_buzzer_remove,
	.driver		= {
		.name	= "gpio-buzzer",
		.of_match_table = of_gpio_buzzer_match,
	},
};

static int __init gpio_buzzer_init(void)
{
	return platform_driver_register(&gpio_buzzer_driver);
}

static void __exit gpio_buzzer_exit(void)
{
	platform_driver_unregister(&gpio_buzzer_driver);
}

late_initcall(gpio_buzzer_init);
module_exit(gpio_buzzer_exit);

MODULE_AUTHOR("weiye <wwytxjy@163.com>");
MODULE_DESCRIPTION("GPIO BUZZER driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-buzzer");
