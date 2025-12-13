# SmolTxt Feature Backlog

## High Priority

### Activity Timer Not Resetting During Text Input
Device goes to sleep (nap mode) while actively entering invite codes or other text input. Keystroke activity should reset the inactivity timer to prevent sleep interruption during active use. This is particularly problematic during the invite code entry flow where the 5-minute timeout can be interrupted by the device sleeping.

### Charging Devices Should Not Nap
Devices that are actively charging should remain awake and not enter nap mode. Check battery charging status before initiating nap mode. This allows devices to act as always-on hubs when plugged in.

### Quiet Hours Feature
Add configurable quiet hours mode for nighttime/classroom use. During quiet hours: napping devices never wake up for incoming messages, and awake devices don't make alert sounds (silent notifications only). Needs UI for setting quiet hours schedule (e.g., 10 PM - 7 AM).

### Reset to Factory Conditions
Add a "Reset to Factory" option in Settings menu that:
- Wipes all village data (all slot files)
- Clears all messages
- Resets preferences to defaults
- Restarts device in clean state
- Useful for recovery from corruption scenarios
- Should require confirmation to prevent accidental data loss

## Medium Priority

## Low Priority

## Ideas / Future Consideration
