@echo off
setlocal
cd /d "%~dp0"
rem Builds LayerTack (Release) into build\Release, and dist\LayerTack-win64.zip.
rem Needs Visual Studio 2022 (C++), CMake. SDL2's development package is fetched into external\ once.
if not exist "external\SDL2-2.30.9\cmake" (
    echo Getting SDL2's development package, once...
    if not exist external mkdir external
    curl -sL -o external\SDL2-devel.zip https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip || goto :error
    tar -xf external\SDL2-devel.zip -C external || goto :error
    del external\SDL2-devel.zip
)
if not exist "external\onnxruntime\onnxruntime.dll" (
    echo Getting ONNX Runtime ^(Select Subject^), once...
    if not exist external\onnxruntime mkdir external\onnxruntime
    curl -sL -o external\ort.zip https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip || goto :error
    tar -xf external\ort.zip -C external || goto :error
    copy /y external\onnxruntime-win-x64-1.20.1\lib\onnxruntime.dll external\onnxruntime\ >nul
    rmdir /s /q external\onnxruntime-win-x64-1.20.1
    del external\ort.zip
)
cmake -B build -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build build --config Release || goto :error
if exist dist\LayerTack rmdir /s /q dist\LayerTack
mkdir dist\LayerTack
xcopy /e /i /q /y build\Release\* dist\LayerTack\ >nul
del /q dist\LayerTack\*.lib 2>nul
copy /y ..\README.md dist\LayerTack\ >nul
copy /y ..\LICENSE dist\LayerTack\ >nul
copy /y ..\NOTICE dist\LayerTack\ >nul
if exist dist\LayerTack-win64.zip del dist\LayerTack-win64.zip
rem Windows' own tar makes a zip with forward slashes, which every unzip tool reads
"%SystemRoot%\System32\tar.exe" -a -c -f dist\LayerTack-win64.zip -C dist\LayerTack * || goto :error
echo Built: build\Release\LayerTack.exe and dist\LayerTack-win64.zip
exit /b 0
:error
echo Build failed, see above.
pause
exit /b 1
