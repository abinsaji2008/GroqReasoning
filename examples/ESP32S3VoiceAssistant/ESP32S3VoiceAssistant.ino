#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WakeNet9Arduino.h>
#include <GroqAudioStream.h>
#include <GroqReasoning.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <stdint.h>
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

const char *INDIAN_TIMEZONE = "IST-5:30";
const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.google.com";
const char *NTP_SERVER_3 = "time.cloudflare.com";
bool ntpConfigured = false;
unsigned long lastNtpAttemptMs = 0;

bool scheduledMessagePending = false;
time_t scheduledMessageEpoch = 0;
String scheduledMessageContact;
String scheduledMessageNumber;
String scheduledMessageText;
unsigned long lastScheduledSendAttemptMs = 0;

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
        message["scheduled"] = wa->scheduled;
        message["scheduledFor"] = wa->scheduledFor;
    } else {
        message["send"] = false;
        message["submitted"] = false;
        message["contact"] = "";
        message["number"] = "";
        message["text"] = "";
        message["error"] = "";
        message["scheduled"] = false;
        message["scheduledFor"] = "";
    }

    serializeJson(output, Serial);
    Serial.println();
}


bool indianClockReady() {
    return time(nullptr) > 1700000000;
}

bool syncIndianClock(bool waitForSync) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    configTzTime(INDIAN_TIMEZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    ntpConfigured = true;
    lastNtpAttemptMs = millis();

    if (!waitForSync) {
        delay(10);
        return indianClockReady();
    }

    const unsigned long started = millis();
    while (millis() - started < 10000) {
        if (indianClockReady()) {
            return true;
        }
        delay(200);
    }
    return indianClockReady();
}


String currentIndianTimeContext() {
    if (!indianClockReady()) return "";

    const time_t now = time(nullptr);
    struct tm localTime;
    if (localtime_r(&now, &localTime) == nullptr) return "";

    char buffer[80];
    strftime(buffer, sizeof(buffer), "%A, %Y-%m-%d %H:%M:%S IST (UTC+05:30)", &localTime);
    return String(buffer);
}

String scheduleDisplayTime(time_t target) {
    struct tm localTime;
    if (localtime_r(&target, &localTime) == nullptr) {
        return "an unknown time";
    }

    char buffer[48];
    strftime(buffer, sizeof(buffer), "%I:%M %p on %d %B", &localTime);
    return String(buffer) + " IST";
}

String lowerWithoutDots(String text) {
    text.toLowerCase();
    text.replace(".", "");
    return text;
}


String deliveryInstructionPrefix(String transcript) {
    String text = lowerWithoutDots(transcript);
    const char *markers[] = {
        " saying ", " say ", " that says ", " that ", " message says ",
        " with the message ", " with text ", " text saying "
    };

    int earliest = -1;
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        const int pos = text.indexOf(markers[i]);
        if (pos >= 0 && (earliest < 0 || pos < earliest)) {
            earliest = pos;
        }
    }
    if (earliest >= 0) {
        text.remove(earliest);
    }
    text.trim();
    return text;
}

bool findClockIntroducer(const String &text, int &timeStart) {
    int selectedPos = -1;
    int selectedStart = -1;
    const char *introducers[] = {" at ", " for "};

    for (size_t i = 0; i < sizeof(introducers) / sizeof(introducers[0]); ++i) {
        int searchFrom = 0;
        while (true) {
            const int pos = text.indexOf(introducers[i], searchFrom);
            if (pos < 0) break;

            int start = pos + strlen(introducers[i]);
            while (start < text.length() && text[start] == ' ') ++start;
            if (start < text.length() && isDigit(text[start])) {
                if (selectedPos < 0 || pos < selectedPos) {
                    selectedPos = pos;
                    selectedStart = start;
                }
                break;
            }
            searchFrom = pos + strlen(introducers[i]);
        }
    }

    if (selectedStart < 0) return false;
    timeStart = selectedStart;
    return true;
}

enum ScheduleParseResult {
    NO_SCHEDULE_REQUEST,
    SCHEDULE_TIME_READY,
    SCHEDULE_NEEDS_CLARIFICATION,
    SCHEDULE_TIME_INVALID
};

