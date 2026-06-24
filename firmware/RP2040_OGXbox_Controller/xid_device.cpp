#include "xid_device.h"

#include <string.h>

#include "xid_gamepad.h"

typedef struct {
  uint8_t itf_num;
  uint8_t ep_in;
  uint8_t ep_out;
  uint8_t rhport;
  CFG_TUSB_MEM_ALIGN uint8_t ep_out_buffer[XID_MAX_PACKET_SIZE];
  CFG_TUSB_MEM_ALIGN uint8_t input_report[XID_MAX_PACKET_SIZE];
  CFG_TUSB_MEM_ALIGN uint8_t output_report[XID_MAX_PACKET_SIZE];
  uint16_t output_report_len;
  bool output_report_pending;
} xid_interface_t;

CFG_TUSB_MEM_SECTION static xid_interface_t xid_itf;

static void xid_init() {
  tu_memclr(&xid_itf, sizeof(xid_itf));
}

static void xid_reset(uint8_t) {
  xid_init();
}

static uint16_t xid_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
  TU_VERIFY(itf_desc->bInterfaceClass == XID_INTERFACE_CLASS, 0);
  TU_VERIFY(itf_desc->bInterfaceSubClass == XID_INTERFACE_SUBCLASS, 0);
  TU_VERIFY(itf_desc->bNumEndpoints == 2, 0);
  TU_ASSERT(max_len >= XID_GAMEPAD_DESC_LEN, 0);

  xid_itf.itf_num = itf_desc->bInterfaceNumber;
  xid_itf.rhport = rhport;

  tusb_desc_endpoint_t const *ep_desc = (tusb_desc_endpoint_t const *)tu_desc_next(itf_desc);
  for (uint8_t i = 0; i < 2; ++i) {
    TU_ASSERT(tu_desc_type(ep_desc) == TUSB_DESC_ENDPOINT, 0);
    TU_ASSERT(usbd_edpt_open(rhport, ep_desc), 0);

    if (tu_edpt_dir(ep_desc->bEndpointAddress) == TUSB_DIR_IN) {
      xid_itf.ep_in = ep_desc->bEndpointAddress;
    } else {
      xid_itf.ep_out = ep_desc->bEndpointAddress;
    }

    ep_desc = (tusb_desc_endpoint_t const *)tu_desc_next(ep_desc);
  }

  if (xid_itf.ep_out != 0) {
    usbd_edpt_xfer(rhport, xid_itf.ep_out, xid_itf.ep_out_buffer, sizeof(xid_itf.ep_out_buffer), false);
  }

  return XID_GAMEPAD_DESC_LEN;
}

bool xid_device_send_report(void const *report, uint16_t len) {
  TU_VERIFY(len <= sizeof(xid_itf.input_report), false);
  memcpy(xid_itf.input_report, report, len);

  TU_VERIFY(xid_itf.ep_in != 0, false);
  TU_VERIFY(tud_ready(), false);
  TU_VERIFY(!usbd_edpt_busy(xid_itf.rhport, xid_itf.ep_in), false);

  if (tud_suspended()) {
    tud_remote_wakeup();
  }

  return usbd_edpt_xfer(xid_itf.rhport, xid_itf.ep_in, xid_itf.input_report, len, false);
}

bool xid_device_get_output_report(void *report, uint16_t len) {
  TU_VERIFY(report != NULL, false);
  TU_VERIFY(len <= sizeof(xid_itf.output_report), false);

  if (!xid_itf.output_report_pending) {
    return false;
  }

  memcpy(report, xid_itf.output_report, len);
  xid_itf.output_report_pending = false;
  return true;
}

static bool xid_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
  (void)rhport;

  if (result == XFER_RESULT_SUCCESS && ep_addr == xid_itf.ep_out) {
    const uint32_t copy_len = tu_min32(xferred_bytes, sizeof(xid_itf.output_report));
    memcpy(xid_itf.output_report, xid_itf.ep_out_buffer, copy_len);
    xid_itf.output_report_len = (uint16_t)copy_len;
    xid_itf.output_report_pending = true;
  }

  if (ep_addr == xid_itf.ep_out && tud_ready() && !usbd_edpt_busy(xid_itf.rhport, xid_itf.ep_out)) {
    usbd_edpt_xfer(xid_itf.rhport, xid_itf.ep_out, xid_itf.ep_out_buffer, sizeof(xid_itf.ep_out_buffer), false);
  }

  return true;
}

bool xid_device_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  if (request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE) {
    return false;
  }

  if ((uint8_t)request->wIndex != xid_itf.itf_num) {
    return false;
  }

  if (request->bmRequestType == 0xA1 && request->bRequest == 0x01 && request->wValue == 0x0100) {
    if (stage == CONTROL_STAGE_SETUP) {
      tud_control_xfer(rhport, request, xid_itf.input_report, tu_min16(request->wLength, sizeof(xid_itf.input_report)));
    }
    return true;
  }

  if (request->bmRequestType == 0x21 && request->bRequest == 0x09 && request->wValue == 0x0200) {
    if (stage == CONTROL_STAGE_SETUP) {
      tud_control_xfer(rhport, request, xid_itf.ep_out_buffer, tu_min16(request->wLength, sizeof(xid_itf.ep_out_buffer)));
    } else if (stage == CONTROL_STAGE_ACK) {
      const uint16_t copy_len = tu_min16(request->wLength, sizeof(xid_itf.output_report));
      memcpy(xid_itf.output_report, xid_itf.ep_out_buffer, copy_len);
      xid_itf.output_report_len = copy_len;
      xid_itf.output_report_pending = true;
    }
    return true;
  }

  return xid_gamepad_control_xfer_cb(rhport, stage, request);
}

static const usbd_class_driver_t xid_driver = {
#if CFG_TUSB_DEBUG >= 2
  .name = "XID",
#endif
  .init = xid_init,
  .reset = xid_reset,
  .open = xid_open,
  .control_xfer_cb = xid_device_control_xfer_cb,
  .xfer_cb = xid_xfer_cb,
  .sof = NULL
};

const usbd_class_driver_t *xid_device_driver() {
  return &xid_driver;
}
