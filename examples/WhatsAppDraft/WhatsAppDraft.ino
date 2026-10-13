#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GroqReasoning.h>
#include "secrets.h"

const char *GROQ_CHAT_URL = "https://api.groq.com/openai/v1/chat/completions";
const char *WHATSAPP_MODEL = "openai/gpt-oss-20b";

struct WhatsAppDraft {
  bool send = false;
  String contact = "";
  String number = "";
  String text = "";
  String error = "";
};

const GroqReasoning::ContactMapping CONTACT_MAP[] = {
  {"Mom", "mother|mom|amma"},
  {"Nurse", "nurse|care nurse"}
};
const size_t CONTACT_COUNT = sizeof(CONTACT_MAP) / sizeof(CONTACT_MAP[0]);

struct ContactPhoneMapping {
  const char *name;
  const char *number;
};

// Replace these placeholders with the correct numbers for your own contacts.
const ContactPhoneMapping CONTACT_PHONE_MAP[] = {
  {"Mom", "+910000000000"},
  {"Nurse", "+910000000000"}
};
const size_t CONTACT_PHONE_COUNT = sizeof(CONTACT_PHONE_MAP) / sizeof(CONTACT_PHONE_MAP[0]);

bool looksLikeMessageRequest(const String &transcript) {
  String text = transcript;
  text.toLowerCase();

  const bool explicitMessage = text.indexOf("whatsapp") >= 0 ||
                               text.indexOf("message") >= 0 ||
                               text.indexOf("text ") >= 0;
  const bool sendVerb = text.indexOf("send ") >= 0 ||
                        text.indexOf("tell ") >= 0 ||
                        text.indexOf("inform ") >= 0 ||
                        text.indexOf("notify ") >= 0 ||
                        text.indexOf("write to ") >= 0 ||
                        text.indexOf("let ") >= 0;
  const bool knownContact = text.indexOf("mom") >= 0 ||
                            text.indexOf("mother") >= 0 ||
                            text.indexOf("amma") >= 0 ||
                            text.indexOf("nurse") >= 0 ||
                            text.indexOf("care nurse") >= 0;
  return explicitMessage || (sendVerb && knownContact);
}

bool generateWhatsAppDraft(const String &transcript, WhatsAppDraft &draft) {
  draft = WhatsAppDraft();
  if (WiFi.status() != WL_CONNECTED) {
    draft.error = "Wi-Fi is not connected.";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, GROQ_CHAT_URL)) {
    draft.error = "Could not connect to Groq.";
    return false;
  }

  http.addHeader("Authorization", String("Bearer ") + SEED_GROQ_KEY);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument request(4096);
  request["model"] = WHATSAPP_MODEL;
  request["response_format"]["type"] = "json_object";
  request["max_completion_tokens"] = 300;

  JsonArray messages = request.createNestedArray("messages");
  JsonObject systemMessage = messages.createNestedObject();
  systemMessage["role"] = "system";
  systemMessage["content"] =
      "Prepare a WhatsApp message from the user's voice request. "
      "Return only a JSON object with fields send, contact, and text. "
      "Allowed canonical contacts: Mom and Nurse. Mom aliases: mother, mom, amma. "
      "Nurse aliases: nurse, care nurse. Always use the canonical contact name. "
      "Set send=true only if the user explicitly asks for an outbound message. "
      "Write a concise natural message. Do not invent facts or phone numbers, "
      "and do not claim that a message has already been sent. If the user did not "
      "request a message or the recipient is unsupported, return "
      "{\"send\":false,\"contact\":\"\",\"text\":\"\"}. "
      "The text field contains only the message body.";

  JsonObject userMessage = messages.createNestedObject();
  userMessage["role"] = "user";
  userMessage["content"] = transcript;

  String requestBody;
  serializeJson(request, requestBody);
  const int httpCode = http.POST(requestBody);
  String responseBody = http.getString();
  http.end();

  if (httpCode < 200 || httpCode >= 300) {
    draft.error = "Groq message generation failed. HTTP " + String(httpCode);
    return false;
  }

  DynamicJsonDocument apiResponse(8192);
  if (deserializeJson(apiResponse, responseBody)) {
    draft.error = "Could not parse Groq API response.";
    return false;
  }

  String aiContent = apiResponse["choices"][0]["message"]["content"] | String("");
  aiContent.trim();
  if (aiContent.isEmpty()) {
    draft.error = "Groq returned an empty response.";
    return false;
  }

  DynamicJsonDocument parsedMessage(1536);
  if (deserializeJson(parsedMessage, aiContent)) {
    draft.error = "Could not parse the generated WhatsApp draft.";
    return false;
  }

  draft.send = parsedMessage["send"] | false;
  draft.contact = parsedMessage["contact"] | String("");
  draft.text = parsedMessage["text"] | String("");

  if (!draft.send) {
    draft.contact = "";
    draft.text = "";
    return true;
  }

  bool allowedContact = false;
  for (size_t i = 0; i < CONTACT_PHONE_COUNT; ++i) {
    if (draft.contact.equalsIgnoreCase(CONTACT_PHONE_MAP[i].name)) {
      draft.contact = CONTACT_PHONE_MAP[i].name;
      draft.number = CONTACT_PHONE_MAP[i].number;
      allowedContact = true;
      break;
    }
  }

  if (!allowedContact || draft.text.isEmpty()) {
    draft.send = false;
    draft.contact = "";
    draft.number = "";
    draft.text = "";
    draft.error = "The generated draft has an unsupported contact or empty message.";
    return false;
  }

  return true;
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SEED_WIFI_SSID, SEED_WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println("\nConnected. Enter a voice transcript in Serial Monitor to test drafting.");
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }

  String transcript = Serial.readStringUntil('\n');
  transcript.trim();
  WhatsAppDraft draft;

  if (!looksLikeMessageRequest(transcript)) {
    Serial.println("Not detected as a WhatsApp request.");
    return;
  }

  if (!generateWhatsAppDraft(transcript, draft)) {
    Serial.print("Draft generation failed: ");
    Serial.println(draft.error);
    return;
  }

  DynamicJsonDocument output(1024);
  output["send"] = draft.send;
  output["contact"] = draft.contact;
  output["number"] = draft.number;
  output["text"] = draft.text;
  output["error"] = draft.error;
  serializeJson(output, Serial);
  Serial.println();
}
