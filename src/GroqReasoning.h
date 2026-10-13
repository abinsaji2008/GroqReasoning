#ifndef GROQ_REASONING_H
#define GROQ_REASONING_H

#include <Arduino.h>

class GroqReasoning {
public:
  struct DeviceMapping {
    const char *name;
    const char *code;  // Example: "l1" produces "l1on" and "l1off".
  };

  struct ContactMapping {
    const char *name;     // Canonical contact name, e.g. "Nurse".
    const char *aliases;  // Alternative names separated with |, e.g. "nurse|care nurse".
  };

  struct ContactNumberMapping {
    const char *name;    // Must match a canonical contact name.
    const char *number;  // International format, e.g. +919876543210.
  };

  struct WhatsAppDraft {
    bool send = false;
    String contact;
    String number;
    String text;
    String error;
    String toJson() const;
  };

  struct Result {
    String response;
    String action;
    String toJson() const;
  };

  // Wi-Fi is managed by the main sketch.
  bool begin(const char *apiKey);
  bool begin(const String &apiKey) { return begin(apiKey.c_str()); }

  // Only contact names and aliases are sent to Groq. Phone numbers are not stored here.
  // A call action is accepted only if the requested contact exists in this allowlist.
  Result ask(const String &transcript,
             const DeviceMapping *devices,
             size_t deviceCount,
             const ContactMapping *contacts = nullptr,
             size_t contactCount = 0);

  bool looksLikeWhatsAppRequest(const String &transcript,
                                 const ContactMapping *contacts = nullptr,
                                 size_t contactCount = 0) const;

  // Creates a draft only; this method does not send a WhatsApp message.
  // Phone numbers stay in firmware and are never sent to Groq.
  bool generateWhatsAppDraft(const String &transcript,
                             const ContactMapping *contacts,
                             size_t contactCount,
                             const ContactNumberMapping *phoneNumbers,
                             size_t phoneNumberCount,
                             WhatsAppDraft &draft);

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