ScheduleParseResult parseScheduledDeliveryTime(
    const String &transcript,
    time_t &targetEpoch,
    String &response) {
    String prefix = deliveryInstructionPrefix(transcript);
    const bool mentionsTomorrow = prefix.indexOf("tomorrow") >= 0;
    const bool mentionsLater = prefix.indexOf("later") >= 0 ||
                               prefix.indexOf("schedule") >= 0 ||
                               prefix.indexOf("scheduled") >= 0;
    int timeStart = -1;
    const bool hasClockTime = findClockIntroducer(prefix, timeStart);

    if (!hasClockTime && !mentionsTomorrow && !mentionsLater) {
        return NO_SCHEDULE_REQUEST;
    }

    if (!hasClockTime) {
        response = "What time in India should I send the message? Please say a time such as tomorrow at 8 PM.";
        return SCHEDULE_NEEDS_CLARIFICATION;
    }

    if (!indianClockReady()) {
        response = "I can't schedule the message until the Indian online clock has synchronized. Please check Wi-Fi and try again.";
        return SCHEDULE_NEEDS_CLARIFICATION;
    }

    String after = prefix.substring(timeStart);
    after.trim();

    if (after.startsWith("about ")) after = after.substring(6);
    else if (after.startsWith("around ")) after = after.substring(7);

    int cursor = 0;
    const int digitsBegin = cursor;
    while (cursor < after.length() && isDigit(after[cursor])) ++cursor;
    if (cursor == digitsBegin) {
        response = "I couldn't understand that delivery time. Please say a time such as 8:30 PM.";
        return SCHEDULE_TIME_INVALID;
    }

    String firstDigits = after.substring(digitsBegin, cursor);
    int hour = 0;
    int minute = 0;
    bool hasColon = false;

    if (cursor < after.length() && after[cursor] == ':') {
        hasColon = true;
        hour = firstDigits.toInt();
        ++cursor;
        const int minuteBegin = cursor;
        while (cursor < after.length() && isDigit(after[cursor])) ++cursor;
        if (cursor == minuteBegin) {
            response = "I couldn't read the minutes. Please repeat the time.";
            return SCHEDULE_TIME_INVALID;
        }
        minute = after.substring(minuteBegin, cursor).toInt();
    } else {
        if (firstDigits.length() >= 3) {
            hour = firstDigits.substring(0, firstDigits.length() - 2).toInt();
            minute = firstDigits.substring(firstDigits.length() - 2).toInt();
            hasColon = true;
        } else {
            hour = firstDigits.toInt();
        }
    }

    String suffix = after.substring(cursor);
    suffix.trim();
    const bool isAM = suffix.startsWith("am") || suffix.startsWith("a m");
    const bool isPM = suffix.startsWith("pm") || suffix.startsWith("p m");

    if (minute > 59 || hour > 23) {
        response = "That time is invalid. Please say a valid time such as 8:30 PM.";
        return SCHEDULE_TIME_INVALID;
    }

    if (isAM || isPM) {
        if (hour < 1 || hour > 12) {
            response = "Please say a valid 12-hour time with AM or PM.";
            return SCHEDULE_TIME_INVALID;
        }
        hour %= 12;
        if (isPM) hour += 12;
    } else if (hour <= 12) {
        response = "Should I use AM or PM for that time? Please repeat the delivery time with AM or PM.";
        return SCHEDULE_NEEDS_CLARIFICATION;
    }

    const time_t now = time(nullptr);
    struct tm nowLocal;
    if (localtime_r(&now, &nowLocal) == nullptr) {
        response = "I couldn't read the Indian clock to schedule the message.";
        return SCHEDULE_TIME_INVALID;
    }

    const bool mentionsToday = prefix.indexOf("today") >= 0;
    struct tm targetLocal = nowLocal;
    targetLocal.tm_hour = hour;
    targetLocal.tm_min = minute;
    targetLocal.tm_sec = 0;
    if (mentionsTomorrow) {
        targetLocal.tm_mday += 1;
    }

    targetEpoch = mktime(&targetLocal);
    if (targetEpoch <= now && !mentionsTomorrow) {
        if (mentionsToday) {
            response = "That time has already passed today. Please give me a future time or say tomorrow.";
            return SCHEDULE_NEEDS_CLARIFICATION;
        }
        targetLocal = nowLocal;
        targetLocal.tm_hour = hour;
        targetLocal.tm_min = minute;
        targetLocal.tm_sec = 0;
        targetLocal.tm_mday += 1;
        targetEpoch = mktime(&targetLocal);
    }

    response = "";
    (void)hasColon;
    return SCHEDULE_TIME_READY;
}

