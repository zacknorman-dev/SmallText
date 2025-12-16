# SmolTxt Feature Backlog

## High Priority

### Periodic Background Sync for All Conversations - FIXED v0.56.15
App now performs automatic sync every 30 seconds while active, pulling new messages from all conversations even when not viewing the specific conversation. Previously messages only synced when entering a conversation, causing delays in receiving messages while on other screens (main menu, conversation list, settings, etc.). The periodic sync ensures messages arrive promptly regardless of which screen the user is viewing.

### Creator Invite Screen Doesn't End When Joiner Joins
When the creator displays an invite code and the joiner successfully joins, the creator remains stuck on the invite code display screen. The screen should automatically transition to the messaging screen when a join is detected, allowing both users to start chatting immediately without manual intervention.

### Creator Bounces to Main Menu After Seeing Both Members Join
After the creator sees the system message showing both members have joined the conversation (e.g., "creator joined" and "joiner joined"), the creator is unexpectedly bounced back to the main menu instead of remaining in the messaging screen. The creator should stay in the active conversation to continue chatting.

### Back Button in Conversation Should Return to Conversation List
When pressing the back button while viewing a conversation, the app currently returns to the main menu. It should instead return to "My Conversations" list, allowing users to quickly navigate between conversations without going through the main menu each time. This makes conversation switching much more fluid.

### Activity Timer Not Resetting During Text Input
Device goes to sleep (nap mode) while actively entering invite codes or other text input. Keystroke activity should reset the inactivity timer to prevent sleep interruption during active use. This is particularly problematic during the invite code entry flow where the 5-minute timeout can be interrupted by the device sleeping.

### Charging Devices Should Not Nap
Devices that are actively charging should remain awake and not enter nap mode. Check battery charging status before initiating nap mode. This allows devices to act as always-on hubs when plugged in.

### Quiet Hours Feature
Add configurable quiet hours mode for nighttime/classroom use. During quiet hours: napping devices never wake up for incoming messages, and awake devices don't make alert sounds (silent notifications only). Needs UI for setting quiet hours schedule (e.g., 10 PM - 7 AM).

### OTA Update Screen White Display
Sometimes the "Check for Update" screen displays completely white instead of showing the update UI. Display refresh issue during OTA update flow. May be related to display refresh timing or partial/full update mode selection during the update process.

### Reset to Factory Conditions
Add a "Reset to Factory" option in Settings menu that:
- Wipes all village data (all slot files)
- Clears all messages
- Resets preferences to defaults
- Restarts device in clean state
- Useful for recovery from corruption scenarios
- Should require confirmation to prevent accidental data loss

### Messages Not Marked as Read When Actively Viewing Conversation
When a user is actively viewing a conversation (in the messaging screen) and receives a new message from the other participant, the message should automatically be marked as read and a read receipt should be sent. Currently messages show as "received" (status 2) instead of "read" (status 3) even when both users are viewing the conversation simultaneously. The read marking logic needs to trigger immediately when a message arrives while the recipient is in the messaging screen for that conversation.

## Medium Priority

### Hide "My Conversations" Menu Item When No Conversations Exist
When there are no active conversations (all village slots are empty), the "My Conversations" menu item should not appear in the main menu. This prevents users from selecting an empty option and provides a cleaner UI when starting fresh. The menu should dynamically show/hide this item based on whether any conversations exist.

### Persist Username in Settings and Reuse Across Conversations
Save the username (display name) that the user chooses to a persistent settings field. When creating or joining new conversations, check if a username is already stored:
- If username exists in settings: automatically use it without prompting
- If no username exists: prompt for username and save to settings for future use
This eliminates repetitive username entry and provides a consistent identity across all conversations. Consider adding a "Change Username" option in Settings menu to update the stored name.

### UI Text: "Join Conversation" → "Join a Conversation"
Change menu text from "Join Conversation" to "Join a Conversation" for better grammar and clarity.

### Remove Village Reference in Conversation Deletion Confirmation
The conversation deletion confirmation dialog still references "village" terminology. Update to use "conversation" consistently with the rest of the UI.

### Change "Leave Group" to "Leave Conversation" in Menu
The conversation menu currently shows "Leave Group" but should say "Leave Conversation" to match the updated terminology throughout the app.

### Update Conversation Naming Screen Text
The conversation creation screen still references "village name". Update the prompt to "Pick a name for this conversation" to match the updated terminology throughout the app.

### Refresh Conversation List When Returning From Conversation View
When navigating back to "My Conversations" from a conversation view, the list should refresh/rebuild to show updated conversation names, message counts, or other changes that may have occurred. Currently the list may show stale data if changes were made while viewing the conversation.

