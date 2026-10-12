<#
.SYNOPSIS
    Start Hemisphere, the orbit feed and the web remote on a Windows PC,
    and keep them running.

.DESCRIPTION
    The Windows counterpart of the Pi's systemd services. Run it in the
    session at the PC itself (signed in at the keyboard, or automatically at
    log-on by install-autostart.ps1), never inside Remote Desktop: a program
    started under RDP draws to the remote screen, not the PC's HDMI outputs.

    - hemisphere.exe runs with its console window hidden; its messages go
      to logs\hemisphere.log. Press ESC on the dome window to quit it for
      good; if it stops for any other reason it is started again.
    - The orbit feed and web remote run from the venv in the repo folder,
      if there is one, and are restarted if they stop.
    - Stop everything with: .\tools\windows\start-hemisphere.ps1 -Stop

.PARAMETER NoFeed
    Do not start the orbit feed.

.PARAMETER NoRemote
    Do not start the web remote.

.PARAMETER Stop
    Stop a running copy of this script and the programs it started.
#>
param(
    [switch]$NoFeed,
    [switch]$NoRemote,
    [switch]$Stop
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # repo folder
$Logs = Join-Path $Root 'logs'
$PidFile = Join-Path $Logs 'start-hemisphere.pid'
$Exe  = Join-Path $Root 'hemisphere.exe'
$Py   = Join-Path $Root 'venv\Scripts\python.exe'
New-Item -ItemType Directory -Force -Path $Logs | Out-Null

function Stop-All {
    if (Test-Path $PidFile) {
        $old = Get-Content $PidFile -ErrorAction SilentlyContinue
        if ($old -and [int]$old -ne $PID) {
            Stop-Process -Id ([int]$old) -Force -ErrorAction SilentlyContinue
        }
        Remove-Item $PidFile -ErrorAction SilentlyContinue
    }
    Get-Process hemisphere -ErrorAction SilentlyContinue | Stop-Process -Force
    # only this repo's feed and remote (a venv's python.exe is a launcher
    # that runs the real Python as a second process, so match the script)
    Get-CimInstance Win32_Process -Filter "Name = 'python.exe'" |
        Where-Object { $_.CommandLine -and $_.CommandLine -match 'orbitfeed\.py|remote\\app\.py' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

if ($Stop) {
    Stop-All
    Write-Host 'Hemisphere stopped.'
    return
}

if (-not (Test-Path $Exe)) {
    throw "hemisphere.exe not found in $Root. Build it first (see 'Windows PC' in the README)."
}

# one copy at a time: replace any earlier one
Stop-All
Set-Content -Path $PidFile -Value $PID

$env:PYTHONUNBUFFERED = '1'        # Python log lines appear straight away

function Start-Hidden([string]$file, [string[]]$arguments, [string]$name) {
    $log = Join-Path $Logs "$name.log"
    $err = Join-Path $Logs "$name.err.log"
    $p = @{
        FilePath               = $file
        WorkingDirectory       = $Root
        WindowStyle            = 'Hidden'
        RedirectStandardOutput = $log
        RedirectStandardError  = $err
        PassThru               = $true
    }
    if ($arguments) { $p.ArgumentList = $arguments }
    $proc = Start-Process @p
    "$(Get-Date -Format s)  started $name (process $($proc.Id))" |
        Add-Content (Join-Path $Logs 'start-hemisphere.log')
    return $proc
}

$jobs = [ordered]@{}
$jobs['hemisphere'] = @{ File = $Exe; Args = @(); Proc = $null; Done = $false }
if (Test-Path $Py) {
    if (-not $NoFeed) {
        $jobs['orbitfeed'] = @{ File = $Py; Args = @('tools\orbitfeed.py'); Proc = $null; Done = $false }
    }
    if (-not $NoRemote) {
        $jobs['remote'] = @{ File = $Py; Args = @('tools\remote\app.py'); Proc = $null; Done = $false }
    }
} else {
    "$(Get-Date -Format s)  no venv in $Root, so no orbit feed or web remote" |
        Add-Content (Join-Path $Logs 'start-hemisphere.log')
}

# start everything, then restart anything that stops
while ($true) {
    foreach ($name in @($jobs.Keys)) {
        $j = $jobs[$name]
        if ($j.Done) { continue }
        if ($j.Proc -and $j.Proc.HasExited) {
            if ($name -eq 'hemisphere' -and $j.Proc.ExitCode -eq 0) {
                # ESC on the dome: you meant to quit, so stop everything
                Stop-All
                return
            }
            "$(Get-Date -Format s)  $name stopped (exit code $($j.Proc.ExitCode)), restarting" |
                Add-Content (Join-Path $Logs 'start-hemisphere.log')
            $j.Proc = $null
            Start-Sleep -Seconds 5
        }
        if (-not $j.Proc) { $j.Proc = Start-Hidden $j.File $j.Args $name }
    }
    Start-Sleep -Seconds 2
}
