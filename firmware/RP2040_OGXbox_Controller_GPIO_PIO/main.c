#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"

#ifndef OGXB_PIO_USB_DP_PIN
#define OGXB_PIO_USB_DP_PIN 2
#endif

#ifndef OGXB_ENABLE_IDLE_SCRIPT
#define OGXB_ENABLE_IDLE_SCRIPT 0
#endif

#define COMMAND_LINE_MAX 96
#define DEFAULT_TAP_MS 120
#define XID_INTERFACE_CLASS 0x58
#define XID_INTERFACE_SUBCLASS 0x42
#define XID_MAX_PACKET_SIZE 32
#define XID_EP_IN 0x81
#define XID_EP_OUT 0x02

#define XID_DPAD_UP (1u << 0)
#define XID_DPAD_DOWN (1u << 1)
#define XID_DPAD_LEFT (1u << 2)
#define XID_DPAD_RIGHT (1u << 3)
#define XID_START (1u << 4)
#define XID_BACK (1u << 5)
#define XID_LEFT_STICK (1u << 6)
#define XID_RIGHT_STICK (1u << 7)

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

static const uint8_t desc_device[] = {
    18, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 64, 0x5e, 0x04, 0x02, 0x02,
    0x21, 0x01, 0x01, 0x02, 0x03, 0x01,
};

static const uint8_t desc_configuration[] = {
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x02, XID_INTERFACE_CLASS, XID_INTERFACE_SUBCLASS, 0x00, 0x00,
    0x07, 0x05, XID_EP_IN, 0x03, XID_MAX_PACKET_SIZE, 0x00, 0x04,
    0x07, 0x05, XID_EP_OUT, 0x03, XID_MAX_PACKET_SIZE, 0x00, 0x04,
};

