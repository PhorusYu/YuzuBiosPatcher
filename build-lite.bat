@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

rem ==========================================================================
rem  YuzuBiosPatcher - one-click build
rem
rem  Self-contained: uses only the bundled toolchain (toolchain\mingw64) and
rem  headers (edk2-include) - nothing to install, nothing to detect.
rem
rem  Usage:  build-lite.bat          build RELEASE
rem          build-lite.bat DEBUG    build DEBUG
rem ==========================================================================

set "BUILD_TARGET=RELEASE"
if /i "%~1"=="DEBUG" set "BUILD_TARGET=DEBUG"

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "SRC_DIR=%PROJECT_DIR%\YuzuBiosPatcher"
set "MINILIB=%PROJECT_DIR%\minilib"
set "OUT_DIR=%PROJECT_DIR%\out"
set "OBJ_DIR=%OUT_DIR%\obj"
set "GCC=%PROJECT_DIR%\toolchain\mingw64\bin\gcc.exe"
set "INC_DIR=%PROJECT_DIR%\edk2-include"

if not exist "%GCC%" (
  echo [ERROR] toolchain\mingw64\bin\gcc.exe not found.
  exit /b 1
)
rem The compiler's helper tools load their runtime DLLs from the same
rem directory - put it first on PATH for the child processes.
for %%i in ("%GCC%") do set "PATH=%%~dpi;%PATH%"

echo [1/2] Compiling (%BUILD_TARGET%) ...
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"

set "OPT=-O2"
if /i "%BUILD_TARGET%"=="DEBUG" set "OPT=-O0 -g"

set "FLAGS=-std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -fshort-wchar -Wall %OPT% -DMDEPKG_NDEBUG -I"%INC_DIR%" -I"%INC_DIR%\X64" -I"%SRC_DIR%""

for %%f in ("%MINILIB%\MiniUefi.c" "%SRC_DIR%\Utility.c" "%SRC_DIR%\Opcode.c" "%SRC_DIR%\YuzuBiosPatcher.c") do (
  echo       %%~nxf
  %GCC% %FLAGS% -c "%%f" -o "%OBJ_DIR%\%%~nf.o"
  if errorlevel 1 (
    echo [ERROR] compilation of %%~nxf failed.
    exit /b 1
  )
)

echo [2/3] Linking ...
rem PE layout flags match the classic EDK2 EFI image profile for maximum
rem firmware loader compatibility (ImageBase 0, no DllChars, version 0).
set "LFLAGS=-nostdlib -Wl,--subsystem=10 -Wl,--entry=efi_main -Wl,--gc-sections -Wl,--image-base=0 -Wl,--disable-dynamicbase -Wl,--disable-nxcompat -Wl,--disable-high-entropy-va -Wl,--major-subsystem-version,0 -Wl,--minor-subsystem-version,0 -Wl,--major-os-version,0 -Wl,--minor-os-version,0 -Wl,--no-insert-timestamp"
%GCC% %LFLAGS% -o "%OUT_DIR%\YuzuBiosPatcher.efi" "%OBJ_DIR%\MiniUefi.o" "%OBJ_DIR%\Utility.o" "%OBJ_DIR%\Opcode.o" "%OBJ_DIR%\YuzuBiosPatcher.o"
if errorlevel 1 (
  echo [ERROR] link failed.
  exit /b 1
)

echo [3/3] Normalizing PE ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\pefix.ps1" "%OUT_DIR%\YuzuBiosPatcher.efi"

copy /y "%PROJECT_DIR%\startup.nsh" "%OUT_DIR%\" >nul
rem Copy the boot kit (UEFI Shell \EFI, Oniguruma.efi regex engine and the
rem patch config) so out\ is a complete, ready-to-copy USB payload.
xcopy "%PROJECT_DIR%\bootkit\*" "%OUT_DIR%\" /E /I /Y >nul
rem Intermediate object files are not part of the kit - remove them.
if exist "%OBJ_DIR%" rmdir /s /q "%OBJ_DIR%"

echo.
echo ==========================================================================
echo  BUILD OK  -  complete USB kit in: %OUT_DIR%
echo    EFI\Boot\BOOTX64.efi   UEFI Shell (boot loader)
echo    YuzuBiosPatcher.efi    main program
echo    Yuzu_Config.cfg        patch config
echo    Oniguruma.efi          regex engine
echo    startup.nsh            UEFI Shell autostart script
echo  Copy everything in out\ to the root of your USB drive (keep EFI\ folder).
echo ==========================================================================
exit /b 0
