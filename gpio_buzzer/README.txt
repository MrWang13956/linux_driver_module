
######driver location######
/driver/misc


######devicetree config######
eg:
	gpio_buzzer: gpio_buzzer {
		compatible = "gpio-buzzer";
		gpios = <&gpioc 7 GPIO_ACTIVE_LOW>;
		default-state = "off";
		status = "okay";
	};
