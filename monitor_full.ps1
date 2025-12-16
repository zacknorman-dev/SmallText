#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Opens dual serial monitors with full output capture to files

.DESCRIPTION
    Captures ALL output from both devices without loss, saves to logs/,
    and displays in real-time with proper prefixes.

.USAGE
    powershell -ExecutionPolicy Bypass -File .\monitor_full.ps1
#>

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "SmolTxt Dual Monitor (Full Capture)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# PlatformIO executable path
$PIO = "C:\Users\zackn\.platformio\penv\Scripts\platformio.exe"

# Check if PlatformIO exists
if (-not (Test-Path $PIO)) {
    Write-Host "ERROR: PlatformIO not found at $PIO" -ForegroundColor Red
    exit 1
}

# Create logs directory if it doesn't exist
$LogDir = ".\logs"
if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir | Out-Null
}

# Generate timestamp for log files
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

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
} catch {
    Write-Host "      ERROR: Failed to detect ports: $_" -ForegroundColor Red
    exit 1
}

# Step 3: Start monitors with file capture
Write-Host ""
Write-Host "[3/4] Starting serial monitors with full capture..." -ForegroundColor Yellow

$monitorJobs = @()
foreach ($port in $ports) {
    $logFile = Join-Path $LogDir "${port}_${Timestamp}.log"
    
    try {
        # Start monitor with output redirected to file and pipeline
        $jobName = "Monitor_$port"
        $job = Start-Job -Name $jobName -ScriptBlock {
            param($PIO, $port, $logFile)
            
            # Start monitor and tee to both console and file
            & $PIO device monitor --port $port | Tee-Object -FilePath $logFile
            
        } -ArgumentList $PIO, $port, $logFile
        
        $monitorJobs += @{
            Port = $port
            Job = $job
            LogFile = $logFile
            LastPosition = 0
        }
        
        Write-Host "      Monitor started for $port" -ForegroundColor Green
        Write-Host "      Logging to: $logFile" -ForegroundColor Gray
        Start-Sleep -Milliseconds 300
    } catch {
        Write-Host "      WARNING: Failed to start monitor for ${port}: $_" -ForegroundColor Yellow
    }
}

# Step 4: Stream output
Write-Host ""
Write-Host "[4/4] Complete! Streaming output..." -ForegroundColor Yellow
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Full output saved to $LogDir" -ForegroundColor Cyan
Write-Host "Press Ctrl+C to stop" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Stream output from both jobs
try {
    while ($true) {
        Start-Sleep -Milliseconds 50  # Very fast polling
        
        foreach ($monitorInfo in $monitorJobs) {
            $port = $monitorInfo.Port
            $jobName = "Monitor_$port"
            
            # Get new output from job
            $output = Receive-Job -Name $jobName -ErrorAction SilentlyContinue
            
            if ($output) {
                foreach ($line in $output) {
                    if ($line) {
                        Write-Host "[$port] " -NoNewline -ForegroundColor Cyan
                        Write-Host $line
                    }
                }
            }
        }
    }
} finally {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host "Monitor jobs still running in background" -ForegroundColor Yellow
    Write-Host "Full logs saved to $LogDir" -ForegroundColor Yellow
    Write-Host "Use 'Stop-Job -Name Monitor_*; Remove-Job -Name Monitor_*' to stop" -ForegroundColor Yellow
    Write-Host "========================================" -ForegroundColor Yellow
}
