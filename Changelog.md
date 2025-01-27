# NCIR1 Temperature Meter V2 - Changelog

## Latest Update: 2025-01-20 08:16:39 EST

### Project Overview
Temperature measurement device using M5CoreS3 with cloud connectivity, adjustable settings and modern user interface.

### Button Mapping
- **Button 1 (Red Button)**: Primary action / Increase values
- **Button 2 (Blue Button)**: Secondary action / Decrease values
- **Button 3 (Key Unit)**: Confirmation / Toggle monitoring

### Recent Changes (2025-01-20)

#### Code Organization
- Updated main source file header with timestamp
- Added reference to Changelog.md in main source file
- Improved code documentation

### Recent Changes (2025-01-19)

#### New Features
- Enhanced emissivity adjustment behavior:
  - Values now loop around when reaching limits (0.65 -> 1.00 -> 0.65)
  - Blue button increases emissivity
  - Red button decreases emissivity
  - Key button shows confirmation screen

### Recent Changes (2025-01-18)

#### New Features
- Added Govee Light Integration
  - Temperature-based color synchronization
  - Automatic on/off with monitoring state
  - Smart rate limit handling to prevent API throttling
  - Optimized API calls (combined power and color commands)
  - Independent 2-second update interval
  - Visual temperature status through light color

- Enhanced Debugging Capabilities
  - Temperature debug toggle (hold Button 2 for 2 seconds)
  - Improved serial output formatting
  - Detailed Govee status messages
  - Better error handling and reporting

#### Performance Improvements
- Separated Govee updates from temperature readings for better responsiveness
- Reduced initialization delay to 100ms for faster startup
- Increased debug message level to 3 for better troubleshooting
- Increased charge current to 1000mA for faster charging
- Improved state management and monitoring toggle

#### Code Organization
- Better separation of concerns between temperature reading and light control
- Cleaner button handling with distinct short and long press actions
- More efficient API call batching
- Enhanced error handling for network operations

### Recent Changes (2025-01-15)

#### New Features
- Added cloud connectivity through Arduino IoT Cloud
  - Remote temperature monitoring
  - Cloud-based control of monitoring state
  - Remote device restart capability
  - Status message synchronization
  - Secure WiFi connection handling
- Implemented smart reconnection logic
  - Auto-retry with increasing intervals
  - Maximum retry attempts before longer wait
  - Connection status display
- Added comprehensive LED feedback system
  - Blue for cold temperatures
  - Green for optimal range
  - Amber for warning states
  - Red for hot temperatures
- Enhanced power management
  - Battery level monitoring
  - Charge current set to 900mA
  - External output control
- Modern UI color scheme implementation
  - Dark theme with consistent colors
  - High contrast for better readability
  - Color-coded status indicators
- Precise temperature ranges
  - Cold: Below 480°F (248.89°C)
  - Optimal: 580°F-640°F (304.44°C-337.78°C)
  - Hot: Above 800°F (426.67°C)
- Display optimization
  - 250ms refresh interval
  - Reduced screen flicker
  - Smooth menu transitions

### Recent Changes (2025-01-14)

#### Bug Fixes and Improvements
- Fixed status box not appearing when exiting settings menu
- Fixed header flickering by only redrawing when necessary
- Fixed emissivity setting being updated too frequently
- Fixed sound feedback playing repeatedly when in target range
- Improved initialization sequence with better delays and status messages
- Removed redundant emissivity display from main screen
- Optimized temperature display updates to reduce screen flicker
- Improved error handling in temperature reading
- Simplified debug logging for emissivity changes
- Added power management initialization
- Added proper hardware initialization delays
- Added "Ready" status message on startup
- Added proper state tracking for temperature target range
- Added full display refresh when switching between menus

#### Emissivity Adjustment Implementation
- Added full emissivity adjustment functionality
  - Blue button increases emissivity
  - Red button decreases emissivity
  - Key Unit opens confirmation dialog
  - Values constrained between 0.65 and 1.00
  - Step size of 0.01
- Added confirmation dialog with restart option
  - Shows warning about required restart
  - Option to restart immediately or cancel
  - Proper saving of settings

#### Settings Menu Improvements
- Fixed visibility issues with Exit button
- Improved menu layout and spacing
- Added clear navigation hints
- Updated button labels to use colors instead of numbers
- Made UI consistent across all settings screens

#### UI Enhancements
- Added proper screen clearing for all menu pages
- Updated button colors and positions
- Improved text alignment and spacing
- Made button labels more user-friendly
- Consistent button behavior across menus

### Recent Changes (2024-01-10)

#### Bug Fixes
- Fixed button behavior in emissivity adjustment screen:
  - Blue button (Button 1) now correctly increases emissivity
  - Red button (Button 2) now correctly decreases emissivity
  - Fixed duplicate handleButtons function
  - Added clearer button labels in debug messages

### Current Status
Currently working on:
- Cloud connectivity optimization
- Remote monitoring capabilities
- Settings menu and button consistency
- Successfully implemented emissivity adjustment with confirmation
- Updated button references to use color names
- Fixed settings menu layout and visibility
- Improved button function consistency

### TODO/Next Steps
1. Testing
   - Verify all menu transitions
   - Test button behaviors in each screen
   - Check emissivity save/load functionality
   - Test restart behavior after changes
   - Validate cloud connectivity features
   - Test remote monitoring capabilities

2. Potential Improvements
   - Add more user feedback for actions
   - Consider additional confirmation dialogs
   - Review error handling
   - Add status messages for key actions
   - Enhance cloud data logging
   - Add historical temperature tracking

### Known Issues
- None currently reported

### Notes
- Button behavior standardized across menus
- Settings are properly saved to persistent storage
- Restart required for emissivity changes to take effect
- Cloud connectivity requires valid WiFi credentials
- Temperature monitoring can be controlled locally or remotely

---
*Last Updated: January 20, 2025 08:16:39 EST*
