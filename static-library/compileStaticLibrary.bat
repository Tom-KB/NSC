@echo off
REM Build the static library from the sources of this repository.
REM Run this script from its own directory.

gcc -c -O2 -I..\include ..\src\NSC.c -o NSC.o
if errorlevel 1 goto :error

ar rcs NSC.a NSC.o
if errorlevel 1 goto :error

REM The *.a file is simply renamed *.lib for the Windows toolchains
copy /Y NSC.a NSC.lib >nul
del NSC.o

echo Static library built : NSC.a and NSC.lib
goto :eof

:error
echo Build failed.
exit /b 1
