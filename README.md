# GroqReasoning v1.3.0

An ESP32 Arduino library that sends a transcript to Groq and returns a JSON object with exactly two string fields:

- `response`: a short user-facing answer.
- `action`: a validated intent such as `l1on`, `f1off`, `refill_water`, `emergency_call`, `call:Nurse`, or `none`.

The library does not execute device actions, start a pump, or place phone calls. The main sketch owns device/contact arrays and decides what to do with the validated intent.

## Built-in defaults

- Endpoint: `https://api.groq.com/openai/v1/chat/completions`
- Model: `openai/gpt-oss-20b`
- Reasoning effort: `low`
- Reasoning format: `hidden`
- JSON response mode: enabled
- Maximum completion tokens: `256`
- Connect timeout: `10 seconds`
- Request timeout: `30 seconds`

## Device actions

Define device mappings in the main sketch. Each base code permits `<code>on` and `<code>off` only.

```cpp
const GroqReasoning::DeviceMapping DEVICE_MAP[] = {
  {"primary light", "l1"},
  {"fan", "f1"}
};
```

Example result:

```json
{"response":"Turning on the primary light.","action":"l1on"}
```

## Contacts: name and aliases only

The contact mapping now has **only two fields**: canonical contact name and alternative names. No phone number belongs in this array.

```cpp
const GroqReasoning::ContactMapping CONTACT_MAP[] = {
  {"Mom", "mother|mom|amma"},
  {"Nurse", "nurse|care nurse"}
};
```

Aliases are separated with `|`. The library sends contact names and aliases to Groq so it can select a canonical name. After the reply, the library validates `call:<name>` against this list:

- Known name or alias: action is normalized to `call:<canonical name>`.
- Unknown person: `action` becomes `none` and the response says the person is not in the contact list.

Phone-number lookup and call execution are not implemented here. They can be added later in the ESP32 firmware using the returned canonical contact name.

## Example usage

```cpp
GroqReasoning reasoning;
reasoning.begin(SEED_GROQ_KEY);

GroqReasoning::Result result = reasoning.ask(
    transcript,
    DEVICE_MAP,
    sizeof(DEVICE_MAP) / sizeof(DEVICE_MAP[0]),
    CONTACT_MAP,
    sizeof(CONTACT_MAP) / sizeof(CONTACT_MAP[0])
);

Serial.println(result.toJson());
```

## Requirements

- ESP32 Arduino core 3.x recommended.
- `ArduinoJson` library.
- Wi-Fi connected by the main sketch.


## WhatsApp message drafts

The library can prepare WhatsApp drafts through `generateWhatsAppDraft()`. It returns the selected canonical contact, validated phone number, and message text. It **does not send** the WhatsApp message; your firmware or backend must handle sending after applying your own confirmation and safety checks.

Phone numbers are passed separately in firmware and are not sent to Groq.

```cpp
const GroqReasoning::ContactNumberMapping PHONE_MAP[] = {
  {"Mom", "+919876543210"},
  {"Nurse", "+919876543211"}
};

GroqReasoning::WhatsAppDraft draft;
bool ok = reasoning.generateWhatsAppDraft(
    transcript,
    CONTACT_MAP,
    sizeof(CONTACT_MAP) / sizeof(CONTACT_MAP[0]),
    PHONE_MAP,
    sizeof(PHONE_MAP) / sizeof(PHONE_MAP[0]),
    draft
);

Serial.println(draft.toJson());
```

Replace the example phone numbers with the intended recipients' numbers. The included example is `examples/WhatsAppDraft/WhatsAppDraft.ino`.

## Security

This development build uses `WiFiClientSecure::setInsecure()`, which disables TLS certificate verification. Configure CA verification before production use. Do not execute model output directly: continue validating actions in firmware.
