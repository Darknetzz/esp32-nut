/*
 * [esp32-nut, EXPERIMENT] Crash-surviving trace of the USB host operations. See usb_trace.h.
 */
#include <string.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "usb/usb_trace.h"

#define USB_TRACE_MAGIC 0x55545243u // "UTRC"

static RTC_NOINIT_ATTR uint32_t s_magic;
static RTC_NOINIT_ATTR uint32_t s_head;   // total events written (index = s_head % LEN)
static RTC_NOINIT_ATTR usb_trace_ev_t s_ring[USB_TRACE_LEN];

// Snapshot of the previous boot's ring, taken by usb_trace_boot()
static usb_trace_ev_t s_prev[USB_TRACE_LEN];
static uint16_t s_prev_count = 0;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_ready = false;

void usb_trace_boot(void)
{
    if (s_magic == USB_TRACE_MAGIC) {
        uint32_t n = s_head < USB_TRACE_LEN ? s_head : USB_TRACE_LEN;
        uint32_t start = s_head - n;
        for (uint32_t i = 0; i < n; i++) {
            s_prev[i] = s_ring[(start + i) % USB_TRACE_LEN];
        }
        s_prev_count = (uint16_t)n;
    }
    s_head = 0;
    s_magic = USB_TRACE_MAGIC;
    s_ready = true;
}

void IRAM_ATTR usb_trace_add(uint8_t ev, uint16_t x, const uint8_t *d, uint8_t dlen)
{
    if (!s_ready) return;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    portENTER_CRITICAL_SAFE(&s_lock);
    // Collapse a run of identical IN completions into one entry with a counter
    if (ev == UT_IN_DONE && s_head > 0) {
        usb_trace_ev_t *last = &s_ring[(s_head - 1) % USB_TRACE_LEN];
        if (last->ev == UT_IN_DONE && last->x == x) {
            uint32_t cnt;
            memcpy(&cnt, &last->d[4], 4);
            cnt++;
            memcpy(&last->d[4], &cnt, 4);
            last->t_ms = now;
            portEXIT_CRITICAL_SAFE(&s_lock);
            return;
        }
    }
    usb_trace_ev_t *e = &s_ring[s_head % USB_TRACE_LEN];
    e->t_ms = now;
    e->ev = ev;
    e->core = (uint8_t)xPortGetCoreID();
    e->x = x;
    memset(e->d, 0, sizeof(e->d));
    if (d && dlen) memcpy(e->d, d, dlen > 8 ? 8 : dlen);
    if (ev == UT_IN_DONE) {
        uint32_t one = 1;
        memcpy(&e->d[4], &one, 4);
    }
    s_head++;
    portEXIT_CRITICAL_SAFE(&s_lock);
}

uint16_t usb_trace_take_previous(usb_trace_ev_t *out, uint16_t max)
{
    uint16_t n = s_prev_count < max ? s_prev_count : max;
    // Keep the newest ones when the caller's buffer is smaller
    memcpy(out, &s_prev[s_prev_count - n], n * sizeof(usb_trace_ev_t));
    s_prev_count = 0;
    return n;
}

uint16_t usb_trace_snapshot(usb_trace_ev_t *out, uint16_t max)
{
    portENTER_CRITICAL_SAFE(&s_lock);
    uint32_t n = s_head < USB_TRACE_LEN ? s_head : USB_TRACE_LEN;
    if (n > max) n = max;
    uint32_t start = s_head - n;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = s_ring[(start + i) % USB_TRACE_LEN];
    }
    portEXIT_CRITICAL_SAFE(&s_lock);
    return (uint16_t)n;
}

const char *usb_trace_ev_name(uint8_t ev)
{
    switch (ev) {
    case UT_CTRL_SUBMIT:     return "CTRL_SUBMIT";
    case UT_CTRL_DONE:       return "CTRL_DONE";
    case UT_CTRL_TIMEOUT:    return "CTRL_TIMEOUT";
    case UT_CTRL_SUBMIT_ERR: return "CTRL_SUBMIT_ERR";
    case UT_CTRL_LATE_DONE:  return "CTRL_LATE_DONE";
    case UT_IN_START:        return "IN_START";
    case UT_IN_DONE:         return "IN_DONE";
    case UT_IN_RESUBMIT_ERR: return "IN_RESUBMIT_ERR";
    case UT_EP_HALT:         return "EP_HALT";
    case UT_EP_FLUSH:        return "EP_FLUSH";
    case UT_EP_CLEAR:        return "EP_CLEAR";
    case UT_IFACE_CLAIM:     return "IFACE_CLAIM";
    case UT_IFACE_RELEASE:   return "IFACE_RELEASE";
    case UT_CLOSE:           return "CLOSE";
    case UT_OPEN:            return "OPEN";
    case UT_DEV_GONE:        return "DEV_GONE";
    case UT_APP:             return "APP";
    case UT_DEV_NEW:         return "DEV_NEW";
    default:                 return "?";
    }
}
