# SmolTxt Build and Release Process

## Complete Release Checklist

When the user says "do the build process", follow these steps **IN ORDER** without skipping any:

### 1. Update Version Number
**File:** `platformio.ini`
**Line:** `build_flags` section
**Change:** `-DFIRMWARE_VERSION=\"X.XX.X\"`
**Example:** `-DFIRMWARE_VERSION=\"0.35.1\"`

### 2. Build the Firmware
**Command:** `C:\Users\zackn\.platformio\penv\Scripts\platformio.exe run`
**Wait for:** "SUCCESS" message
**Verify:** Firmware size is displayed (usually ~1.1 MB)

### 3. Git Commit and Push
**Commands (in order):**
```powershell
git add -A
git commit -m "vX.XX.X - [Brief description of changes]"
git push origin main
```

### 4. Create and Push Git Tag
**Commands:**
```powershell
git tag vX.XX.X
git push origin vX.XX.X
```

### 5. Create GitHub Release with Firmware Binary
**Command:**
```powershell
gh release create vX.XX.X ".pio\build\heltec_vision_master_e290\firmware.bin" --title "vX.XX.X - [Title]" --notes "[Release notes describing what changed]"
```

**Important Notes:**
- The firmware path is `.pio\build\heltec_vision_master_e290\firmware.bin` (NOT esp32-s3-devkitc-1)
- Use double quotes around the path
- Release notes should be clear about what bugs were fixed or features added

## Version Numbering Scheme
- **Major.Minor.Patch** (e.g., 0.35.1)
- Increment patch for bug fixes
- Increment minor for new features
- Major version changes are rare

## Common Mistakes to Avoid
1. ❌ **DON'T** create the GitHub release before updating the version number in platformio.ini
2. ❌ **DON'T** forget to rebuild after changing the version number
3. ❌ **DON'T** use the wrong firmware path (it's heltec_vision_master_e290, not esp32-s3-devkitc-1)
4. ❌ **DON'T** skip the git tag step
5. ❌ **DON'T** forget to push the tag to GitHub

## Quick Reference
**Full Path to PlatformIO:** `C:\Users\zackn\.platformio\penv\Scripts\platformio.exe`
**Firmware Binary Location:** `.pio\build\heltec_vision_master_e290\firmware.bin`
**Current Version Check:** Look in `platformio.ini` under `build_flags`

## Example Complete Process for v0.35.2
```powershell
# 1. Update platformio.ini to version 0.35.2
# 2. Build
C:\Users\zackn\.platformio\penv\Scripts\platformio.exe run

# 3. Git operations
git add -A
git commit -m "v0.35.2 - Fix read receipt bug"
git push origin main

# 4. Tag
git tag v0.35.2
git push origin v0.35.2

# 5. Release
gh release create v0.35.2 ".pio\build\heltec_vision_master_e290\firmware.bin" --title "v0.35.2 - Read Receipt Fix" --notes "Fixed bug where read receipts were not updating message status correctly."
```

## If You Mess Up
If you create a release with the wrong version:
1. Delete the release: `gh release delete vX.XX.X`
2. Delete the tag locally: `git tag -d vX.XX.X`
3. Delete the tag remotely: `git push origin :refs/tags/vX.XX.X`
4. Start over from step 1

## When Complete
✅ Verify the release appears at: https://github.com/zacknorman-dev/SmallText/releases
✅ Verify the firmware.bin is attached to the release
✅ The device can check for updates and see the new version