void loadScheduledMessage() {
    Preferences prefs;
    if (!prefs.begin("wa_sched", true)) {
        scheduledMessagePending = false;
        return;
    }

    scheduledMessagePending = prefs.getBool("pending", false);
    if (scheduledMessagePending) {
        scheduledMessageEpoch = static_cast<time_t>(prefs.getULong64("epoch", 0));
        scheduledMessageContact = prefs.getString("contact", "");
        scheduledMessageNumber = prefs.getString("number", "");
        scheduledMessageText = prefs.getString("text", "");
        if (scheduledMessageEpoch <= 0 || scheduledMessageContact.isEmpty() ||
            scheduledMessageNumber.isEmpty() || scheduledMessageText.isEmpty()) {
            scheduledMessagePending = false;
        }
    }
    prefs.end();

    if (scheduledMessagePending) {
        Serial.print("[SCHEDULE] Restored pending WhatsApp message for ");
        Serial.print(scheduledMessageContact);
        Serial.print(" at ");
        Serial.println(scheduleDisplayTime(scheduledMessageEpoch));
    }
}

bool saveScheduledMessage(time_t target, const GroqReasoning::WhatsAppDraft &draft) {
    Preferences prefs;
    if (!prefs.begin("wa_sched", false)) return false;

    prefs.putULong64("epoch", static_cast<uint64_t>(target));
    const size_t contactBytes = prefs.putString("contact", draft.contact);
    const size_t numberBytes = prefs.putString("number", draft.number);
    const size_t textBytes = prefs.putString("text", draft.text);
    const bool stored = contactBytes > 0 && numberBytes > 0 && textBytes > 0;
    prefs.putBool("pending", stored);
    prefs.end();

    if (!stored) return false;
    scheduledMessageEpoch = target;
    scheduledMessageContact = draft.contact;
    scheduledMessageNumber = draft.number;
    scheduledMessageText = draft.text;
    scheduledMessagePending = true;
    return true;
}

void clearScheduledMessage() {
    Preferences prefs;
    if (prefs.begin("wa_sched", false)) {
        prefs.putBool("pending", false);
        prefs.remove("epoch");
        prefs.remove("contact");
        prefs.remove("number");
        prefs.remove("text");
        prefs.end();
    }

    scheduledMessagePending = false;
    scheduledMessageEpoch = 0;
    scheduledMessageContact = "";
    scheduledMessageNumber = "";
    scheduledMessageText = "";
    lastScheduledSendAttemptMs = 0;
}

