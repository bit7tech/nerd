@echo off
if [%INSTALL_PATH%] NEQ [] (
    copy /y _bin\Win64_Release_nerd\nerd.exe %INSTALL_PATH%
    copy /y data/system.n %INSTALL_PATH%
) else (
    echo Please define INSTALL_PATH in the environment
)

