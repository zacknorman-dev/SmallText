# SmolTxt Release Script
# 
# USAGE:
#   Auto-increment version:
#     powershell -ExecutionPolicy Bypass -File .\release.ps1 -Message "Your changes"
#   
#   Specify version manually:
#     powershell -ExecutionPolicy Bypass -File .\release.ps1 -Version "0.42.5" -Message "Your changes"
#
# EXPLANATION:
#   -ExecutionPolicy Bypass  = Temporarily disable script execution restrictions (only for this command)
#   -File                    = Execute the specified script file
#   -Version                 = (Optional) Version number without 'v' prefix (e.g., "0.42.5")
#   -Message                 = Release description

param(
    [Parameter(Mandatory=$false)]
    [string]$Version,
    
    [Parameter(Mandatory=$true)]
    [string]$Message
)

$ErrorActionPreference = "Stop"

# Stop any running monitor jobs first
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Stopping monitor jobs..." -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan
Stop-Job -Name Monitor_* -ErrorAction SilentlyContinue
Remove-Job -Name Monitor_* -ErrorAction SilentlyContinue
Write-Host "      Monitor jobs stopped" -ForegroundColor Green

# Auto-detect next version if not specified
if (-not $Version) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "Auto-detecting next version..." -ForegroundColor Cyan
    Write-Host "========================================`n" -ForegroundColor Cyan
    
    # Get latest tag from GitHub
    $latestTag = git describe --tags --abbrev=0 2>$null
    if ($LASTEXITCODE -eq 0 -and $latestTag) {
        # Parse version (strip 'v' prefix)
        $latestVersion = $latestTag -replace '^v', ''
        $parts = $latestVersion -split '\.'
        if ($parts.Count -eq 3) {
            $major = [int]$parts[0]
            $minor = [int]$parts[1]
            $patch = [int]$parts[2]
            
            # Increment patch version
            $patch++
            $Version = "$major.$minor.$patch"
            
            Write-Host "      Latest tag: $latestTag" -ForegroundColor Gray
            Write-Host "      Next version: v$Version" -ForegroundColor Green
        } else {
            Write-Host "      ERROR: Could not parse latest tag '$latestTag'" -ForegroundColor Red
            exit 1
        }
    } else {
        # No tags found, start with 0.1.0
        $Version = "0.1.0"
        Write-Host "      No existing tags found" -ForegroundColor Gray
        Write-Host "      Starting with: v$Version" -ForegroundColor Green
    }
} else {
    # Strip leading 'v' if present (script adds it automatically)
    if ($Version -match '^v') {
        $Version = $Version.Substring(1)
        Write-Host "Note: Removed leading 'v' from version. Script expects version without 'v' prefix (e.g., '0.41.4')" -ForegroundColor Yellow
    }
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "SmolTxt Release Process v$Version" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Step 1: Update version in version.h
Write-Host "[1/8] Updating version in version.h..." -ForegroundColor Yellow
$versionPath = "src\version.h"
$content = Get-Content $versionPath -Raw
$newVersionLine = "#define BUILD_NUMBER ""v$Version"""
$content = $content -replace '#define BUILD_NUMBER "v[\d\.]+"', $newVersionLine
Set-Content $versionPath $content -NoNewline
Write-Host "      Version updated to v$Version" -ForegroundColor Green

# Step 2: Clean and build firmware
Write-Host "`n[2/8] Cleaning and building firmware..." -ForegroundColor Yellow
& C:\Users\zackn\.platformio\penv\Scripts\platformio.exe run --target clean
& C:\Users\zackn\.platformio\penv\Scripts\platformio.exe run
if ($LASTEXITCODE -ne 0) {
    Write-Host "      Build failed!" -ForegroundColor Red
    # Clean up tag if it was created
    $ErrorActionPreference = "SilentlyContinue"
    git tag -d "v$Version" 2>&1 | Out-Null
    $ErrorActionPreference = "Stop"
    exit 1
}
Write-Host "      Build successful" -ForegroundColor Green

# Step 3: Verify firmware.bin exists
Write-Host "`n[3/8] Verifying firmware.bin..." -ForegroundColor Yellow
$firmwarePath = ".pio\build\heltec_vision_master_e290\firmware.bin"
if (!(Test-Path $firmwarePath)) {
    Write-Host "      firmware.bin not found!" -ForegroundColor Red
    # Clean up tag if it was created
    $ErrorActionPreference = "SilentlyContinue"
    git tag -d "v$Version" 2>&1 | Out-Null
    $ErrorActionPreference = "Stop"
    exit 1
}
$fileSize = (Get-Item $firmwarePath).Length
Write-Host "      firmware.bin found ($([math]::Round($fileSize/1KB, 2)) KB)" -ForegroundColor Green

# Step 4: Copy firmware.bin to releases folder
Write-Host "`n[4/8] Copying firmware to releases folder..." -ForegroundColor Yellow
$releasesFolder = "releases"
if (!(Test-Path $releasesFolder)) {
    New-Item -ItemType Directory -Path $releasesFolder | Out-Null
}
$releaseFirmware = "$releasesFolder\firmware-v$Version.bin"
Copy-Item $firmwarePath $releaseFirmware -Force
Write-Host "      Copied to $releaseFirmware" -ForegroundColor Green

# Step 5: Stage and commit changes
Write-Host "`n[5/8] Committing changes..." -ForegroundColor Yellow
git add -A
git add -f $releaseFirmware
git commit -m "Release v$Version - $Message"
if ($LASTEXITCODE -ne 0) {
    Write-Host "      Commit failed (may already be committed)" -ForegroundColor Yellow
}
Write-Host "      Changes committed" -ForegroundColor Green

# Step 6: Create annotated tag
Write-Host "`n[6/8] Creating Git tag..." -ForegroundColor Yellow
$ErrorActionPreference = "SilentlyContinue"
git tag -a "v$Version" -m "Release v$Version`n`n$Message"
$tagCreateResult = $LASTEXITCODE
$ErrorActionPreference = "Stop"
if ($tagCreateResult -ne 0) {
    Write-Host "      Tag already exists, deleting and recreating..." -ForegroundColor Yellow
    git tag -d "v$Version" 2>&1 | Out-Null
    $ErrorActionPreference = "SilentlyContinue"
    git push origin ":refs/tags/v$Version" 2>&1 | Out-Null
    $ErrorActionPreference = "Stop"
    git tag -a "v$Version" -m "Release v$Version`n`n$Message"
}
Write-Host "      Tag v$Version ready" -ForegroundColor Green

# Step 7: Push to GitHub
Write-Host "`n[7/8] Pushing to GitHub..." -ForegroundColor Yellow

# Push main branch and tags together
Write-Host "      Pushing main branch..." -ForegroundColor Cyan
$ErrorActionPreference = "Continue"
$pushOutput = git push origin main 2>&1 | Out-String
$pushExitCode = $LASTEXITCODE
$ErrorActionPreference = "Stop"
Write-Host "      $pushOutput" -ForegroundColor Gray
if ($pushExitCode -eq 0 -or $pushOutput -like "*Everything up-to-date*") {
    Write-Host "      Main branch pushed successfully" -ForegroundColor Green
} else {
    Write-Host "      WARNING: Push to main may have failed" -ForegroundColor Yellow
}

# Push tag (force to handle recreated tags)
Write-Host "      Pushing tag v$Version..." -ForegroundColor Cyan
$ErrorActionPreference = "Continue"
$tagOutput = git push --force origin "refs/tags/v$Version" 2>&1 | Out-String
$tagExitCode = $LASTEXITCODE
$ErrorActionPreference = "Stop"
Write-Host "      $tagOutput" -ForegroundColor Gray
if ($tagExitCode -eq 0) {
    Write-Host "      Tag v$Version pushed successfully" -ForegroundColor Green
} else {
    Write-Host "      WARNING: Tag push failed" -ForegroundColor Red
    Write-Host "      Error: $tagOutput" -ForegroundColor Red
    # Clean up local tag
    $ErrorActionPreference = "SilentlyContinue"
    git tag -d "v$Version" 2>&1 | Out-Null
    $ErrorActionPreference = "Stop"
    exit 1
}

# Step 8: Create GitHub Release
Write-Host "`n[8/8] Creating GitHub Release..." -ForegroundColor Yellow

# Check if GitHub CLI is installed
$ghExists = Get-Command gh -ErrorAction SilentlyContinue
if ($ghExists) {
    # Use GitHub CLI to create release
    Write-Host "      Using GitHub CLI to create release..." -ForegroundColor Cyan
    
    # Create release notes
    $releaseNotes = @"
## SmolTxt v$Version

$Message

### Installation
1. Download ``firmware-v$Version.bin``
2. Flash to ESP32-S3:
   ``````
   esptool.py --chip esp32s3 --port COM13 write_flash 0x10000 firmware-v$Version.bin
   ``````

### Hardware
- **Device**: Heltec Vision Master E290
- **Display**: 2.90" e-paper (296x128)
- **MCU**: ESP32-S3
"@
    
    # Check if release already exists
    $ErrorActionPreference = "SilentlyContinue"
    $releaseCheck = gh release view "v$Version" 2>&1
    $checkResult = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    
    if ($checkResult -eq 0) {
        # Release exists, just upload the binary and ensure it's published
        Write-Host "      Release already exists, uploading binary and publishing..." -ForegroundColor Cyan
        $ErrorActionPreference = "SilentlyContinue"
        gh release upload "v$Version" $releaseFirmware --clobber 2>&1 | Out-Null
        gh release edit "v$Version" --draft=false 2>&1 | Out-Null
        $uploadResult = $LASTEXITCODE
        $ErrorActionPreference = "Stop"
        
        if ($uploadResult -eq 0) {
            Write-Host "      Binary uploaded and release published successfully!" -ForegroundColor Green
            Write-Host "      View at: https://github.com/zacknorman-dev/SmallText/releases/tag/v$Version" -ForegroundColor Cyan
        } else {
            Write-Host "      Binary upload failed!" -ForegroundColor Red
        }
    } else {
        # Create new release with binary (published, not draft)
        Write-Host "      Creating new release..." -ForegroundColor Cyan
        $ErrorActionPreference = "SilentlyContinue"
        gh release create "v$Version" $releaseFirmware --title "SmolTxt v$Version" --notes "$Message" --latest 2>&1 | Out-Null
        $createResult = $LASTEXITCODE
        $ErrorActionPreference = "Stop"
        
        if ($createResult -eq 0) {
            Write-Host "      GitHub Release created successfully!" -ForegroundColor Green
            Write-Host "      View at: https://github.com/zacknorman-dev/SmallText/releases/tag/v$Version" -ForegroundColor Cyan
        } else {
            Write-Host "      GitHub CLI failed, opening browser instead..." -ForegroundColor Yellow
            Start-Process "https://github.com/zacknorman-dev/SmallText/releases/new?tag=v$Version"
        }
    }
} else {
    # Fallback to browser
    Write-Host "      GitHub CLI not found, opening browser..." -ForegroundColor Yellow
    Write-Host "`n      Please install GitHub CLI for automated releases:" -ForegroundColor Cyan
    Write-Host "      winget install --id GitHub.cli" -ForegroundColor White
    Write-Host "`n      Or manually create release at:" -ForegroundColor Cyan
    Write-Host "      https://github.com/zacknorman-dev/SmallText/releases/new?tag=v$Version" -ForegroundColor White
    Start-Process "https://github.com/zacknorman-dev/SmallText/releases/new?tag=v$Version"
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "Release v$Version Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
