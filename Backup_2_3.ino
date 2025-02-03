#include <Wire.h>

/*
 * Terp Meter Core - Temperature Monitoring Device
 * See Changelog.md for version history and recent changes
 * Last Updated: 2025-02-03 07:37:27 EST
 */

#include <M5CoreS3.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <Preferences.h>
#include <Update.h>
#include "thingProperties.h"

// Pin Definitions
#define KEY_PIN 8      // Key unit input pin (G8)
#define LED_PIN 9      // Key unit LED pin (G9)
#define BUTTON1_PIN 17 // Dual Button unit - Button 1 (G17)
#define BUTTON2_PIN 18 // Dual Button unit - Button 2 (G18)
#define NUM_LEDS 1

// LED Array
CRGB leds[NUM_LEDS];

// Menu States
enum MenuState {
    MAIN_DISPLAY,
    SETTINGS_MENU,
    UNIT_SELECTION,
    BRIGHTNESS_ADJUSTMENT,
    SOUND_SETTINGS,
    EMISSIVITY_ADJUSTMENT,
    EMISSIVITY_CONFIRM,
    RESTART_CONFIRM
};

// Configuration namespace
namespace Config {
    namespace Display {
        // Modern color scheme
        const uint32_t COLOR_BACKGROUND = 0x1A1A1A;  // Dark gray background
        const uint32_t COLOR_TEXT = 0xE0E0E0;        // Light gray text
        const uint32_t COLOR_PRIMARY = 0x0099FF;     // Bright blue for primary elements
        const uint32_t COLOR_SUCCESS = 0x00E676;     // Material green
        const uint32_t COLOR_ERROR = 0xFF5252;       // Material red
        const uint32_t COLOR_WARNING = 0xFFD740;     // Material amber
        const uint32_t COLOR_ACCENT = 0x7C4DFF;      // Material deep purple
        const uint32_t COLOR_SECONDARY_BG = 0x2D2D2D; // Slightly lighter background for contrast
        const uint32_t COLOR_BORDER = 0x404040;      // Medium gray for borders
        // Battery colors
        const uint32_t COLOR_BATTERY_LOW = 0xFF3D00;     // Red-orange for low battery
        const uint32_t COLOR_BATTERY_MEDIUM = 0xFFB300;  // Amber for medium battery
        const uint32_t COLOR_BATTERY_HIGH = 0x00C853;    // Green for high battery
        const uint32_t COLOR_BATTERY_CHARGING = 0x2979FF; // Blue for charging
        
        // UI Constants
        const int HEADER_HEIGHT = 40;
        const int PADDING = 10;
        const int CORNER_RADIUS = 8;  // For rounded rectangles
        
        // Fonts
        const lgfx::v1::IFont* FONT_HEADER = &fonts::FreeSansBold12pt7b;  // Smaller header font
        const lgfx::v1::IFont* FONT_TEMP = &fonts::FreeSansBold18pt7b;    // Temperature display
        const lgfx::v1::IFont* FONT_STATUS = &fonts::FreeSans12pt7b;      // Status messages
        const lgfx::v1::IFont* FONT_MENU = &fonts::FreeSans12pt7b;        // Menu items
        const lgfx::v1::IFont* FONT_SMALL = &fonts::FreeSans9pt7b;        // Small text
    }
    
    namespace Emissivity {
        const float MIN = 0.65f;
        const float MAX = 1.00f;
        const float STEP = 0.01f;
    }
}

// Temperature ranges in Fahrenheit
const int16_t TEMP_COLD_F = 480;    // Below this is too cold
const int16_t TEMP_MIN_F = 580;     // Start of perfect range
const int16_t TEMP_MAX_F = 640;     // End of perfect range
const int16_t TEMP_HOT_F = 800;     // Above this is too hot

// Temperature ranges in Celsius (converted from Fahrenheit)
const int16_t TEMP_COLD_C = (TEMP_COLD_F - 32) * 5 / 9;
const int16_t TEMP_MIN_C = (TEMP_MIN_F - 32) * 5 / 9;
const int16_t TEMP_MAX_C = (TEMP_MAX_F - 32) * 5 / 9;
const int16_t TEMP_HOT_C = (TEMP_HOT_F - 32) * 5 / 9;

// Custom colors for temperature status
const uint32_t COLOR_COLD = 0x1E90FF;  // Snowy blue
const CRGB LED_COLOR_COLD = CRGB(30, 144, 255);  // Matching LED color for cold
const CRGB LED_COLOR_WARNING = CRGB(255, 191, 0);  // Amber for warning
const CRGB LED_COLOR_PERFECT = CRGB(0, 255, 0);  // Lime green for perfect
const CRGB LED_COLOR_HOT = CRGB(255, 0, 0);  // Red for too hot

// Status priority levels
enum StatusPriority {
    PRIORITY_ALERT = 0,        // Highest priority: Critical alerts
    PRIORITY_MONITORING = 1,   // Monitoring state changes
    PRIORITY_TEMP = 2,        // Temperature status
    PRIORITY_CLOUD = 3,       // Cloud connection status
    PRIORITY_READY = 4        // Lowest priority: Ready state
};

// Settings structure
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    uint8_t brightness = 255;  // Default to 100% (PWM value 255)
    float emissivity = 0.95f;
    
    void save() {
        Preferences prefs;
        prefs.begin("settings", false);  // false = RW mode
        prefs.putBool("useCelsius", useCelsius);
        prefs.putUChar("brightness", brightness);
        prefs.putBool("soundEnabled", soundEnabled);
        prefs.putFloat("emissivity", emissivity);
        prefs.end();
    }
    
    void load() {
        Preferences prefs;
        prefs.begin("settings", true);  // true = read-only mode
        useCelsius = prefs.getBool("useCelsius", true);  // default to Celsius
        brightness = prefs.getUChar("brightness", 255);   // default to 100%
        soundEnabled = prefs.getBool("soundEnabled", true); // default to sound on
        emissivity = prefs.getFloat("emissivity", 0.95f);  // default to 0.95
        prefs.end();
    }
} settings;

// State structure
struct State {
    bool isMonitoring = false;
    bool keyPressed = false;
    bool ledActive = false;
    MenuState menuState = MAIN_DISPLAY;
    String statusMessage = "";
    uint32_t statusColor = Config::Display::COLOR_TEXT;
    int menuSelection = 0;
    bool menuNeedsRedraw = true;  // New flag to track menu redraw
    float lastTemp = 0;
    unsigned long lastDisplayUpdate = 0;
    static const unsigned long DISPLAY_UPDATE_INTERVAL = 250; // Update every 250ms
    bool cloudConnected = false;
    unsigned long lastReconnectAttempt = 0;
    int reconnectAttempts = 0;
    static const unsigned long RECONNECT_INTERVAL = 5000; // Try every 5 seconds
    static const int MAX_RECONNECT_ATTEMPTS = 5; // Max attempts before waiting longer
    static const unsigned long LONG_RECONNECT_INTERVAL = 60000; // 1 minute
    float batteryLevel = 0.0f;
    bool isCharging = false;
    unsigned long lastBatteryCheck = 0;
    static const unsigned long BATTERY_CHECK_INTERVAL = 5000; // Check battery every 5 seconds
    StatusPriority currentPriority = PRIORITY_READY;
    
    void updateBatteryStatus() {
        unsigned long currentTime = millis();
        if (currentTime - lastBatteryCheck >= BATTERY_CHECK_INTERVAL) {
            batteryLevel = CoreS3.Power.getBatteryLevel();
            isCharging = CoreS3.Power.isCharging();
            lastBatteryCheck = currentTime;
        }
    }

    void updateStatus(const String& message, uint32_t color = Config::Display::COLOR_TEXT) {
        statusMessage = message;
        statusColor = color;
    }

    void updateStatusWithPriority(const String& message, uint32_t color, StatusPriority priority) {
        // Only update if new priority is higher (lower number) or same priority
        if (priority <= currentPriority) {
            statusMessage = message;
            statusColor = color;
            currentPriority = priority;
        }
    }

    void clearStatus(StatusPriority minPriority) {
        // Clear current status if its priority is >= minPriority
        if (currentPriority >= minPriority) {
            statusMessage = "";
            statusColor = Config::Display::COLOR_TEXT;
            currentPriority = PRIORITY_READY;
        }
    }

