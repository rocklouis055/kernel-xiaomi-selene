/* SPDX-License-Identifier: GPL-2.0 */
/*
 * selene (Redmi 10) panels drive the LCD bias regulator through SoC pins
 * 165/169. The 4.14 drivers hardcoded global GPIO numbers (base 325); on
 * 4.19 the MT6768 pin controller's gpiochip base is assigned dynamically,
 * so translate pin numbers at runtime.
 */
#ifndef __SELENE_LCM_GPIO_H__
#define __SELENE_LCM_GPIO_H__

#include <linux/gpio/driver.h>
#include <linux/of.h>

static int selene_pio_match(struct gpio_chip *gc, void *data)
{
	return gc->parent && gc->parent->of_node &&
	       of_device_is_compatible(gc->parent->of_node,
				       "mediatek,mt6768-pinctrl");
}

static inline unsigned int selene_pio_gpio(unsigned int pin)
{
	static int base = -1;

	if (base < 0) {
		struct gpio_chip *gc = gpiochip_find(NULL, selene_pio_match);

		if (!gc)
			return ~0U;	/* invalid; gpio_request() fails */
		base = gc->base;
	}
	return base + pin;
}

#endif
