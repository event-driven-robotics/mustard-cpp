param(
    [string]$BuildDir = "build-win",
    [string]$Configuration = "Release",
    [string]$OutputDir = "",
    [string]$Executable = "",
    [string]$VcpkgRoot = "",
    [string]$MetavisionRoot = "",
    [string[]]$ExtraBinDirs = @()
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Resolve-FromBase {
    param(
        [string]$Path,
        [string]$BaseDir
    )
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDir $Path))
}

function Add-UniquePath {
    param(
        [System.Collections.Generic.List[string]]$List,
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }

    $full = Resolve-FullPath $Path
    if ((Test-Path -LiteralPath $full -PathType Container) -and -not $List.Contains($full)) {
        $List.Add($full)
    }
}

function Read-CMakeCacheValue {
    param(
        [string]$CachePath,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
        return ""
    }

    $line = Select-String -LiteralPath $CachePath -Pattern "^$([regex]::Escape($Name)):[^=]+=(.*)$" | Select-Object -First 1
    if ($null -eq $line) {
        return ""
    }

    return $line.Matches[0].Groups[1].Value
}

function Get-Dependents {
    param([string]$Binary)

    $output = & dumpbin /dependents $Binary 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed while scanning $Binary`n$output"
    }

    $deps = New-Object System.Collections.Generic.List[string]
    foreach ($line in $output) {
        $trimmed = $line.Trim()
        if ($trimmed -match "^[A-Za-z0-9_.+-]+\.dll$") {
            $deps.Add($trimmed)
        }
    }
    return $deps
}

function Find-Dll {
    param(
        [string]$Name,
        [System.Collections.Generic.List[string]]$SearchDirs
    )

    foreach ($dir in $SearchDirs) {
        $candidate = Join-Path $dir $Name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    return ""
}

function Copy-IfNewer {
    param(
        [string]$Source,
        [string]$Destination
    )

    $destDir = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$scriptDir = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    Split-Path -Parent $PSCommandPath
} else {
    $PSScriptRoot
}
$repoDir = Resolve-FullPath (Join-Path $scriptDir "..\..")
$buildDirFull = Resolve-FromBase $BuildDir $repoDir

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $buildDirFull "dist\mustard-windows"
}
$outputDirFull = Resolve-FromBase $OutputDir $repoDir

if ([string]::IsNullOrWhiteSpace($Executable)) {
    $ninjaExe = Join-Path $buildDirFull "mustard_app.exe"
    $vsExe = Join-Path (Join-Path $buildDirFull $Configuration) "mustard_app.exe"

    if (Test-Path -LiteralPath $ninjaExe -PathType Leaf) {
        $Executable = $ninjaExe
    } elseif (Test-Path -LiteralPath $vsExe -PathType Leaf) {
        $Executable = $vsExe
    } else {
        throw "Could not find mustard_app.exe. Pass -Executable or check -BuildDir/-Configuration."
    }
}
$exeFull = Resolve-FromBase $Executable $repoDir

if (-not (Test-Path -LiteralPath $exeFull -PathType Leaf)) {
    throw "Executable not found: $exeFull"
}

$dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
if ($null -eq $dumpbin) {
    throw "dumpbin was not found. Run this script from 'x64 Native Tools Command Prompt for VS 2022' or a Developer PowerShell."
}

$cachePath = Join-Path $buildDirFull "CMakeCache.txt"
$toolchainFile = Read-CMakeCacheValue $cachePath "CMAKE_TOOLCHAIN_FILE"
$metavisionSdkDir = Read-CMakeCacheValue $cachePath "MetavisionSDK_DIR"

if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -and -not [string]::IsNullOrWhiteSpace($toolchainFile)) {
    $toolchainFull = Resolve-FullPath $toolchainFile
    $scriptsDir = Split-Path -Parent $toolchainFull
    $VcpkgRoot = Split-Path -Parent (Split-Path -Parent $scriptsDir)
}

if ([string]::IsNullOrWhiteSpace($MetavisionRoot) -and -not [string]::IsNullOrWhiteSpace($metavisionSdkDir)) {
    $dir = Resolve-FullPath $metavisionSdkDir
    while (-not [string]::IsNullOrWhiteSpace($dir)) {
        if ((Test-Path -LiteralPath (Join-Path $dir "bin") -PathType Container) -or
            (Test-Path -LiteralPath (Join-Path $dir "lib") -PathType Container)) {
            $MetavisionRoot = $dir
            break
        }
        $parent = Split-Path -Parent $dir
        if ($parent -eq $dir) {
            break
        }
        $dir = $parent
    }
}

$searchDirs = New-Object System.Collections.Generic.List[string]
Add-UniquePath $searchDirs (Split-Path -Parent $exeFull)

if (-not [string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $triplet = "x64-windows"
    if ($Configuration -ieq "Debug") {
        Add-UniquePath $searchDirs (Join-Path $VcpkgRoot "installed\$triplet\debug\bin")
        Add-UniquePath $searchDirs (Join-Path $VcpkgRoot "installed\$triplet\bin")
    } else {
        Add-UniquePath $searchDirs (Join-Path $VcpkgRoot "installed\$triplet\bin")
        Add-UniquePath $searchDirs (Join-Path $VcpkgRoot "installed\$triplet\debug\bin")
    }
}

if (-not [string]::IsNullOrWhiteSpace($MetavisionRoot)) {
    Add-UniquePath $searchDirs (Join-Path $MetavisionRoot "bin")
    Add-UniquePath $searchDirs (Join-Path $MetavisionRoot "bin\$Configuration")
    Add-UniquePath $searchDirs (Join-Path $MetavisionRoot "$Configuration\bin")
}

foreach ($dir in $ExtraBinDirs) {
    Add-UniquePath $searchDirs $dir
}

foreach ($dir in ($env:PATH -split [System.IO.Path]::PathSeparator)) {
    Add-UniquePath $searchDirs $dir
}

$systemDlls = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "combase.dll", "comdlg32.dll",
    "crypt32.dll", "d3d9.dll", "dwmapi.dll", "gdi32.dll", "imm32.dll",
    "kernel32.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll",
    "opengl32.dll", "powrprof.dll", "rpcrt4.dll", "secur32.dll", "setupapi.dll",
    "shell32.dll", "shlwapi.dll", "ucrtbase.dll", "user32.dll", "uxtheme.dll", "version.dll",
    "winmm.dll", "winspool.drv", "ws2_32.dll"
) | ForEach-Object { [void]$systemDlls.Add($_) }

Remove-Item -LiteralPath $outputDirFull -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $outputDirFull | Out-Null

Copy-IfNewer $exeFull (Join-Path $outputDirFull "mustard_app.exe")
$bundledExe = Join-Path $outputDirFull "mustard_app.exe"

if (-not (Test-Path -LiteralPath $bundledExe -PathType Leaf)) {
    throw "Failed to copy mustard_app.exe to bundle output: $bundledExe"
}

$queue = [System.Collections.Generic.Queue[string]]::new()
$scanned = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$copied = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$missing = [System.Collections.Generic.SortedSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

$queue.Enqueue((Join-Path $outputDirFull "mustard_app.exe"))

while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if (-not $scanned.Add($binary)) {
        continue
    }

    foreach ($depName in (Get-Dependents $binary)) {
        if ($systemDlls.Contains($depName) -or $depName -like "api-ms-win-*.dll" -or $depName -like "ext-ms-*.dll") {
            continue
        }

        $depPath = Find-Dll $depName $searchDirs
        if ([string]::IsNullOrWhiteSpace($depPath)) {
            [void]$missing.Add($depName)
            continue
        }

        $dest = Join-Path $outputDirFull $depName
        if ($copied.Add($depName)) {
            Copy-IfNewer $depPath $dest
            $queue.Enqueue($dest)
        }
    }
}

$pluginOutDir = Join-Path $outputDirFull "metavision\hal\plugins"
$pluginSearchRoots = New-Object System.Collections.Generic.List[string]
if (-not [string]::IsNullOrWhiteSpace($MetavisionRoot)) {
    Add-UniquePath $pluginSearchRoots $MetavisionRoot
}
foreach ($dir in $searchDirs) {
    if ($dir -match "(?i)(metavision|prophesee|openeb)") {
        Add-UniquePath $pluginSearchRoots $dir
    }
}

$pluginFiles = New-Object System.Collections.Generic.List[string]
foreach ($root in $pluginSearchRoots) {
    Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -like "hal*.dll" -or
            $_.Name -like "*plugin*.dll" -or
            $_.FullName -match "\\metavision\\hal\\plugins\\"
        } |
        ForEach-Object {
            if (-not $pluginFiles.Contains($_.FullName)) {
                $pluginFiles.Add($_.FullName)
            }
        }
}

foreach ($plugin in $pluginFiles) {
    $dest = Join-Path $pluginOutDir (Split-Path -Leaf $plugin)
    Copy-IfNewer $plugin $dest
    $queue.Enqueue($dest)
}

while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if (-not $scanned.Add($binary)) {
        continue
    }

    foreach ($depName in (Get-Dependents $binary)) {
        if ($systemDlls.Contains($depName) -or $depName -like "api-ms-win-*.dll" -or $depName -like "ext-ms-*.dll") {
            continue
        }

        $depPath = Find-Dll $depName $searchDirs
        if ([string]::IsNullOrWhiteSpace($depPath)) {
            [void]$missing.Add($depName)
            continue
        }

        $dest = Join-Path $outputDirFull $depName
        if ($copied.Add($depName)) {
            Copy-IfNewer $depPath $dest
            $queue.Enqueue($dest)
        }
    }
}

@"
@echo off
setlocal
set APP_DIR=%~dp0
set PATH=%APP_DIR%;%APP_DIR%metavision\hal\plugins;%PATH%
set MV_HAL_PLUGIN_PATH=%APP_DIR%metavision\hal\plugins
set MV_HAL_PLUGIN_SEARCH_MODE=PLUGIN_PATH_ONLY
start "" /D "%APP_DIR%" "%APP_DIR%mustard_app.exe" %*
"@ | Set-Content -LiteralPath (Join-Path $outputDirFull "mustard_app.bat") -Encoding ASCII

@"
Mustard Windows Bundle

Run mustard_app.exe directly, or run mustard_app.bat to force Metavision HAL
plugin lookup to use the bundled plugin directory.

This folder was created by packaging\scripts\bundle_windows.ps1.
"@ | Set-Content -LiteralPath (Join-Path $outputDirFull "README.txt") -Encoding ASCII

$dllCount = (Get-ChildItem -LiteralPath $outputDirFull -Filter "*.dll" -File -ErrorAction SilentlyContinue).Count
$pluginCount = 0
if (Test-Path -LiteralPath $pluginOutDir -PathType Container) {
    $pluginCount = (Get-ChildItem -LiteralPath $pluginOutDir -Filter "*.dll" -File -ErrorAction SilentlyContinue).Count
}

Write-Host "Bundled Mustard app:"
Write-Host "  $outputDirFull"
Write-Host "  executable: $bundledExe"
Write-Host "  DLLs: $dllCount"
Write-Host "  Metavision plugins: $pluginCount"

if ($missing.Count -gt 0) {
    Write-Warning "Some DLLs could not be resolved. They may be system DLLs or may need another -ExtraBinDirs entry:"
    foreach ($name in $missing) {
        Write-Warning "  $name"
    }
}
