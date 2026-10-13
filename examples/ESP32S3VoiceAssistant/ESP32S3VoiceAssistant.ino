#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WakeNet9Arduino.h>
#include <GroqAudioStream.h>
#include <GroqReasoning.h>
#include <ArduinoJson.h>
#include "secrets.h"

#ifndef SEED_EVOLUTION_API_URL
#define SEED_EVOLUTION_API_URL ""
#endif
#ifndef SEED_EVOLUTION_INSTANCE
#define SEED_EVOLUTION_INSTANCE ""
#endif
#ifndef SEED_EVOLUTION_API_KEY
#define SEED_EVOLUTION_API_KEY ""
#endif

#define MIC_SD_PIN 16
#define MIC_SCK_PIN 17
#define MIC_WS_PIN 15

const char *GROQ_STT_URL = "https://api.groq.com/openai/v1/audio/transcriptions";
const char *WHISPER_MODEL = "whisper-large-v3-turbo";
const char *WAKE_WORD = "Hi ESP";

WakeNet9Arduino wakeNet;
GroqAudioStream groqAudio;
GroqReasoning reasoning;

WakeNet9Arduino::Config wakeConfig;
GroqAudioStream::Config audioConfig;

const GroqReasoning::DeviceMapping DEVICE_MAP[] = {
    {"primary light", "l1"},
    {"fan", "f1"}
};
const size_t DEVICE_COUNT = sizeof(DEVICE_MAP) / sizeof(DEVICE_MAP[0]);

const GroqReasoning::ContactMapping CONTACT_MAP[] = {
    {"Mom", "mother|mom|amma"},
    {"Nurse", "nurse|care nurse"}
};
const size_t CONTACT_COUNT = sizeof(CONTACT_MAP) / sizeof(CONTACT_MAP[0]);

const GroqReasoning::ContactNumberMapping CONTACT_PHONE_MAP[] = {
    {"Mom", "+91XXXXXXXXXX"},
    {"Nurse", "+91XXXXXXXXXX"}
};
const size_t CONTACT_PHONE_COUNT = sizeof(CONTACT_PHONE_MAP) / sizeof(CONTACT_PHONE_MAP[0]);

volatile bool wakePending = false;
bool wakeStarted = false;
bool sttReady = false;
bool reasoningReady = false;

void onWakeWord() {
    wakePending = true;
}

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(SEED_WIFI_SSID, SEED_WIFI_PASS);

    Serial.print("[WIFI] Connecting");

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 25000) {
        Serial.print(".");
        delay(500);
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Connected.");
        Serial.print("[WIFI] IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[WIFI] Connection timed out. Auto-reconnect enabled.");
    }
}

void printResult(
    const String &response,
    const String &action = "none",
    const GroqReasoning::WhatsAppDraft *wa = nullptr
) {
    DynamicJsonDocument output(2048);
    output["response"] = response;
    output["action"] = action;

    JsonObject message = output.createNestedObject("message");
    if (wa != nullptr) {
        message["send"] = wa->send;
        message["submitted"] = wa->submitted;
        message["contact"] = wa->contact;
        message["number"] = wa->number;
        message["text"] = wa->text;
        message["error"] = wa->error;
    } else {
        message["send"] = false;
        message["submitted"] = false;
        message["contact"] = "";
        message["number"] = "";
        message["text"] = "";
        message["error"] = "";
    }

    serializeJson(output, Serial);
    Serial.println();
}

bool startWakeNet() {
    wakeNet.setWakeWordCallback(onWakeWord);
    wakeStarted = wakeNet.begin(wakeConfig);
    if (!wakeStarted) {
        printResult("Voice detector could not start.");
    }
    return wakeStarted;
}

