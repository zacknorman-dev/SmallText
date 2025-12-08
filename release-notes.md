# v0.33.8 - NTP Time Sync and Timestamp Preservation

## Fixed Message Chronological Order Corruption

**The Problem:**
- Messages were getting out of order during sync
- Each device used `millis()` (device-specific boot time) as timestamps
- Receiving device would overwrite original timestamps, destroying chronological order
- Made messages appear with wrong usernames or in wrong order

**The Solution:**
- **NTP Time Sync**: Syncs real world time on WiFi connect and every 24 hours
- **Unix Timestamps**: All messages now use real Unix timestamps (seconds since 1970)
- **Preserve Original Timestamps**: No more overwriting during sync
- **Cross-Device Compatibility**: All devices share same time baseline

## Changes
- Added NTP sync to WiFiManager (syncs on connect + every 24 hours)
- New `getCurrentTime()` helper returns Unix timestamp
- Updated message creation to use real timestamps
- Removed timestamp adjustment logic that was corrupting order
- Removed millis()-based timestamp baseline

## Important Note
**Requires new village for clean testing** - old messages have millis()-based timestamps that won't sort correctly with new Unix timestamps. Create a fresh village to test this release.

## What's Next
Test with new village, verify:
- Messages stay in correct chronological order after sync
- Timestamps are preserved from original sender
- No more "same username on both sides" corruption
