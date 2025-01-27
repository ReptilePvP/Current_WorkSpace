/*
 * Terp Meter Core with Voice Control
 * Enhanced with OpenAI Integration for intelligent responses
 */

#include <M5CoreS3.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <Wire.h>
#include <Preferences.h>
#include <Update.h>
#include <WebSockets.h>

// Pin Definitions (maintained from original)
#define KEY_PIN 8
#define LED_PIN 9
#define BUTTON1_PIN 17
#define BUTTON2_PIN 18
#define NUM_LEDS 1

// LED Array
CRGB leds[NUM_LEDS];

// Temperature ranges in Fahrenheit (maintained from original)
const int16_t TEMP_COLD_F = 480;
const int16_t TEMP_MIN_F = 580;
const int16_t TEMP_MAX_F = 640;
const int16_t TEMP_HOT_F = 800;

// Temperature ranges in Celsius
const int16_t TEMP_COLD_C = (TEMP_COLD_F - 32) * 5 / 9;
const int16_t TEMP_MIN_C = (TEMP_MIN_F - 32) * 5 / 9;
const int16_t TEMP_MAX_C = (TEMP_MAX_F - 32) * 5 / 9;
const int16_t TEMP_HOT_C = (TEMP_HOT_F - 32) * 5 / 9;

// MLX90614 I2C address
#define MLX90614_I2C_ADDR 0x5A

// OpenAI Configuration
const char* OPENAI_API_KEY = "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";
const char* OPENAI_ENDPOINT = "https://api.openai.com/v1/chat/completions";

// Real-time Audio API endpoints
const char* REALTIME_SESSION_ENDPOINT = "https://api.openai.com/v1/audio/realtime/sessions";
const char* REALTIME_WEBSOCKET_HOST = "api.openai.com";
const int REALTIME_WEBSOCKET_PORT = 443;

// Audio Recording Configuration
static constexpr const size_t RECORD_LENGTH = 320;
static constexpr const size_t RECORD_NUMBER = 256;
static constexpr const size_t RECORD_SIZE = RECORD_NUMBER * RECORD_LENGTH;
static constexpr const size_t RECORD_SAMPLERATE = 17000;
static int16_t* rec_data = nullptr;
static size_t rec_record_idx = 0;

// WebSocket client for real-time audio
WebSockets::WebSocketsClient webSocket;
bool sessionActive = false;
String sessionId = "";

// Settings structure
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    uint8_t brightness = 255;
    float emissivity = 0.95f;
    
    void save() {
        Preferences prefs;
        prefs.begin("settings", false);
        prefs.putBool("useCelsius", useCelsius);
        prefs.putUChar("brightness", brightness);
        prefs.putBool("soundEnabled", soundEnabled);
        prefs.putFloat("emissivity", emissivity);
        prefs.end();
    }
    
    void load() {
        Preferences prefs;
        prefs.begin("settings", true);
        useCelsius = prefs.getBool("useCelsius", true);
        brightness = prefs.getUChar("brightness", 255);
        soundEnabled = prefs.getBool("soundEnabled", true);
        emissivity = prefs.getFloat("emissivity", 0.95f);
        prefs.end();
    }
} settings;

// State structure
struct State {
    bool isMonitoring = false;
    bool isListening = false;
    String voiceCommand = "";
    float lastTemp = 0;
    String statusMessage = "";
    uint32_t statusColor = 0xE0E0E0;
} state;

// Function to encode audio data to base64
String encodeAudioToBase64(const int16_t* audioData, size_t length) {
    // Simple base64 encoding implementation
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    String result;
    int i = 0;
    int j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    size_t input_len = length * 2; // *2 because int16_t is 2 bytes
    const uint8_t* bytes = (const uint8_t*)audioData;

    while (input_len--) {
        char_array_3[i++] = *(bytes++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; i < 4; i++)
                result += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++)
            result += base64_chars[char_array_4[j]];

        while((i++ < 3))
            result += '=';
    }

    return result;
}