    bool shouldUpdateDisplay() {
        unsigned long currentTime = millis();
        if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
            lastDisplayUpdate = currentTime;
            return true;
        }
        return false;
    }
} state;

// Govee API configuration
const char* GOVEE_API_KEY = "d2287253-b08a-48a9-9f43-d4ddeeff1d61";
const char* GOVEE_DEVICE_MAC = "C3:80:CE:F8:03:86:6E:1E";
const char* GOVEE_DEVICE_MODEL = "H6056";

// Govee light state
bool isLightOn = false;

// Temperature-based colors for Govee light
const struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} TEMP_COLORS[] = {
    {30, 144, 255},   // Cold (Snowy blue)
    {255, 191, 0},    // Warning (Amber)
    {0, 255, 0},      // Perfect (Green)
    {255, 0, 0}       // Hot (Red)
};

// Govee API rate limiting
unsigned long lastGoveeUpdate = 0;
const unsigned long GOVEE_UPDATE_INTERVAL = 2000; // Minimum 2 seconds between updates

// Utility functions
float celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0f / 5.0f) + 32.0f;
}

bool isValidTemperature(float temp) {
    return temp >= -70.0f && temp <= 380.0f;  // MLX90614 object temperature range
}

// Function to control Govee light with improved error handling
bool updateGoveeLight(bool isOn, uint8_t r, uint8_t g, uint8_t b) {
    // Check if enough time has passed since last update
    unsigned long currentTime = millis();
    if (currentTime - lastGoveeUpdate < GOVEE_UPDATE_INTERVAL) {
        return false; // Skip update if not enough time has passed
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Error: WiFi not connected!");
        return false;
    }

    // If we got a rate limit error, wait longer before trying again
    static bool gotRateLimit = false;
    static unsigned long rateLimitStartTime = 0;
    if (gotRateLimit && (currentTime - rateLimitStartTime < 30000)) { // Wait 30 seconds after rate limit
        return false;
    }
    gotRateLimit = false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    
    if(!http.begin(client, "https://developer-api.govee.com/v1/devices/control")) {
        Serial.println("Failed to connect to Govee API");
        return false;
    }
    
    http.addHeader("Govee-API-Key", GOVEE_API_KEY);
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON document
    StaticJsonDocument<200> doc;
    doc["device"] = GOVEE_DEVICE_MAC;
    doc["model"] = GOVEE_DEVICE_MODEL;
    JsonObject cmd = doc.createNestedObject("cmd");

    if (!isOn) {
        // Turn off the light
        cmd["name"] = "turn";
        cmd["value"] = "off";
    } else {
        // Set color (this will also turn on the light)
        cmd["name"] = "color";
        JsonObject colorValue = cmd.createNestedObject("value");
        colorValue["r"] = r;
        colorValue["g"] = g;
        colorValue["b"] = b;
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpResponseCode = http.PUT(jsonString);
    http.end();
    
    if (httpResponseCode == 200) {
        lastGoveeUpdate = currentTime;
        return true;
    } else {
        Serial.printf("HTTP Error %d: ", httpResponseCode);
        if (httpResponseCode == 429) {
            Serial.println("Rate limit exceeded. Try again later.");
            gotRateLimit = true;
            rateLimitStartTime = currentTime;
        } else {
            Serial.println("Failed to control light.");
        }
        return false;
    }
}

// Function to sync Govee light with temperature status
void syncGoveeWithTemp(float temperature) {
    static bool wasMonitoring = false;  // Track previous monitoring state
    static bool lastLightState = false; // Track last light state

    // Handle monitoring state changes
    if (!state.isMonitoring) {
        if (wasMonitoring || lastLightState) {  // Only turn off if it was previously on
            if (updateGoveeLight(false, 0, 0, 0)) {
                lastLightState = false;
                Serial.println("Monitoring disabled - Light turned off");
            }
        }
        wasMonitoring = false;
        return;
    }
    wasMonitoring = true;

    // Convert temperature if needed
    float displayTemp = settings.useCelsius ? temperature : celsiusToFahrenheit(temperature);
    
    // Only update if temperature is valid
    if (!isValidTemperature(temperature)) {
        Serial.println("Invalid temperature reading - skipping Govee update");
        return;
    }

    // Debug temperature and color selection
    Serial.printf("Temp: %.2f°%s - ", displayTemp, settings.useCelsius ? "C" : "F");

    // Determine color based on temperature range
    const auto& color = [&]() -> const auto& {
        if (settings.useCelsius) {
            if (displayTemp < TEMP_COLD_C) {
                Serial.println("Color: Cold (Blue)");
                return TEMP_COLORS[0];      // Cold
            }
            if (displayTemp < TEMP_MIN_C) {
                Serial.println("Color: Warning (Amber)");
                return TEMP_COLORS[1];      // Warning (warming)
            }
            if (displayTemp <= TEMP_MAX_C) {
                Serial.println("Color: Perfect (Green)");
                return TEMP_COLORS[2];      // Perfect
            }
            Serial.println("Color: Hot (Red)");
            return TEMP_COLORS[3];          // Hot
        } else {
            if (displayTemp < TEMP_COLD_F) {
                Serial.println("Color: Cold (Blue)");
                return TEMP_COLORS[0];      // Cold
            }
            if (displayTemp < TEMP_MIN_F) {
                Serial.println("Color: Warning (Amber)");
                return TEMP_COLORS[1];      // Warning (warming)
            }
            if (displayTemp <= TEMP_MAX_F) {
                Serial.println("Color: Perfect (Green)");
                return TEMP_COLORS[2];      // Perfect
            }
            Serial.println("Color: Hot (Red)");
            return TEMP_COLORS[3];          // Hot
        }
    }();

    // Only update if color would change or light state needs to change
    static uint8_t lastR = 0, lastG = 0, lastB = 0;
    if (lastR != color.r || lastG != color.g || lastB != color.b || !lastLightState) {
        if (updateGoveeLight(true, color.r, color.g, color.b)) {
            lastR = color.r;
            lastG = color.g;
            lastB = color.b;
            lastLightState = true;
        }
    }
}

// Function declarations
void handleButtons();
void drawMainDisplay(float temperature);
void drawSettingsMenu();
void drawUnitSelection();
void drawBrightnessAdjustment();
void drawSoundSettings();
void drawEmissivityAdjustment();
void drawEmissivityConfirm();
void drawRestartConfirm();
void updateDisplay();
void handleKeyAndLED();
float readTemperature();
void drawStatusBox();
void playSuccessSound();
void playErrorSound();
void onMonitoringChange();
void handleMonitoringStateChange(bool newState, bool fromCloud);
void handleCloudConnection();
void drawBatteryIndicator(int x, int y, int width, int height);
void drawStatusIndicators();
bool updateGoveeLight(bool isOn, uint8_t r, uint8_t g, uint8_t b);
void syncGoveeWithTemp(float temperature);

const char* menuItems[] = {"Temperature Unit", "Brightness", "Sound", "Emissivity", "Exit"};

bool enableTempDebug = false;  // Flag to control temperature debug output

// Status message management
class StatusManager {
public:
    // Temperature status messages
    static const char* MSG_TOO_COLD;
    static const char* MSG_WARMING_UP;
    static const char* MSG_PERFECT_TEMP;
    static const char* MSG_COOLING_DOWN;
    static const char* MSG_TOO_HOT;
    static const char* MSG_INVALID_TEMP;
    
    // Monitoring status messages
    static const char* MSG_MONITORING_ON;
    static const char* MSG_MONITORING_OFF;
    
    // Cloud status messages
    static const char* MSG_CLOUD_CONNECTING;
    static const char* MSG_CLOUD_CONNECTED;
    static const char* MSG_CLOUD_DISCONNECTED;
    
    // System status messages
    static const char* MSG_READY;
    
    void updateTemperatureStatus(float temperature, bool useCelsius) {
        if (!isValidTemperature(temperature)) {
            updateStatus(MSG_INVALID_TEMP, Config::Display::COLOR_ERROR, PRIORITY_TEMP);
            return;
        }
        
        float displayTemp = useCelsius ? temperature : celsiusToFahrenheit(temperature);
        
        // Determine temperature status, display color, and LED color
        uint32_t tempColor;
        CRGB ledColor;
        String statusMsg;
        bool inTargetRange = false;
        
        if (useCelsius) {
            if (displayTemp < TEMP_COLD_C) {
                statusMsg = MSG_TOO_COLD;
                tempColor = COLOR_COLD;
                ledColor = LED_COLOR_COLD;
            } else if (displayTemp < TEMP_MIN_C) {
                statusMsg = MSG_WARMING_UP;
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
            } else if (displayTemp <= TEMP_MAX_C) {
                statusMsg = MSG_PERFECT_TEMP;
                tempColor = Config::Display::COLOR_SUCCESS;
                ledColor = LED_COLOR_PERFECT;
                inTargetRange = true;
            } else if (displayTemp > TEMP_HOT_C) {
                statusMsg = MSG_TOO_HOT;
                tempColor = Config::Display::COLOR_ERROR;
                ledColor = LED_COLOR_HOT;
            } else {
                statusMsg = MSG_COOLING_DOWN;
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
            }
        } else {
            if (displayTemp < TEMP_COLD_F) {
                statusMsg = MSG_TOO_COLD;
                tempColor = COLOR_COLD;
                ledColor = LED_COLOR_COLD;
            } else if (displayTemp < TEMP_MIN_F) {
                statusMsg = MSG_WARMING_UP;
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
            } else if (displayTemp <= TEMP_MAX_F) {
                statusMsg = MSG_PERFECT_TEMP;
                tempColor = Config::Display::COLOR_SUCCESS;
                ledColor = LED_COLOR_PERFECT;
                inTargetRange = true;
            } else if (displayTemp > TEMP_HOT_F) {
                statusMsg = MSG_TOO_HOT;
                tempColor = Config::Display::COLOR_ERROR;
                ledColor = LED_COLOR_HOT;
            } else {
                statusMsg = MSG_COOLING_DOWN;
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
            }
        }
        
        if (state.isMonitoring) {
            updateStatus(statusMsg, tempColor, PRIORITY_TEMP);
            
            // Update LED
            leds[0] = ledColor;
            FastLED.show();
            
            // Handle target range sound
            if (inTargetRange && !wasInTargetRange && settings.soundEnabled) {
                CoreS3.Speaker.tone(1000, 100);
            }
            wasInTargetRange = inTargetRange;
        }
    }
    
    void updateMonitoringStatus(bool isMonitoring) {
        // Clear temperature status when turning monitoring off
        if (!isMonitoring) {
            clearStatus(PRIORITY_TEMP);
        }
        
        // Update monitoring status
        const char* msg = isMonitoring ? MSG_MONITORING_ON : MSG_MONITORING_OFF;
        uint32_t color = isMonitoring ? Config::Display::COLOR_SUCCESS : Config::Display::COLOR_ERROR;
        updateStatus(msg, color, PRIORITY_MONITORING);
        
        // Update LED
        leds[0] = isMonitoring ? CRGB::Green : CRGB::Black;
        FastLED.show();
    }
    
    void updateCloudStatus(bool isConnected, bool isConnecting = false) {
        const char* msg;
        uint32_t color;
        
        if (isConnecting) {
            msg = MSG_CLOUD_CONNECTING;
            color = Config::Display::COLOR_WARNING;
        } else {
            msg = isConnected ? MSG_CLOUD_CONNECTED : MSG_CLOUD_DISCONNECTED;
            color = isConnected ? Config::Display::COLOR_SUCCESS : Config::Display::COLOR_ERROR;
        }
        
        updateStatus(msg, color, PRIORITY_CLOUD);
    }
    
    void updateStatus(const String& message, uint32_t color, StatusPriority priority) {
        if (priority <= currentPriority) {
            statusMessage = message;
            statusColor = color;
            currentPriority = priority;
            // Update cloud status if needed
            if (state.cloudConnected) {
                ::statusMessage = statusMessage;  // Update cloud variable
            }
        }
    }
    
    void clearStatus(StatusPriority minPriority) {
        if (currentPriority >= minPriority) {
            statusMessage = MSG_READY;
            statusColor = Config::Display::COLOR_TEXT;
            currentPriority = PRIORITY_READY;
            if (state.cloudConnected) {
                ::statusMessage = statusMessage;  // Update cloud variable
            }
        }
    }
    
    String getMessage() const { return statusMessage; }
    uint32_t getColor() const { return statusColor; }
    StatusPriority getPriority() const { return currentPriority; }
    
private:
    String statusMessage = MSG_READY;
    uint32_t statusColor = Config::Display::COLOR_TEXT;
    StatusPriority currentPriority = PRIORITY_READY;
    bool wasInTargetRange = false;
};

// Define static message constants
const char* StatusManager::MSG_TOO_COLD = "Too Cold!";
const char* StatusManager::MSG_WARMING_UP = "Warming Up";
const char* StatusManager::MSG_PERFECT_TEMP = "Perfect Temperature";
const char* StatusManager::MSG_COOLING_DOWN = "Cooling Down";
const char* StatusManager::MSG_TOO_HOT = "Too Hot!";
const char* StatusManager::MSG_INVALID_TEMP = "Invalid Temperature";
const char* StatusManager::MSG_MONITORING_ON = "Monitoring On";
const char* StatusManager::MSG_MONITORING_OFF = "Monitoring Off";
const char* StatusManager::MSG_CLOUD_CONNECTING = "Connecting to Cloud...";
const char* StatusManager::MSG_CLOUD_CONNECTED = "Cloud Connected";
const char* StatusManager::MSG_CLOUD_DISCONNECTED = "Cloud Disconnected";
const char* StatusManager::MSG_READY = "Ready";

// Create global status manager
StatusManager statusMgr;

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\nStarting initialization...");

    // Initialize Core S3 with all features enabled
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    delay(100);  // Give hardware time to initialize

    // Initialize Arduino Cloud (this also handles WiFi connection)
    initProperties();
    ArduinoCloud.begin(ArduinoIoTPreferredConnection);
    setDebugMessageLevel(3);
    ArduinoCloud.printDebugInfo();
    
    // Initialize power management
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(1000);
    state.updateBatteryStatus(); // Initial battery check
    delay(100);
    
    // Initialize pins
    pinMode(KEY_PIN, INPUT_PULLUP);
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);
    Serial.println("Pins initialized");
    
    // Initialize FastLED
    FastLED.addLeds<SK6812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(255);
    leds[0] = CRGB::Black;
    FastLED.show();
    Serial.println("FastLED initialized");
    
    // Initialize display
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(settings.brightness);
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    Serial.println("Display initialized");
    
    // Load settings
    settings.load();
    Serial.println("Settings loaded");
    
    // Initialize I2C for NCIR1
    Wire.begin(2, 1);
    delay(500);
    Serial.println("I2C initialized");
    
    // Read current emissivity from sensor
    Wire.beginTransmission(0x5A);
    Wire.write(0x24);  // Emissivity register address
    Wire.endTransmission(false);
    Wire.requestFrom(0x5A, 2);
    uint16_t currentEmissivity = 0;
    if (Wire.available() == 2) {
        uint8_t lsb = Wire.read();
        uint8_t msb = Wire.read();
        currentEmissivity = (msb << 8) | lsb;
        float actualEmissivity = currentEmissivity / 65535.0;
        Serial.printf("Current sensor emissivity: %.3f (raw: 0x%04X)\n", actualEmissivity, currentEmissivity);
    }
    
    // Set emissivity from settings if different
    uint16_t targetEmissivity = settings.emissivity * 65535;
    if (currentEmissivity != targetEmissivity) {
        Wire.beginTransmission(0x5A);
        Wire.write(0x24);
        Wire.write(targetEmissivity & 0xFF);
        Wire.write(targetEmissivity >> 8);
        Wire.endTransmission();
        Serial.printf("Setting emissivity to: %.3f (raw: 0x%04X)\n", settings.emissivity, targetEmissivity);
    }
    
    // Initial display update
    state.updateStatusWithPriority("Ready", Config::Display::COLOR_SUCCESS, PRIORITY_READY);
    updateDisplay();
    Serial.println("Initialization complete");
   
}

