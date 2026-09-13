/*
 * The board's supply, as far as the Pico can see it.
 *
 * VSYS is the only thing measurable. The cell is behind D1 (hardware README,
 * "D1 in series with VSYS"), so what GP29 reads is the cell minus the diode's
 * drop when the board runs from the battery, and ~4.6 V when USB is plugged
 * into the Pico and its internal Schottky is ORing VBUS in.
 *
 * NEITHER READING SAYS ANYTHING ABOUT CHARGING. The TP4056 is off-board on
 * J5 and invisible to this board. USB into the Pico only carries the board;
 * the cell is unmeasurable while it does, because D1 blocks it.
 *
 * Both functions touch the CYW43 and must be called from the BTstack context
 * — see the note above main() in apps/handoff/handoff.c.
 */
#ifndef HANDOFF_POWER_H
#define HANDOFF_POWER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * VSYS in millivolts, after D1. Read the way pico-examples/adc/read_vsys
 * does: on a Pico 2 W GP29 is shared with the CYW43 SPI clock, so the CYW43
 * has to be awake and the pin briefly claimed for the ADC.
 */
uint16_t power_vsys_mv(void);

/* VBUS present at the Pico's USB connector, from the CYW43's WL_GPIO2. */
bool power_on_usb(void);

#endif /* HANDOFF_POWER_H */
