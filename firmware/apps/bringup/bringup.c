/*
 * Handoff — PCB bring-up console.
 *
 * Hardware: the PCB, one stage at a time, with a multimeter and nothing
 * else. docs/hardware-bringup.md is the procedure; this is the instrument
 * it drives through scripts/bringup.py.
 *
 * Every command puts one pin into one known state and leaves it there, so a
 * meter can be read against it at leisure. Nothing else runs: no receiver
 * ring, no core 1, no PWM — a pin is high, low or released, and what the
 * meter says is what the copper does.
 *
 * The one exception is the radio, and `v` is why. On a Pico 2 W GP29 is the
 * CYW43's SPI clock as well as VSYS/3 (hardware/README.md), and the divider
 * only reads while that chip is powered and awake: with it off, ADC3 sits at
 * the bottom of the range. So cyw43_arch_init() runs at boot and `v` wakes
 * the chip for the read, exactly as power.c does for the product image. No
 * radio is ever brought up beyond that.
 *
 *   l r|g|b|w|0   RGB LED on GP17/18/19, plain outputs (no PWM)
 *   m 1|0         motor gate, GP28
 *   p 1|0|x       pad: driven high, driven low, released (input buffer OFF,
 *                 the link's high-Z state — RP2350-E9, see pio_carrier.h)
 *   c 40|200|0    continuous carrier on the pad, or off (released)
 *   f             measure the carrier on the pad with the PIO counter
 *   a [n]         ADC0 (GP26): n samples, mean / min / max in mV
 *   v             VSYS (GP29 / 3) in mV
 *   i             BTN and ROLE now
 *   h             this list
 *
 * A `hb` line every second carries BTN, ROLE, the pad state and the motor,
 * and a change on BTN or ROLE is printed the moment it happens, so a press
 * of SW2 or a wire across JP8 shows up without being asked for.
 *
 * At boot the RGB LED flashes green for a second: on the battery there is no
 * console, and that flash is how the battery path is known to have booted.
 *
 * The transmit pin is HANDOFF_PIN_TX (hal_pico.h): the PCB's GP11 when built
 * through scripts/bringup.py, which passes --tx-pin 11.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "hal_pico.h"
#include "pio_carrier.h"

#define PIN_LED_R   17
#define PIN_LED_G   18
#define PIN_LED_B   19
#define PIN_MOTOR   28
#define PIN_ROLE    14
#define PIN_BTN     15

#define ADC_DEFAULT_N   4096u
#define ADC_MAX_N       262144u
#define HEARTBEAT_US    1000000u

static const char *s_pad = "hiz";
static bool        s_motor;
static int         s_btn = -1, s_role = -1;
static bool        s_cyw43;

/* ---------------------------------------------------------------------- */

static void led(bool r, bool g, bool b)
{
    gpio_put(PIN_LED_R, r);
    gpio_put(PIN_LED_G, g);
    gpio_put(PIN_LED_B, b);
}

static void pad_release(void)
{
    pio_carrier_mark_continuous(false);
    pio_carrier_hold(-1);
    pio_carrier_drive(false);        /* SIO input, pulls off, buffer off */
    s_pad = "hiz";
}

static void pad_hold(int level)
{
    pio_carrier_mark_continuous(false);
    pio_carrier_drive(true);
    pio_carrier_hold(level);
    s_pad = level ? "high" : "low";
}

static void pad_carrier(uint32_t khz)
{
    pio_carrier_init(khz * 1000u);   /* re-init sets the divider; loads once */
    pio_carrier_sense(false);
    pio_carrier_drive(true);
    pio_carrier_mark_continuous(true);
    s_pad = khz == 40u ? "40k" : "200k";
}

static uint32_t adc_mv(uint32_t raw)
{
    return (raw * 3300u + 2047u) / 4095u;
}