void loop() {
    ArduinoCloud.update();
    CoreS3.update();  // Update button states
    
    // Add this line to handle cloud connection
    handleCloudConnection();
    
    // Add debug output for state tracking
    static MenuState lastDebugState = MAIN_DISPLAY;
    if (lastDebugState != state.menuState) {
        Serial.printf("State changed from %d to %d\n", lastDebugState, state.menuState);
        lastDebugState = state.menuState;
    }

    handleButtons();
    handleKeyAndLED();
    
    static unsigned long lastTempUpdate = 0;
    static unsigned long lastDebugTime = 0;
    static unsigned long lastGoveeCheck = 0;
    const unsigned long TEMP_UPDATE_INTERVAL = 100;  // Keep at 100ms for live readings
    const unsigned long DEBUG_INTERVAL = 1000;  // Debug output every second
    const unsigned long GOVEE_CHECK_INTERVAL = 2000;  // Check Govee every 2 seconds

    static unsigned long lastCloudUpdate = 0;
    const unsigned long CLOUD_UPDATE_INTERVAL = 1000; // Update cloud every second
  
    unsigned long currentTime = millis();

    // Handle cloud updates
    if (currentTime - lastCloudUpdate >= CLOUD_UPDATE_INTERVAL) {
        float temp = readTemperature();
        if (isValidTemperature(temp)) {
            // Update cloud temperature
            temperature = round(settings.useCelsius ? temp : celsiusToFahrenheit(temp));
            // Update cloud monitoring state
            cloudMonitoring = state.isMonitoring;
            isMonitoring = state.isMonitoring;
        }
        lastCloudUpdate = currentTime;
    }
    
    // Debug output every second - now controlled by enableTempDebug flag
    if (enableTempDebug && currentTime - lastDebugTime >= DEBUG_INTERVAL) {
        float currentTemp = readTemperature();
        Serial.print("Menu State: ");
        Serial.print(state.menuState);
        Serial.print(" IsMonitoring: ");
        Serial.print(state.isMonitoring);
        Serial.print(" Temp: ");
        Serial.print(currentTemp);
        Serial.print("°C (");
        Serial.print(celsiusToFahrenheit(currentTemp));
        Serial.println("°F)");
        lastDebugTime = millis();
    }
    
    // Temperature reading and display update
    if (currentTime - lastTempUpdate >= TEMP_UPDATE_INTERVAL) {
        float temp = readTemperature();
        bool tempChanged = abs(temp - state.lastTemp) > 0.5;
        
        if (state.menuState == MAIN_DISPLAY) {
            state.lastTemp = temp;  // Always update the last temperature
            if (tempChanged || state.shouldUpdateDisplay()) {
                updateDisplay();
            }
        }
        lastTempUpdate = currentTime;
    }

    // Govee light update - separate from temperature updates
    if (currentTime - lastGoveeCheck >= GOVEE_CHECK_INTERVAL) {
        if (state.menuState == MAIN_DISPLAY) {
            syncGoveeWithTemp(state.lastTemp);
        }
        lastGoveeCheck = currentTime;
    }

    delay(10);  // Reduced delay since we have better timing control now
}

