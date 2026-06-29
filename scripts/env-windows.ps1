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

function Import-VisualStudioEnvironment {
    if (Get-Command cl -ErrorAction SilentlyContinue) {
        Write-Host "Visual Studio C++ toolchain already active."
        return
    }

    $vsWhereCandidates = @()
    if (${env:ProgramFiles(x86)}) {
        $vsWhereCandidates += (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe")
    }
    $vsWhereCandidates += "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

    $installPath = $null
    foreach ($vsWhere in $vsWhereCandidates) {
        if (-not (Test-Path $vsWhere)) {
            continue
        }

        $candidate = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($candidate)) {
            $installPath = $candidate.Trim()
            break
        }
    }

    if ([string]::IsNullOrWhiteSpace($installPath)) {
        $fallbackRoots = @(
            "C:\Program Files\Microsoft Visual Studio\2022\Community",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
            "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
        )
        foreach ($root in $fallbackRoots) {
            if (Test-Path (Join-Path $root "Common7\Tools\VsDevCmd.bat")) {
                $installPath = $root
                break
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($installPath)) {
        Write-Warning "Visual Studio C++ toolchain was not found. Native CMake presets need Visual Studio Build Tools with the C++ workload."
        return
    }

    $vsDevCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        Write-Warning "Visual Studio developer environment script not found: $vsDevCmd"
        return
    }

    $envDump = & cmd.exe /d /s /c "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to import Visual Studio environment from $vsDevCmd"
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
    Write-Host "Activated Visual Studio C++ toolchain: $vsDevCmd"
}

function Test-EmscriptenToolchain {
    param([Parameter(Mandatory = $true)][string]$SdkDir)

    $ToolchainFile = Join-Path $SdkDir "upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake"
    if (-not (Test-Path $ToolchainFile)) {
        Write-Warning "Emscripten CMake toolchain is missing. Run scripts\install-windows.ps1 to complete emsdk install/activate."
        return
    }

    if (-not (Get-Command emcc -ErrorAction SilentlyContinue)) {
        Write-Warning "emcc is not available after activating emsdk. If .deps\emsdk exists, rerun scripts\install-windows.ps1 so emsdk install/activate completes."
    }
}

if (Test-Path $VenvActivate) {
    . $VenvActivate
    Write-Host "Activated Python virtual environment: $VenvActivate"
} else {
    Write-Warning "Python virtual environment not found. Run scripts\install-windows.ps1 first."
}

Update-ProcessPath
Import-VisualStudioEnvironment
Update-ProcessPath
Import-EmsdkEnvironment -SdkDir $EmsdkDir
Update-ProcessPath
Test-EmscriptenToolchain -SdkDir $EmsdkDir

Write-Host ""
Write-Host "Rocket Rogue dev shell is ready for this PowerShell session."
Write-Host "Try: node tools\verify-toolchain.mjs; cmake --preset web-release; cmake --build --preset web-release; npm.cmd run serve:web"
