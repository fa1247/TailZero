<#
.SYNOPSIS
    TailZero device development helper (Windows host -> Cardputer Zero over SSH).

.DESCRIPTION
    One entry point for the edit/build/run/screenshot loop. Every build slot gets
    its own remote directory (~/tzdev/TailZero-<slot>) so several agents can build
    in parallel without fighting over a build tree. The LVGL library is compiled
    once into a shared sysroot (~/tzdev/sysroot) and keyed on lv_conf.h md5.

.EXAMPLES
    pwsh tools/dev.ps1 build
    pwsh tools/dev.ps1 build -Slot ui -BuildTarget tailzero-ui-preview
    pwsh tools/dev.ps1 shot -Slot ui -Binary tailzero-ui-preview -Demo devices -Out .recon/ui-devices.png
    pwsh tools/dev.ps1 test
    pwsh tools/dev.ps1 exec -RemoteCommand "tailscale status | head -5"
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('sync', 'lvgl', 'build', 'run', 'shot', 'test', 'install', 'logs', 'exec')]
    [string]$Action,

    [string]$Slot = 'lead',
    [string]$Demo = '-',
    [int]$Seconds = 6,
    [int]$Jobs = 2,
    [string]$Out = '',
    [string]$BuildTarget = '',
    [string]$Binary = 'tailzero',
    [string]$RemoteCommand = '',
    [string]$ExtraEnv = '',
    [switch]$Clean,
    [string]$RemoteHost = $(if ($env:TAILZERO_HOST) { $env:TAILZERO_HOST } else { '' }),
    [string]$User = $(if ($env:TAILZERO_USER) { $env:TAILZERO_USER } else { '' }),
    [string]$Password = $(if ($env:TAILZERO_PASS) { $env:TAILZERO_PASS } else { '' }),
    [string]$HostKey = $(if ($env:TAILZERO_HOSTKEY) { $env:TAILZERO_HOSTKEY } else { '' })
)

$ErrorActionPreference = 'Stop'
$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Plink = 'D:\Program\PuTTY\plink.exe'
$Pscp = 'D:\Program\PuTTY\pscp.exe'
$SshTarget = "$User@$RemoteHost"
$RemoteSlot = "tzdev/TailZero-$Slot"
$RemoteLogDir = 'tzdev/logs'

if (-not (Test-Path $Plink)) { throw "plink not found at $Plink" }

# Device access is configured through the environment only: this repository is
# public and must not contain device credentials or the lab host address.
if (-not $RemoteHost -or -not $User -or -not $Password) {
    throw "Device access is not configured: set TAILZERO_HOST, TAILZERO_USER and TAILZERO_PASS (optionally TAILZERO_HOSTKEY with the SSH host key fingerprint). See docs/DEPLOY.md."
}
$HostKeyArgs = @()
if ($HostKey) { $HostKeyArgs = @('-hostkey', $HostKey) }

function Invoke-Remote {
    param([string]$Command, [int]$TimeoutSec = 120)
    $plinkArgs = @('-ssh', '-batch', '-pw', $Password) + $HostKeyArgs + @($SshTarget, $Command)
    $tmpOut = Join-Path $env:TEMP "tz-remote-$([guid]::NewGuid().ToString('N')).txt"
    $p = Start-Process -FilePath $Plink -ArgumentList $plinkArgs -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $tmpOut -RedirectStandardError "$tmpOut.err"
    $text = ''
    if (Test-Path $tmpOut) { $text += (Get-Content $tmpOut -Raw -ErrorAction SilentlyContinue) }
    if (Test-Path "$tmpOut.err") { $text += (Get-Content "$tmpOut.err" -Raw -ErrorAction SilentlyContinue) }
    Remove-Item $tmpOut, "$tmpOut.err" -ErrorAction SilentlyContinue
    return @{ ExitCode = $p.ExitCode; Output = $text }
}

function Invoke-RemoteLong {
    <# Run a remote shell script detached, poll its log until a TZ_DONE marker appears. #>
    param([string]$ScriptBody, [string]$LogName, [int]$TimeoutSec = 1800)

    $remoteScript = "/tmp/tz-$LogName.sh"
    $logPath = "/tmp/tz-$LogName.out"
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($ScriptBody -replace "`r`n", "`n")))
    $launch = "mkdir -p ~/$RemoteLogDir; echo $b64 | base64 -d > $remoteScript; chmod +x $remoteScript; " +
              ": > $logPath; nohup bash -c 'bash $remoteScript >> $logPath 2>&1; echo TZ_DONE rc=`$? >> $logPath' > /dev/null 2>&1 & echo LAUNCHED"
    $r = Invoke-Remote -Command $launch
    if ($r.Output -notmatch 'LAUNCHED') { throw "failed to launch remote job: $($r.Output)" }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 4
        $q = Invoke-Remote -Command "tail -c 200000 $logPath 2>/dev/null; echo '<<<TZ_TAIL_DONE>>>'"
        $text = $q.Output
        $idx = $text.IndexOf('<<<TZ_TAIL_DONE>>>')
        if ($idx -ge 0) { $text = $text.Substring(0, $idx) }
        if ($text -match 'TZ_DONE rc=(\d+)') {
            Write-Host "remote job finished (rc=$($Matches[1]))"
            return $text
        }
    }
    throw "remote job timed out after $TimeoutSec s (log: $logPath)"
}

