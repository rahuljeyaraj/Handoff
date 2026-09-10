/*
 * Handoff — decimated score stream over BLE. STUB until M2.
 *
 * design §13 forbids a USB tether to a mains-powered laptop while anyone is
 * touching an electrode, so from M10 onward this is the ONLY way measurements
 * leave the wristband. It is not a convenience.
 */
#include "tlm.h"

void tlm_ble_init(uint16_t decimate) { (void)decimate; }
void tlm_ble_score(uint16_t score)   { (void)score; }