void handleButtons() {
    static bool lastButton1State = true;
    static bool lastButton2State = true;
    static bool lastButton3State = true;
    
    // Read button states
    bool button1State = digitalRead(BUTTON1_PIN);
    bool button2State = digitalRead(BUTTON2_PIN);
    bool button3State = digitalRead(KEY_PIN);
    
    // Handle long press of Button 2 to toggle temperature debug output
    static unsigned long button2PressStart = 0;
    if (!button2State && lastButton2State) {  // Button just pressed
        button2PressStart = millis();
    }
    if (!button2State && !lastButton2State) {  // Button being held
        if (millis() - button2PressStart > 2000) {  // 2 second long press
            enableTempDebug = !enableTempDebug;
            Serial.printf("\nTemperature debug output %s\n", enableTempDebug ? "enabled" : "disabled");
            button2PressStart = millis();  // Reset timer to prevent multiple toggles
        }
    }

    // Button 1 (Blue) press - Increase emissivity / Cancel
    if (!button1State && lastButton1State) {
        playSuccessSound();
        Serial.print("Button 1 (Blue) pressed - ");
        
        MenuState prevState = state.menuState;
        switch (state.menuState) {
            case MAIN_DISPLAY:
                state.menuState = SETTINGS_MENU;
                state.menuSelection = 0;
                state.menuNeedsRedraw = true;  // Full redraw when entering settings
                Serial.println("Entering Settings Menu");
                break;
                
            case SETTINGS_MENU:
                switch (state.menuSelection) {
                    case 0: // Temperature Unit
                        state.menuState = UNIT_SELECTION;
                        Serial.println("Entering Temperature Unit");
                        break;
                    case 1: // Brightness
                        state.menuState = BRIGHTNESS_ADJUSTMENT;
                        Serial.println("Entering Brightness");
                        break;
                    case 2: // Sound
                        state.menuState = SOUND_SETTINGS;
                        Serial.println("Entering Sound");
                        break;
                    case 3: // Emissivity
                        state.menuState = EMISSIVITY_ADJUSTMENT;
                        Serial.println("Entering Emissivity");
                        break;
                    case 4: // Exit
                        state.menuState = MAIN_DISPLAY;
                        Serial.println("Exiting to Main Display");
                        break;
                }
                if (state.menuState != prevState) {
                    state.menuNeedsRedraw = true;  // Full redraw when changing menu screens
                }
                break;
                
            case UNIT_SELECTION:
            case BRIGHTNESS_ADJUSTMENT:
            case SOUND_SETTINGS:
                state.menuState = SETTINGS_MENU;
                state.menuNeedsRedraw = true;  // Full redraw when returning to settings menu
                settings.save();
                Serial.println("Saving and returning to Menu");
                break;

            case EMISSIVITY_ADJUSTMENT:  // Blue button increases emissivity
                settings.emissivity += Config::Emissivity::STEP;
                if (settings.emissivity > Config::Emissivity::MAX) {
                    settings.emissivity = Config::Emissivity::MIN;  // Loop back to minimum
                }
                Serial.printf("Emissivity increased to: %.3f\n", settings.emissivity);
                break;

            case EMISSIVITY_CONFIRM:  // Cancel changes
                settings.load();  // Reload previous settings
                state.menuState = SETTINGS_MENU;
                state.menuNeedsRedraw = true;  // Full redraw when returning to settings menu
                Serial.println("Canceling emissivity changes");
                break;
        }
        updateDisplay();
    }
    
    // Button 2 (Red) press - Decrease emissivity / Navigate
    if (!button2State && lastButton2State && (millis() - button2PressStart < 2000)) {
        playSuccessSound();
        Serial.print("Button 2 (Red) pressed - ");
        
        switch (state.menuState) {
            case SETTINGS_MENU:
                state.menuSelection = (state.menuSelection + 1) % 5;
                // No need to set menuNeedsRedraw here as drawSettingsMenu handles partial updates
                Serial.print("Menu Selection: ");
                Serial.println(menuItems[state.menuSelection]);
                break;
                
            case UNIT_SELECTION:
                settings.useCelsius = !settings.useCelsius;
                state.menuNeedsRedraw = true;  // Redraw when changing unit
                Serial.print("Temperature Unit: ");
                Serial.println(settings.useCelsius ? "Celsius" : "Fahrenheit");
                break;
                
            case BRIGHTNESS_ADJUSTMENT:
                settings.brightness = (settings.brightness + 64) % 256;
                CoreS3.Display.setBrightness(settings.brightness);
                state.menuNeedsRedraw = true;  // Redraw when changing brightness
                Serial.print("Brightness: ");
                Serial.println(settings.brightness);
                break;
                
            case SOUND_SETTINGS:
                settings.soundEnabled = !settings.soundEnabled;
                state.menuNeedsRedraw = true;  // Redraw when changing sound setting
                Serial.print("Sound: ");
                Serial.println(settings.soundEnabled ? "Enabled" : "Disabled");
                break;
                
            case EMISSIVITY_ADJUSTMENT:  // Red button decreases emissivity
                settings.emissivity -= Config::Emissivity::STEP;
                if (settings.emissivity < Config::Emissivity::MIN) {
                    settings.emissivity = Config::Emissivity::MAX;  // Loop back to maximum
                }
                state.menuNeedsRedraw = true;  // Redraw when changing emissivity
                Serial.printf("Emissivity decreased to: %.3f\n", settings.emissivity);
                break;

            case EMISSIVITY_CONFIRM:  // Confirm and restart
                settings.save();
                Serial.println("Saving emissivity changes and restarting");
                ESP.restart();
                break;
        }
        updateDisplay();
    }
    
    // Button 3 (Key Unit) - Toggle monitoring and show emissivity confirm
    if (!button3State && lastButton3State) {
        playSuccessSound();
        MenuState prevState = state.menuState;
        
        switch (state.menuState) {
            case MAIN_DISPLAY:  // Toggle monitoring from main display
                state.isMonitoring = !state.isMonitoring;
                handleMonitoringStateChange(state.isMonitoring, false);
                Serial.print("Monitoring ");
                Serial.println(state.isMonitoring ? "Started" : "Stopped");
                break;
                
            case EMISSIVITY_ADJUSTMENT:  // Show confirmation screen
                state.menuState = EMISSIVITY_CONFIRM;
                if (state.menuState != prevState) {
                    state.menuNeedsRedraw = true;  // Full redraw when showing confirmation
                }
                Serial.println("Showing emissivity confirmation screen");
                break;
        }
        updateDisplay();
    }
    
    // Update button states
    lastButton1State = button1State;
    lastButton2State = button2State;
    lastButton3State = button3State;
}

