/*
 * Copyright (C) 2015 Balázs Triszka <balika011@protonmail.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pwm.h>
#include <linux/of_gpio.h>
#include <linux/err.h>

#include "../staging/android/timed_output.h"

struct isa1000_vib {
	int gpio_isa1000_en;
	int gpio_haptic_en;
	int timeout;
	struct mutex lock;
	struct hrtimer vib_timer;
	struct timed_output_dev timed_dev;
};

static struct isa1000_vib isa1000_vibrator_data = {
};

static int isa1000_vib_set(struct isa1000_vib *vib, int on)
{
	if (on)
		gpio_set_value_cansleep(vib->gpio_haptic_en, 1);
	else
		gpio_set_value_cansleep(vib->gpio_haptic_en, 0);

	return 0;
}

static void isa1000_vib_enable(struct timed_output_dev *dev, int value)
{
	struct isa1000_vib *vib = container_of(dev, struct isa1000_vib, timed_dev);

	mutex_lock(&vib->lock);
	hrtimer_cancel(&vib->vib_timer);

	if (value == 0)
		isa1000_vib_set(vib, 0);
	else {
		isa1000_vib_set(vib, 1);
		value = (value > vib->timeout ? vib->timeout : value);
		hrtimer_start(&vib->vib_timer, ktime_set(value / 1000, (value % 1000) * 1000000), HRTIMER_MODE_REL);
	}

	mutex_unlock(&vib->lock);
}

static int isa1000_vib_get_time(struct timed_output_dev *dev)
{
	struct isa1000_vib *vib = container_of(dev, struct isa1000_vib, timed_dev);

	if (hrtimer_active(&vib->vib_timer)) {
		ktime_t r = hrtimer_get_remaining(&vib->vib_timer);
		return (int) ktime_to_us(r);
	}

	return 0;
}

static enum hrtimer_restart isa1000_vib_timer_func(struct hrtimer *timer)
{
	struct isa1000_vib *vib = container_of(timer, struct isa1000_vib, vib_timer);

	isa1000_vib_set(vib, 0);

	return HRTIMER_NORESTART;
}

static int isa1000_vibrator_probe(struct platform_device *pdev)
{
	struct isa1000_vib *vib;
	int rc;

	platform_set_drvdata(pdev, &isa1000_vibrator_data);
	vib = platform_get_drvdata(pdev);

	rc = of_get_named_gpio_flags(pdev->dev.of_node, "gpio-isa1000-en", 0, NULL);
	if (rc < 0)
	{
		dev_err(&pdev->dev, "please check enable gpio");
		return rc;
	}
	vib->gpio_isa1000_en = rc;

	rc = of_get_named_gpio_flags(pdev->dev.of_node, "gpio-haptic-en", 0, NULL);
	if (rc < 0)
	{
		dev_err(&pdev->dev, "please check enable gpio");
		return rc;
	}
	vib->gpio_haptic_en = rc;

	rc = of_property_read_u32(pdev->dev.of_node, "timeout-ms", &vib->timeout);
	if (rc < 0)
		dev_err(&pdev->dev,"please check timeout");

	rc = gpio_is_valid(vib->gpio_isa1000_en);
	if (rc) {
		rc = gpio_request(vib->gpio_isa1000_en, "gpio_isa1000_en");
		if (rc) {
			dev_err(&pdev->dev, "gpio %d request failed",vib->gpio_isa1000_en);
			return rc;
		}
	} else {
		dev_err(&pdev->dev, "invalid gpio %d\n", vib->gpio_isa1000_en);
		return rc;
	}

	rc = gpio_is_valid(vib->gpio_haptic_en);
	if (rc) {
		rc = gpio_request(vib->gpio_haptic_en, "gpio_haptic_en");
		if (rc) {
			dev_err(&pdev->dev, "gpio %d request failed\n", vib->gpio_haptic_en);
			return rc;
		}
	} else {
		dev_err(&pdev->dev, "invalid gpio %d\n", vib->gpio_haptic_en);
		return rc;
	}

	gpio_direction_output(vib->gpio_isa1000_en, 1);
	gpio_direction_output(vib->gpio_haptic_en, 0);

	mutex_init(&vib->lock);

	hrtimer_init(&vib->vib_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vib->vib_timer.function = isa1000_vib_timer_func;

	vib->timed_dev.name = "vibrator";
	vib->timed_dev.get_time = isa1000_vib_get_time;
	vib->timed_dev.enable = isa1000_vib_enable;

	rc = timed_output_dev_register(&vib->timed_dev);
	if (rc < 0)
		return rc;

	return 0;
}

static int isa1000_vibrator_remove(struct platform_device *pdev)
{
	struct isa1000_vib *vib = platform_get_drvdata(pdev);

	timed_output_dev_unregister(&vib->timed_dev);

	hrtimer_cancel(&vib->vib_timer);

	mutex_destroy(&vib->lock);

	gpio_free(vib->gpio_haptic_en);
	gpio_free(vib->gpio_isa1000_en);

	return 0;
}

static struct of_device_id vibrator_match_table[] = {
	{ .compatible = "imagis,isa1000", },
	{ },
};

static struct platform_driver isa1000_vibrator_driver = {
	.probe		= isa1000_vibrator_probe,
	.remove		= isa1000_vibrator_remove,
	.driver		= {
		.name	= "isa1000",
		.of_match_table = vibrator_match_table,
	},
};

static int __init isa1000_vibrator_init(void)
{
	return platform_driver_register(&isa1000_vibrator_driver);
}
module_init(isa1000_vibrator_init);

static void __exit isa1000_vibrator_exit(void)
{
	return platform_driver_unregister(&isa1000_vibrator_driver);
}
module_exit(isa1000_vibrator_exit);

MODULE_AUTHOR("Balázs Triszka <balika011@protonmail.ch>");
MODULE_DESCRIPTION("ISA1000 Haptic Motor driver");
MODULE_LICENSE("GPL v2");
