#ifndef XID_DEVICE_H
#define XID_DEVICE_H

#include <stdint.h>
#include <tusb.h>
#include <device/usbd_pvt.h>

#define XID_INTERFACE_CLASS 0x58
#define XID_INTERFACE_SUBCLASS 0x42
#define XID_MAX_PACKET_SIZE 32

bool xid_device_send_report(void const *report, uint16_t len);
bool xid_device_get_output_report(void *report, uint16_t len);
bool xid_device_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
const usbd_class_driver_t *xid_device_driver();

#endif