void handleKeyAndLED() {
    bool currentKeyState = digitalRead(KEY_PIN);
    
    // Key press detection (with debounce)
    static unsigned long lastKeyPressTime = 0;
    const unsigned long debounceDelay = 250;
    unsigned long currentTime = millis();

  
    if (!currentKeyState && !state.keyPressed && (currentTime - lastKeyPressTime > debounceDelay)) {
        state.keyPressed = true;
        lastKeyPressTime = currentTime;
        
        // Handle key press based on current menu state
        switch (state.menuState) {
            case MAIN_DISPLAY:
                handleMonitoringStateChange(!state.isMonitoring, false);
                break;
                
            case EMISSIVITY_ADJUSTMENT:
                state.menuState = EMISSIVITY_CONFIRM;
                updateDisplay();
                break;
        }
        
        // Toggle LED state
        state.ledActive = !state.ledActive;
        leds[0] = state.ledActive ? CRGB::Green : CRGB::Black;
        FastLED.show();
        
        // Play sound if enabled
        if (settings.soundEnabled) {
            playSuccessSound();
        }
    } else if (currentKeyState && state.keyPressed) {
        state.keyPressed = false;
    }
}

void updateDisplay() {
    // Clear any previous status message when entering settings menu
    if (state.menuState == SETTINGS_MENU) {
        state.clearStatus(PRIORITY_READY);
    }
    
    switch (state.menuState) {
        case MAIN_DISPLAY:
            drawMainDisplay(readTemperature());
            break;
        case SETTINGS_MENU:
            drawSettingsMenu();
            break;
        case UNIT_SELECTION:
            drawUnitSelection();
            break;
        case BRIGHTNESS_ADJUSTMENT:
            drawBrightnessAdjustment();
            break;
        case SOUND_SETTINGS:
            drawSoundSettings();
            break;
        case EMISSIVITY_ADJUSTMENT:
            drawEmissivityAdjustment();
            break;
        case EMISSIVITY_CONFIRM:
            drawEmissivityConfirm();
            break;
        case RESTART_CONFIRM:
            drawRestartConfirm();
            break;
    }
    
    // Only draw status box if not in settings menu
    if (state.menuState != SETTINGS_MENU) {
        drawStatusBox();
    }
}

void drawMainDisplay(float temperature) {
    static float lastDisplayedTemp = -999;
    static bool headerDrawn = false;
    static MenuState lastMenuState = MAIN_DISPLAY;
    static bool wasInTarget = false;
    
    // Force a full redraw if we're coming from a different menu state
    bool needsFullRedraw = (lastMenuState != state.menuState);
    lastMenuState = state.menuState;
    
    // Ensure a full redraw when transitioning to the main display
    if (state.menuState == MAIN_DISPLAY && needsFullRedraw) {
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        lastDisplayedTemp = -999;  // Force temperature redraw
        headerDrawn = false;       // Force header redraw
        wasInTarget = false;       // Reset temperature status
        Serial.println("Main display - Full refresh triggered");
        
        // Always draw header on full refresh
        CoreS3.Display.fillRoundRect(Config::Display::PADDING, 
                                   Config::Display::PADDING, 
                                   CoreS3.Display.width() - (Config::Display::PADDING * 2),
                                   Config::Display::HEADER_HEIGHT,
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_SECONDARY_BG);
                               
        CoreS3.Display.setFont(Config::Display::FONT_HEADER);
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        
        // Calculate vertical center of header box
        int headerCenterY = Config::Display::PADDING + (Config::Display::HEADER_HEIGHT / 2);
        
        CoreS3.Display.drawString("Terp Monitor",
                            CoreS3.Display.width() / 2, 
                            headerCenterY);  // Position at vertical center of header box
        headerDrawn = true;
    }

    // Draw monitoring indicator in top left
    drawStatusIndicators();

    // Draw header background and text only if not already drawn
    if (!headerDrawn) {
        // Draw header background
        CoreS3.Display.fillRoundRect(Config::Display::PADDING, 
                                   Config::Display::PADDING, 
                                   CoreS3.Display.width() - (Config::Display::PADDING * 2),
                                   Config::Display::HEADER_HEIGHT,
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_SECONDARY_BG);
                               
        // Draw header text             
        CoreS3.Display.setFont(Config::Display::FONT_HEADER);
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        
        // Calculate vertical center of header box
        int headerCenterY = Config::Display::PADDING + (Config::Display::HEADER_HEIGHT / 2);
        
        CoreS3.Display.drawString("Terp Monitor",
                            CoreS3.Display.width() / 2, 
                            headerCenterY);  // Position at vertical center of header box
        
        headerDrawn = true;
    }

    
    if (isValidTemperature(temperature)) {
        float displayTemp = settings.useCelsius ? temperature : celsiusToFahrenheit(temperature);
        
        // Determine temperature status, display color, and LED color
        uint32_t tempColor;
        CRGB ledColor;
        String statusMsg;
        bool inTargetRange = false;
        
        if (settings.useCelsius) {
            if (displayTemp < TEMP_COLD_C) {
                tempColor = COLOR_COLD;
                ledColor = LED_COLOR_COLD;
                statusMsg = "Too Cold!";
            } else if (displayTemp < TEMP_MIN_C) {
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
                statusMsg = "Warming Up";
            } else if (displayTemp <= TEMP_MAX_C) {
                tempColor = Config::Display::COLOR_SUCCESS;
                ledColor = LED_COLOR_PERFECT;
                statusMsg = "Perfect Temperature";
                inTargetRange = true;
            } else if (displayTemp > TEMP_HOT_C) {
                tempColor = Config::Display::COLOR_ERROR;
                ledColor = LED_COLOR_HOT;
                statusMsg = "Too Hot!";
            } else {
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
                statusMsg = "Cooling Down";
            }
        } else {
            if (displayTemp < TEMP_COLD_F) {
                tempColor = COLOR_COLD;
                ledColor = LED_COLOR_COLD;
                statusMsg = "Too Cold!";
            } else if (displayTemp < TEMP_MIN_F) {
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
                statusMsg = "Warming Up";
            } else if (displayTemp <= TEMP_MAX_F) {
                tempColor = Config::Display::COLOR_SUCCESS;
                ledColor = LED_COLOR_PERFECT;
                statusMsg = "Perfect Temperature";
                inTargetRange = true;
            } else if (displayTemp > TEMP_HOT_F) {
                tempColor = Config::Display::COLOR_ERROR;
                ledColor = LED_COLOR_HOT;
                statusMsg = "Too Hot!";
            } else {
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
                statusMsg = "Cooling Down";
            }
        }
        
        // Update status message and color
        if (state.isMonitoring) {
            statusMgr.updateTemperatureStatus(temperature, settings.useCelsius);
            
            // Update LED color
            leds[0] = ledColor;
            FastLED.show();
        }
        
        // Play sound when entering target range
        if (inTargetRange && !wasInTarget && settings.soundEnabled) {
            CoreS3.Speaker.tone(1000, 100);
        }
        wasInTarget = inTargetRange;
        
        // Update if temperature changed significantly or needs full redraw
        if (abs(displayTemp - lastDisplayedTemp) >= 0.5 || needsFullRedraw) {
            char tempStr[10];
            char unitStr[2] = {settings.useCelsius ? 'C' : 'F', '\0'};
            sprintf(tempStr, "%d", (int)round(displayTemp));
            
            // Calculate all positions first
            int centerX = CoreS3.Display.width() / 2;
            int centerY = CoreS3.Display.height() / 2;
            
            // Calculate dimensions for temperature display
            CoreS3.Display.setFont(Config::Display::FONT_TEMP);  // Larger temperature
            int tempWidth = CoreS3.Display.textWidth(tempStr);
            int tempHeight = CoreS3.Display.fontHeight();
            
            // Calculate dimensions for unit display
            CoreS3.Display.setFont(Config::Display::FONT_STATUS);  // Smaller unit
            int unitWidth = CoreS3.Display.textWidth(unitStr);
            
            // Calculate the total width needed
            int totalWidth = tempWidth + unitWidth + Config::Display::PADDING;
            
            // Clear previous temperature area
            int clearWidth = totalWidth + (Config::Display::PADDING * 4);
            int clearHeight = tempHeight + (Config::Display::PADDING * 4);
            int tempAreaX = centerX - (clearWidth/2);
            int tempAreaY = centerY - (clearHeight/2);
            
            CoreS3.Display.fillRoundRect(tempAreaX,
                                       tempAreaY,
                                       clearWidth,
                                       clearHeight,
                                       Config::Display::CORNER_RADIUS,
                                       Config::Display::COLOR_SECONDARY_BG);
            
            // Draw temperature - always centered
            CoreS3.Display.setFont(Config::Display::FONT_TEMP);
            CoreS3.Display.setTextColor(tempColor);  // Use status-based color
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.drawString(tempStr, 
                                    centerX - (unitWidth/2), 
                                    centerY);
            
            // Draw unit
            CoreS3.Display.setFont(Config::Display::FONT_STATUS);
            CoreS3.Display.drawString(unitStr,
                                    centerX + (tempWidth/2) + Config::Display::PADDING,
                                    centerY - (tempHeight/4));  // Align with top of temperature
            
            lastDisplayedTemp = displayTemp;
        }
    } else if (needsFullRedraw) {
        state.updateStatus("Invalid Temperature", Config::Display::COLOR_ERROR);
    }
    
    // Update battery status
    state.updateBatteryStatus();
    
    // Draw emissivity value in top-right corner
    char emissStr[10];
    sprintf(emissStr, "E:%.2f", settings.emissivity);
    CoreS3.Display.setFont(Config::Display::FONT_SMALL);
    CoreS3.Display.setTextDatum(middle_right);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString(emissStr, CoreS3.Display.width() - 30, 70);
    
    // Draw battery indicator in top-right corner
    drawBatteryIndicator(CoreS3.Display.width() - 80, 15, 50, 20);
    
    // Sync Govee light with temperature status
    //syncGoveeWithTemp(temperature);
}

