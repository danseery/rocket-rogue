#Requires -Version 5.1
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$VenvActivate = Join-Path $RepoRoot ".venv\Scripts\Activate.ps1"
$EmsdkDir = Join-Path $RepoRoot ".deps\emsdk"

function Update-ProcessPath {
    $pathValues = @(
        [Environment]::GetEnvironmentVariable("Path", "Machine"),
        [Environment]::GetEnvironmentVariable("Path", "User"),
        $env:Path
    )

    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $entries = New-Object 'System.Collections.Generic.List[string]'

    foreach ($pathValue in $pathValues) {
        if ([string]::IsNullOrWhiteSpace($pathValue)) {
            continue
        }

        foreach ($entry in ($pathValue -split ';')) {
            $expanded = [Environment]::ExpandEnvironmentVariables($entry.Trim())
            if ([string]::IsNullOrWhiteSpace($expanded)) {
                continue
            }
            if ($seen.Add($expanded)) {
                $entries.Add($expanded)
            }
        }
    }

    $knownToolPaths = @(
        "C:\Program Files\CMake\bin",
        "C:\Program Files\Git\cmd",
        "C:\Program Files\nodejs",
        "$env:LOCALAPPDATA\Programs\Python\Python312",
        "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts"
    )

    foreach ($knownPath in $knownToolPaths) {
        if ((Test-Path $knownPath) -and $seen.Add($knownPath)) {
            $entries.Add($knownPath)
        }
    }

    $env:Path = $entries -join ';'
}

function Import-EmsdkEnvironment {
    param([Parameter(Mandatory = $true)][string]$SdkDir)

    $EmsdkEnvPs1 = Join-Path $SdkDir "emsdk_env.ps1"
    if (Test-Path $EmsdkEnvPs1) {
        . $EmsdkEnvPs1
        Write-Host "Activated Emscripten SDK environment: $EmsdkEnvPs1"
        return
    }

    $EmsdkEnvBat = Join-Path $SdkDir "emsdk_env.bat"
    if (-not (Test-Path $EmsdkEnvBat)) {
        Write-Warning "Emscripten SDK environment not found. Run scripts\install-windows.ps1 first."
        return
    }

    $envDump = & cmd.exe /d /s /c "`"$EmsdkEnvBat`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to import Emscripten environment from $EmsdkEnvBat"
    }

    foreach ($line in $envDump) {
        $equals = $line.IndexOf("=")
        if ($equals -le 0) {
            continue
        }
        $name = $line.Substring(0, $equals)
        $value = $line.Substring($equals + 1)
        Set-Item -Path "Env:$name" -Value $value
    }
    Write-Host "Activated Emscripten SDK environment: $EmsdkEnvBat"
}

if (Test-Path $VenvActivate) {
    . $VenvActivate
    Write-Host "Activated Python virtual environment: $VenvActivate"
} else {
    Write-Warning "Python virtual environment not found. Run scripts\install-windows.ps1 first."
}

Update-ProcessPath
Import-EmsdkEnvironment -SdkDir $EmsdkDir
Update-ProcessPath

Write-Host ""
Write-Host "Rocket Rogue dev shell is ready for this PowerShell session."
Write-Host "Try: cmake --preset web-release; cmake --build --preset web-release; npm run serve:web"
