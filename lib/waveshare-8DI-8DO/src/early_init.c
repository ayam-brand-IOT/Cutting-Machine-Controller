/**
 * early_init.c  —  Safe-boot GPIO initialization
 *
 * Technique adapted from the EdgeBox_ESP_100 "Fixed-atomic-outputs" branch.
 *
 * On the ESP32-S3 some GPIOs float or glitch HIGH during the boot/reset window,
 * before Arduino's setup() ever runs. For pins wired to something that can act
 * on a spurious HIGH (a buzzer, an RS485 driver-enable, an SPI chip-select),
 * that glitch is a real-world event. This file forces those pins into a known
 * safe state as early as the C runtime allows.
 *
 * WHY A CONSTRUCTOR
 *   __attribute__((constructor(101))) runs during C++ static init, before
 *   app_main()/setup(). Priority 101 is the lowest user priority, so this runs
 *   ahead of ordinary constructors. `used` keeps the linker from stripping it.
 *
 * WHAT IT MAY AND MAY NOT TOUCH
 *   - Uses the ESP-IDF gpio driver directly (gpio_config + gpio_set_level).
 *     Arduino pinMode()/digitalWrite() are NOT available this early.
 *   - Only DIRECT ESP32 GPIOs are handled here.
 *   - It must NOT touch the digital outputs DO1..DO8: those live behind an I2C
 *     expander and cannot be reached without Wire, which is illegal this early.
 *     They are forced OFF at the top of beginOutputs() instead.
 *   - It must NOT touch GPIO0 (BOOT) or any flash/USB pins.
 *
 * SAFE STATES CHOSEN FOR THIS BOARD
 *   Buzzer (GPIO46)   -> LOW   : silent.
 *   RGB    (GPIO38)   -> LOW   : WS2812 data line idle, no spurious frame.
 *   RS485 DE (GPIO21) -> LOW   : receiver enabled / driver disabled, so we never
 *                                drive the shared bus during boot.
 *   ETH CS (GPIO16)   -> HIGH  : W5500 deselected, avoids SPI bus contention.
 */
#include "driver/gpio.h"
#include "W8DI8DO_Pins.h"

/* Runtime-visible proof that the early init actually ran. */
volatile bool early_init_ran = false;

static void w8_force_output(int pin, int level) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level((gpio_num_t)pin, level);
}

__attribute__((constructor(101), used))
static void w8di8do_early_init(void) {
    w8_force_output(W8DI8DO_BUZZER,    0);  /* silent            */
    w8_force_output(W8DI8DO_RGB,       0);  /* LED data idle     */
    w8_force_output(W8DI8DO_RS485_RTS, 0);  /* RX mode, bus free */
    w8_force_output(W8DI8DO_ETH_CS,    1);  /* W5500 deselected  */

    early_init_ran = true;
}