void handleWakeEvent() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        printResult("I'm not connected to Wi-Fi right now.");
        return;
    }

    if (!sttReady || !reasoningReady) {
        printResult("The voice assistant is not ready.");
        return;
    }

    wakeStarted = false;
    wakeNet.end();

    Serial.println("[STT] Transcribing speech...");
    String transcript = groqAudio.transcribe();
    transcript.trim();

    Serial.print("[STT] Transcript: ");
    Serial.println(transcript);

    if (transcript.isEmpty()) {
        printResult("Sorry, I couldn't understand that.");
        startWakeNet();
        return;
    }

    GroqReasoning::Result result = reasoning.ask(
        transcript,
        DEVICE_MAP,
        DEVICE_COUNT,
        CONTACT_MAP,
        CONTACT_COUNT
    );

    if (!reasoning.lastError().isEmpty()) {
        Serial.print("[REASONING] HTTP status: ");
        Serial.println(reasoning.lastHttpCode());
        Serial.print("[REASONING] Error: ");
        Serial.println(reasoning.lastError());
    }

    String finalResponse = result.response;
    String finalAction = result.action;

    GroqReasoning::WhatsAppDraft wa;

    if (reasoning.looksLikeWhatsAppRequest(transcript, CONTACT_MAP, CONTACT_COUNT)) {
        bool generated = reasoning.generateWhatsAppDraft(
            transcript,
            CONTACT_MAP,
            CONTACT_COUNT,
            CONTACT_PHONE_MAP,
            CONTACT_PHONE_COUNT,
            wa
        );

        if (!generated) {
            if (!finalResponse.isEmpty()) finalResponse += " ";
            finalResponse += "I couldn't prepare the WhatsApp message: " + wa.error;
        } else if (wa.send) {
            GroqReasoning::WhatsAppConfig waConfig = {
                SEED_EVOLUTION_API_URL,
                SEED_EVOLUTION_INSTANCE,
                SEED_EVOLUTION_API_KEY
            };
            bool submitted = reasoning.sendWhatsAppMessage(wa, waConfig);

            if (!finalResponse.isEmpty()) finalResponse += " ";

            if (submitted) {
                finalResponse += "WhatsApp message submitted to Evolution API for " + wa.contact + ".";
            } else {
                finalResponse += "WhatsApp message could not be submitted: " + wa.error;
            }
        } else {
            if (!finalResponse.isEmpty()) finalResponse += " ";
            finalResponse += "What would you like me to say in the message?";
        }
    }

    printResult(finalResponse, finalAction, &wa);
    startWakeNet();
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("====================================");
    Serial.println(" ESP32-S3 AI Voice Assistant");
    Serial.println(" WakeNet9 + Groq + WhatsApp AI");
    Serial.println("====================================");

    connectWiFi();

    wakeConfig.dataPin = MIC_SD_PIN;
    wakeConfig.bclkPin = MIC_SCK_PIN;
    wakeConfig.wsPin = MIC_WS_PIN;
    wakeConfig.wakeWordLabel = WAKE_WORD;

    audioConfig.sdPin = MIC_SD_PIN;
    audioConfig.sckPin = MIC_SCK_PIN;
    audioConfig.wsPin = MIC_WS_PIN;
    groqAudio.setConfig(audioConfig);

    Serial.println("[STT] Initializing...");
    sttReady = groqAudio.begin(
        GROQ_STT_URL,
        SEED_GROQ_KEY,
        WHISPER_MODEL,
        "en"
    );
    Serial.println(sttReady ? "[STT] Initialized." : "[STT] Initialization failed.");

    Serial.println("[REASONING] Initializing...");
    reasoningReady = reasoning.begin(SEED_GROQ_KEY);
    Serial.println(reasoningReady ? "[REASONING] Initialized." : "[REASONING] Initialization failed.");

    Serial.println("[WAKENET] Starting...");
    startWakeNet();
    Serial.println("[SYSTEM] Ready. Say 'Hi ESP'.");
}

void loop() {
    if (wakeStarted) {
        wakeNet.handleSerial();
    }

    if (wakePending) {
        wakePending = false;
        handleWakeEvent();
    }

    delay(1);
}
