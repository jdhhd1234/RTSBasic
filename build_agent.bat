@echo off
rem Per-agent build dir (parallel agents must not share build\). Usage: build_agent.bat NAME
rem Reuses already downloaded dependency sources from build\_deps (no network). ASCII only.
if "%~1"=="" (
    echo usage: build_agent.bat NAME
    exit /b 2
)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo Visual Studio not found
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "DEPS=%~dp0build\_deps"
cmake -S "%~dp0." -B "%~dp0build_%~1" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DFETCHCONTENT_SOURCE_DIR_RAYLIB="%DEPS%\raylib-src" ^
    -DFETCHCONTENT_SOURCE_DIR_ENTT="%DEPS%\entt-src" ^
    -DFETCHCONTENT_SOURCE_DIR_LUA="%DEPS%\lua-src" ^
    -DFETCHCONTENT_SOURCE_DIR_JSON="%DEPS%\json-src" || exit /b 1
cmake --build "%~dp0build_%~1" %2 %3 %4 %5 || exit /b 1
