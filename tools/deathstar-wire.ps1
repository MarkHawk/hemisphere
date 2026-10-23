# deathstar-wire.ps1  -  push the wireframe-only Death Star settings to the dome
# through the web remote, save them, and replay the zoom.
# Run from PowerShell:   powershell -ExecutionPolicy Bypass -File deathstar-wire.ps1
param([string]$Pi = "raspberrypi.local")

$api = "http://${Pi}:5000/api"

function Post($path, $body) {
    Invoke-RestMethod -Method Post -Uri "$api/$path" -ContentType "application/json" `
        -Body ($body | ConvertTo-Json -Compress) | Out-Null
}

# 1. switch to the Death Star (scene 0 = key 1)
Post "scene" @{ scene = 0 }
Start-Sleep -Milliseconds 500

# 2. the settings (order matters little; style first so nothing flashes)
$settings = [ordered]@{
    style     = 0       # wireframe only
    lines     = 1.0     # full brightness
    dot_size  = 2.5
    dot_sp    = 0.020   # solid-looking lines
    grid_deg  = 10
    pole_dim  = 0.3
    trail     = 0       # no ghosting
    phosphor  = 0       # green
    dish_lat  = 30      # top half
    dish_dep  = 0.10
    intro     = 1
    intro_s   = 8
    start_lon = -55
}
foreach ($k in $settings.Keys) {
    Post "setting" @{ name = $k; value = [double]$settings[$k] }
    Write-Host ("{0,-10} = {1}" -f $k, $settings[$k])
    Start-Sleep -Milliseconds 150
}

# 3. save to deathstar.cfg on the Pi, then replay the zoom
Post "save" @{}
Post "key" @{ key = "z" }
Write-Host "Saved and replaying the intro."