void processScheduledMessage() {
    if (!scheduledMessagePending || !indianClockReady()) return;
    if (time(nullptr) < scheduledMessageEpoch) return;
    if (WiFi.status() != WL_CONNECTED || !reasoningReady) return;

    const unsigned long nowMs = millis();
    if (lastScheduledSendAttemptMs != 0 &&
        nowMs - lastScheduledSendAttemptMs < 60000) {
        return;
    }
    lastScheduledSendAttemptMs = nowMs;

    GroqReasoning::WhatsAppDraft draft;
    draft.send = true;
    draft.contact = scheduledMessageContact;
    draft.number = scheduledMessageNumber;
    draft.text = scheduledMessageText;
    GroqReasoning::WhatsAppConfig config = {
        SEED_EVOLUTION_API_URL,
        SEED_EVOLUTION_INSTANCE,
        SEED_EVOLUTION_API_KEY
    };

    Serial.print("[SCHEDULE] Sending scheduled WhatsApp message to ");
    Serial.println(draft.contact);

    if (reasoning.sendWhatsAppMessage(draft, config)) {
        Serial.print("[SCHEDULE] Evolution API accepted the scheduled message. HTTP ");
        Serial.println(draft.apiStatus);
        clearScheduledMessage();
    } else {
        Serial.print("[SCHEDULE] Send failed; will retry. HTTP ");
        Serial.println(reasoning.lastHttpCode());
        Serial.println(draft.error);
    }
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
        startWakeNet();
        return;
    }

    if (!sttReady || !reasoningReady) {
        printResult("The voice assistant is not ready.");
        startWakeNet();
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

    GroqReasoning::WhatsAppDraft wa;
    String finalResponse;
    String finalAction = "none";

    const bool whatsappRequest =
        reasoning.looksLikeWhatsAppRequest(transcript, CONTACT_MAP, CONTACT_COUNT);

    if (whatsappRequest) {
        time_t deliveryEpoch = 0;
        String scheduleResponse;
        const ScheduleParseResult scheduleResult =
            parseScheduledDeliveryTime(transcript, deliveryEpoch, scheduleResponse);

        if (scheduleResult == SCHEDULE_NEEDS_CLARIFICATION ||
            scheduleResult == SCHEDULE_TIME_INVALID) {
            printResult(scheduleResponse);
            startWakeNet();
            return;
        }

        if (scheduleResult == SCHEDULE_TIME_READY && scheduledMessagePending) {
            finalResponse = "There is already a scheduled WhatsApp message for " +
                            scheduleDisplayTime(scheduledMessageEpoch) +
                            ". It must be sent or cancelled before scheduling another.";
        } else {
            Serial.println("[WHATSAPP AI] Messaging request detected.");
            Serial.println("[WHATSAPP AI] Generating message...");

            const String timeContext = currentIndianTimeContext();
            const bool generated = reasoning.generateWhatsAppDraft(
                transcript,
                CONTACT_MAP,
                CONTACT_COUNT,
                CONTACT_PHONE_MAP,
                CONTACT_PHONE_COUNT,
                wa,
                timeContext.c_str()
            );

            if (!generated) {
                Serial.print("[WHATSAPP AI] HTTP status: ");
                Serial.println(reasoning.lastHttpCode());
                Serial.print("[WHATSAPP AI] Error: ");
                Serial.println(wa.error);
                finalResponse = "I couldn't prepare the WhatsApp message: " + wa.error;
            } else if (!wa.send) {
                finalResponse = "What would you like me to say in the message?";
            } else if (scheduleResult == SCHEDULE_TIME_READY) {
                wa.scheduled = true;
                wa.scheduledFor = scheduleDisplayTime(deliveryEpoch);

                if (saveScheduledMessage(deliveryEpoch, wa)) {
                    finalResponse = "Scheduled the WhatsApp message to " + wa.contact +
                                    " for " + wa.scheduledFor + ".";
                    Serial.print("[SCHEDULE] Saved message for ");
                    Serial.println(wa.scheduledFor);
                } else {
                    wa.scheduled = false;
                    wa.scheduledFor = "";
                    wa.error = "I couldn't save the scheduled message to memory.";
                    finalResponse = wa.error;
                }
            } else {
                GroqReasoning::WhatsAppConfig waConfig = {
                    SEED_EVOLUTION_API_URL,
                    SEED_EVOLUTION_INSTANCE,
                    SEED_EVOLUTION_API_KEY
                };

                const bool submitted = reasoning.sendWhatsAppMessage(wa, waConfig);
                if (submitted) {
                    finalResponse = "WhatsApp message submitted to Evolution API for " + wa.contact + ".";
                } else {
                    Serial.print("[WHATSAPP AI] Send HTTP status: ");
                    Serial.println(reasoning.lastHttpCode());
                    Serial.print("[WHATSAPP AI] Send error: ");
                    Serial.println(wa.error);
                    finalResponse = "WhatsApp message could not be submitted: " + wa.error;
                }
            }
        }
    } else {
        Serial.println("[REASONING] Processing request...");
        const String timeContext = currentIndianTimeContext();
        GroqReasoning::Result result = reasoning.ask(
            transcript,
            DEVICE_MAP,
            DEVICE_COUNT,
            CONTACT_MAP,
            CONTACT_COUNT,
            timeContext.c_str()
        );

        if (!reasoning.lastError().isEmpty()) {
            Serial.print("[REASONING] HTTP status: ");
            Serial.println(reasoning.lastHttpCode());
            Serial.print("[REASONING] Error: ");
            Serial.println(reasoning.lastError());
        }

        finalResponse = result.response;
        finalAction = result.action;
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

    Serial.println("[TIME] Synchronizing online clock to Indian Standard Time...");
    if (syncIndianClock(true)) {
        Serial.print("[TIME] Indian time synchronized: ");
        Serial.println(currentIndianTimeResponse());
    } else {
        Serial.println("[TIME] NTP synchronization is pending; time queries and scheduling will wait for sync.");
    }
    loadScheduledMessage();

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

    if (WiFi.status() == WL_CONNECTED && !indianClockReady() &&
        millis() - lastNtpAttemptMs > 60000) {
        syncIndianClock(false);
    }

    processScheduledMessage();
    delay(1);
}