void drawSettingsMenu() {
    // Store last selection to only update changed items
    static int lastSelection = -1;
    
    // Calculate positions for menu items
    const int screenHeight = CoreS3.Display.height();
    const int navHintHeight = 40;  // Space for navigation hint
    const int titleHeight = 50;    // Space for title
    
    // Calculate available space for menu items
    const int availableHeight = screenHeight - titleHeight - navHintHeight;
    const int totalItems = 5;  // Including Exit
    
    // Calculate optimal spacing to fit all items in available space
    const int itemHeight = 35;  // Height of each menu item background
    const int itemSpacing = (availableHeight - (itemHeight * totalItems)) / (totalItems + 1);
    const int startY = titleHeight + itemSpacing + (itemHeight / 2);  // Start after title with one spacing unit
    
    const int menuWidth = CoreS3.Display.width() - 40;  // Slightly narrower for better aesthetics
    const int itemX = 20;  // Left margin for menu items
    const int textX = itemX + 15;  // Text indent from item background
    
    // Clear and redraw everything if needed
    if (state.menuNeedsRedraw) {
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        
        // Draw title
        CoreS3.Display.setFont(Config::Display::FONT_HEADER);
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(Config::Display::COLOR_PRIMARY);
        CoreS3.Display.drawString("Settings", CoreS3.Display.width()/2, 30);
        
        // Draw navigation hint at the very bottom with proper spacing
        CoreS3.Display.setFont(Config::Display::FONT_SMALL);
        CoreS3.Display.setTextDatum(bottom_center);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        CoreS3.Display.drawString("Blue: Select  |  Red: Next", CoreS3.Display.width()/2, CoreS3.Display.height() - 15);
        
        // Draw all menu items
        CoreS3.Display.setFont(Config::Display::FONT_MENU);
        CoreS3.Display.setTextDatum(middle_left);
        
        for (int i = 0; i < totalItems; i++) {
            int y = startY + (i * (itemHeight + itemSpacing));
            
            // Draw background for selected item
            if (i == state.menuSelection) {
                CoreS3.Display.fillRoundRect(itemX, y - (itemHeight/2), menuWidth, itemHeight, 8, 
                    Config::Display::COLOR_SECONDARY_BG);
                CoreS3.Display.setTextColor(Config::Display::COLOR_PRIMARY);
            } else {
                CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
            }
            
            // Draw menu item text
            CoreS3.Display.drawString(menuItems[i], textX, y);
        }
        
        state.menuNeedsRedraw = false;
        lastSelection = state.menuSelection;
        return;  // Exit since we've redrawn everything
    }
    
    // Handle selection changes
    if (lastSelection != state.menuSelection) {
        // Clear previous selection's background if it exists
        if (lastSelection >= 0) {
            int lastY = startY + (lastSelection * (itemHeight + itemSpacing));
            CoreS3.Display.fillRoundRect(itemX, lastY - (itemHeight/2), menuWidth, itemHeight, 8, 
                Config::Display::COLOR_BACKGROUND);
            // Redraw the previous selection's text
            CoreS3.Display.setFont(Config::Display::FONT_MENU);
            CoreS3.Display.setTextDatum(middle_left);
            CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
            CoreS3.Display.drawString(menuItems[lastSelection], textX, lastY);
        }
        
        // Draw new selection's background and text
        int newY = startY + (state.menuSelection * (itemHeight + itemSpacing));
        CoreS3.Display.fillRoundRect(itemX, newY - (itemHeight/2), menuWidth, itemHeight, 8, 
            Config::Display::COLOR_SECONDARY_BG);
        CoreS3.Display.setFont(Config::Display::FONT_MENU);
        CoreS3.Display.setTextDatum(middle_left);
        CoreS3.Display.setTextColor(Config::Display::COLOR_PRIMARY);
        CoreS3.Display.drawString(menuItems[state.menuSelection], textX, newY);
        
        lastSelection = state.menuSelection;
    }
}

