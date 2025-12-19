# Continuous Serial Monitor with Auto-Logging and Rotation
# Captures COM13 (Rex) and COM14 (Archer) to separate log files
# Run with: .\monitor_log.ps1

# Clean up any old jobs first
Write-Host "Cleaning up old jobs..."
Get-Job | Stop-Job -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
Get-Job | Remove-Job -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$COM13_PORT = "COM13"
$COM14_PORT = "COM14"
$BAUD_RATE = 115200
$MAX_LINES = 5000

$LOG_DIR = "$PSScriptRoot\logs"
$COM13_LOG = "$LOG_DIR\com13.log"
$COM14_LOG = "$LOG_DIR\com14.log"

if (-not (Test-Path $LOG_DIR)) {
    New-Item -ItemType Directory -Path $LOG_DIR | Out-Null
}

Write-Host "==================================================="
Write-Host "Dual Serial Monitor with Auto-Logging"
Write-Host "==================================================="
Write-Host "COM13 (Rex) -> $COM13_LOG"
Write-Host "COM14 (Archer) -> $COM14_LOG"
Write-Host "Press Ctrl+C to stop"
Write-Host "==================================================="

# Trim function - only called from main script to avoid file lock conflicts
function Trim-LogFile {
    param([string]$FilePath)
    if ((Test-Path $FilePath)) {
        try {
            $lines = Get-Content $FilePath -ErrorAction SilentlyContinue
            if ($lines.Count -gt $MAX_LINES) {
                ($lines | Select-Object -Last $MAX_LINES) | Set-Content $FilePath -ErrorAction SilentlyContinue
            }
        }
        catch { }
    }
}

# Script block for monitoring a port
$monitorScript = {
    param($Port, $Log, $Baud, $Max)
    while ($true) {
        try {
            $p = New-Object System.IO.Ports.SerialPort $Port, $Baud
            $p.Open()
            $cnt = 0
            while ($p.IsOpen) {
                try {
                    $line = $p.ReadLine()
                    Add-Content -Path $Log -Value $line -ErrorAction SilentlyContinue
                    # Skip rotation in the job - let main script handle it to avoid file locks
                    $cnt++
                }
                catch [System.TimeoutException] { }
                catch { break }
            }
            $p.Close()
            $p.Dispose()
        }
        catch { }
        Start-Sleep -Seconds 2
    }
}

# Start both monitors
$job13 = Start-Job -ScriptBlock $monitorScript -ArgumentList $COM13_PORT, $COM13_LOG, $BAUD_RATE, $MAX_LINES
$job14 = Start-Job -ScriptBlock $monitorScript -ArgumentList $COM14_PORT, $COM14_LOG, $BAUD_RATE, $MAX_LINES

Write-Host "Monitoring started. Press Ctrl+C to stop.`n"

try {
    $rotationCounter = 0
    while ($true) {
        $out13 = Receive-Job -Job $job13
        $out14 = Receive-Job -Job $job14
        
        if ($out13) { $out13 | ForEach-Object { Write-Host "[COM13] $_" -ForegroundColor Cyan } }
        if ($out14) { $out14 | ForEach-Object { Write-Host "[COM14] $_" -ForegroundColor Green } }
        
        # Rotate logs every 50 iterations (~5 seconds) to avoid file lock conflicts
        if (++$rotationCounter -ge 50) {
            Trim-LogFile $COM13_LOG
            Trim-LogFile $COM14_LOG
            $rotationCounter = 0
        }
        
        Start-Sleep -Milliseconds 100
    }
}
finally {
    Stop-Job -Job $job13, $job14
    Remove-Job -Job $job13, $job14
    Write-Host "`nLogs: $COM13_LOG, $COM14_LOG"
}
