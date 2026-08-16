<#
.SYNOPSIS
Registers the InEquator ASCOM tracker driver for development.

Run from an elevated PowerShell after building the driver:
    .\tools\register-ascom.ps1 -Configuration Release
#>
param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root "ascom\InEquatorDriver\bin\$Configuration\ASCOM.InEquator.Tracker.dll"

if (-not (Test-Path $dll)) {
    throw "Driver DLL not found: $dll  Build it first with MSBuild."
}

$regasm = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\RegAsm.exe"
Write-Host "Registering $dll ..."
& $regasm /codebase $dll
if ($LASTEXITCODE -ne 0) { throw "RegAsm failed with exit code $LASTEXITCODE" }

# ASCOM Profile defaults (user scope, mirrors SetupDialog defaults).
$profile = "HKCU:\Software\ASCOM\Profile\ASCOM.InEquator.Tracker"
New-Item -Path $profile -Force | Out-Null
Set-ItemProperty -Path $profile -Name "Transport"        -Value "TCP"
Set-ItemProperty -Path $profile -Name "TcpHost"          -Value "192.168.4.1"
Set-ItemProperty -Path $profile -Name "TcpPort"          -Value "4030"
Set-ItemProperty -Path $profile -Name "COM Port"         -Value "COM1"
Set-ItemProperty -Path $profile -Name "CommandTimeoutMs" -Value "3000"
Set-ItemProperty -Path $profile -Name "Trace Level"      -Value "false"

Write-Host "Registered ASCOM.InEquator.Tracker with default profile (TCP 192.168.4.1:4030)."
