param(
    [string]$SdkRoot = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$NdkRoot = $env:ANDROID_NDK_HOME
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$Abi = "arm64-v8a"
if (-not $NdkRoot) {
    $NdkParent = Join-Path $SdkRoot "ndk"
    $NdkRoot = Get-ChildItem -LiteralPath $NdkParent -Directory |
        Sort-Object { [version](($_.Name -split '-')[0]) } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $NdkRoot) {
    throw "No side-by-side Android NDK installation was found under $SdkRoot\ndk"
}

$ToolchainBin = Join-Path $NdkRoot "toolchains/llvm/prebuilt/windows-x86_64/bin"
$Compiler = Join-Path $ToolchainBin "aarch64-linux-android26-clang++.cmd"
$Strip = Join-Path $ToolchainBin "llvm-strip.exe"
if (-not (Test-Path -LiteralPath $Compiler) -or -not (Test-Path -LiteralPath $Strip)) {
    throw "Android ARM64 compiler or llvm-strip was not found under $ToolchainBin"
}

$BuildDir = Join-Path $ProjectRoot "build/$Abi"
$StageParent = Join-Path $ProjectRoot "build/root-package-stage"
$StageDir = Join-Path $StageParent "AirXPadFix"
$OutputZip = Join-Path $ProjectRoot "build/AirXPadFix-root-$Abi.zip"
$Daemon = Join-Path $BuildDir "airxpadfixd"

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
& $Compiler `
    -std=c++17 -O2 -DNDEBUG `
    -Wall -Wextra -Wpedantic -Werror `
    -ffunction-sections -fdata-sections `
    -static-libstdc++ "-Wl,--gc-sections" `
    (Join-Path $ProjectRoot "src/main.cpp") `
    (Join-Path $ProjectRoot "src/xbox_proxy.cpp") `
    -o $Daemon
if ($LASTEXITCODE -ne 0) {
    throw "Android C++ compilation failed with exit code $LASTEXITCODE"
}
& $Strip --strip-unneeded $Daemon
if ($LASTEXITCODE -ne 0) {
    throw "llvm-strip failed with exit code $LASTEXITCODE"
}

if (Test-Path -LiteralPath $StageParent) {
    Remove-Item -LiteralPath $StageParent -Recurse -Force
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
Copy-Item -Path (Join-Path $ProjectRoot "root-package/*") -Destination $StageDir -Recurse
Copy-Item -LiteralPath (Join-Path $ProjectRoot "tools/probe.sh") -Destination (Join-Path $StageDir "probe.sh")
Copy-Item -LiteralPath $Daemon -Destination (Join-Path $StageDir "airxpadfixd")
Copy-Item -Path (Join-Path $ProjectRoot "launchers/*") -Destination $StageParent

if (Test-Path -LiteralPath $OutputZip) {
    Remove-Item -LiteralPath $OutputZip -Force
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ZipStream = [IO.File]::Open($OutputZip, [IO.FileMode]::CreateNew)
$Archive = [IO.Compression.ZipArchive]::new(
    $ZipStream,
    [IO.Compression.ZipArchiveMode]::Create,
    $false
)
try {
    Get-ChildItem -LiteralPath $StageParent -File -Recurse | ForEach-Object {
        $RelativePath = $_.FullName.Substring($StageParent.Length).TrimStart('\') -replace '\\', '/'
        $Entry = $Archive.CreateEntry($RelativePath, [IO.Compression.CompressionLevel]::Optimal)
        $InputStream = [IO.File]::OpenRead($_.FullName)
        $EntryStream = $Entry.Open()
        try {
            $InputStream.CopyTo($EntryStream)
        } finally {
            $EntryStream.Dispose()
            $InputStream.Dispose()
        }
    }
} finally {
    $Archive.Dispose()
    $ZipStream.Dispose()
}
Write-Host "Built $OutputZip"
Write-Host "NDK: $NdkRoot"
Write-Host "Extract the AirXPadFix folder into the device's Download folder."
