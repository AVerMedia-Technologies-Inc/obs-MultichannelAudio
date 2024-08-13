@echo off

SET QT_KIT_BAT="C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
SET MSVC_COMPILER=C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/MSVC/14.29.30133/bin/HostX64/x64/cl.exe

SET PROJECT_ROOT=%~dp0..
REM echo %PROJECT_ROOT%
SET QT_VERSION_FILE=%PROJECT_ROOT%\qt.version
if exist "%QT_VERSION_FILE%" (
	for /f "usebackq delims=" %%a in ("%QT_VERSION_FILE%") do set QT_VERSION=%%a
) else (
	SET QT_VERSION=6.2.4
)
echo Use Qt %QT_VERSION%

REM Check Qt root folder
if "%QT_ROOT_PATH%" == "" (
	REM check default installation
	if exist "C:\Qt\%QT_VERSION%\msvc2019_64\bin\qtenv2.bat" (
		echo Default Qt installation found.
		SET QT_ROOT_PATH=C:\Qt
	)
)
echo QT_ROOT_PATH=%QT_ROOT_PATH%
if exist "%QT_ROOT_PATH%\%QT_VERSION%\msvc2019_64\bin\qtenv2.bat" (
    echo Found Qt root folder: %QT_ROOT_PATH%
    GOTO READY
) else (
	echo.
    echo   Qt root folder incorrect: %QT_ROOT_PATH%
)

echo.
echo   By default Qt installation, will be C:\Qt. Set QT_ROOT_PATH=C:\Qt on your computer.
echo.
echo   Set your System Environment Variables to correct installation.
echo.
pause
exit /b 1

:READY

SET QT_ENV2_BAT="%QT_ROOT_PATH%\%QT_VERSION%\msvc2019_64\bin\qtenv2.bat"
SET QT_JOM_BAT="%QT_ROOT_PATH%\Tools\QtCreator\bin\jom\jom.exe"
SET QT_CMAKE_BAT="%QT_ROOT_PATH%\Tools\CMake_64\bin\cmake.exe"
SET QT_QMAKE_BAT="%QT_ROOT_PATH%\%QT_VERSION%\msvc2019_64\bin\qmake.exe"

call %QT_ENV2_BAT%
if not %ERRORLEVEL% == 0 (
    EXIT /b 1
)

call %QT_KIT_BAT% amd64
if not %ERRORLEVEL% == 0 (
    EXIT /b 1
)

REM we need to change directory since previous script will change to Qt Kit folder
echo %PROJECT_ROOT%
REM change drive first
%PROJECT_ROOT:~0,2%
REM change to project root
cd %PROJECT_ROOT%
