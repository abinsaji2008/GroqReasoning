# GroqReasoning v1.3.0

An ESP32 Arduino library that sends a transcript to Groq and returns a JSON object with exactly two string fields:

- **response**: a short user-facing answer.
- **action**: a validated intent such as `l1on`, `f1off`, `refill_water`, `emergency_call`, `call:Nurse`, or `none`.

The library does not execute device actions, start a pump, or place phone calls. The main sketch owns device/contact arrays and decides what to do with the validated intent.

## Built-in defaults

- Endpoint: https://api.groq.com/openai/v1/chat/completions
- Model: openai/gpt-oss-20b
- Reasoning effort: low
- Reasoning format: hidden
- JSON response mode: enabled
- Maximum completion tokens: 256
- Connect timeout: 10 seconds
- Request timeout: 30 seconds

## Device actions

Define device mappings in the main sketch. Each base code permits only <code>on</code> and <code>off> actions.

~~~cpp
const GroqReasoning::DeviceMapping DEVICE_MAP[] = {
  {"primary light", "l1"},
  {"fan", "f1"}
};
~~~

Example result:

~~~json
{"response":"Turning on the primary light.","action":"l1on"}
~~~

## Contacts: name and aliases only

The contact mapping has two fields: canonical contact name and alternative names. Do not place phone numbers in this array.

~~~cpp
const GroqReasoning::ContactMapping CONTACT_MAP[] = {
  {"Mom", "mother|mom|amma"},
  {"Nurse", "nurse|care nurse"}
};
~~~

Aliases are separated with a pipe character. The library sends contact names and aliases to Groq so it can select a canonical name. After the reply, it validates call:<name> against this list:

- Known name or alias: action is normalized to call:<canonical name>.
- Unknown person: action becomes none and the response says the person is not in the contact list.

Phone-number lookup and call execution are not implemented here. They can be added later in the ESP32 firmware using the returned canonical contact name.

## Example usage

~~~cpp
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
~~~

## Requirements

- ESP32 Arduino core 3.x recommended.
- ArduinoJson library.
- Wi-Fi connected by the main sketch.

The basic example is in examples/BasicReasoning/BasicReasoning.ino. Copy examples/BasicReasoning/secrets.h.example to secrets.h in that example folder and enter your own Wi-Fi credentials and Groq API key.

## Security

This development build uses WiFiClientSecure::setInsecure(), which disables TLS certificate verification. Configure CA verification before production use. Do not execute model output directly: continue validating actions in firmware.