static const uint8_t xid_descriptor[] = {
    0x10, 0x42, 0x00, 0x01, 0x01, 0x01, sizeof(xid_gamepad_report_t),
    sizeof(xid_gamepad_rumble_t), 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static const uint8_t xid_probe_device_prefix[] = {
    0x12, 0x01, 0x10, 0x01, 0x00, 0x00,
};

static const uint8_t xid_probe_hid_config[] = {
    0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, 0x65, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x04,
    0x07, 0x05, 0x02, 0x03, 0x40, 0x00, 0x04,
};

static const uint8_t capabilities_in[] = {
    0x00, sizeof(xid_gamepad_report_t), 0xff, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static const uint8_t capabilities_out[] = {
    0x00, sizeof(xid_gamepad_rumble_t), 0xff, 0xff, 0xff, 0xff,
};

static const char *string_descriptors_base[] = {
    [0] = "\x09\x04",
    [1] = "Microsoft",
    [2] = "Controller",
    [3] = "00000001",
};

static const uint8_t *empty_report_desc[] = {NULL};
static string_descriptor_t string_descriptors[4];
static usb_descriptor_buffers_t descriptors = {
    .device = desc_device,
    .config = desc_configuration,
    .hid_report = empty_report_desc,
    .string = string_descriptors,
};

static critical_section_t report_lock;
static usb_device_t *usb_device;
static xid_gamepad_report_t report;
static xid_gamepad_rumble_t rumble;
static bool script_enabled = OGXB_ENABLE_IDLE_SCRIPT != 0;
static bool command_mode_seen;
static uint32_t clear_report_at_ms;
static char command_line[COMMAND_LINE_MAX];
static uint8_t command_line_len;
static uint8_t rumble_ep_buffer[XID_MAX_PACKET_SIZE];
static uint8_t rumble_control_buffer[XID_MAX_PACKET_SIZE];

static uint16_t setup_length(const usb_setup_packet_t *packet) {
  return packet->length_lsb | ((uint16_t)packet->length_msb << 8);
}

static uint16_t setup_value(const usb_setup_packet_t *packet) {
  return packet->value_lsb | ((uint16_t)packet->value_msb << 8);
}

static uint32_t millis(void) {
  return to_ms_since_boot(get_absolute_time());
}

static void neutral_report(void) {
  critical_section_enter_blocking(&report_lock);
  memset(&report, 0, sizeof(report));
  report.length = sizeof(report);
  clear_report_at_ms = 0;
  critical_section_exit(&report_lock);
}

static void copy_report(xid_gamepad_report_t *dest) {
  critical_section_enter_blocking(&report_lock);
  *dest = report;
  critical_section_exit(&report_lock);
}

static void scripted_input(uint32_t now_ms) {
  xid_gamepad_report_t next = {0};
  next.length = sizeof(next);
  next.digital_buttons = ((now_ms % 4000) < 2000) ? XID_DPAD_DOWN : XID_DPAD_UP;

  critical_section_enter_blocking(&report_lock);
  report = next;
  critical_section_exit(&report_lock);
}

static void command_reply(const char *text) {
  printf("%s\r\n", text);
  fflush(stdout);
}

static int clamp_int(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

static int parse_int_token(const char *token, int fallback) {
  return (token && *token) ? atoi(token) : fallback;
}

static bool set_named_control(const char *name, int value) {
  value = clamp_int(value, 0, 255);

  critical_section_enter_blocking(&report_lock);
  if (!strcmp(name, "A")) {
    report.a = value;
  } else if (!strcmp(name, "B")) {
    report.b = value;
  } else if (!strcmp(name, "X")) {
    report.x = value;
  } else if (!strcmp(name, "Y")) {
    report.y = value;
  } else if (!strcmp(name, "BLACK") || !strcmp(name, "BLK")) {
    report.black = value;
  } else if (!strcmp(name, "WHITE") || !strcmp(name, "WHT")) {
    report.white = value;
  } else if (!strcmp(name, "L") || !strcmp(name, "LT")) {
    report.left_trigger = value;
  } else if (!strcmp(name, "R") || !strcmp(name, "RT")) {
    report.right_trigger = value;
  } else if (!strcmp(name, "UP")) {
    if (value) report.digital_buttons |= XID_DPAD_UP;
    else report.digital_buttons &= (uint8_t)~XID_DPAD_UP;
  } else if (!strcmp(name, "DOWN") || !strcmp(name, "DN")) {
    if (value) report.digital_buttons |= XID_DPAD_DOWN;
    else report.digital_buttons &= (uint8_t)~XID_DPAD_DOWN;
  } else if (!strcmp(name, "LEFT")) {
    if (value) report.digital_buttons |= XID_DPAD_LEFT;
    else report.digital_buttons &= (uint8_t)~XID_DPAD_LEFT;
  } else if (!strcmp(name, "RIGHT")) {
    if (value) report.digital_buttons |= XID_DPAD_RIGHT;
    else report.digital_buttons &= (uint8_t)~XID_DPAD_RIGHT;
  } else if (!strcmp(name, "START")) {
    if (value) report.digital_buttons |= XID_START;
    else report.digital_buttons &= (uint8_t)~XID_START;
  } else if (!strcmp(name, "BACK")) {
    if (value) report.digital_buttons |= XID_BACK;
    else report.digital_buttons &= (uint8_t)~XID_BACK;
  } else if (!strcmp(name, "LS") || !strcmp(name, "LSTICK")) {
    if (value) report.digital_buttons |= XID_LEFT_STICK;
    else report.digital_buttons &= (uint8_t)~XID_LEFT_STICK;
  } else if (!strcmp(name, "RS") || !strcmp(name, "RSTICK")) {
    if (value) report.digital_buttons |= XID_RIGHT_STICK;
    else report.digital_buttons &= (uint8_t)~XID_RIGHT_STICK;
  } else {
    critical_section_exit(&report_lock);
    return false;
  }
  critical_section_exit(&report_lock);
  return true;
}

static bool set_axis(const char *name, int value) {
  value = clamp_int(value, -32768, 32767);

  critical_section_enter_blocking(&report_lock);
  if (!strcmp(name, "LX")) {
    report.left_stick_x = (int16_t)value;
  } else if (!strcmp(name, "LY")) {
    report.left_stick_y = (int16_t)value;
  } else if (!strcmp(name, "RX")) {
    report.right_stick_x = (int16_t)value;
  } else if (!strcmp(name, "RY")) {
    report.right_stick_y = (int16_t)value;
  } else {
    critical_section_exit(&report_lock);
    return false;
  }
  critical_section_exit(&report_lock);
  return true;
}

static void uppercase_line(char *line) {
  for (char *p = line; *p; ++p) {
    *p = (char)toupper((unsigned char)*p);
  }
}

static void schedule_clear(uint32_t duration_ms) {
  clear_report_at_ms = duration_ms ? millis() + duration_ms : 0;
}

static void print_status(void) {
  xid_gamepad_report_t snapshot;
  copy_report(&snapshot);
  printf("OK STATUS script=%s buttons=%02x A=%u B=%u LX=%d LY=%d rumble=%u,%u\r\n",
         script_enabled ? "on" : "off", snapshot.digital_buttons, snapshot.a,
         snapshot.b, snapshot.left_stick_x, snapshot.left_stick_y,
         rumble.left_motor, rumble.right_motor);
  fflush(stdout);
}

static void process_command(char *line) {
  uppercase_line(line);

  char *cmd = strtok(line, " \t");
  if (!cmd) {
    return;
  }

  char *arg1 = strtok(NULL, " \t");
  char *arg2 = strtok(NULL, " \t");

  if (!strcmp(cmd, "PING")) {
    command_reply("OK PONG");
    return;
  }

  if (!strcmp(cmd, "HELP")) {
    command_reply("OK COMMANDS: TAP A 100 | DOWN 100 | HOLD A | RELEASE A | AXIS LX -22000 | TRIG R 255 | CLEAR | SCRIPT ON");
    return;
  }

  if (!strcmp(cmd, "STATUS")) {
    print_status();
    return;
  }

  command_mode_seen = true;

  if (!strcmp(cmd, "SCRIPT")) {
    if (arg1 && !strcmp(arg1, "ON")) {
#if OGXB_ENABLE_IDLE_SCRIPT
      script_enabled = true;
      command_mode_seen = false;
      neutral_report();
      command_reply("OK SCRIPT ON");
#else
      script_enabled = false;
      neutral_report();
      command_reply("ERR SCRIPT DISABLED AT BUILD TIME");
#endif
    } else {
      script_enabled = false;
      neutral_report();
      command_reply("OK SCRIPT OFF");
    }
    return;
  }

  script_enabled = false;

  if (!strcmp(cmd, "CLEAR") || !strcmp(cmd, "NEUTRAL")) {
    neutral_report();
    command_reply("OK CLEAR");
    return;
  }

  if (!strcmp(cmd, "AXIS")) {
    command_reply((arg1 && arg2 && set_axis(arg1, parse_int_token(arg2, 0))) ? "OK AXIS" : "ERR AXIS");
    return;
  }

  if (!strcmp(cmd, "TRIG")) {
    if (!arg1 || !arg2) {
      command_reply("ERR TRIG");
      return;
    }
    if (!strcmp(arg1, "L")) {
      set_named_control("L", parse_int_token(arg2, 0));
      command_reply("OK TRIG");
    } else if (!strcmp(arg1, "R")) {
      set_named_control("R", parse_int_token(arg2, 0));
      command_reply("OK TRIG");
    } else {
      command_reply("ERR TRIG");
    }
    return;
  }

  if (!strcmp(cmd, "BTN") || !strcmp(cmd, "SET")) {
    command_reply((arg1 && arg2 && set_named_control(arg1, parse_int_token(arg2, 0))) ? "OK SET" : "ERR SET");
    return;
  }

  if (!strcmp(cmd, "HOLD")) {
    if (arg1 && set_named_control(arg1, 255)) {
      clear_report_at_ms = 0;
      command_reply("OK HOLD");
    } else {
      command_reply("ERR HOLD");
    }
    return;
  }

  if (!strcmp(cmd, "RELEASE")) {
    if (!arg1) {
      neutral_report();
      command_reply("OK RELEASE ALL");
    } else if (set_named_control(arg1, 0)) {
      command_reply("OK RELEASE");
    } else {
      command_reply("ERR RELEASE");
    }
    return;
  }

  if (!strcmp(cmd, "TAP")) {
    if (arg1 && set_named_control(arg1, 255)) {
      schedule_clear((uint32_t)parse_int_token(arg2, DEFAULT_TAP_MS));
      command_reply("OK TAP");
    } else {
      command_reply("ERR TAP");
    }
    return;
  }

  if (set_named_control(cmd, 255)) {
    schedule_clear((uint32_t)parse_int_token(arg1, DEFAULT_TAP_MS));
    command_reply("OK TAP");
    return;
  }

  if (set_axis(cmd, parse_int_token(arg1, 0))) {
    command_reply("OK AXIS");
    return;
  }

  command_reply("ERR UNKNOWN");
}

static void poll_commands(void) {
  while (true) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) {
      return;
    }

    if (c == '\r' || c == '\n') {
      if (command_line_len > 0) {
        command_line[command_line_len] = '\0';
        process_command(command_line);
        command_line_len = 0;
      }
      continue;
    }

    if ((size_t)command_line_len + 1 < sizeof(command_line)) {
      command_line[command_line_len++] = (char)c;
    } else {
      command_line_len = 0;
      command_reply("ERR LINE TOO LONG");
    }
  }
}

static void init_string_desc(void) {
  for (int idx = 0; idx < 4; idx++) {
    uint8_t len = 0;
    uint16_t *wchar_str = (uint16_t *)&string_descriptors[idx];
    if (idx == 0) {
      wchar_str[1] = (uint8_t)string_descriptors_base[0][0] |
                     ((uint16_t)(uint8_t)string_descriptors_base[0][1] << 8);
      len = 1;
    } else {
      len = (uint8_t)strnlen(string_descriptors_base[idx], 31);
      for (uint8_t i = 0; i < len; i++) {
        wchar_str[i + 1] = (uint8_t)string_descriptors_base[idx][i];
      }
    }
    wchar_str[0] = (uint16_t)((0x03 << 8) | (2 * len + 2));
  }
}

static void send_control_data(const usb_setup_packet_t *packet,
                              const uint8_t *data,
                              uint8_t len) {
  uint16_t requested = setup_length(packet);
  uint8_t actual = (requested < len) ? (uint8_t)requested : len;
  pio_usb_device_prepare_control_data((uint8_t *)data, actual);
}

bool pio_usb_device_control_request_cb(uint8_t *setup_packet) {
  const usb_setup_packet_t *packet = (const usb_setup_packet_t *)setup_packet;
  uint16_t value = setup_value(packet);
  uint16_t length = setup_length(packet);

  if (packet->request_type == 0xc1 && packet->request == 0x06 && value == 0x4200) {
    if (length == sizeof(xid_probe_device_prefix)) {
      send_control_data(packet, xid_probe_device_prefix, sizeof(xid_probe_device_prefix));
    } else if (length == 0x50) {
      send_control_data(packet, xid_probe_hid_config, sizeof(xid_probe_hid_config));
    } else {
      send_control_data(packet, xid_descriptor, sizeof(xid_descriptor));
    }
    return true;
  }

  if (packet->request_type == 0xc1 && packet->request == 0x01 && value == 0x0100) {
    send_control_data(packet, capabilities_in, sizeof(capabilities_in));
    return true;
  }

  if (packet->request_type == 0xc1 && packet->request == 0x01 && value == 0x0200) {
    send_control_data(packet, capabilities_out, sizeof(capabilities_out));
    return true;
  }

  if (packet->request_type == 0xa1 && packet->request == 0x01 && value == 0x0100) {
    static xid_gamepad_report_t control_report;
    copy_report(&control_report);
    send_control_data(packet, (const uint8_t *)&control_report, sizeof(control_report));
    return true;
  }

  if (packet->request_type == 0x21 && packet->request == 0x09 && value == 0x0200) {
    uint8_t actual = (length < sizeof(rumble_control_buffer)) ? (uint8_t)length : sizeof(rumble_control_buffer);
    pio_usb_device_prepare_control_rx(rumble_control_buffer, actual);
    return true;
  }

  return false;
}

static void core1_main(void) {
  sleep_ms(10);
  init_string_desc();

  static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
  config.pin_dp = OGXB_PIO_USB_DP_PIN;
  usb_device = pio_usb_device_init(&config, &descriptors);

  uint32_t last_report_ms = 0;
  while (true) {
    pio_usb_device_task();

    endpoint_t *ep_out = pio_usb_device_get_endpoint_by_address(XID_EP_OUT);
    if (ep_out && ep_out->size && !ep_out->is_tx && !ep_out->has_transfer && !ep_out->new_data_flag) {
      pio_usb_ll_transfer_start(ep_out, rumble_ep_buffer, sizeof(rumble_ep_buffer));
    }
    if (ep_out) {
      xid_gamepad_rumble_t next_rumble;
      int rx_len = pio_usb_get_in_data(ep_out, (uint8_t *)&next_rumble, sizeof(next_rumble));
      if (rx_len >= (int)sizeof(next_rumble)) {
        rumble = next_rumble;
      }
    }

    uint32_t now = millis();
    if (now - last_report_ms >= 8) {
      endpoint_t *ep_in = pio_usb_device_get_endpoint_by_address(XID_EP_IN);
      if (ep_in && ep_in->size && ep_in->is_tx && !ep_in->has_transfer) {
        xid_gamepad_report_t snapshot;
        copy_report(&snapshot);
        pio_usb_set_out_data(ep_in, (const uint8_t *)&snapshot, sizeof(snapshot));
        last_report_ms = now;
      }
    }
  }
}

int main(void) {
  set_sys_clock_khz(120000, true);
  stdio_init_all();
  critical_section_init(&report_lock);
  neutral_report();
  memset(&rumble, 0, sizeof(rumble));

  sleep_ms(1000);
  command_reply("OK OGXB GPIO PIO READY");

  multicore_reset_core1();
  multicore_launch_core1(core1_main);

  while (true) {
    uint32_t now = millis();
    poll_commands();

    if (clear_report_at_ms != 0 && (int32_t)(now - clear_report_at_ms) >= 0) {
      neutral_report();
    }

    if (OGXB_ENABLE_IDLE_SCRIPT && script_enabled && !command_mode_seen) {
      scripted_input(now);
    }

    sleep_ms(1);
  }
}
