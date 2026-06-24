#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include "xid_device.h"
#include "xid_gamepad.h"

#define COMMAND_UART_RX_PIN 1
#define COMMAND_UART_TX_PIN 0
#define COMMAND_UART_BAUD 115200
#define COMMAND_LINE_MAX 96
#define DEFAULT_TAP_MS 120

#ifndef OGXB_ENABLE_IDLE_SCRIPT
#define OGXB_ENABLE_IDLE_SCRIPT 1
#endif

#ifndef OGXB_COMMAND_USE_USB_SERIAL
#define OGXB_COMMAND_USE_USB_SERIAL 0
#endif

#if OGXB_COMMAND_USE_USB_SERIAL
#define COMMAND_STREAM Serial
#else
#define COMMAND_STREAM Serial1
#endif

class XidGamepadUsb : public Adafruit_USBD_Interface {
public:
  bool begin() {
#if !OGXB_COMMAND_USE_USB_SERIAL
    SerialTinyUSB.end();
#endif

    if (!TinyUSBDevice.addInterface(*this)) {
      return false;
    }

    TinyUSBDevice.setID(XID_GAMEPAD_VID, XID_GAMEPAD_PID_DUKE);
    TinyUSBDevice.setVersion(0x0110);
    TinyUSBDevice.setDeviceVersion(0x0121);
    TinyUSBDevice.setConfigurationAttribute(TU_BIT(7));
    TinyUSBDevice.setConfigurationMaxPower(100);
    return true;
  }

  uint16_t getInterfaceDescriptor(uint8_t, uint8_t *buf, uint16_t bufsize) override {
    uint8_t itfnum = 0;
    uint8_t ep_in = 0;
    uint8_t ep_out = 0;

    if (buf) {
      itfnum = TinyUSBDevice.allocInterface(1);
      ep_in = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
      ep_out = TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT);
    }

    const uint8_t desc[] = { XID_GAMEPAD_INTERFACE_DESCRIPTOR(itfnum, ep_in, ep_out) };
    if (bufsize < sizeof(desc)) {
      return 0;
    }

    memcpy(buf, desc, sizeof(desc));
    return sizeof(desc);
  }
};

static XidGamepadUsb xid_usb;
static xid_gamepad_report_t report;
static xid_gamepad_rumble_t rumble;
static bool script_enabled = OGXB_ENABLE_IDLE_SCRIPT != 0;
static bool command_mode_seen = false;
static uint32_t clear_report_at_ms = 0;
static char command_line[COMMAND_LINE_MAX];
static uint8_t command_line_len = 0;

static void set_status_led(bool on) {
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#endif
}

static void neutral_report() {
  xid_gamepad_clear_report(&report);
  clear_report_at_ms = 0;
}

static void scripted_input(uint32_t now_ms) {
  neutral_report();

  const uint32_t phase = now_ms % 4000;

  if (phase < 2000) {
    report.digital_buttons = XID_DPAD_DOWN;
  } else {
    report.digital_buttons = XID_DPAD_UP;
  }
}

static void command_reply(const char *text) {
  COMMAND_STREAM.print(text);
  COMMAND_STREAM.print("\r\n");
}

static int parse_int_token(const char *token, int fallback) {
  if (!token || !*token) {
    return fallback;
  }
  return atoi(token);
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

static bool set_named_control(const char *name, int value) {
  value = clamp_int(value, 0, 255);

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
    else report.digital_buttons &= ~XID_DPAD_UP;
  } else if (!strcmp(name, "DOWN") || !strcmp(name, "DN")) {
    if (value) report.digital_buttons |= XID_DPAD_DOWN;
    else report.digital_buttons &= ~XID_DPAD_DOWN;
  } else if (!strcmp(name, "LEFT")) {
    if (value) report.digital_buttons |= XID_DPAD_LEFT;
    else report.digital_buttons &= ~XID_DPAD_LEFT;
  } else if (!strcmp(name, "RIGHT")) {
    if (value) report.digital_buttons |= XID_DPAD_RIGHT;
    else report.digital_buttons &= ~XID_DPAD_RIGHT;
  } else if (!strcmp(name, "START")) {
    if (value) report.digital_buttons |= XID_START;
    else report.digital_buttons &= ~XID_START;
  } else if (!strcmp(name, "BACK")) {
    if (value) report.digital_buttons |= XID_BACK;
    else report.digital_buttons &= ~XID_BACK;
  } else if (!strcmp(name, "LS") || !strcmp(name, "LSTICK")) {
    if (value) report.digital_buttons |= XID_LEFT_STICK;
    else report.digital_buttons &= ~XID_LEFT_STICK;
  } else if (!strcmp(name, "RS") || !strcmp(name, "RSTICK")) {
    if (value) report.digital_buttons |= XID_RIGHT_STICK;
    else report.digital_buttons &= ~XID_RIGHT_STICK;
  } else {
    return false;
  }

  return true;
}

static bool set_axis(const char *name, int value) {
  value = clamp_int(value, -32768, 32767);

  if (!strcmp(name, "LX")) {
    report.left_stick_x = value;
  } else if (!strcmp(name, "LY")) {
    report.left_stick_y = value;
  } else if (!strcmp(name, "RX")) {
    report.right_stick_x = value;
  } else if (!strcmp(name, "RY")) {
    report.right_stick_y = value;
  } else {
    return false;
  }

  return true;
}

