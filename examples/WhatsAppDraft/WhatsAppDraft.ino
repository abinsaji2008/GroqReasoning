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

const GroqReasoning::ContactNumberMapping CONTACT_PHONE_MAP[] = {
  {"Mom", "+910000000000"},
  {"Nurse", "+910000000000"}
};

const size_t DEVICE_COUNT = sizeof(DEVICE_MAP) / sizeof(DEVICE_MAP[0]);
const size_t CONTACT_COUNT = sizeof(CONTACT_MAP) / sizeof(CONTACT_MAP[0]);
const size_t CONTACT_PHONE_COUNT =
    sizeof(CONTACT_PHONE_MAP) / sizeof(CONTACT_PHONE_MAP[0]);

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(30000);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SEED_WIFI_SSID, SEED_WIFI_PASS);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.println("\nWi-Fi connected.");

  if (!reasoning.begin(SEED_GROQ_KEY)) {
    Serial.print("Groq initialization failed: ");
    Serial.println(reasoning.lastError());
    return;
  }

  Serial.println("Enter a request in Serial Monitor.");
  Serial.println("Example: Send Mom a WhatsApp message saying I will be home late.");
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }

  String transcript = Serial.readStringUntil('\n');
  transcript.trim();
  if (transcript.isEmpty()) return;

  if (reasoning.looksLikeWhatsAppRequest(transcript, CONTACT_MAP, CONTACT_COUNT)) {
    GroqReasoning::WhatsAppDraft draft;

    const bool generated = reasoning.generateWhatsAppDraft(
        transcript,
        CONTACT_MAP,
        CONTACT_COUNT,
        CONTACT_PHONE_MAP,
        CONTACT_PHONE_COUNT,
        draft
    );

    Serial.println(draft.toJson());
    if (!generated) {
      Serial.print("Draft error: ");
      Serial.println(reasoning.lastError());
    }
    return;
  }

  GroqReasoning::Result result = reasoning.ask(
      transcript,
      DEVICE_MAP,
      DEVICE_COUNT,
      CONTACT_MAP,
      CONTACT_COUNT
  );

  Serial.println(result.toJson());
}
