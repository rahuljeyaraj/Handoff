/* Handoff — score stream and triggered raw bursts over USB CDC. STUB until M4. */
#include "tlm.h"

void tlm_usb_init(uint16_t decimate) { (void)decimate; }
void tlm_usb_score(uint16_t score)   { (void)score; }
void tlm_usb_event(const char *text) { (void)text; }
void tlm_usb_raw_trigger(void)       { }
bool tlm_usb_raw_busy(void)          { return false; }

void tlm_sink(void *ctx, hal_tlm_kind_t kind, const void *data, size_t len)
{
    (void)ctx; (void)kind; (void)data; (void)len;
}