static void cmd_adc(uint32_t n)
{
    uint32_t i, raw, lo = 4095u, hi = 0u;
    uint64_t sum = 0;

    if (n == 0) n = ADC_DEFAULT_N;
    if (n > ADC_MAX_N) n = ADC_MAX_N;

    adc_select_input(HANDOFF_ADC_CHANNEL);
    for (i = 0; i < n; i++) {
        raw = adc_read();
        sum += raw;
        if (raw < lo) lo = raw;
        if (raw > hi) hi = raw;
    }
    raw = (uint32_t)(sum / n);
    printf("    adc0 %lu samples: mean %lu mV  min %lu  max %lu  p-p %lu mV\n",
           (unsigned long)n, (unsigned long)adc_mv(raw),
           (unsigned long)adc_mv(lo), (unsigned long)adc_mv(hi),
           (unsigned long)(adc_mv(hi) - adc_mv(lo)));
}

static void cmd_vsys(void)
{
    uint32_t i, sum = 0;

    /* GP29 belongs to the CYW43 until we take it. Wake the chip first — its
     * SPI clock line is the divider's only path to the pin — then claim
     * the pad, throw the settling conversions away (the divider is ~66 kOhm, so
     * the first reads come in low), and average. The driver restores GP29
     * on its next transaction; do not restore it by hand. */
    if (!s_cyw43) {
        printf("    vsys unavailable: the CYW43 did not start, and GP29 "
               "reads nothing without it\n");
        return;
    }

    cyw43_thread_enter();
    (void)cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);

    adc_gpio_init(HANDOFF_PIN_VSYS);
    adc_select_input(HANDOFF_VSYS_CHANNEL);
    for (i = 0; i < 8u; i++) (void)adc_read();
    for (i = 0; i < 256u; i++) sum += adc_read();

    cyw43_thread_exit();

    printf("    vsys %lu mV\n", (unsigned long)(adc_mv(sum / 256u) * 3u));
    adc_select_input(HANDOFF_ADC_CHANNEL);
}

static void cmd_freq(void)
{
    uint32_t hz;

    /* The counter needs the input buffer; the pad is driven while it runs,
     * so E9 cannot latch anything. Back off straight after. */
    pio_carrier_sense(true);
    hz = pio_carrier_measure_hz(200000u);
    pio_carrier_sense(false);
    printf("    pad %lu Hz\n", (unsigned long)hz);
}

static void print_pins(const char *tag)
{
    printf("    %s btn=%d role=%d pad=%s motor=%d\n",
           tag, s_btn, s_role, s_pad, s_motor ? 1 : 0);
}

static void help(void)
{
    printf("    l r|g|b|w|0   LED red / green / blue / white / off\n"
           "    m 1|0         motor on / off (GP28)\n"
           "    p 1|0|x       pad GP%d driven high / driven low / released\n"
           "    c 40|200|0    carrier on the pad, kHz; 0 = off\n"
           "    f             measure the carrier on the pad\n"
           "    a [n]         ADC0: n samples (%u), mean/min/max mV\n"
           "    v             VSYS in mV\n"
           "    i             BTN and ROLE now\n",
           HANDOFF_PIN_TX, (unsigned)ADC_DEFAULT_N);
}

static void dispatch(const char *line)
{
    char cmd;
    char arg = 0;
    uint32_t num = 0;

    while (*line == ' ') line++;
    if (!*line) return;
    cmd = *line++;
    while (*line == ' ') line++;
    if (*line) {
        arg = *line;
        num = (uint32_t)strtoul(line, NULL, 10);
    }

    switch (cmd) {
    case 'l':
        switch (arg) {
        case 'r': led(1, 0, 0); printf("    led red\n");   break;
        case 'g': led(0, 1, 0); printf("    led green\n"); break;
        case 'b': led(0, 0, 1); printf("    led blue\n");  break;
        case 'w': led(1, 1, 1); printf("    led white\n"); break;
        case '0': led(0, 0, 0); printf("    led off\n");   break;
        default:  printf("    ? l r|g|b|w|0\n");            break;
        }
        break;
    case 'm':
        s_motor = num != 0;
        gpio_put(PIN_MOTOR, s_motor);
        printf("    motor %s\n", s_motor ? "on" : "off");
        break;
    case 'p':
        if (arg == 'x')      { pad_release(); printf("    pad released (high-Z)\n"); }
        else if (arg == '1') { pad_hold(1);   printf("    pad driven high\n"); }
        else if (arg == '0') { pad_hold(0);   printf("    pad driven low\n"); }
        else printf("    ? p 1|0|x\n");
        break;
    case 'c':
        if (num == 40u || num == 200u) {
            pad_carrier(num);
            printf("    carrier %lu kHz on GP%d\n", (unsigned long)num, HANDOFF_PIN_TX);
        } else if (num == 0) {
            pad_release();
            printf("    carrier off, pad released\n");
        } else {
            printf("    ? c 40|200|0\n");
        }
        break;
    case 'f': cmd_freq(); break;
    case 'a': cmd_adc(num); break;
    case 'v': cmd_vsys(); break;
    case 'i': print_pins("pins"); break;
    case 'h': case '?': help(); break;
    default:  printf("    ? (h for help)\n"); break;
    }
}

