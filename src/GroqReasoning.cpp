#include "GroqReasoning.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace {
constexpr char CHAT_ENDPOINT[] = "https://api.groq.com/openai/v1/chat/completions";
constexpr char DEFAULT_MODEL[] = "openai/gpt-oss-20b";
constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 30000;
constexpr uint16_t MAX_COMPLETION_TOKENS = 256;

String normalizedName(String value) {
  value.trim();
  value.toLowerCase();
  while (value.length() > 0) {
    const char last = value[value.length() - 1];
    if (last == '.' || last == ',' || last == '!' || last == '?' ||
        last == ':' || last == ';' || last == '\'') {
      value.remove(value.length() - 1);
      value.trim();
    } else {
      break;
    }
  }
  while (value.length() > 0 &&
         (value[0] == '\'' || value[0] == '"')) {
    value.remove(0, 1);
    value.trim();
  }
  return value;
}

bool isCallAction(const String &action) {
  if (!action.startsWith("call:")) return false;
  String person = action.substring(5);
  person.trim();
  return !person.isEmpty() && person.length() <= 64;
}

String makeSystemPrompt(const GroqReasoning::DeviceMapping *devices,
                        size_t deviceCount,
                        const GroqReasoning::ContactMapping *contacts,
                        size_t contactCount) {
  String prompt;
  prompt.reserve(1200 + deviceCount * 140 + contactCount * 100);
  prompt =
      "You are a concise voice assistant and intent router. Return ONLY a valid JSON object "
      "with exactly two string fields: response and action. No markdown or extra text. "
      "The response is a short natural sentence suitable for speaking. The action is one intent string. "
      "For normal conversation or questions, set action to none. Never invent device codes or phone numbers. "
      "The firmware validates every device action and contact locally. Do not claim an action or phone call "
      "has already completed.\n\n"
      "Special actions:\n"
      "- none: no supported action requested.\n"
      "- emergency_call: the user explicitly requests emergency calling.\n"
      "- refill_water: the user asks to refill water.\n"
      "- call:<person>: the user asks to call a person or role. For any call request, put the requested "
      "person/role after call:, even when that person is not listed below; local firmware will check availability. "
      "If it matches a listed contact or alias, use that contact's canonical name. Never create a phone number.\n\n"
      "Available devices and their only valid on/off actions:\n";

  bool printedDevice = false;
  if (devices != nullptr) {
    for (size_t i = 0; i < deviceCount; ++i) {
      if (devices[i].name == nullptr || devices[i].code == nullptr ||
          devices[i].name[0] == '\0' || devices[i].code[0] == '\0') continue;
      printedDevice = true;
      prompt += "- Device: \"";
      prompt += devices[i].name;
      prompt += "\"; on: \"";
      prompt += devices[i].code;
      prompt += "on\"; off: \"";
      prompt += devices[i].code;
      prompt += "off\".\n";
    }
  }
  if (!printedDevice) prompt += "- No devices are configured. Use action none for device requests.\n";

  prompt += "\nAllowed contacts (names and aliases only; phone numbers are handled separately by firmware):\n";
  bool printedContact = false;
  if (contacts != nullptr) {
    for (size_t i = 0; i < contactCount; ++i) {
      if (contacts[i].name == nullptr || contacts[i].name[0] == '\0') continue;
      printedContact = true;
      prompt += "- Contact: \"";
      prompt += contacts[i].name;
      prompt += "\"";
      if (contacts[i].aliases != nullptr && contacts[i].aliases[0] != '\0') {
        prompt += "; aliases: ";
        prompt += contacts[i].aliases;
      }
      prompt += "\n";
    }
  }
  if (!printedContact) prompt += "- No contacts are configured.\n";

  prompt +=
      "Examples: if primary light has base code l1, 'turn on primary light' maps to "
      "{\"response\":\"Turning on the primary light.\",\"action\":\"l1on\"}. "
      "If the user asks to call a person, make the action call:<person> and let firmware validate it. "
      "If the user asks for water, use refill_water. Always return both JSON fields.";
  return prompt;
}

