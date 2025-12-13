# Release Notes - v0.54.7

## Invite System UX Improvements

### Join Flow Enhancements
- **Successful join now goes directly to messaging screen** instead of conversation list
- Success message displays for 2 seconds: "Successfully joined: [village name]"
- Loading screen shows: "Entering conversation... One moment..."
- User is placed directly into the conversation - no need to navigate again

### Error Handling Improvements
- All join errors now return to main menu instead of looping back to code entry
- Better error messages:
  - "Code not found or has expired - Check the code and try again"
  - "Network error - Please try again"
- No more confusing loop where failed attempts keep showing the code entry screen

### Invite Cleanup
- Added `unpublishInvite()` function to clear retained MQTT messages
- Invite codes are now properly cleaned up when:
  - The code expires (60 second timeout)
  - User cancels the invite display screen
- Prevents stale invite codes from persisting on the MQTT broker

### Technical Details
- MQTT invite messages use `retain=1` flag (already present in v0.54.6)
- Retained messages are cleared by publishing empty payload with retain flag
- Join flow: Verify → Looking up → Success → Loading → Direct to messaging
- Error flow: Verify → Looking up → Error → Back to main menu

## Status
- v0.54.6 race condition fix confirmed working in testing
- Join system tested successfully with code 75168935
- UX flow now matches user expectations - no navigation needed after successful join
