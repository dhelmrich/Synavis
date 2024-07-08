@echo off
REM Check if python is installed and in the path
python --version > NUL 2>&1
if errorlevel 1 (
    echo Python is not installed or not in the path. Please install Python 3.6 or higher and add it to the path.
    exit /b 1
)
REM STart signalling server with fixed log file to not overburden the folder
python signalling_server.py --log-file logfile_signalling.log

:EOF
```