@echo off
setlocal
if not defined LIBTORCH_DIR set "LIBTORCH_DIR=C:\Users\kym10\Desktop\libtorch"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo Visual Studio not found
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
set "DEPS=%~dp0build\_deps"
cmake -S "%~dp0." -B "%~dp0build_rl" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    "-DMINRTS_TORCH=ON" "-DCMAKE_PREFIX_PATH=%LIBTORCH_DIR%" ^
    "-DFETCHCONTENT_SOURCE_DIR_RAYLIB=%DEPS%\raylib-src" ^
    "-DFETCHCONTENT_SOURCE_DIR_ENTT=%DEPS%\entt-src" ^
    "-DFETCHCONTENT_SOURCE_DIR_LUA=%DEPS%\lua-src" ^
    "-DFETCHCONTENT_SOURCE_DIR_JSON=%DEPS%\json-src" || exit /b 1
cmake --build "%~dp0build_rl" %*
exit /b %errorlevel%
