<#
.SYNOPSIS
    Start Hemisphere automatically when you sign in to this PC, and let
    phones on your home network reach the web remote.

.DESCRIPTION
    Run once from an administrator PowerShell window in the repo folder:

        powershell -ExecutionPolicy Bypass -File tools\windows\install-autostart.ps1

    It adds:
    - a scheduled task, "Hemisphere", that runs start-hemisphere.ps1 when
      you sign in (in your own session, so it can use the HDMI outputs);
    - a Windows Firewall rule allowing the web remote's port on Private
      networks only (your home network, not public Wi-Fi).

    For a PC that should come up on its own after a power cut, also turn on
    automatic sign-in (see "Windows PC" in the README).

.PARAMETER Port
    The web remote's port, if you changed REMOTE_PORT. Default 5000.

.PARAMETER NoFirewall
    Leave the firewall alone (you only use the remote on the PC itself).

.PARAMETER Remove
    Take the scheduled task and the firewall rule away again.
#>
param(
    [int]$Port = 5000,
    [switch]$NoFirewall,
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$TaskName = 'Hemisphere'
$RuleName = 'Hemisphere web remote'
$Script   = Join-Path $PSScriptRoot 'start-hemisphere.ps1'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
         ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    throw 'Run this from an administrator PowerShell window (right-click, Run as administrator).'
}

if ($Remove) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Remove-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue
    Write-Host 'Removed the Hemisphere scheduled task and firewall rule.'
    return
}

# who signs in at the PC: the task runs for this user, in their session
$user = [Security.Principal.WindowsIdentity]::GetCurrent().Name

$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$Script`"" `
    -WorkingDirectory (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
$trigger.Delay = 'PT15S'                  # let the desktop and displays settle
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Force | Out-Null
Write-Host "Scheduled task '$TaskName' will start Hemisphere when $user signs in."

if (-not $NoFirewall) {
    Remove-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue
    New-NetFirewallRule -DisplayName $RuleName -Direction Inbound -Protocol TCP `
        -LocalPort $Port -Action Allow -Profile Private | Out-Null
    Write-Host "Firewall: port $Port open on Private networks for the web remote."
    $cat = (Get-NetConnectionProfile | Select-Object -First 1).NetworkCategory
    if ($cat -ne 'Private') {
        Write-Host "Note: this PC's network is set to $cat. Set it to Private in Settings," `
                   'Network and internet, so phones can reach the remote.'
    }
}

Write-Host 'Start it now without signing out with:'
Write-Host "    Start-ScheduledTask -TaskName $TaskName"
