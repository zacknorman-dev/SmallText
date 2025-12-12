# SmolTxt v0.53.0 - Invite Code System

## Major Changes
- **Redesigned Joining System**: Replaced passphrase-based joining with temporary 8-digit invite codes
- **Enhanced Security**: 5-minute expiring codes make brute force attacks impossible (100M combinations in 5 min window)
- **Kid-Friendly UX**: Simple numeric codes that are easier to share and enter than complex passphrases

## New Features
- **Village Creation Flow**: After creating a conversation, users now see "Invite a Friend" menu
- **Invite Code Generation**: Generate random 8-digit codes with 5-minute expiry
- **Countdown Timer**: Real-time display of remaining invite code validity
- **Join Code Entry**: Numeric-only input field for entering 8-digit invite codes
- **Explanatory Screens**: Clear instructions guide users through inviting and joining

## UI Updates
- Changed "New Village" → "New Conversation" in main menu
- Changed "Join Village" → "Join Conversation" in main menu
- Added 5 new screens: Village Created, Invite Explain, Code Display, Join Explain, Code Input
- Added invite code management methods to UI class

## Technical Implementation
- Added 5 new UI states (VILLAGE_CREATED, INVITE_EXPLAIN, INVITE_CODE_DISPLAY, JOIN_EXPLAIN, JOIN_CODE_INPUT)
- Implemented invite code storage with expiry timestamps
- Added handlers for all new screens and navigation flows
- Prepared infrastructure for MQTT invite protocol (coming in future release)

## Known Limitations
- MQTT invite code exchange not yet implemented (placeholder for v0.54.0)
- Passphrase joining still available via old code path (will be removed)

## Developer Notes
- Invite codes are 8 random digits (10000000-99999999)
- Expiry time is 300000ms (5 minutes) from generation
- Code display refreshes every second to update countdown timer
- Future: MQTT topic `village/invites/[code]` will carry encrypted village credentials
