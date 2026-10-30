// `dwdiag`: driver-independent DW3000 wiring diagnostic.
//
// Takes the DW3000 pins back as plain GPIOs and bit-bangs SPI at ~25 kHz, so the
// result does not depend on the SPI peripheral, the GPIO matrix routing or the
// DW3000 driver. Checks, in order: power (RSTn released level), reset, whether
// anything drives MISO, and the DEV_ID register in all four SPI modes (a strap
// problem shows up as "answers in mode N, not mode 0").
//
// The SPI driver loses its pins afterwards: reboot before uwbprobe / the reader.

#include <cstdio>
#include <cstring>

#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <sdkconfig.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_SCLK   ((gpio_num_t)CONFIG_ULTRAWIDELOCK_DW3000_PIN_SCLK)
#define PIN_MOSI   ((gpio_num_t)CONFIG_ULTRAWIDELOCK_DW3000_PIN_MOSI)
#define PIN_MISO   ((gpio_num_t)CONFIG_ULTRAWIDELOCK_DW3000_PIN_MISO)
#define PIN_CS     ((gpio_num_t)CONFIG_ULTRAWIDELOCK_DW3000_PIN_CS)
#define PIN_RST    ((gpio_num_t)CONFIG_ULTRAWIDELOCK_DW3000_PIN_RST)
#define PIN_IRQ    ((gpio_num_t)CONFIG_ULTRAWIDELOCK_DW3000_PIN_IRQ)
#define PIN_WAKEUP ((gpio_num_t)CONFIG_ULTRAWIDELOCK_DW3000_PIN_WAKEUP)

#define HALF_US 20 /* half SCLK period: ~25 kHz */

static void pin_out(gpio_num_t p, int level)
{
	gpio_reset_pin(p);
	gpio_set_direction(p, GPIO_MODE_OUTPUT);
	gpio_set_level(p, level);
}

static void pin_in(gpio_num_t p, gpio_pull_mode_t pull)
{
	gpio_reset_pin(p);
	gpio_set_direction(p, GPIO_MODE_INPUT);
	gpio_set_pull_mode(p, pull);
}

/* Read a level with a weak pull up and then down. Returns 'H'/'L' if the line is
 * driven (same both ways), 'F' if it follows the pull (floating). */
static char probe_line(gpio_num_t p)
{
	gpio_set_pull_mode(p, GPIO_PULLUP_ONLY);
	esp_rom_delay_us(200);
	int up = gpio_get_level(p);
	gpio_set_pull_mode(p, GPIO_PULLDOWN_ONLY);
	esp_rom_delay_us(200);
	int down = gpio_get_level(p);
	gpio_set_pull_mode(p, GPIO_FLOATING);
	if (up != down) {
		return 'F';
	}
	return up ? 'H' : 'L';
}

static uint8_t bb_xfer(uint8_t out, int cpol, int cpha)
{
	uint8_t in = 0;

	for (int bit = 7; bit >= 0; bit--) {
		int b = (out >> bit) & 1;
		if (cpha == 0) {
			gpio_set_level(PIN_MOSI, b);
			esp_rom_delay_us(HALF_US);
			gpio_set_level(PIN_SCLK, !cpol); /* leading edge: sample */
			in = (uint8_t)((in << 1) | gpio_get_level(PIN_MISO));
			esp_rom_delay_us(HALF_US);
			gpio_set_level(PIN_SCLK, cpol); /* trailing edge */
		} else {
			gpio_set_level(PIN_SCLK, !cpol); /* leading edge: shift */
			gpio_set_level(PIN_MOSI, b);
			esp_rom_delay_us(HALF_US);
			gpio_set_level(PIN_SCLK, cpol); /* trailing edge: sample */
			in = (uint8_t)((in << 1) | gpio_get_level(PIN_MISO));
			esp_rom_delay_us(HALF_US);
		}
	}
	return in;
}

/* DW3000 register file 0x00 (DEV_ID), short read: one header byte 0x00, then 4
 * bytes LSB first. Expect 0xDECA03xx. */
static uint32_t bb_read_devid(int cpol, int cpha, uint8_t raw[4])
{
	gpio_set_level(PIN_SCLK, cpol);
	esp_rom_delay_us(HALF_US);
	gpio_set_level(PIN_CS, 0);
	esp_rom_delay_us(HALF_US);
	(void)bb_xfer(0x00, cpol, cpha);
	for (int i = 0; i < 4; i++) {
		raw[i] = bb_xfer(0x00, cpol, cpha);
	}
	gpio_set_level(PIN_CS, 1);
	gpio_set_level(PIN_SCLK, cpol);
	esp_rom_delay_us(HALF_US);
	return ((uint32_t)raw[3] << 24) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[1] << 8) | raw[0];
}

static bool is_deca(uint32_t id)
{
	return (id & 0xFFFFFF00u) == 0xDECA0300u;
}

