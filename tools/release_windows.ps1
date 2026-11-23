<#
.SYNOPSIS
    Builds the Windows release of Chess and, if asked, installs it.

.DESCRIPTION
    Runs tools/package_release.py, which builds the release version and puts the program, its
    data, the Visual C++ runtime, the Stockfish engine and all licenses into

        release\Chess-<version>-windows-x86_64\        (the folder to copy anywhere)
        release\Chess-<version>-windows-x86_64.zip     (the same, zipped)

    The program keeps settings and games in %APPDATA%\ChessDesktop and writes nothing next to
    itself, so the folder works from a read-only place such as Program Files.

    With -Install the folder is copied to -InstallDir (default: "C:\Program Files\Chess") and a
    Start Menu shortcut is created. Copying into Program Files asks for administrator rights.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\release_windows.ps1
    Builds and packages only.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\release_windows.ps1 -Install
    Builds, packages and installs to C:\Program Files\Chess.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\release_windows.ps1 -Install -InstallDir "$env:LOCALAPPDATA\Programs\Chess"
    Installs for the current user only; no administrator rights needed.
#>
param(
    [switch]$Install,                                  # copy the release to -InstallDir
    [string]$InstallDir = "$env:ProgramFiles\Chess",   # where -Install puts it
    [switch]$NoBuild,                                  # package the existing release build
    [switch]$NoShortcut,                               # with -Install: no Start Menu shortcut
    [string]$Engine = ""                               # Stockfish executable (default: first one in engines\)
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# --- Build and package ---------------------------------------------------------------
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
if (-not $python) { throw "Python was not found. Install Python 3 or run tools\package_release.py another way." }

$packArgs = @((Join-Path $root "tools\package_release.py"))
if ($NoBuild) { $packArgs += "--no-build" }
if ($Engine -ne "") { $packArgs += @("--engine", $Engine) }
& $python.Source @packArgs
if ($LASTEXITCODE -ne 0) { throw "Packaging failed (exit code $LASTEXITCODE)." }

# The newest staged folder is the one just made.
$stage = Get-ChildItem (Join-Path $root "release") -Directory -Filter "Chess-*-windows-*" |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $stage) { throw "No release folder found under release\." }
if (-not (Test-Path (Join-Path $stage.FullName "chess.exe"))) { throw "$($stage.FullName) has no chess.exe." }
Write-Host ""
Write-Host "Release folder: $($stage.FullName)"

if (-not $Install) {
    Write-Host "Copy that folder wherever you like, or run this script again with -Install."
    exit 0
}

# --- Install -------------------------------------------------------------------------
$target = [System.IO.Path]::GetFullPath($InstallDir)

# The copy mirrors the release, deleting anything else in the target. Only do that to an
# empty folder or to an earlier installation of this program.
if (Test-Path $target) {
    $existing = Get-ChildItem $target -Force
    if ($existing -and -not (Test-Path (Join-Path $target "chess.exe"))) {
        throw "$target exists and is not a Chess installation. Choose another -InstallDir."
    }
}
if (Get-Process -Name chess -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$target\*" }) {
    throw "Chess is running from $target. Close it and run the script again."
}

function Test-CanWrite([string]$folder) {
    # Walk up to the nearest existing folder and try to create a file there.
    $probeDir = $folder
    while (-not (Test-Path $probeDir)) { $probeDir = Split-Path -Parent $probeDir }
    $probe = Join-Path $probeDir ("chess-write-test-" + [guid]::NewGuid().ToString("N"))
    try {
        [System.IO.File]::WriteAllText($probe, "")
        Remove-Item $probe -Force
        return $true
    } catch {
        return $false
    }
}

# robocopy exit codes below 8 mean success.
$copyArgs = @("`"$($stage.FullName)`"", "`"$target`"", "/MIR", "/NJH", "/NJS", "/NDL", "/NFL", "/NP", "/R:2", "/W:1")
if (Test-CanWrite $target) {
    $copy = Start-Process robocopy -ArgumentList $copyArgs -Wait -PassThru -NoNewWindow
} else {
    Write-Host "Administrator rights are needed to write to $target ..."
    $copy = Start-Process robocopy -ArgumentList $copyArgs -Wait -PassThru -Verb RunAs
}
if ($copy.ExitCode -ge 8) { throw "Copying to $target failed (robocopy exit code $($copy.ExitCode))." }
if (-not (Test-Path (Join-Path $target "chess.exe"))) { throw "Copying to $target did not complete." }
Write-Host "Installed to: $target"

if (-not $NoShortcut) {
    $shortcutPath = Join-Path ([Environment]::GetFolderPath("Programs")) "Chess.lnk"
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = Join-Path $target "chess.exe"
    $shortcut.WorkingDirectory = $target
    $shortcut.Description = "Chess"
    $shortcut.Save()
    Write-Host "Start Menu shortcut: $shortcutPath"
}
Write-Host "To uninstall, delete $target and the shortcut. Settings and games are in $env:APPDATA\ChessDesktop."
