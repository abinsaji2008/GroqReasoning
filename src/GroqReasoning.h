#ifndef GROQ_REASONING_H
#define GROQ_REASONING_H

#include <Arduino.h>

class GroqReasoning {
public:
  struct DeviceMapping {
    const char *name;
    const char *code;
  };

  struct ContactMapping {
    const char *name;
    const char *aliases;
  };

  struct ContactNumberMapping {
    const char *name;
    const char *number;
  };

  struct WhatsAppDraft {
    bool send = false;
    bool submitted = false;
    String contact;
    String number;
    String text;
    String error;
    String toJson() const;
  };

  struct WhatsAppConfig {
    const char *apiUrl;
    const char *instance;
    const char *apiKey;
  };

  struct Result {
    String response;
    String action;
    String toJson() const;
  };

  bool begin(const char *apiKey);
  bool begin(const String &apiKey) { return begin(apiKey.c_str()); }

  Result ask(const String &transcript,
             const DeviceMapping *devices,
             size_t deviceCount,
             const ContactMapping *contacts = nullptr,
             size_t contactCount = 0);

  bool looksLikeWhatsAppRequest(const String &transcript,
                                const ContactMapping *contacts = nullptr,
                                size_t contactCount = 0) const;

  bool generateWhatsAppDraft(const String &transcript,
                             const ContactMapping *contacts,
                             size_t contactCount,
                             const ContactNumberMapping *phoneNumbers,
                             size_t phoneNumberCount,
                             WhatsAppDraft &draft);

  bool sendWhatsAppMessage(WhatsAppDraft &draft,
                           const WhatsAppConfig &config);

  String lastError() const { return lastError_; }
  int lastHttpCode() const { return lastHttpCode_; }

private:
  int findContact(const String &person,
                  const ContactMapping *contacts,
                  size_t contactCount) const;

  bool isAllowedAction(const String &action,
                       const DeviceMapping *devices,
                       size_t deviceCount) const;

  String apiKey_;
  String lastError_;
  int lastHttpCode_ = 0;
  bool initialized_ = false;
};

#endif
