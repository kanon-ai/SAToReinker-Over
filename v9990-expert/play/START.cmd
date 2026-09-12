@echo off
setlocal
cd /d "%~dp0"
if not defined OPENMSX_EXE set "OPENMSX_EXE=C:\Program Files\openMSX\openmsx.exe"
if not exist "%OPENMSX_EXE%" (
 echo openMSX not found. Set OPENMSX_EXE to your openmsx.exe.
 pause
 exit /b 1
)
if not exist "EXPERT-SAVE.dsk" copy /b "blank-expert.dsk" "EXPERT-SAVE.dsk" >nul
"%OPENMSX_EXE%" -machine Panasonic_FS-A1ST -exta gfx9000 -cartb "%~dp0..\outputs\SAToReinker-Over-EXPERT.rom" -romtype ASCII8 -diska "%~dp0EXPERT-SAVE.dsk" -script "%~dp0launch.tcl"
endlocal
