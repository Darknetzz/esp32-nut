/*
 * [esp32-nut, EXPERIMENT] Crash-surviving trace of the USB host operations.
 *
 * A ring of the last USB_TRACE_LEN events in RTC memory that survives a panic reset
 * (not a power cycle). Used to find which request is in flight when the hcd_dwc.c
 * "_buffer_parse: urb != NULL" assert fires (Vertiv Edge 10af:0002). Not for merge.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_TRACE_LEN 256

typedef enum {
    UT_CTRL_SUBMIT = 1,  // d = setup packet, x = iface handle's iface_num (0xFF if n/a)
    UT_CTRL_DONE,        // x = transfer status, d[0..1] = actual bytes
    UT_CTRL_TIMEOUT,     // d = setup packet of the request that timed out
    UT_CTRL_SUBMIT_ERR,  // x = esp_err_t (low 16 bits)
    UT_CTRL_LATE_DONE,   // callback after the requester gave up; x = status
    UT_IN_START,         // x = iface_num, d[0] = ep
    UT_IN_DONE,          // x = status, d[0] = ep, d[4..7] = repeat count of identical events
    UT_IN_RESUBMIT_ERR,  // x = err
    UT_EP_HALT,          // x = iface_num, d[0] = ep, d[1] = err
    UT_EP_FLUSH,
    UT_EP_CLEAR,
    UT_IFACE_CLAIM,      // x = iface_num, d[0] = err
    UT_IFACE_RELEASE,    // x = iface_num, d[0] = err
    UT_CLOSE,            // x = iface_num, d[0] = state, d[1] = best_effort
    UT_OPEN,             // x = iface_num
    UT_DEV_GONE,         // x = 0
    UT_APP,              // x = app code (usb_trace_app_t), d = free form
    UT_DEV_NEW,          // x = USB address of the new device
} usb_trace_ev_type_t;

// Codes of UT_APP, written by USBHostUPS
typedef enum {
    UTA_CLAIMED = 1,     // UPS interface claimed, d[0] = iface_num
    UTA_XFER_ERROR,      // HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR received
    UTA_RECOVER,         // recoverInterface() starts
    UTA_RESTART_REQ,     // requestRestart()
    UTA_DISCONNECTED,    // handleDisconnected()
    UTA_IN_RESTARTED,    // retryInterfaceStart() succeeded
} usb_trace_app_t;

typedef struct {
    uint32_t t_ms;   // ms since boot
    uint8_t  ev;     // usb_trace_ev_type_t
    uint8_t  core;
    uint16_t x;
    uint8_t  d[8];
} usb_trace_ev_t;

void usb_trace_add(uint8_t ev, uint16_t x, const uint8_t *d, uint8_t dlen);

/** Copy the events left by the previous boot (oldest first), then clear the ring.
 *  Returns the number copied; 0 when the RTC memory holds no valid trace. */
uint16_t usb_trace_take_previous(usb_trace_ev_t *out, uint16_t max);

/** Copy the events of this boot so far (oldest first), leaving the ring untouched. */
uint16_t usb_trace_snapshot(usb_trace_ev_t *out, uint16_t max);

/** Call once at boot, before any USB activity: snapshots the previous ring and starts a new one. */
void usb_trace_boot(void);

const char *usb_trace_ev_name(uint8_t ev);

#ifdef __cplusplus
}
#endif
