
Last updated: 2025-01-29 02:16:27 UTC
NCIR1 Temperature Meter V2 - Project Overview

## Hardware Components
1. **Main Controller**: M5CoreS3
   - Display: Built-in LCD screen
   - Speaker: Built-in for audio feedback
   - WiFi: Built-in for cloud connectivity
   - Power Management: Battery monitoring and charging capabilities

2. **Input Devices**:
   - Red Button (Button 1) - Connected to GPIO17
   - Blue Button (Button 2) - Connected to GPIO18
   - Key Unit Button (Button 3) - Connected to GPIO8

3. **Sensors**:
   - MLX90614 NCIR Temperature Sensor
   - Connected via I2C (SDA: GPIO2, SCL: GPIO1)

4. **Output Devices**:
   - Key Unit LED (GPIO9)
   - SK6812 RGB LED for status indication
   - Integrated Display with adjustable brightness

## Features

### 1. Temperature Monitoring
- Real-time temperature measurement
- Temperature ranges:
  - Cold: Below 480°F (248.89°C)
  - Warning (Warming Up): 480°F - 580°F
  - Optimal Range: 580°F - 640°F (304.44°C - 337.78°C)
  - Warning (Cooling Down): 640°F - 800°F
  - Hot: Above 800°F (426.67°C)

### 2. Display Interface
- Modern dark theme UI
- Temperature display in °C or °F
- Status indicators:
  - Monitoring state
  - Cloud connection
  - Battery level with charging indication
  - Temperature status messages
- Visual feedback through color coding:
  - Blue: Cold temperature
  - Amber: Warning/Warming up
  - Green: Perfect temperature
  - Red: Hot temperature

### 3. Settings Menu
- Temperature Unit Selection (°C/°F)
- Display Brightness (25%, 50%, 75%, 100%)
- Sound Settings (Enable/Disable)
- Emissivity Adjustment (0.65 - 1.00)

### 4. Cloud Integration
- Arduino IoT Cloud connectivity
- Remote monitoring capabilities
- Cloud-controlled monitoring state
- Remote device restart functionality
- Status message synchronization
- Automatic reconnection handling

### 5. Govee Smart Light Integration
- Temperature-based color synchronization
- Automatic on/off with monitoring state
- API rate limit handling
- Color-coded temperature status

## Button Mapping

### Main Display
- **Red Button (Button 1)**
  - Press: Enter Settings Menu
  
- **Blue Button (Button 2)**
  - Press: No action
  - Long Press (2s): Toggle temperature debug output
  
- **Key Unit (Button 3)**
  - Press: Toggle monitoring state

### Settings Menu
- **Red Button**
  - Press: Select menu item
  
- **Blue Button**
  - Press: Navigate through menu items
  
### Settings Submenus
1. **Temperature Unit**
   - Red: Save and return
   - Blue: Toggle °C/°F

2. **Brightness**
   - Red: Save and return
   - Blue: Cycle brightness (25% -> 50% -> 75% -> 100%)

3. **Sound**
   - Red: Save and return
   - Blue: Toggle sound on/off

4. **Emissivity**
   - Red: Decrease value
   - Blue: Increase value
   - Key Unit: Confirm changes

## Technical Specifications
- Operating Temperature Range: -70°C to 380°C
- Emissivity Range: 0.65 to 1.00
- Display Update Interval: 250ms
- Cloud Update Interval: 1000ms
- LED Update Interval: 2000ms
- Battery Monitoring Interval: 5000ms

## Safety Features
- Temperature validation checks
- Error handling for sensor readings
- Battery monitoring and charging management
- Cloud connection status monitoring
- Data validation for emissivity settings

## Additional Features
- Audio feedback for button presses
- Visual feedback through LED
- Persistent settings storage
- Smart power management
- Detailed debugging capabilities
- Automatic display updates
- Status message priority system

## Getting Started
1. Clone the repository
2. Install the required dependencies
3. Configure your WiFi credentials in thingProperties.h
4. Upload to your M5CoreS3 device

## Dependencies
- M5CoreS3 library
- Arduino IoT Cloud
- FastLED library
- ArduinoJson library

## Project Structure
- [Current_WorkSpace.ino](Current_WorkSpace.ino) - Main source code
- [thingProperties.h](thingProperties.h) - IoT cloud configuration
- [Changelog.md](Changelog.md) - Version history and updates