void drawUnitSelection() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setFont(Config::Display::FONT_STATUS);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    // Draw title
    CoreS3.Display.drawString("Temperature Unit", CoreS3.Display.width()/2, 30);
    
    // Draw current selection
    CoreS3.Display.setFont(Config::Display::FONT_TEMP);
    CoreS3.Display.drawString(settings.useCelsius ? "Celsius" : "Fahrenheit", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    
    // Clear the bottom area first
    CoreS3.Display.fillRect(0, CoreS3.Display.height() - 60, CoreS3.Display.width(), 60, Config::Display::COLOR_BACKGROUND);
    
    // Draw instructions
    CoreS3.Display.setFont(Config::Display::FONT_STATUS);
    CoreS3.Display.drawString("Press Red to save", CoreS3.Display.width()/2, CoreS3.Display.height() - 80);
    CoreS3.Display.drawString("Press Blue to change", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawBrightnessAdjustment() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setFont(Config::Display::FONT_STATUS);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    // Draw title
    CoreS3.Display.drawString("Brightness", CoreS3.Display.width()/2, 30);
    
    // Draw current value
    CoreS3.Display.setFont(Config::Display::FONT_TEMP);
    String brightnessText;
    switch(settings.brightness) {
        case 64:  brightnessText = "25%"; break;
        case 128: brightnessText = "50%"; break;
        case 192: brightnessText = "75%"; break;
        case 255: brightnessText = "100%"; break;
        default:  brightnessText = "100%"; break;  // Default to 100% for any other value
    }
    CoreS3.Display.drawString(brightnessText, CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    
    // Clear the bottom area first
    CoreS3.Display.fillRect(0, CoreS3.Display.height() - 60, CoreS3.Display.width(), 60, Config::Display::COLOR_BACKGROUND);
    
    // Draw instructions
    CoreS3.Display.setFont(Config::Display::FONT_STATUS);
    CoreS3.Display.drawString("Press Red to save", CoreS3.Display.width()/2, CoreS3.Display.height() - 80);
    CoreS3.Display.drawString("Press Blue to change", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawSoundSettings() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setFont(Config::Display::FONT_STATUS);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    // Draw title
    CoreS3.Display.drawString("Sound", CoreS3.Display.width()/2, 30);
    
    // Draw current state
    CoreS3.Display.setFont(Config::Display::FONT_TEMP);
    CoreS3.Display.drawString(settings.soundEnabled ? "ON" : "OFF", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    
    // Clear the bottom area first
    CoreS3.Display.fillRect(0, CoreS3.Display.height() - 60, CoreS3.Display.width(), 60, Config::Display::COLOR_BACKGROUND);
    
    // Draw instructions
    CoreS3.Display.setFont(Config::Display::FONT_STATUS);
    CoreS3.Display.drawString("Press Red to save", CoreS3.Display.width()/2, CoreS3.Display.height() - 80);
    CoreS3.Display.drawString("Press Blue to change", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawEmissivityAdjustment() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Draw title
    CoreS3.Display.setFont(Config::Display::FONT_HEADER);
    CoreS3.Display.setTextDatum(top_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_PRIMARY);
    CoreS3.Display.drawString("Emissivity Setting", CoreS3.Display.width()/2, 10);
    
    // Draw current value
    char valueStr[10];
    sprintf(valueStr, "%.2f", settings.emissivity);
    CoreS3.Display.setFont(Config::Display::FONT_TEMP);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString(valueStr, CoreS3.Display.width()/2, 100);
    
    // Draw buttons with modern style
    const int btnWidth = 80;
    const int btnHeight = 40;
    
    // Up button
    if (settings.emissivity < Config::Emissivity::MAX) {
        CoreS3.Display.fillRoundRect(220, 60, btnWidth, btnHeight, 
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_PRIMARY);
        CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.drawString("+", 260, 80);
    }
    
    // Down button
    if (settings.emissivity > Config::Emissivity::MIN) {
        CoreS3.Display.fillRoundRect(220, 140, btnWidth, btnHeight,
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_PRIMARY);
        CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.drawString("-", 260, 160);
    }
    
    // Back button
    CoreS3.Display.fillRoundRect(10, 180, 100, 50,
                                Config::Display::CORNER_RADIUS,
                                Config::Display::COLOR_ACCENT);
    CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.drawString("Back", 60, 205);
    
    // Draw min/max values
    CoreS3.Display.setFont(Config::Display::FONT_SMALL);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    char minStr[15], maxStr[15];
    sprintf(minStr, "Min: %.2f", Config::Emissivity::MIN);
    sprintf(maxStr, "Max: %.2f", Config::Emissivity::MAX);
    CoreS3.Display.drawString(minStr, 20, 140);
    CoreS3.Display.drawString(maxStr, 20, 60);
}

void drawEmissivityConfirm() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Draw title and message
    CoreS3.Display.setFont(Config::Display::FONT_HEADER);
    CoreS3.Display.setTextDatum(top_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_WARNING);
    CoreS3.Display.drawString("Restart Required", CoreS3.Display.width()/2, 10);
    
    CoreS3.Display.setFont(Config::Display::FONT_STATUS);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Device must be restarted for", CoreS3.Display.width()/2, 60);
    CoreS3.Display.drawString("emissivity changes to take effect", CoreS3.Display.width()/2, 80);
    
    // Draw buttons
    const int btnWidth = 120;
    const int btnHeight = 40;
    const int btnSpacing = 20;
    const int startY = 140;
    
    // Restart button (Blue - Button 1)
    CoreS3.Display.fillRoundRect(CoreS3.Display.width()/2 - btnWidth - btnSpacing/2, startY,
                               btnWidth, btnHeight, Config::Display::CORNER_RADIUS,
                               Config::Display::COLOR_PRIMARY);
    CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.drawString("Restart now", CoreS3.Display.width()/2 - btnWidth/2 - btnSpacing/2, startY + btnHeight/2);
    
    // Cancel button (Red - Button 2)
    CoreS3.Display.fillRoundRect(CoreS3.Display.width()/2 + btnSpacing/2, startY,
                               btnWidth, btnHeight, Config::Display::CORNER_RADIUS,
                               Config::Display::COLOR_ERROR);
    CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.drawString("Cancel", CoreS3.Display.width()/2 + btnWidth/2 + btnSpacing/2, startY + btnHeight/2);
}

void drawRestartConfirm() {
    static bool lastButton1State = true;
    static bool lastButton2State = true;
    bool button1State = digitalRead(BUTTON1_PIN);
    bool button2State = digitalRead(BUTTON2_PIN);
    
    // Button 1 (Restart)
    if (!button2State && lastButton2State) {
        if (settings.soundEnabled) {
            playSuccessSound();
        }
        delay(500);
        ESP.restart();
    }
    
    // Button 2 (Cancel)
    if (!button1State && lastButton1State) {
        state.menuState = SETTINGS_MENU;
        if (settings.soundEnabled) {
            playSuccessSound();
        }
        delay(50);
    }
    
    lastButton1State = button1State;
    lastButton2State = button2State;
}

void drawStatusBox() {
    static String lastStatus = "";
    static uint32_t lastColor = 0;
    static MenuState lastMenuState = SETTINGS_MENU;  // Initialize to SETTINGS_MENU to force first draw
    
    // Force redraw if we just came from settings menu
    bool forceRedraw = (lastMenuState == SETTINGS_MENU && state.menuState == MAIN_DISPLAY);
    lastMenuState = state.menuState;
    
    // Only redraw if the status/color has changed or we're forcing a redraw
    if (lastStatus != statusMgr.getMessage() || lastColor != statusMgr.getColor() || forceRedraw) {
        const int boxHeight = 50;
        const int boxMargin = Config::Display::PADDING;
        const int boxWidth = CoreS3.Display.width() - (boxMargin * 2);
        const int boxY = CoreS3.Display.height() - boxHeight - boxMargin;
        
        // Draw status box with rounded corners
        CoreS3.Display.fillRoundRect(boxMargin, 
                                   boxY, 
                                   boxWidth, 
                                   boxHeight, 
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_SECONDARY_BG);
        
        // Add subtle border
        CoreS3.Display.drawRoundRect(boxMargin,
                                   boxY,
                                   boxWidth,
                                   boxHeight,
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_BORDER);
        
        // Only draw status text if there is a message
        if (statusMgr.getMessage().length() > 0) {
            CoreS3.Display.setFont(Config::Display::FONT_STATUS);
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.setTextColor(statusMgr.getColor());
            
            // Draw text with a slight shadow effect
            CoreS3.Display.drawString(statusMgr.getMessage().c_str(), 
                                    CoreS3.Display.width()/2 + 1,
                                    boxY + boxHeight/2 + 1,
                                    Config::Display::COLOR_BACKGROUND);
            CoreS3.Display.drawString(statusMgr.getMessage().c_str(),
                                    CoreS3.Display.width()/2,
                                    boxY + boxHeight/2);
        }
        
        // Update the last known values
        lastStatus = statusMgr.getMessage();
        lastColor = statusMgr.getColor();
    }
}

void drawBatteryIndicator(int x, int y, int width, int height) {
    const int batteryNub = 4;  // Size of battery connector nub
    const int borderWidth = 2;
    const int innerWidth = width - (borderWidth * 2);
    const int innerHeight = height - (borderWidth * 2);
    
    // Draw outer shell
    CoreS3.Display.drawRoundRect(x, y, width, height, 3, Config::Display::COLOR_TEXT);
    CoreS3.Display.drawRoundRect(x + width, y + (height - batteryNub) / 2, 
                                batteryNub, batteryNub, 1, Config::Display::COLOR_TEXT);
    
    // Calculate fill width based on battery level
    int fillWidth = (state.batteryLevel / 100.0f) * innerWidth;
    
    // Choose color based on battery level and charging state
    uint32_t fillColor;
    if (state.isCharging) {
        fillColor = Config::Display::COLOR_BATTERY_CHARGING;
    } else if (state.batteryLevel <= 20) {
        fillColor = Config::Display::COLOR_BATTERY_LOW;
    } else if (state.batteryLevel <= 50) {
        fillColor = Config::Display::COLOR_BATTERY_MEDIUM;
    } else {
        fillColor = Config::Display::COLOR_BATTERY_HIGH;
    }
    
    // Fill battery level
    if (fillWidth > 0) {
        CoreS3.Display.fillRoundRect(x + borderWidth, y + borderWidth, 
                                   fillWidth, innerHeight, 2, fillColor);
    }
    
    // Draw percentage text
    CoreS3.Display.setFont(Config::Display::FONT_SMALL);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    String batteryText = String(static_cast<int>(state.batteryLevel)) + "%";
    if (state.isCharging) {
        batteryText += "⚡";
    }
    
    CoreS3.Display.drawString(batteryText, 
                            x + (width / 2), 
                            y + (height / 2));
}

float readTemperature() {
    float object_temp = 0;
    float ambient_temp = 0;
    
    // Read ambient temperature
    Wire.beginTransmission(0x5A);
    Wire.write(0x06);
    Wire.endTransmission(false);
    Wire.requestFrom(0x5A, 2);
    if (Wire.available() == 2) {
        uint16_t ambient_raw = (Wire.read() | (Wire.read() << 8));
        ambient_temp = ambient_raw * 0.02 - 273.15;
    }
    
    // Read object temperature
    Wire.beginTransmission(0x5A);
    Wire.write(0x07);
    Wire.endTransmission(false);
    Wire.requestFrom(0x5A, 2);
    if (Wire.available() == 2) {
        uint16_t object_raw = (Wire.read() | (Wire.read() << 8));
        object_temp = object_raw * 0.02 - 273.15;
    }
    
    return object_temp;
}

void playSuccessSound() {
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(1047, 100);  // 1047Hz for 100ms
        delay(100);
        CoreS3.Speaker.tone(1319, 100);  // 1319Hz for 100ms
        delay(100);
    }
}

void playErrorSound() {
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(880, 200);  // 880Hz for 200ms
        delay(250);
        CoreS3.Speaker.tone(660, 200);  // 660Hz for 200ms
        delay(200);
    }
}

void onStatusMessageChange() {
    // Only update local status if the change originated from the cloud
    // and it's not just a monitoring or temperature status
    if (statusMgr.getPriority() > PRIORITY_TEMP) {
        statusMgr.updateStatus(statusMessage, state.statusColor, PRIORITY_CLOUD);
        updateDisplay();
    }
}

void onCloudMonitoringChange() {
    handleMonitoringStateChange(cloudMonitoring, true);
}

void onIsMonitoringChange() {
    handleMonitoringStateChange(isMonitoring, false);
}

void handleMonitoringStateChange(bool newState, bool fromCloud) {
    // Update local state
    state.isMonitoring = newState;
    
    // Sync cloud variables
    if (!fromCloud) {
        cloudMonitoring = newState;
        isMonitoring = newState;
    }
    
    // Update status and LED through status manager
    statusMgr.updateMonitoringStatus(newState);
    
    // Update display
    updateDisplay();
}

void onKeyPress() {
    handleMonitoringStateChange(!state.isMonitoring, false);
}

void onCloudRestartChange() {
    if (cloudRestart) {
        // Clear the screen first
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        
        // Draw a centered countdown message
        CoreS3.Display.setFont(Config::Display::FONT_HEADER);
        CoreS3.Display.setTextDatum(middle_center);
        
        // Countdown from 3
        for (int i = 3; i > 0; i--) {
            // Clear previous number
            CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
            
            // Update status
            String countMsg = "Restarting in " + String(i) + "...";
            statusMgr.updateStatus(countMsg, Config::Display::COLOR_WARNING, PRIORITY_CLOUD);
            
            // Draw large countdown number
            CoreS3.Display.setTextColor(Config::Display::COLOR_WARNING);
            CoreS3.Display.drawString(String(i), 
                CoreS3.Display.width()/2, 
                CoreS3.Display.height()/2);
                
            // Force display update
            updateDisplay();
            
            // Play warning beep if sound enabled
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(880, 200);
            }
            
            // Wait for a full second
            delay(1000);
        }
        
        // Final "Restarting now!" message
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        statusMgr.updateStatus("Restarting now!", Config::Display::COLOR_WARNING, PRIORITY_CLOUD);
        CoreS3.Display.drawString("Restarting now!", 
            CoreS3.Display.width()/2, 
            CoreS3.Display.height()/2);
        updateDisplay();
        
        // Final warning beep if sound enabled
        if (settings.soundEnabled) {
            CoreS3.Speaker.tone(440, 500);
        }
        
        delay(500);
        
        // Reset the cloud variable
        cloudRestart = false;
        
        // Restart the device
        ESP.restart();
    }
}

void handleCloudConnection() {
    bool currentlyConnected = ArduinoCloud.connected();
    
    // Connection state changed
    if (currentlyConnected != state.cloudConnected) {
        state.cloudConnected = currentlyConnected;
        if (currentlyConnected) {
            // Successfully connected
            state.reconnectAttempts = 0;
            statusMgr.updateCloudStatus(true);
            if (settings.soundEnabled) {
                playSuccessSound();
            }
        } else {
            // Lost connection
            statusMgr.updateCloudStatus(false);
            if (settings.soundEnabled) {
                playErrorSound();
            }
        }
    }
    
    // Handle reconnection if disconnected
    if (!currentlyConnected) {
        unsigned long currentTime = millis();
        unsigned long interval = (state.reconnectAttempts >= state.MAX_RECONNECT_ATTEMPTS) ? 
                               state.LONG_RECONNECT_INTERVAL : 
                               state.RECONNECT_INTERVAL;
                               
        if (currentTime - state.lastReconnectAttempt >= interval) {
            state.lastReconnectAttempt = currentTime;
            state.reconnectAttempts++;
            
            // Update status to show reconnection attempt
            char statusMsg[32];
            sprintf(statusMsg, "Reconnecting... (%d)", state.reconnectAttempts);
            statusMgr.updateCloudStatus(false, true);
            
            // Attempt to reconnect
            if (WiFi.status() != WL_CONNECTED) {
                ArduinoIoTPreferredConnection.disconnect();
                ArduinoIoTPreferredConnection.connect();
            }
            
            if (!ArduinoCloud.connected()) {
                ArduinoCloud.begin(ArduinoIoTPreferredConnection);
            }
            
            // If we've hit max attempts, show longer wait message
            if (state.reconnectAttempts >= state.MAX_RECONNECT_ATTEMPTS) {
                statusMgr.updateCloudStatus(false);
            }
        }
    }
}

void drawStatusIndicators() {
    const int leftMargin = 15;
    const int topMargin = 60;
    const int indicatorSpacing = 35;
    const int circleSize = 8;  // Radius of the circles
    
    int baseX = leftMargin + circleSize;
    int baseY = topMargin + circleSize;

    // Draw Monitor Status
    CoreS3.Display.setFont(Config::Display::FONT_SMALL);
    CoreS3.Display.setTextDatum(middle_left);
    
    // Clear the monitoring indicator area
    CoreS3.Display.fillCircle(baseX, baseY, circleSize, Config::Display::COLOR_BACKGROUND);
    
    // Draw monitoring indicator (circle)
    if (state.isMonitoring) {
        CoreS3.Display.fillCircle(baseX, baseY, circleSize, Config::Display::COLOR_SUCCESS);
    } else {
        CoreS3.Display.drawCircle(baseX, baseY, circleSize, Config::Display::COLOR_ERROR);
    }

    // Draw Cloud Status below monitoring status
    baseY += indicatorSpacing;
    
    // Clear the cloud indicator area
    CoreS3.Display.fillCircle(baseX, baseY, circleSize, Config::Display::COLOR_BACKGROUND);
    
    if (state.cloudConnected) {
        CoreS3.Display.fillCircle(baseX, baseY, circleSize, Config::Display::COLOR_SUCCESS);
    } else {
        CoreS3.Display.drawCircle(baseX, baseY, circleSize, Config::Display::COLOR_ERROR);
    }

    // Simple text labels
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("M", baseX + 15, baseY + 2 - indicatorSpacing);
    CoreS3.Display.drawString("C", baseX + 15, baseY + 2);
}
