# v0.25.0 - Improved OTA Update Experience

## 🎯 Major UX Improvements

### Boot-Time Update Check
- Device now checks for updates on every reboot
- If update available, shows choice screen: **RIGHT to update, LEFT to skip**
- User stays in control - no forced updates

### Remote Critical Updates
- `update` command via MQTT now shows "CRITICAL UPDATE" screen
- Only triggers when user is on main menu (won't interrupt messaging)
- User must press RIGHT to continue with update
- **No more surprise reboots!**

### Bug Fixes
- Fixed: OTA update messages no longer leak into village chat input
- Fixed: Input field properly cleared when exiting OTA screens

## 📡 How It Works

**On Boot:**
1. Device checks GitHub for new version
2. If newer version exists, shows update screen
3. User chooses: update now or skip

**Remote Update Command:**
```bash
mosquitto_pub -h test.mosquitto.org -t "smoltxt/DEVICE_MAC/command" -m "update"
```
- Shows "CRITICAL UPDATE" screen
- User must press RIGHT to proceed
- Only works when user is on main menu

## 🔄 No More Background Checks
- Removed 60-second automatic checks
- Updates only check on boot or via remote command
- Simpler, more predictable behavior

## 🎮 User Experience
- ✅ User always in control
- ✅ Clear messaging about updates
- ✅ No interruptions during active use
- ✅ No text bleeding into chat