String unavailableContactMessage(const String &person) {
  String label = person;
  label.trim();
  if (label.isEmpty()) label = "that person";
  return "Sorry, I can't contact " + label + " because that person isn't in my contact list.";
}
} // namespace

String GroqReasoning::Result::toJson() const {
  DynamicJsonDocument doc(256 + response.length() * 2 + action.length() * 2);
  doc["response"] = response;
  doc["action"] = action.length() ? action : "none";
  String json;
  serializeJson(doc, json);
  return json;
}

bool GroqReasoning::begin(const char *apiKey) {
  lastError_ = "";
  lastHttpCode_ = 0;
  initialized_ = false;

  if (apiKey == nullptr || apiKey[0] == '\0') {
    lastError_ = "Groq API key is missing.";
    return false;
  }

  apiKey_ = apiKey;
  apiKey_.trim();
  if (apiKey_.isEmpty()) {
    lastError_ = "Groq API key is missing.";
    return false;
  }

  initialized_ = true;
  return true;
}

int GroqReasoning::findContact(const String &person,
                               const ContactMapping *contacts,
                               size_t contactCount) const {
  if (contacts == nullptr) return -1;
  const String wanted = normalizedName(person);
  if (wanted.isEmpty()) return -1;

  for (size_t i = 0; i < contactCount; ++i) {
    if (contacts[i].name == nullptr || contacts[i].name[0] == '\0') continue;
    if (normalizedName(String(contacts[i].name)) == wanted) return static_cast<int>(i);

    if (contacts[i].aliases == nullptr || contacts[i].aliases[0] == '\0') continue;
    String aliases(contacts[i].aliases);
    int start = 0;
    while (start <= aliases.length()) {
      int separator = aliases.indexOf('|', start);
      if (separator < 0) separator = aliases.length();
      String alias = aliases.substring(start, separator);
      if (normalizedName(alias) == wanted) return static_cast<int>(i);
      if (separator >= aliases.length()) break;
      start = separator + 1;
    }
  }
  return -1;
}

bool GroqReasoning::isAllowedAction(const String &action,
                                   const DeviceMapping *devices,
                                   size_t deviceCount) const {
  if (action == "none" || action == "emergency_call" || action == "refill_water") {
    return true;
  }
  if (isCallAction(action)) return true; // Locally contact-validated after JSON parsing.

  if (devices == nullptr) return false;
  for (size_t i = 0; i < deviceCount; ++i) {
    if (devices[i].code == nullptr || devices[i].code[0] == '\0') continue;
    const String code(devices[i].code);
    if (action == code + "on" || action == code + "off") return true;
  }
  return false;
}

