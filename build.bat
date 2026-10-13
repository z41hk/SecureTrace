@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set CMAKE=cmake
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
  set "CMAKE=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if "%MYSQL_CONNECTOR_DIR%"=="" set "MYSQL_CONNECTOR_DIR=C:\Program Files\MySQL\MySQL Connector C++ 8.0"
if "%OPENSSL_ROOT_DIR%"=="" set "OPENSSL_ROOT_DIR=C:\Program Files\OpenSSL-Win64"
"%CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64 -DMYSQL_CONNECTOR_DIR="%MYSQL_CONNECTOR_DIR%" -DOPENSSL_ROOT_DIR="%OPENSSL_ROOT_DIR%"
if errorlevel 1 exit /b 1
"%CMAKE%" --build build --config Release
if errorlevel 1 exit /b 1
echo.
echo Built. Run: build\Release\securetrace.exe
echo Help:  build\Release\securetrace.exe --help
echo Demo:  build\Release\securetrace.exe --demo
exit /b 0