// Function to create a real-time session
bool createRealtimeSession() {
    WiFiClientSecure client;
    client.setInsecure();  // Note: In production, use proper certificate validation
    
    HTTPClient http;
    http.begin(client, REALTIME_SESSION_ENDPOINT);
    http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
    http.addHeader("Content-Type", "application/json");
    
    // Create session configuration
    StaticJsonDocument<512> doc;
    doc["model"] = "whisper-1";
    doc["language"] = "en";  // Set to English
    doc["sample_rate"] = RECORD_SAMPLERATE;
    doc["audio_format"] = "int16";  // Using 16-bit PCM
    doc["compression"] = "none";
    
    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpCode = http.POST(requestBody);
    bool success = false;
    
    if (httpCode == HTTP_CODE_OK) {
        deserializeJson(doc, http.getString());
        sessionId = doc["session_id"].as<String>();
        String url = doc["url"].as<String>();
        
        // Configure WebSocket connection
        webSocket.beginSSL(REALTIME_WEBSOCKET_HOST, REALTIME_WEBSOCKET_PORT, url);
        webSocket.onEvent([](WStype_t type, uint8_t * payload, size_t length) {
            switch(type) {
                case WStype_CONNECTED:
                    sessionActive = true;
                    break;
                case WStype_TEXT:
                    handleWebSocketMessage((char*)payload);
                    break;
                case WStype_DISCONNECTED:
                    sessionActive = false;
                    break;
            }
        });
        
        success = true;
    }
    
    http.end();
    return success;
}

// Handle WebSocket messages (transcriptions and responses)
void handleWebSocketMessage(const char* message) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (!error) {
        if (doc.containsKey("text")) {
            String transcription = doc["text"].as<String>();
            if (transcription.length() > 0) {
                // Get AI response for the transcribed text
                String response = getAIResponse(transcription);
                state.statusMessage = response;
                state.statusColor = 0x00FF00;  // Green for success
            }
        }
    }
}

// Function to get AI response
String getAIResponse(const String& userText) {
    WiFiClientSecure client;
    client.setInsecure();  // Note: In production, use proper certificate validation
    
    HTTPClient http;
    http.begin(client, OPENAI_ENDPOINT);
    http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
    http.addHeader("Content-Type", "application/json");
    
    // Prepare the message with context
    String temp = String(state.lastTemp, 1);
    String unit = settings.useCelsius ? "°C" : "°F";
    
    StaticJsonDocument<1024> doc;
    doc["model"] = "gpt-3.5-turbo";
    JsonArray messages = doc.createNestedArray("messages");
    
    JsonObject systemMsg = messages.createNestedObject();
    systemMsg["role"] = "system";
    systemMsg["content"] = "You are a helpful assistant for a temperature monitoring device. "
                          "Current temperature is " + temp + unit + ". "
                          "Ideal range is " + String(TEMP_MIN_F) + "°F to " + String(TEMP_MAX_F) + "°F. "
                          "Keep responses brief and focused on temperature-related information.";
    
    JsonObject userMsg = messages.createNestedObject();
    userMsg["role"] = "user";
    userMsg["content"] = userText;
    
    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpCode = http.POST(requestBody);
    String response = "";
    
    if (httpCode == HTTP_CODE_OK) {
        deserializeJson(doc, http.getString());
        response = doc["choices"][0]["message"]["content"].as<String>();
    } else {
        response = "Error: Could not get AI response";
    }
    
    http.end();
    return response;
}

// Function to start voice recording
void startVoiceRecording() {
    state.isListening = true;
    state.statusMessage = "Connecting...";
    state.statusColor = 0xFFFF00;  // Yellow while connecting
    
    // Create new real-time session
    if (createRealtimeSession()) {
        state.statusMessage = "Listening...";
        state.statusColor = 0x00FF00;  // Green while listening
        
        // Turn off speaker and enable microphone
        M5.Speaker.end();
        M5.Mic.begin();
        
        // Allocate memory for recording if not already allocated
        if (rec_data == nullptr) {
            rec_data = (int16_t*)heap_caps_malloc(RECORD_SIZE * sizeof(int16_t), MALLOC_CAP_8BIT);
            memset(rec_data, 0, RECORD_SIZE * sizeof(int16_t));
        }
        
        rec_record_idx = 0;
    } else {
        state.statusMessage = "Connection Failed";
        state.statusColor = 0xFF0000;  // Red for error
        state.isListening = false;
    }
}

// Function to stop voice recording and process command
void stopVoiceRecording() {
    state.isListening = false;
    M5.Mic.end();
    
    // Close WebSocket connection
    if (sessionActive) {
        webSocket.disconnect();
        sessionActive = false;
    }
    
    // Clean up
    if (rec_data != nullptr) {
        heap_caps_free(rec_data);
        rec_data = nullptr;
    }
}

// Function to handle external buttons
bool wasButton1Pressed() {
    static bool lastState = HIGH;
    bool currentState = digitalRead(BUTTON1_PIN);
    bool wasPressed = (lastState == HIGH && currentState == LOW);
    lastState = currentState;
    return wasPressed;
}

