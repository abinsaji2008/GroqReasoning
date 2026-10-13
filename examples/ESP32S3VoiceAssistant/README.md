# ESP32-S3 Voice Assistant example

Copy this folder to your Arduino sketches directory, rename the folder to `ESP32S3VoiceAssistant` if needed, and create a local `secrets.h` from `secrets.h.example`.

This sketch connects Wi-Fi first, then initializes WakeNet9, GroqAudioStream, and GroqReasoning. It transcribes speech with Groq Whisper, returns `response` and `action`, generates an AI WhatsApp draft when requested, and submits that draft using Evolution API.

Required libraries:
- `WakeNet9Arduino`
- `GroqAudioStream`
- `GroqReasoning`
- `ArduinoJson`

Configure the following in your private `secrets.h`:
- `SEED_WIFI_SSID` and `SEED_WIFI_PASS`
- `SEED_GROQ_KEY`
- `SEED_EVOLUTION_API_URL`: base URL only, without `/message/sendText/...`
- `SEED_EVOLUTION_INSTANCE`: the exact instance name configured in Evolution API
- `SEED_EVOLUTION_API_KEY`

Edit `CONTACT_PHONE_MAP` in the sketch and replace the phone-number placeholders with your own contacts' numbers in international format. Real contact numbers and API credentials should not be committed to a public repository.

Evolution API request: `POST {base URL}/message/sendText/{instance}`, with the `apikey` header and JSON fields `number` (digits with country code, no leading plus) and `text`. A successful HTTP response indicates API acceptance, not confirmed recipient delivery.

The library currently uses `WiFiClientSecure::setInsecure()` for development. Add proper TLS certificate validation before production deployment.

## Indian time and scheduled WhatsApp

The sketch synchronizes online time with NTP using IST (UTC+05:30). Ask “What time is it?” to hear the current Indian time. Use commands like “Send hi to Mom at 8:30 PM” or “Send I will be home soon to Mom tomorrow at 9 AM” to schedule a message. One pending scheduled message is saved in ESP32 NVS. It is submitted after the target time when Wi-Fi and Evolution API are available; delivery may be late if either is unavailable. Avoid mentioning a time inside the body after “saying” unless you intend that time to be part of the message.
