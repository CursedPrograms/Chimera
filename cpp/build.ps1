# Builds LayerTack (Release) into build\Release, and dist\LayerTack-win64.zip.
# Needs Visual Studio 2022 (C++), CMake. SDL2's development package is fetched into external\ once.
# Native tools are checked by their exit codes: CMake prints harmless warnings on stderr, which "Stop" would treat as
# failures in Windows PowerShell 5.1.
Set-Location $PSScriptRoot
function Run { & $args[0] $args[1..($args.Count - 1)]; if ($LASTEXITCODE) { Write-Host "Build failed, see above."; exit 1 } }
if (-not (Test-Path "external\SDL2-2.30.9\cmake")) {
    Write-Host "Getting SDL2's development package, once..."
    New-Item -ItemType Directory -Force external | Out-Null
    Invoke-WebRequest https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip -OutFile external\SDL2-devel.zip
    Expand-Archive external\SDL2-devel.zip -DestinationPath external -Force
    Remove-Item external\SDL2-devel.zip
}
if (-not (Test-Path "external\onnxruntime\onnxruntime.dll")) {
    Write-Host "Getting ONNX Runtime (Select Subject), once..."
    New-Item -ItemType Directory -Force external\onnxruntime | Out-Null
    Invoke-WebRequest https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip -OutFile external\ort.zip
    Expand-Archive external\ort.zip -DestinationPath external -Force
    Copy-Item external\onnxruntime-win-x64-1.20.1\lib\onnxruntime.dll external\onnxruntime\
    Remove-Item -Recurse -Force external\onnxruntime-win-x64-1.20.1, external\ort.zip
}
Run cmake -B build -G "Visual Studio 17 2022" -A x64
Run cmake --build build --config Release
Remove-Item -Recurse -Force dist\LayerTack -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force dist\LayerTack | Out-Null
Copy-Item -Recurse build\Release\* dist\LayerTack\
Remove-Item dist\LayerTack\*.lib -ErrorAction SilentlyContinue
Copy-Item ..\README.md, ..\LICENSE, ..\NOTICE dist\LayerTack\
Remove-Item dist\LayerTack-win64.zip -ErrorAction SilentlyContinue
# Windows' own tar makes a zip with forward slashes, which every unzip tool reads
Run "$env:SystemRoot\System32\tar.exe" -a -c -f dist\LayerTack-win64.zip -C dist\LayerTack *
Write-Host "Built: build\Release\LayerTack.exe and dist\LayerTack-win64.zip"
