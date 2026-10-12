param([switch]$Upload, [switch]$Tests, [switch]$Keyboard, [switch]$PrepareOnly, [string]$Port)
$ErrorActionPreference = "Stop"
if ($Tests -and $Keyboard) { throw "Choose either -Tests or -Keyboard" }
$taskWorkspace = Split-Path -Parent $PSScriptRoot
$taskVenv = Join-Path $taskWorkspace ".tools\pio-venv"
$taskPython = Join-Path $taskVenv "Scripts\python.exe"
$taskPio = Join-Path $taskVenv "Scripts\platformio.exe"
if (-not (Test-Path -LiteralPath $taskPio)) {
    python -m venv $taskVenv
    if ($LASTEXITCODE -ne 0) { throw "Could not create Python environment" }
    & $taskPython -m pip install "platformio==6.2.0"
    if ($LASTEXITCODE -ne 0) { throw "Could not install PlatformIO" }
}
# ESP-IDF/PlatformIO forbids spaces in framework/build paths on Windows.
# Stage our sources in a dedicated temp directory and alias our local tool cache.
$taskStage = Join-Path ([IO.Path]::GetTempPath()) "pcb2-espidf-01a10781"
$taskProject = Join-Path $taskStage "firmware"
$taskCache = Join-Path $taskWorkspace ".tools\platformio"
$taskAlias = Join-Path $taskStage "pio"
New-Item -ItemType Directory -Force -Path $taskProject, $taskCache | Out-Null
if (-not (Test-Path -LiteralPath $taskAlias)) {
    New-Item -ItemType Junction -Path $taskAlias -Value $taskCache | Out-Null
} else {
    $taskLink = Get-Item -LiteralPath $taskAlias
    if ($taskLink.LinkType -ne "Junction" -or $taskLink.Target -notcontains $taskCache) {
        throw "Build cache alias is occupied by an unexpected directory"
    }
}
# Copy only maintained sources/configuration; preserve the staged build cache.
foreach ($taskName in @("CMakeLists.txt", "platformio.ini", "sdkconfig.defaults", "partitions.csv")) {
    Copy-Item -LiteralPath (Join-Path $taskWorkspace "firmware\$taskName") -Destination $taskProject -Force
}
foreach ($taskDirectory in @("main", "components")) {
    Copy-Item -LiteralPath (Join-Path $taskWorkspace "firmware\$taskDirectory") -Destination $taskProject -Recurse -Force
}
$taskActiveProject = $taskProject
if ($Tests) {
    Copy-Item -LiteralPath (Join-Path $taskWorkspace "firmware\tests") -Destination $taskProject -Recurse -Force
    $taskActiveProject = Join-Path $taskProject "tests"
}
if ($Keyboard) {
    Copy-Item -LiteralPath (Join-Path $taskWorkspace "firmware\keyboard") -Destination $taskProject -Recurse -Force
    $taskActiveProject = Join-Path $taskProject "keyboard"
}
if ($PrepareOnly) {
    Write-Output "Prepared ESP-IDF project: $taskActiveProject"
    Write-Output "PlatformIO cache alias: $taskAlias"
    return
}
$taskOldCore = $env:PLATFORMIO_CORE_DIR
try {
    $env:PLATFORMIO_CORE_DIR = $taskAlias
    $taskArguments = @("run", "--project-dir", $taskActiveProject)
    if ($Upload) {
        $taskArguments += @("--target", "upload")
        if ($Port) { $taskArguments += @("--upload-port", $Port) }
    }
    & $taskPio @taskArguments
    if ($LASTEXITCODE -ne 0) { throw "ESP-IDF build failed with exit code $LASTEXITCODE" }
    $taskArtifacts = Join-Path $taskWorkspace "firmware\artifacts"
    if ($Tests) { $taskArtifacts = Join-Path $taskArtifacts "tests" }
    if ($Keyboard) { $taskArtifacts = Join-Path $taskArtifacts "keyboard" }
    New-Item -ItemType Directory -Force -Path $taskArtifacts | Out-Null
    foreach ($taskArtifact in @("firmware.bin", "firmware.elf", "bootloader.bin", "partitions.bin")) {
        Copy-Item -LiteralPath (Join-Path $taskActiveProject ".pio\build\pcb2_s3\$taskArtifact") -Destination $taskArtifacts -Force
    }
    if (Test-Path -LiteralPath (Join-Path $taskProject "dependencies.lock")) {
        Copy-Item -LiteralPath (Join-Path $taskProject "dependencies.lock") -Destination (Join-Path $taskWorkspace "firmware") -Force
    }
    if ($Keyboard -and (Test-Path -LiteralPath (Join-Path $taskActiveProject "dependencies.lock"))) {
        Copy-Item -LiteralPath (Join-Path $taskActiveProject "dependencies.lock") -Destination (Join-Path $taskWorkspace "firmware\keyboard") -Force
    }
    Write-Output "Artifacts: $taskArtifacts"
    Write-Output "Staged build: $taskActiveProject"
} finally {
    $env:PLATFORMIO_CORE_DIR = $taskOldCore
}
