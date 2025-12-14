#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Opens dual serial monitors for both ESP32 devices

.DESCRIPTION
    Automatically finds available COM ports, kills any existing monitors,
    and streams output from both devices in the VS Code terminal.

.USAGE
    # Standard usage (auto-detects ports, streams output):
    powershell -ExecutionPolicy Bypass -File .\monitor.ps1
    
    # Run as background job in VS Code terminal:
    powershell -ExecutionPolicy Bypass -File .\monitor.ps1
    
    # The script will:
    # 1. Kill existing monitor processes
    # 2. Auto-detect COM13 and COM14
    # 3. Start background jobs for each port
    # 4. Stream output with [COMxx] prefixes
    # 5. Press Ctrl+C to stop viewing (jobs continue in background)

.NOTES
    To stop all monitors:
    Stop-Job -Name Monitor_*; Remove-Job -Name Monitor_*
#>

param(
    [switch]$Help
)

if ($Help) {
    Get-Help $MyInvocation.MyCommand.Path -Detailed
    exit 0
}

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "SmolTxt Dual Monitor Launcher" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# PlatformIO executable path
$PIO = "C:\Users\zackn\.platformio\penv\Scripts\platformio.exe"

# Check if PlatformIO exists
if (-not (Test-Path $PIO)) {
    Write-Host "ERROR: PlatformIO not found at $PIO" -ForegroundColor Red
    exit 1
}

# Step 1: Kill any existing monitor processes
Write-Host "[1/4] Stopping existing monitor processes..." -ForegroundColor Yellow
try {
    Stop-Process -Name "platformio" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    Write-Host "      Existing monitors stopped" -ForegroundColor Green
} catch {
    Write-Host "      No existing monitors found" -ForegroundColor Gray
}

# Step 2: Detect available COM ports
Write-Host ""
Write-Host "[2/4] Detecting COM ports..." -ForegroundColor Yellow
try {
    $ports = & $PIO device list 2>$null | Select-String "^COM\d+" | ForEach-Object { $_.ToString().Trim() }
    
    if ($ports.Count -eq 0) {
        Write-Host "      ERROR: No COM ports found. Are devices connected?" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "      Found $($ports.Count) port(s): $($ports -join ', ')" -ForegroundColor Green
    
    # Try to identify which port is which
    foreach ($port in $ports) {
        $portInfo = & $PIO device list 2>$null | Select-String -Context 0,2 "^$port"
        Write-Host "      $port detected" -ForegroundColor Gray
    }
} catch {
    Write-Host "      ERROR: Failed to detect ports: $_" -ForegroundColor Red
    exit 1
}

# Step 3: Start monitors in background jobs
Write-Host ""
Write-Host "[3/4] Starting serial monitors..." -ForegroundColor Yellow

$monitorCount = 0
foreach ($port in $ports) {
    try {
        # Start monitor as background job
        $jobName = "Monitor_$port"
        Start-Job -Name $jobName -ScriptBlock {
            param($PIO, $port)
            & $PIO device monitor --port $port
        } -ArgumentList $PIO, $port | Out-Null
        
        $monitorCount++
        Write-Host "      Monitor $monitorCount started for $port (job: $jobName)" -ForegroundColor Green
        Start-Sleep -Milliseconds 300  # Stagger the starts
    } catch {
        Write-Host "      WARNING: Failed to start monitor for ${port}: $_" -ForegroundColor Yellow
    }
}

# Step 4: Summary
# Step 4: Summary
Write-Host ""
Write-Host "[4/4] Complete!" -ForegroundColor Yellow
Write-Host "      Started $monitorCount monitor job(s)" -ForegroundColor Green
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Monitors running as background jobs" -ForegroundColor Cyan
Write-Host "Use 'Get-Job' to see status" -ForegroundColor Cyan
Write-Host "Use 'Receive-Job -Name Monitor_COMxx' to view output" -ForegroundColor Cyan
Write-Host "Use 'Stop-Job -Name Monitor_*; Remove-Job -Name Monitor_*' to stop" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Streaming output from monitors (Ctrl+C to stop viewing)..." -ForegroundColor Yellow
Write-Host ""

# Stream output from both jobs
try {
    while ($true) {
        Start-Sleep -Milliseconds 500
        foreach ($port in $ports) {
            $jobName = "Monitor_$port"
            $output = Receive-Job -Name $jobName -ErrorAction SilentlyContinue
            if ($output) {
                Write-Host "[$port] " -NoNewline -ForegroundColor Cyan
                Write-Host $output
            }
        }
    }
} finally {
    Write-Host ""
    Write-Host "Monitor jobs still running in background" -ForegroundColor Yellow
}