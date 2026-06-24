#include <Arduino.h>

#define BRIDGE_UART_RX_PIN 1
#define BRIDGE_UART_TX_PIN 0
#define BRIDGE_BAUD 115200

static void print_help() {
  Serial.println();
  Serial.println("OG Xbox controller UART bridge");
  Serial.println("USB <-> UART1 passthrough at 115200 baud");
  Serial.println();
  Serial.println("Wiring:");
  Serial.println("  Bridge GND  -> Controller GND");
  Serial.println("  Bridge GP0  -> Controller GP1");
  Serial.println("  Bridge GP1  -> Controller GP0");
  Serial.println();
  Serial.println("Example commands:");
  Serial.println("  DOWN 100");
  Serial.println("  TAP A 120");
  Serial.println("  AXIS LX -22000");
  Serial.println("  CLEAR");
  Serial.println();
}

static void pulse_led() {
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, HIGH);
#endif
}

void setup() {
  Serial.begin(BRIDGE_BAUD);
  Serial1.setRX(BRIDGE_UART_RX_PIN);
  Serial1.setTX(BRIDGE_UART_TX_PIN);
  Serial1.begin(BRIDGE_BAUD);

#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
#endif

  uint32_t start = millis();
  while (!Serial && millis() - start < 2000) {
    delay(10);
  }
  print_help();
}

void loop() {
  static uint32_t led_off_at = 0;

  while (Serial.available() > 0) {
    int c = Serial.read();
    Serial1.write((uint8_t)c);
    pulse_led();
    led_off_at = millis() + 20;
  }

  while (Serial1.available() > 0) {
    int c = Serial1.read();
    Serial.write((uint8_t)c);
    pulse_led();
    led_off_at = millis() + 20;
  }

#ifdef LED_BUILTIN
  if (led_off_at != 0 && (int32_t)(millis() - led_off_at) >= 0) {
    digitalWrite(LED_BUILTIN, LOW);
    led_off_at = 0;
  }
#endif
}