### Full Refresh on Generated Code Screen
The invite code display screen needs a full display refresh to ensure proper rendering. Currently may show artifacts or incomplete rendering when transitioning to the code display state.

### Full Refresh on Invite a Friend Screen
The "Invite a Friend" screen needs a full display refresh to ensure proper rendering. Currently may show artifacts or incomplete rendering when transitioning to the invite friend display state.

### Improve Username Entry Description When Joining Conversation
The screen where joiners enter their display name after successfully entering an invite code needs a better, more descriptive prompt. Current text may not clearly explain that this is setting the name other members will see. Should clarify: "Enter your display name" or "Choose how others will see you in this conversation".

### Back Button From Conversation Should Return to My Conversations Menu
When pressing the back button while viewing a conversation, the navigation should return to the "My Conversations" menu instead of going to the main menu. This provides a more logical navigation flow and reduces the number of steps to view other conversations.

### Full Refresh When Entering Conversation View
When navigating into a conversation from the conversation list, the display should perform a full refresh instead of a partial update. This ensures the conversation view renders cleanly without artifacts or incomplete rendering from the previous screen state.

### Device Information Screen in Settings
Add a "Device Info" or "About Device" option in the Settings menu that displays:
- MAC Address (for debugging network issues and device identification)
- IP Address (current WiFi network connection)
- WiFi SSID (network name)
- Signal strength (RSSI)
- Firmware version
- Free storage space

This information is useful for troubleshooting connectivity issues, verifying device identity for MQTT/network debugging, and helping users understand their device status. The screen should be read-only with a back button to return to settings.

## Low Priority

### Message Storage Architecture: Per-Conversation Files
**ARCHITECTURAL CHANGE** - Refactor message storage from single global `/messages.dat` to per-conversation files (`/village_X_messages.dat`). Current architecture orphans messages when conversations are deleted, gradually filling flash storage with inaccessible data.

**Current Problem:**
- All messages stored in `/messages.dat`, filtered by `villageId` at load time
- Deleting a conversation (village slot) leaves orphaned messages in the file
- No automatic cleanup - flash fills with unrecoverable messages
- Users cannot free space by deleting old conversations

**Proposed Solution (Hybrid Architecture):**
- Metadata: `/village_0.dat` through `/village_9.dat` (conversation info)
- Messages: `/village_0_messages.dat` through `/village_9_messages.dat` (per-conversation)
- Deleting slot X removes both `village_X.dat` and `village_X_messages.dat` atomically
- Messages remain append-only (flash-friendly)
- Each conversation is self-contained and portable

**Benefits:**
- True space recovery when deleting conversations
- Self-contained conversations (easy backup/export)
- No orphaned data accumulation
- Cleaner mental model (1 conversation = 2 related files)

**Implementation:**
- Update `Village::loadMessages()` to read from slot-specific file
- Update message save logic to write to slot-specific file
- Update `Village::deleteSlot()` to remove both files
- Migration: Copy messages from global file to per-slot files on first load
- Handle slot reassignment edge cases

**Files to modify:**
- src/Village.cpp (loadMessages, saveMessage, deleteSlot)
- src/Village.h (method signatures if needed)

**Testing required:**
- Message persistence across restarts
- Conversation deletion fully frees space
- Migration from old to new format
- Slot reuse scenarios

## Ideas / Future Consideration
### Refactor All Messages to JSON Format (Transport-Agnostic Architecture)
**MAJOR REFACTORING** - Convert entire messaging system from colon-delimited string format to pure JSON. This would:

**Benefits:**
- Eliminate parsing bugs between sync and regular message paths (current bug source)
- Single unified parser for all message types
- Transport-agnostic: same JSON works for MQTT, LoRa, HTTP, WebSocket, Bluetooth
- Easy to extend with new fields without breaking existing code
- Self-documenting structure with named fields
- Handles special characters in content without escaping issues
- Future-proof for switching transport layers or adding new ones

**Implementation:**
1. Define JSON schema for all message types (SHOUT, ACK, READ_RECEIPT, etc.)
2. Add message version field for future compatibility
3. Create unified JSON parser with type-based routing
4. Update all send functions to generate JSON instead of colon-strings
5. Update all receive handlers to parse JSON
6. Optional: Create Transport interface for easy provider swapping

**Scope:**
- MQTTMessenger.cpp: Major rewrite of parsing and formatting
- All message send/receive handlers
- Breaking change: Old/new versions can't communicate
- Alternative: Add version negotiation for gradual migration

**Files to modify:**
- src/MQTTMessenger.cpp (core changes)
- src/MQTTMessenger.h (message structures)
- src/Messages.h (message format definitions)

**Testing required:**
- All message types (SHOUT, ACK, READ_RECEIPT)
- Sync flow (request/response)
- Multi-device scenarios
- Edge cases (empty fields, special characters)