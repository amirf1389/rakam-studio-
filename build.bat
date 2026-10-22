@echo off
rem Builds Rakam Studio on Windows. Needs: Visual Studio 2022 (Desktop development with C++), CMake, Git.
rem The first build downloads JUCE and takes 5-10 minutes. Later builds are much faster.

cd /d "%~dp0"

where cmake >nul 2>nul || (echo CMake not found. Install it from https://cmake.org/download/ and tick "Add CMake to PATH". & pause & exit /b 1)
where git   >nul 2>nul || (echo Git not found. Install it from https://git-scm.com/download/win & pause & exit /b 1)

cmake -B build -A x64 || goto :error
cmake --build build --config Release --target RakamStudio --parallel || goto :error

echo.
echo ============================================================
echo  Done!  build\RakamStudio_artefacts\Release\Rakam Studio.exe
echo ============================================================
start "" "build\RakamStudio_artefacts\Release"
pause
exit /b 0

:error
echo.
echo Build failed - scroll up to see the first error.
pause
exit /b 1