function Sync-Project {
    $tar = Join-Path $env:TEMP "tz-src-$Slot.tar.gz"
    if (Test-Path $tar) { Remove-Item $tar -Force }
    Push-Location $ProjectDir
    try {
        & tar.exe -czf $tar --exclude=./build --exclude=./.git --exclude=./.recon --exclude=./dist --exclude=./.cache -C $ProjectDir . 2>&1 | Out-Null
    } finally { Pop-Location }
    & $Pscp -batch -pw $Password @HostKeyArgs $tar "$($SshTarget):/tmp/tz-src-$Slot.tar.gz" | Out-Null
    $r = Invoke-Remote -Command "mkdir -p ~/$RemoteSlot && tar xzf /tmp/tz-src-$Slot.tar.gz -C ~/$RemoteSlot && mkdir -p ~/tzdev ~/$RemoteLogDir && cp ~/$RemoteSlot/tools/device/fb_capture.py ~/tzdev/ && chmod +x ~/$RemoteSlot/tools/device/*.sh && echo SYNCED"
    if ($r.Output -notmatch 'SYNCED') { throw "sync failed: $($r.Output)" }
    Write-Host "synced -> ~/$RemoteSlot"
}

function Ensure-Lvgl {
    $body = @"
cd ~/$RemoteSlot
bash tools/device/build-lvgl.sh ~/$RemoteSlot/src/config/lv_conf.h
echo "TZ_LVGL_RC=`$?"
"@
    $out = Invoke-RemoteLong -ScriptBody $body -LogName "lvgl-$Slot" -TimeoutSec 2400
    if ($out -notmatch 'build-lvgl: (installed|up to date)') {
        Write-Host $out
        throw 'LVGL build failed'
    }
    Write-Host (($out -split "`n" | Where-Object { $_ -match 'build-lvgl' }) -join "`n")
}

function Build-App {
    $body = @"
cd ~/$RemoteSlot
bash tools/device/build-app.sh ~/$RemoteSlot ~/tzdev/sysroot $Jobs "$BuildTarget"
echo "TZ_APP_RC=`$?"
"@
    $out = Invoke-RemoteLong -ScriptBody $body -LogName "build-$Slot" -TimeoutSec 2400
    Write-Host $out
    if ($out -notmatch 'build ok') { throw 'application build failed' }
}

function Invoke-Tests {
    $body = @"
cd ~/$RemoteSlot
bash tools/device/run-tests.sh
echo "TZ_TEST_RC=`$?"
"@
    $out = Invoke-RemoteLong -ScriptBody $body -LogName "test-$Slot" -TimeoutSec 600
    Write-Host $out
}

function Invoke-Run {
    $body = @"
cd ~/$RemoteSlot
$ExtraEnv bash tools/device/run-app.sh ~/$RemoteSlot/build/$Binary $Seconds "$Demo" /tmp/tz-shot-$Slot.png
echo "TZ_RUN_RC=`$?"
"@
    $remoteOutput = Invoke-RemoteLong -ScriptBody $body -LogName "run-$Slot" -TimeoutSec 900
    Write-Host $remoteOutput
    $local = if ($Out) { $Out } else { Join-Path $ProjectDir ".recon/shot-$Slot.png" }
    $localDir = Split-Path $local -Parent
    if ($localDir -and -not (Test-Path $localDir)) { New-Item -ItemType Directory -Force -Path $localDir | Out-Null }
    & $Pscp -batch -pw $Password @HostKeyArgs "$($SshTarget):/tmp/tz-shot-$Slot.png" $local | Out-Null
    Write-Host "screenshot -> $local"
    return $local
}

# The preview binary implies its own build target unless one was given explicitly.
if (-not $BuildTarget -and $Binary -eq 'tailzero-ui-preview') {
    $BuildTarget = 'tailzero-ui-preview'
}

switch ($Action) {
    'sync' { Sync-Project }
    'lvgl' { Sync-Project; Ensure-Lvgl }
    'build' { Sync-Project; Ensure-Lvgl; Build-App }
    'run' { Sync-Project; Build-App; Invoke-Run }
    'shot' { Sync-Project; Build-App; Invoke-Run | Out-Null }
    'test' { Sync-Project; Invoke-Tests }
    'install' {
        $body = @"
cd ~/$RemoteSlot
bash tools/device/install-deb.sh
echo "TZ_INSTALL_RC=`$?"
"@
        $out = Invoke-RemoteLong -ScriptBody $body -LogName "install-$Slot" -TimeoutSec 600
        Write-Host $out
    }
    'logs' {
        $r = Invoke-Remote -Command "tail -50 ~/$RemoteLogDir/*.log 2>/dev/null; tail -30 /tmp/tz-app.log 2>/dev/null"
        Write-Host $r.Output
    }
    'exec' {
        if (-not $RemoteCommand) { throw '-exec requires -RemoteCommand' }
        $r = Invoke-Remote -Command $RemoteCommand -TimeoutSec 600
        Write-Host $r.Output
        exit $r.ExitCode
    }
}
