#include <Arduino.h>
#include <WiFi.h>
#include <WakeNet9Arduino.h>
#include <GroqAudioStream.h>
#include <GroqReasoning.h>
#include "secrets.h"

#ifndef DEBUG_MODE
#define DEBUG_MODE 0
#endif
#if DEBUG_MODE
  #define DBG(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG(...) do {} while (0)
#endif

#define MIC_SD_PIN  16
#define MIC_SCK_PIN 17
#define MIC_WS_PIN  15

const char *STT_URL = "https://api.groq.com/openai/v1/audio/transcriptions";
const char *STT_MODEL = "whisper-large-v3-turbo";
const char *WAKE_WORD = "Hi ESP";

const GroqReasoning::DeviceMapping DEVICES[] = {
  {"primary light", "l1"},
  {"fan", "f1"}
};

const GroqReasoning::ContactMapping CONTACTS[] = {
  {"Mom", "mother|mom|amma", "+918086999662"},
  {"Nurse", "nurse|care nurse", "+918086999662"}
};

const GroqReasoning::WhatsAppConfig WHATSAPP = {
  SEED_EVOLUTION_API_URL, SEED_EVOLUTION_INSTANCE, SEED_EVOLUTION_API_KEY
};

WakeNet9Arduino wakeNet;
GroqAudioStream groqAudio;
GroqReasoning reasoning;
WakeNet9Arduino::Config wakeConfig;
GroqAudioStream::Config audioConfig;
volatile bool wakePending = false;
bool wakeStarted = false, audioReady = false, aiReady = false;

void onWakeWord() { wakePending = true; }

void startWakeNet() {
  wakeNet.setWakeWordCallback(onWakeWord);
  wakeStarted = wakeNet.begin(wakeConfig);
  DBG("[WAKE] %s\n", wakeStarted ? "ready" : "failed");
}

void handleVoice() {
  wakePending = false;
  wakeNet.end();
  String transcript = groqAudio.transcribe();
  transcript.trim();

  if (transcript.length() && aiReady) {
    DBG("[STT] %s\n", transcript.c_str());
    GroqReasoning::Result result = reasoning.processTranscript(
      transcript, DEVICES, sizeof(DEVICES) / sizeof(DEVICES[0]),
      CONTACTS, sizeof(CONTACTS) / sizeof(CONTACTS[0]), WHATSAPP
    );
    Serial.println(result.toJson()); // One compact JSON line per request.
    DBG("[AI] %s\n", reasoning.lastError().c_str());
  } else if (DEBUG_MODE) {
    DBG("[VOICE] %s\n", audioReady ? "No transcript" : "Audio not ready");
  }
  startWakeNet();
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SEED_WIFI_SSID, SEED_WIFI_PASS);
  for (uint32_t t = millis(); WiFi.status() != WL_CONNECTED && millis() - t < 25000; delay(250)) {}

  aiReady = reasoning.begin(SEED_GROQ_KEY);
  if (aiReady) reasoning.syncIndianClock(true);
  DBG("[AI] %s; [TIME] %s\n", aiReady ? "ready" : reasoning.lastError().c_str(),
      reasoning.currentIndianTimeContext().c_str());

  audioConfig.sdPin = MIC_SD_PIN;
  audioConfig.sckPin = MIC_SCK_PIN;
  audioConfig.wsPin = MIC_WS_PIN;
  groqAudio.setConfig(audioConfig);
  audioReady = groqAudio.begin(STT_URL, SEED_GROQ_KEY, STT_MODEL, "en");
  DBG("[STT] %s\n", audioReady ? "ready" : "failed");

  wakeConfig.dataPin = MIC_SD_PIN;
  wakeConfig.bclkPin = MIC_SCK_PIN;
  wakeConfig.wsPin = MIC_WS_PIN;
  wakeConfig.wakeWordLabel = WAKE_WORD;
  startWakeNet();
}

void loop() {
  if (wakeStarted) wakeNet.handleSerial();
  if (wakePending && audioReady && aiReady) handleVoice();
  reasoning.serviceScheduledMessages(WHATSAPP);
  delay(1);
}