/* ---------------------------------------------------------------------- */

static void poll_inputs(void)
{
    int btn  = gpio_get(PIN_BTN)  ? 1 : 0;
    int role = gpio_get(PIN_ROLE) ? 1 : 0;

    if (btn != s_btn)   { s_btn = btn;   printf("    btn %d%s\n",  btn,  btn  ? "" : "  (pressed)"); }
    if (role != s_role) { s_role = role; printf("    role %d%s\n", role, role ? "" : "  (JP8 bridged)"); }
}

int main(void)
{
    char line[32];
    size_t len = 0;
    uint64_t next_hb;
    uint32_t beat = 0;

    /* LED and motor first: the motor gate must be driven low before anything
     * else happens, and the boot flash has to work with no console attached. */
    gpio_init(PIN_MOTOR);  gpio_set_dir(PIN_MOTOR, GPIO_OUT); gpio_put(PIN_MOTOR, 0);
    gpio_init(PIN_LED_R);  gpio_set_dir(PIN_LED_R, GPIO_OUT);
    gpio_init(PIN_LED_G);  gpio_set_dir(PIN_LED_G, GPIO_OUT);
    gpio_init(PIN_LED_B);  gpio_set_dir(PIN_LED_B, GPIO_OUT);
    led(0, 1, 0);

    /* BTN has R15 on the board; no internal pull, so a missing R15 reads as
     * a floating pin rather than being hidden. ROLE has only JP8, so the
     * firmware pull-up is the design (hardware README). */
    gpio_init(PIN_BTN);   gpio_set_dir(PIN_BTN, GPIO_IN);  gpio_disable_pulls(PIN_BTN);
    gpio_init(PIN_ROLE);  gpio_set_dir(PIN_ROLE, GPIO_IN); gpio_pull_up(PIN_ROLE);

    adc_init();
    adc_gpio_init(HANDOFF_PIN_ADC);
    adc_select_input(HANDOFF_ADC_CHANNEL);

    /* Only so that `v` has a divider to read; see the note at the top. A
     * failure here costs nothing but VSYS, so it is reported, not fatal. */
    s_cyw43 = (cyw43_arch_init() == 0);

    pio_carrier_init(200000u);
    pad_release();

    stdio_init_all();
    sleep_ms(1000);
    led(0, 0, 0);
    sleep_ms(1000);          /* let the USB console attach before the banner */

    printf("\nhandoff bringup: TX on GP%d, ADC0 on GP%d%s\n",
           HANDOFF_PIN_TX, HANDOFF_PIN_ADC,
           s_cyw43 ? "" : "  (CYW43 down: `v` cannot read VSYS)");
    poll_inputs();
    help();

    next_hb = time_us_64() + HEARTBEAT_US;
    for (;;) {
        int ch = getchar_timeout_us(0);

        poll_inputs();
        if (ch == PICO_ERROR_TIMEOUT) {
            if (time_us_64() >= next_hb) {
                char tag[16];
                next_hb += HEARTBEAT_US;
                snprintf(tag, sizeof tag, "hb %lu", (unsigned long)++beat);
                print_pins(tag);
            }
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            line[len] = 0;
            if (len) dispatch(line);
            len = 0;
        } else if (len + 1 < sizeof line) {
            line[len++] = (char)ch;
        }
    }
}