int cmd_dwdiag(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	printf("dwdiag: pins SCLK=%d MOSI=%d MISO=%d CS=%d RST=%d IRQ=%d WAKEUP=%d\n", PIN_SCLK,
	       PIN_MOSI, PIN_MISO, PIN_CS, PIN_RST, PIN_IRQ, PIN_WAKEUP);

	pin_out(PIN_CS, 1);
	pin_out(PIN_SCLK, 0);
	pin_out(PIN_MOSI, 0);
	pin_out(PIN_WAKEUP, 1);
	pin_in(PIN_MISO, GPIO_FLOATING);
	pin_in(PIN_IRQ, GPIO_FLOATING);
	pin_in(PIN_RST, GPIO_FLOATING);
	vTaskDelay(pdMS_TO_TICKS(5));

	/* 1. Power. RSTn is the DW3000's own open-drain reset with an internal pull-up
	 *    to its supply: released and powered, it reads high. */
	/* Read with no pull at all: the chip's internal pull-up can be weaker than
	 * the ESP's, so a pull-down probe would misread a powered part as floating. */
	char rst = gpio_get_level(PIN_RST) ? 'H' : 'L';
	printf("1. RSTn released      : %c  %s\n", rst,
	       rst == 'H' ? "(ok: DW3000 powered, out of reset)"
			  : "(BAD: low -> DW3000 unpowered, RSTn not connected, or reset stuck)");

	/* 2. Reset pulse; IRQ commonly rises once the part reaches IDLE_RC. */
	pin_out(PIN_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(2));
	pin_in(PIN_RST, GPIO_FLOATING);
	vTaskDelay(pdMS_TO_TICKS(10));
	char rst2 = gpio_get_level(PIN_RST) ? 'H' : 'L';
	char irq = probe_line(PIN_IRQ);
	printf("2. after reset pulse  : RSTn %c, IRQ %c  (IRQ H/L = driven: wire ok; F = IRQ not connected)\n",
	       rst2, irq);

	/* 3. Who drives MISO. With CS high the DW3000 tri-states it (F is normal);
	 *    with CS low it must drive it (H or L). */
	char miso_idle = probe_line(PIN_MISO);
	gpio_set_level(PIN_CS, 0);
	esp_rom_delay_us(50);
	char miso_sel = probe_line(PIN_MISO);
	gpio_set_level(PIN_CS, 1);
	printf("3. MISO CS-high / CS-low: %c / %c  %s\n", miso_idle, miso_sel,
	       miso_sel != 'F' ? "(ok: chip drives MISO when selected)"
			       : "(BAD: nothing drives MISO when selected -> MISO or CS wire, or no power)");

	/* 4. DEV_ID in every SPI mode. */
	int good_mode = -1;
	for (int mode = 0; mode < 4; mode++) {
		int cpol = mode >> 1, cpha = mode & 1;
		uint8_t raw[4];
		uint32_t id = 0;
		for (int tries = 0; tries < 3; tries++) {
			id = bb_read_devid(cpol, cpha, raw);
			if (is_deca(id)) {
				break;
			}
			vTaskDelay(pdMS_TO_TICKS(2));
		}
		printf("4. DEV_ID mode %d      : 0x%08x  [%02x %02x %02x %02x]%s\n", mode,
		       (unsigned)id, raw[0], raw[1], raw[2], raw[3], is_deca(id) ? "  <-- DW3000" : "");
		if (is_deca(id) && good_mode < 0) {
			good_mode = mode;
		}
	}

	printf("\nverdict: ");
	if (good_mode == 0) {
		printf("wiring and power are GOOD (DW3000 answers in mode 0).\n"
		       "         If uwbprobe still fails, the problem is the SPI peripheral path\n"
		       "         (clock speed / signal integrity): try shorter wires.\n");
	} else if (good_mode > 0) {
		printf("DW3000 answers in SPI mode %d, not mode 0 -> the SPIPOL/SPIPHA straps\n"
		       "         are not reading as GND. Check the strap wires/jumpers on the EVB.\n",
		       good_mode);
	} else if (rst != 'H') {
		printf("the DW3000 is not powered (RSTn not high). Check 3V3/GND to the EVB and\n"
		       "         the power-select jumper; measure 3.3 V on the EVB.\n");
	} else if (miso_sel == 'F') {
		printf("powered, but MISO is not driven when CS is low -> check CS (D7->EVB D10)\n"
		       "         and MISO (D9->EVB D12) wires.\n");
	} else {
		printf("powered and MISO is driven, but no valid DEV_ID in any mode -> check\n"
		       "         SCLK (D8->EVB D13) and MOSI (D10->EVB D11), and that no lines are swapped.\n");
	}
	printf("\nNOTE: the DW3000 pins are now plain GPIOs. Reboot before uwbprobe.\n");
	return 0;
}