GroqReasoning::Result GroqReasoning::ask(const String &transcript,
                                         const DeviceMapping *devices,
                                         size_t deviceCount,
                                         const ContactMapping *contacts,
                                         size_t contactCount) {
  Result result;
  result.response = "Sorry, I couldn't process that request right now.";
  result.action = "none";
  lastError_ = "";
  lastHttpCode_ = 0;

  if (!initialized_) {
    lastError_ = "Call begin(apiKey) before ask().";
    return result;
  }

  String cleanTranscript = transcript;
  cleanTranscript.trim();
  if (cleanTranscript.isEmpty()) {
    result.response = "Sorry, I couldn't understand that.";
    lastError_ = "Transcript is empty.";
    return result;
  }

  if (WiFi.status() != WL_CONNECTED) {
    result.response = "I'm not connected to Wi-Fi right now.";
    lastError_ = "Wi-Fi is not connected.";
    return result;
  }

  WiFiClientSecure tls;
  // Development convenience. Configure certificate verification for production.
  tls.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(REQUEST_TIMEOUT_MS);

  if (!http.begin(tls, CHAT_ENDPOINT)) {
    result.response = "Sorry, I couldn't connect to the assistant service.";
    lastError_ = "Could not initialize the Groq HTTPS request.";
    return result;
  }

  http.addHeader("Authorization", String("Bearer ") + apiKey_);
  http.addHeader("Content-Type", "application/json");

  const String systemPrompt = makeSystemPrompt(devices, deviceCount, contacts, contactCount);
  DynamicJsonDocument requestDoc(2048 + systemPrompt.length() * 2 + cleanTranscript.length() * 2);
  requestDoc["model"] = DEFAULT_MODEL;
  requestDoc["max_completion_tokens"] = MAX_COMPLETION_TOKENS;
  requestDoc["reasoning_effort"] = "low";
  requestDoc["reasoning_format"] = "hidden";
  JsonObject responseFormat = requestDoc.createNestedObject("response_format");
  responseFormat["type"] = "json_object";

  JsonArray messages = requestDoc.createNestedArray("messages");
  JsonObject systemMessage = messages.createNestedObject();
  systemMessage["role"] = "system";
  systemMessage["content"] = systemPrompt;

  JsonObject userMessage = messages.createNestedObject();
  userMessage["role"] = "user";
  userMessage["content"] = cleanTranscript;

  String requestBody;
  serializeJson(requestDoc, requestBody);

  lastHttpCode_ = http.POST(requestBody);
  String responseBody = http.getString();

  if (lastHttpCode_ >= 200 && lastHttpCode_ < 300) {
    DynamicJsonDocument responseDoc(8192);
    DeserializationError error = deserializeJson(responseDoc, responseBody);

    if (error) {
      result.response = "Sorry, I couldn't understand the assistant's reply.";
      lastError_ = String("Could not parse Groq response JSON: ") + error.c_str();
    } else {
      String content = responseDoc["choices"][0]["message"]["content"] | "";
      content.trim();

      if (content.isEmpty()) {
        result.response = "Sorry, the assistant returned an empty reply.";
        lastError_ = "Groq returned no message content.";
      } else {
        DynamicJsonDocument outputDoc(2048 + content.length() * 2);
        DeserializationError outputError = deserializeJson(outputDoc, content);
        if (outputError) {
          result.response = "Sorry, I couldn't process the assistant's reply.";
          lastError_ = String("Groq answer was not valid response/action JSON: ") + outputError.c_str();
        } else {
          result.response = outputDoc["response"] | "";
          result.action = outputDoc["action"] | "none";
          result.response.trim();
          result.action.trim();

          if (result.response.isEmpty()) {
            result.response = "Sorry, I couldn't process that request.";
            result.action = "none";
            lastError_ = "Groq JSON did not contain a response string.";
          } else if (!isAllowedAction(result.action, devices, deviceCount)) {
            result.action = "none";
            result.response = "Sorry, I couldn't safely process that action.";
            lastError_ = "Groq returned an unsupported action; changed to none.";
          } else if (isCallAction(result.action)) {
            const String requestedPerson = result.action.substring(5);
            const int contactIndex = findContact(requestedPerson, contacts, contactCount);

            if (contactIndex < 0) {
              result.response = unavailableContactMessage(requestedPerson);
              result.action = "none";
              lastError_ = "Requested contact is not in the contact list.";
            } else {
              result.action = String("call:") + contacts[contactIndex].name;
            }
          }
        }
      }
    }
  } else if (lastHttpCode_ <= 0) {
    result.response = "Sorry, I couldn't reach the assistant service.";
    lastError_ = String("Groq request failed: ") + HTTPClient::errorToString(lastHttpCode_);
  } else {
    result.response = "Sorry, the assistant service returned an error.";
    lastError_ = String("Groq API returned HTTP ") + lastHttpCode_;
    DynamicJsonDocument errorDoc(2048);
    if (!deserializeJson(errorDoc, responseBody)) {
      const char *message = errorDoc["error"]["message"] | "";
      if (message[0] != '\0') {
        lastError_ += ": ";
        lastError_ += message;
      }
    }
  }

  http.end();
  return result;
}
