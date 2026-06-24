#ifndef XID_GAMEPAD_H
#define XID_GAMEPAD_H

#include <stdint.h>
#include <tusb.h>

#include "xid_device.h"

#define XID_GAMEPAD_VID 0x045E
#define XID_GAMEPAD_PID_DUKE 0x0202
#define XID_GAMEPAD_PID_CONTROLLER_S 0x0289

#define XID_GAMEPAD_DESC_LEN (9 + 7 + 7)

#define XID_DPAD_UP    (1u << 0)
#define XID_DPAD_DOWN  (1u << 1)
#define XID_DPAD_LEFT  (1u << 2)
#define XID_DPAD_RIGHT (1u << 3)
#define XID_START      (1u << 4)
#define XID_BACK       (1u << 5)
#define XID_LEFT_STICK (1u << 6)
#define XID_RIGHT_STICK (1u << 7)

#define XID_GAMEPAD_INTERFACE_DESCRIPTOR(_itfnum, _epin, _epout) \
  9, TUSB_DESC_INTERFACE, _itfnum, 0, 2, XID_INTERFACE_CLASS, XID_INTERFACE_SUBCLASS, 0x00, 0x00, \
  7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(XID_MAX_PACKET_SIZE), 4, \
  7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(XID_MAX_PACKET_SIZE), 4

typedef enum {
  XID_BUTTON_A,
  XID_BUTTON_B,
  XID_BUTTON_X,
  XID_BUTTON_Y,
  XID_BUTTON_BLACK,
  XID_BUTTON_WHITE
} xid_analog_button_t;

typedef struct __attribute__((packed)) {
  uint8_t report_id;
  uint8_t length;
  uint8_t digital_buttons;
  uint8_t reserved;
  uint8_t a;
  uint8_t b;
  uint8_t x;
  uint8_t y;
  uint8_t black;
  uint8_t white;
  uint8_t left_trigger;
  uint8_t right_trigger;
  int16_t left_stick_x;
  int16_t left_stick_y;
  int16_t right_stick_x;
  int16_t right_stick_y;
} xid_gamepad_report_t;

typedef struct __attribute__((packed)) {
  uint8_t report_id;
  uint8_t length;
  uint8_t left_motor;
  uint8_t left_motor_hi;
  uint8_t right_motor;
  uint8_t right_motor_hi;
} xid_gamepad_rumble_t;

void xid_gamepad_clear_report(xid_gamepad_report_t *report);
void xid_gamepad_press_analog(xid_gamepad_report_t *report, xid_analog_button_t button, uint8_t value);
bool xid_gamepad_send(xid_gamepad_report_t const *report);
bool xid_gamepad_get_rumble(xid_gamepad_rumble_t *rumble);
bool xid_gamepad_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);

#endif
