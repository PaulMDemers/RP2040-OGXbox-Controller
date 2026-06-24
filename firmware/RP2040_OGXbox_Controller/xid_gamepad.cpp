#include "xid_gamepad.h"

#include <string.h>

static const uint8_t xid_descriptor[] = {
  0x10,
  0x42,
  0x00, 0x01,
  0x01,
  0x01,
  sizeof(xid_gamepad_report_t),
  sizeof(xid_gamepad_rumble_t),
  0xFF, 0xFF,
  0xFF, 0xFF,
  0xFF, 0xFF,
  0xFF, 0xFF
};

static const uint8_t xid_probe_device_prefix[] = {
  0x12, 0x01, 0x10, 0x01, 0x00, 0x00
};

static const uint8_t xid_probe_hid_config[] = {
  0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
  0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
  0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, 0x65, 0x00,
  0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x04,
  0x07, 0x05, 0x02, 0x03, 0x40, 0x00, 0x04
};

static const uint8_t capabilities_in[] = {
  0x00,
  sizeof(xid_gamepad_report_t),
  0xFF,
  0x00,
  0xFF,
  0xFF,
  0xFF,
  0xFF,
  0xFF,
  0xFF,
  0xFF,
  0xFF,
  0xFF, 0xFF,
  0xFF, 0xFF,
  0xFF, 0xFF,
  0xFF, 0xFF
};

static const uint8_t capabilities_out[] = {
  0x00,
  sizeof(xid_gamepad_rumble_t),
  0xFF, 0xFF,
  0xFF, 0xFF
};

void xid_gamepad_clear_report(xid_gamepad_report_t *report) {
  memset(report, 0, sizeof(*report));
  report->length = sizeof(*report);
}

void xid_gamepad_press_analog(xid_gamepad_report_t *report, xid_analog_button_t button, uint8_t value) {
  switch (button) {
    case XID_BUTTON_A:
      report->a = value;
      break;
    case XID_BUTTON_B:
      report->b = value;
      break;
    case XID_BUTTON_X:
      report->x = value;
      break;
    case XID_BUTTON_Y:
      report->y = value;
      break;
    case XID_BUTTON_BLACK:
      report->black = value;
      break;
    case XID_BUTTON_WHITE:
      report->white = value;
      break;
  }
}

bool xid_gamepad_send(xid_gamepad_report_t const *report) {
  return xid_device_send_report(report, sizeof(*report));
}

bool xid_gamepad_get_rumble(xid_gamepad_rumble_t *rumble) {
  return xid_device_get_output_report(rumble, sizeof(*rumble));
}

bool xid_gamepad_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  if (request->bmRequestType == 0xC1 && request->bRequest == 0x06 && request->wValue == 0x4200) {
    if (stage == CONTROL_STAGE_SETUP) {
      if (request->wLength == sizeof(xid_probe_device_prefix)) {
        tud_control_xfer(rhport, request, (void *)xid_probe_device_prefix, sizeof(xid_probe_device_prefix));
      } else if (request->wLength == 0x50) {
        tud_control_xfer(rhport, request, (void *)xid_probe_hid_config, sizeof(xid_probe_hid_config));
      } else {
        tud_control_xfer(rhport, request, (void *)xid_descriptor, sizeof(xid_descriptor));
      }
    }
    return true;
  }

  if (request->bmRequestType == 0xC1 && request->bRequest == 0x01 && request->wValue == 0x0100) {
    if (stage == CONTROL_STAGE_SETUP) {
      tud_control_xfer(rhport, request, (void *)capabilities_in, sizeof(capabilities_in));
    }
    return true;
  }

  if (request->bmRequestType == 0xC1 && request->bRequest == 0x01 && request->wValue == 0x0200) {
    if (stage == CONTROL_STAGE_SETUP) {
      tud_control_xfer(rhport, request, (void *)capabilities_out, sizeof(capabilities_out));
    }
    return true;
  }

  return false;
}
