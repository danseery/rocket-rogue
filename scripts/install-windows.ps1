#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$EmsdkVersion = $(if ($env:EMSDK_VERSION) { $env:EMSDK_VERSION } else { "6.0.0" }),
    [switch]$SkipSystemPackages,
    [switch]$SkipNativeCompiler,
    [switch]$SkipPythonVenv,
    [switch]$SkipNpmInstall,
    [switch]$VerifyBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DepsDir = Join-Path $RepoRoot ".deps"
$EmsdkDir = Join-Path $DepsDir "emsdk"
$VenvDir = Join-Path $RepoRoot ".venv"

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )

    Write-Host ("+ {0} {1}" -f $FilePath, ($Arguments -join " "))
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Test-Command {
    param([Parameter(Mandatory = $true)][string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Require-Command {
    param([Parameter(Mandatory = $true)][string]$Name)
    if (-not (Test-Command $Name)) {
        throw "Missing required command after installation: $Name"
    }
}

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

function Install-WingetPackage {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string]$Name = $Id,
        [string[]]$OverrideArgs = @()
    )

    if (-not (Test-Command winget)) {
        throw "winget is required for automatic Windows dependency installation. Install App Installer from Microsoft Store, or rerun with -SkipSystemPackages and install dependencies manually."
    }

    $installed = winget list --id $Id --exact --accept-source-agreements 2>$null
    if ($LASTEXITCODE -eq 0 -and ($installed -match [regex]::Escape($Id))) {
        Write-Host "$Name is already installed."
        return
    }

    $args = @(
        "install",
        "--id", $Id,
        "--exact",
        "--accept-package-agreements",
        "--accept-source-agreements",
        "--source", "winget"
    )

    if ($OverrideArgs.Count -gt 0) {
        $args += $OverrideArgs
    }

    Invoke-External "winget" $args
}

function Get-PythonLauncher {
    if (Test-Command py) {
        return @{ File = "py"; Args = @("-3") }
    }
    if (Test-Command python) {
        return @{ File = "python"; Args = @() }
    }
    throw "Python 3 was not found on PATH."
}

function Import-EmsdkEnvironment {
    param([Parameter(Mandatory = $true)][string]$SdkDir)

    $EmsdkEnvPs1 = Join-Path $SdkDir "emsdk_env.ps1"
    if (Test-Path $EmsdkEnvPs1) {
        . $EmsdkEnvPs1
        return
    }

    $EmsdkEnvBat = Join-Path $SdkDir "emsdk_env.bat"
    if (-not (Test-Path $EmsdkEnvBat)) {
        throw "Could not find emsdk_env.ps1 or emsdk_env.bat under $SdkDir"
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
}

Set-Location $RepoRoot
New-Item -ItemType Directory -Force -Path $DepsDir | Out-Null

if (-not $SkipSystemPackages) {
    Install-WingetPackage -Id "Git.Git" -Name "Git"
    Install-WingetPackage -Id "Kitware.CMake" -Name "CMake"
    Install-WingetPackage -Id "Ninja-build.Ninja" -Name "Ninja"
    Install-WingetPackage -Id "Python.Python.3.12" -Name "Python 3"
    Install-WingetPackage -Id "OpenJS.NodeJS.LTS" -Name "Node.js LTS"

    if (-not $SkipNativeCompiler) {
        Install-WingetPackage `
            -Id "Microsoft.VisualStudio.2022.BuildTools" `
            -Name "Visual Studio 2022 Build Tools" `
            -OverrideArgs @("--override", "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended")
    }

    Write-Host ""
    Write-Host "Refreshing this PowerShell session PATH after package installation."
    Update-ProcessPath
}

Update-ProcessPath

Require-Command git
Require-Command cmake
Require-Command ninja
Require-Command node
Require-Command npm.cmd

if (-not $SkipPythonVenv) {
    $python = Get-PythonLauncher
    if (-not (Test-Path $VenvDir)) {
        Invoke-External $python.File ($python.Args + @("-m", "venv", $VenvDir))
    }

    $VenvPython = Join-Path $VenvDir "Scripts\python.exe"
    Invoke-External $VenvPython @("-m", "pip", "install", "--upgrade", "pip")

    $Requirements = Join-Path $RepoRoot "requirements-dev.txt"
    $requirementsContent = Get-Content $Requirements -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch '^\s*(#|$)' }
    if ($requirementsContent) {
        Invoke-External $VenvPython @("-m", "pip", "install", "-r", $Requirements)
    }
}

if (-not (Test-Path (Join-Path $EmsdkDir ".git"))) {
    Invoke-External "git" @("clone", "https://github.com/emscripten-core/emsdk.git", $EmsdkDir)
} else {
    Invoke-External "git" @("-C", $EmsdkDir, "fetch", "--tags", "--prune")
    Invoke-External "git" @("-C", $EmsdkDir, "pull", "--ff-only")
}

$EmsdkBat = Join-Path $EmsdkDir "emsdk.bat"
Invoke-External $EmsdkBat @("install", $EmsdkVersion)
Invoke-External $EmsdkBat @("activate", $EmsdkVersion)

Import-EmsdkEnvironment -SdkDir $EmsdkDir
Update-ProcessPath

if (-not $SkipNpmInstall) {
    if (Test-Path (Join-Path $RepoRoot "package-lock.json")) {
        Invoke-External "npm.cmd" @("ci")
    } else {
        Invoke-External "npm.cmd" @("install")
    }
}

Require-Command emcc
Invoke-External "node" @("tools/sanity-check.mjs")

if ($VerifyBuild) {
    Invoke-External "cmake" @("--preset", "native-debug")
    Invoke-External "cmake" @("--build", "--preset", "native-debug")
    Invoke-External "ctest" @("--preset", "native-debug")
    Invoke-External "cmake" @("--preset", "native-release")
    Invoke-External "cmake" @("--build", "--preset", "native-release")
    Invoke-External "ctest" @("--preset", "native-release")
    Invoke-External "cmake" @("--build", "--preset", "package-native")
    Invoke-External "cmake" @("--preset", "web-release")
    Invoke-External "cmake" @("--build", "--preset", "web-release")
}

Write-Host ""
Write-Host "Rocket Rogue dependencies are installed."
Write-Host ""
Write-Host "For each new PowerShell session:"
Write-Host "  . .\scripts\env-windows.ps1"
Write-Host ""
Write-Host "Build and run the native game:"
Write-Host "  cmake --preset native-debug"
Write-Host "  cmake --build --preset native-debug"
Write-Host "  .\build\native-debug\bin\RocketRogue.exe"
Write-Host ""
Write-Host "Build and run the browser version:"
Write-Host "  cmake --preset web-release"
Write-Host "  cmake --build --preset web-release"
Write-Host "  npm.cmd run serve:web"
Write-Host ""
Write-Host "Installed Emscripten SDK:"
Write-Host "  $EmsdkDir"
