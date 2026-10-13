#include <Arduino.h>
#include <WiFi.h>
#include <GroqReasoning.h>
#include "secrets.h"

GroqReasoning reasoning;

const GroqReasoning::DeviceMapping DEVICE_MAP[] = {
  {"primary light", "l1"},
  {"fan", "f1"}
};

const GroqReasoning::ContactMapping CONTACT_MAP[] = {
  {"Mom", "mother|mom|amma"},
  {"Nurse", "nurse|care nurse"}
};

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SEED_WIFI_SSID, SEED_WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  if (!reasoning.begin(SEED_GROQ_KEY)) {
    Serial.println("[ERROR] Reasoning initialization failed.");
    return;
  }

  GroqReasoning::Result result = reasoning.ask(
      "Call the nurse",
      DEVICE_MAP,
      sizeof(DEVICE_MAP) / sizeof(DEVICE_MAP[0]),
      CONTACT_MAP,
      sizeof(CONTACT_MAP) / sizeof(CONTACT_MAP[0])
  );
  Serial.println(result.toJson());
}

void loop() {}
