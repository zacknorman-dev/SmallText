# SmolTxt Feature Backlog

## High Priority

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

## Medium Priority

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

### Improve Username Entry Description When Joining Conversation
The screen where joiners enter their display name after successfully entering an invite code needs a better, more descriptive prompt. Current text may not clearly explain that this is setting the name other members will see. Should clarify: "Enter your display name" or "Choose how others will see you in this conversation".

## Low Priority

## Ideas / Future Consideration
