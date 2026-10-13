# GroqReasoning v1.4.5

An ESP32 Arduino library that sends a transcript to Groq and returns a JSON object with exactly two string fields:

- `response`: a short user-facing answer.
- `action`: a validated intent such as `l1on`, `f1off`, `refill_water`, `emergency_call`, `call:Nurse`, or `none`.

The library does not execute device actions, start a pump, or place phone calls. The main sketch owns device/contact arrays and decides what to do with the validated intent. It can generate AI-written WhatsApp drafts and submit text messages through a configured Evolution API instance.


## Install in Arduino IDE

1. Open this repository on GitHub and choose **Code → Download ZIP**.
2. In Arduino IDE, select **Sketch → Include Library → Add .ZIP Library…** and choose the downloaded ZIP.
3. Install the **ArduinoJson** dependency when prompted. Select an ESP32 board and compile one of the examples.

Copy the relevant `secrets.h.example` to `secrets.h` in the example folder, then enter your own Wi-Fi credentials and Groq API key.


## Built-in defaults

- Endpoint: `https://api.groq.com/openai/v1/chat/completions`
- Model: `openai/gpt-oss-20b`
- Reasoning effort: `low`
- Include reasoning content: disabled
- Strict JSON Schema output: enabled for intent and WhatsApp draft responses
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

The library can prepare WhatsApp drafts through `generateWhatsAppDraft()`. It returns the selected canonical contact, validated phone number, and message text. Use `sendWhatsAppMessage()` to submit a ready draft to Evolution API. A successful HTTP response means the API accepted the request, not necessarily that the recipient received it.

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

GroqReasoning::WhatsAppConfig waConfig = {
  SEED_EVOLUTION_API_URL,
  SEED_EVOLUTION_INSTANCE,
  SEED_EVOLUTION_API_KEY
};

if (ok && draft.send) {
  bool submitted = reasoning.sendWhatsAppMessage(draft, waConfig);
  Serial.println(submitted ? "Evolution API accepted the request" :
                             reasoning.lastError());
}
```

Add these settings to your local `secrets.h` and replace the placeholders:

```cpp
#define SEED_EVOLUTION_API_URL "https://your-evolution-api-host"
#define SEED_EVOLUTION_INSTANCE "your-instance-name"
#define SEED_EVOLUTION_API_KEY "your-evolution-api-key"
```

Replace the example phone numbers with the intended recipients' numbers. The included example is `examples/WhatsAppDraft/WhatsAppDraft.ino`.

## Security

This development build uses `WiFiClientSecure::setInsecure()`, which disables TLS certificate verification. Configure CA verification before production use. Do not execute model output directly: continue validating actions in firmware.


## Evolution API send endpoint

The library submits text messages with `POST /message/sendText/{instance}`, using the `apikey` request header and a JSON body containing `number` and `text`. Phone numbers are sent as digits with the country code, without a leading plus sign.

A successful 2xx response sets `draft.submitted=true`. This indicates that the Evolution API returned success for the request; it is not a guarantee of final WhatsApp delivery.

The HTTPS client currently uses `setInsecure()` for development. Configure certificate validation before production use.


## ESP32-S3 Voice Assistant

An end-to-end WakeNet9 + Groq Whisper + GroqReasoning + Evolution API example is included at `examples/ESP32S3VoiceAssistant/ESP32S3VoiceAssistant.ino`.

The sketch connects Wi-Fi at the start of `setup()`, starts WakeNet9 on the configured INMP441 pins, transcribes speech, returns a JSON object containing `response`, `action`, and `message`, and submits requested WhatsApp messages through Evolution API.

Copy `examples/ESP32S3VoiceAssistant/secrets.h.example` to a private `secrets.h` and enter your Wi-Fi, Groq, and Evolution API credentials. Edit `CONTACT_PHONE_MAP` in the sketch to configure your recipients. Phone numbers and credentials are intentionally placeholders in the public repository.

Required additional libraries for this example: `WakeNet9Arduino` and `GroqAudioStream`.

Evolution API submission uses `POST {base URL}/message/sendText/{instance}` with the `apikey` header and JSON fields `number` and `text`. The number is sent as digits including its country code, without a leading plus. A successful 2xx response means the API accepted the request; final WhatsApp delivery should be checked using your server's status/webhook flow.

The prototype uses `WiFiClientSecure::setInsecure()`; configure TLS certificate verification before production use.

### Compatibility with older sketches

Version 1.4.5 keeps compatibility overloads for sketches using `ContactPhoneMapping`, `begin(apiKey, model)`, `looksLikeWhatsAppRequest(transcript)`, and the four-argument `sendWhatsAppMessage(draft, apiBaseUrl, instanceName, apiKey)` form. Replace both `src/GroqReasoning.h` and `src/GroqReasoning.cpp` together when updating the library; mixing files from different versions can cause linker errors.

### Groq GPT-OSS compatibility

For `openai/gpt-oss-20b`, the library sets `include_reasoning=false` rather than passing `reasoning_format`, which the current Groq documentation lists as unsupported for GPT-OSS models. The voice-assistant example also prints the Groq HTTP status and error details to Serial when a request fails.

### Strict JSON output and validation diagnostics

Version 1.4.4 uses Groq Structured Outputs with `json_schema` and `strict: true` for both reasoning intents and WhatsApp drafts. It also sets `reasoning_effort=low` and `include_reasoning=false` for draft generation. When Groq returns an API error, `lastError()` includes the `failed_generation` details when the API provides them.
When the user requests a message without clearly dictating what it should say, the library now avoids inventing message content and leaves sending disabled so the voice assistant can ask for the intended text.

### WhatsApp intent handling

The ESP32-S3 example checks for a WhatsApp request before normal intent routing, preventing phrases such as “send it to my mother” from being classified as phone-call actions. The draft prompt includes examples for “send hi to my mother” and for sending a preceding sentence when the user says “send it”. If no message text was spoken, the example asks for the message instead of sending an invented greeting.