bool wasButton2Pressed() {
    static bool lastState = HIGH;
    bool currentState = digitalRead(BUTTON2_PIN);
    bool wasPressed = (lastState == HIGH && currentState == LOW);
    lastState = currentState;
    return wasPressed;
}

bool wasKeyPressed() {
    static bool lastState = HIGH;
    bool currentState = digitalRead(KEY_PIN);
    bool wasPressed = (lastState == HIGH && currentState == LOW);
    lastState = currentState;
    return wasPressed;
}

void handleKeyAndLED() {
    static bool ledState = false;
    if (wasKeyPressed()) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
}

float readTemperature() {
    float object_temp = 0;
    uint16_t rawTemp;
    
    // Read object temperature
    Wire.beginTransmission(MLX90614_I2C_ADDR);
    Wire.write(0x07);             // RAM address for object temperature
    if (Wire.endTransmission(false) != 0) {
        return state.lastTemp;     // Return last temp if communication fails
    }
    
    if (Wire.requestFrom(MLX90614_I2C_ADDR, (uint8_t)3) != 3) {
        return state.lastTemp;     // Return last temp if read fails
    }
    
    rawTemp = Wire.read();        // Read low byte
    rawTemp |= Wire.read() << 8;  // Read high byte
    uint8_t pec = Wire.read();    // Read PEC (checksum)
    
    object_temp = (rawTemp * 0.02) - 273.15;  // Convert to Celsius
    
    if (!settings.useCelsius) {
        object_temp = object_temp * 9/5 + 32;  // Convert to Fahrenheit if needed
    }
    
    return object_temp;
}

void updateDisplay() {
    M5.Lcd.fillScreen(0x1A1A1A); // Dark background
    
    // Display temperature
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(0xFFFFFF);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.print(String(state.lastTemp, 1));
    M5.Lcd.print(settings.useCelsius ? " C" : " F");
    
    // Display status message
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(state.statusColor);
    M5.Lcd.setCursor(10, 160);
    M5.Lcd.print(state.statusMessage);
    
    // Display voice control status
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(0xAAAAAA);
    M5.Lcd.setCursor(10, 220);
    M5.Lcd.print(state.isListening ? "Listening..." : "Press Button 1 for voice control");
}

void setup() {
    M5.begin();        // Initialize M5CoreS3
    
    // Initialize I2C communication
    Wire.begin(2, 1);  // SDA = 2, SCL = 1 for CoreS3
    Wire.setClock(100000);  // Set I2C clock to 100kHz
    
    // Small delay to let the sensor initialize
    delay(100);
    
    WiFi.begin("Wack House", "justice69");
    
    // Initialize LED
    FastLED.addLeds<SK6812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(settings.brightness);
    
    // Load settings
    settings.load();
    
    // Initialize display
    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(0x1A1A1A);
    
    // Set up button and LED pins
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);
    pinMode(KEY_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    
    // Initialize sensor communication
    Wire.beginTransmission(MLX90614_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
        state.statusMessage = "Ready - Sensor OK";
    } else {
        state.statusMessage = "Sensor Error";
        state.statusColor = 0xFF0000;  // Red
    }
}

void loop() {
    M5.update();
    
    // Handle WebSocket events
    if (sessionActive) {
        webSocket.loop();
    }
    
    // Read temperature
    state.lastTemp = readTemperature();
    
    // Handle voice control button (Button 1)
    if (wasButton1Pressed()) {
        if (!state.isListening) {
            startVoiceRecording();
        } else {
            stopVoiceRecording();
        }
    }
    
    // Handle Button 2 (can be used for menu navigation or other features)
    if (wasButton2Pressed()) {
        // Add functionality for Button 2 if needed
    }
    
    // Handle Key unit and LED
    handleKeyAndLED();
    
    // Record and stream audio when listening
    if (state.isListening && M5.Mic.isEnabled() && sessionActive) {
        auto data = &rec_data[rec_record_idx * RECORD_LENGTH];
        if (M5.Mic.record(data, RECORD_LENGTH, RECORD_SAMPLERATE)) {
            // Send audio data through WebSocket
            webSocket.sendBIN((uint8_t*)data, RECORD_LENGTH * sizeof(int16_t));
            rec_record_idx++;
            if (rec_record_idx >= RECORD_NUMBER) {
                // Buffer is full, reset index
                rec_record_idx = 0;
            }
        }
    }
    
    // Update display
    updateDisplay();
    
    delay(10);
}