static void uppercase_line(char *line) {
  for (char *p = line; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') {
      *p = (char)(*p - 'a' + 'A');
    }
  }
}

static void schedule_clear(uint32_t duration_ms) {
  if (duration_ms == 0) {
    clear_report_at_ms = 0;
  } else {
    clear_report_at_ms = millis() + duration_ms;
  }
}

static void process_command(char *line) {
  uppercase_line(line);

  char *cmd = strtok(line, " \t");
  if (!cmd) {
    return;
  }

  char *arg1 = strtok(NULL, " \t");
  char *arg2 = strtok(NULL, " \t");
  char *arg3 = strtok(NULL, " \t");

  if (!strcmp(cmd, "PING")) {
    command_reply("OK PONG");
    return;
  }

  if (!strcmp(cmd, "HELP")) {
    command_reply("OK COMMANDS: TAP A 100 | DOWN 100 | HOLD A | RELEASE A | AXIS LX -22000 | TRIG R 255 | CLEAR | SCRIPT ON");
    return;
  }

  if (!strcmp(cmd, "STATUS")) {
    COMMAND_STREAM.print("OK STATUS script=");
    COMMAND_STREAM.print(script_enabled ? "on" : "off");
    COMMAND_STREAM.print(" buttons=");
    COMMAND_STREAM.print(report.digital_buttons, HEX);
    COMMAND_STREAM.print(" A=");
    COMMAND_STREAM.print(report.a);
    COMMAND_STREAM.print(" B=");
    COMMAND_STREAM.print(report.b);
    COMMAND_STREAM.print(" LX=");
    COMMAND_STREAM.print(report.left_stick_x);
    COMMAND_STREAM.print(" LY=");
    COMMAND_STREAM.print(report.left_stick_y);
    COMMAND_STREAM.print("\r\n");
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
    if (arg1 && arg2 && set_axis(arg1, parse_int_token(arg2, 0))) {
      command_reply("OK AXIS");
    } else {
      command_reply("ERR AXIS");
    }
    return;
  }

  if (!strcmp(cmd, "TRIG")) {
    if (!arg1 || !arg2) {
      command_reply("ERR TRIG");
      return;
    }
    if (!strcmp(arg1, "L")) {
      report.left_trigger = clamp_int(parse_int_token(arg2, 0), 0, 255);
      command_reply("OK TRIG");
    } else if (!strcmp(arg1, "R")) {
      report.right_trigger = clamp_int(parse_int_token(arg2, 0), 0, 255);
      command_reply("OK TRIG");
    } else {
      command_reply("ERR TRIG");
    }
    return;
  }

  if (!strcmp(cmd, "BTN") || !strcmp(cmd, "SET")) {
    if (arg1 && arg2 && set_named_control(arg1, parse_int_token(arg2, 0))) {
      command_reply("OK SET");
    } else {
      command_reply("ERR SET");
    }
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

static void poll_command_uart() {
  while (COMMAND_STREAM.available() > 0) {
    char c = (char)COMMAND_STREAM.read();

    if (c == '\r' || c == '\n') {
      if (command_line_len > 0) {
        command_line[command_line_len] = '\0';
        process_command(command_line);
        command_line_len = 0;
      }
      continue;
    }

    if (command_line_len + 1 < sizeof(command_line)) {
      command_line[command_line_len++] = c;
    } else {
      command_line_len = 0;
      command_reply("ERR LINE TOO LONG");
    }
  }
}

void setup() {
  rp2040.enableDoubleResetBootloader();

#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  set_status_led(false);
#endif

  neutral_report();
  xid_gamepad_send(&report);
  memset(&rumble, 0, sizeof(rumble));

#if OGXB_COMMAND_USE_USB_SERIAL
  Serial.begin(COMMAND_UART_BAUD);
#else
  Serial1.setRX(COMMAND_UART_RX_PIN);
  Serial1.setTX(COMMAND_UART_TX_PIN);
  Serial1.begin(COMMAND_UART_BAUD);
#endif
  command_reply("OK OGXB READY");

  xid_usb.begin();
}

void loop() {
  static uint32_t last_report_ms = 0;
  static uint8_t last_left_motor = 0;
  static uint8_t last_right_motor = 0;

  const uint32_t now = millis();
  poll_command_uart();

  if (clear_report_at_ms != 0 && (int32_t)(now - clear_report_at_ms) >= 0) {
    neutral_report();
  }

  if (now - last_report_ms >= 8) {
    if (OGXB_ENABLE_IDLE_SCRIPT && script_enabled && !command_mode_seen) {
      scripted_input(now);
    }
    xid_gamepad_send(&report);
    last_report_ms = now;
  }

  if (xid_gamepad_get_rumble(&rumble)) {
    if (rumble.left_motor != last_left_motor || rumble.right_motor != last_right_motor) {
      last_left_motor = rumble.left_motor;
      last_right_motor = rumble.right_motor;
      set_status_led((last_left_motor | last_right_motor) != 0);
    }
  }
}

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count += 1;
  return xid_device_driver();
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  return xid_device_control_xfer_cb(rhport, stage, request);
}
