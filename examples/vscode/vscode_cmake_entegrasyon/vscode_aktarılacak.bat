@echo off
echo VS Code CMake ortami hazirlaniyor...

:: Derleyici, CMake, Ninja ve OpenOCD Yollari (Kendi sistemine gore ayarli)
set "TOOLCHAIN_PATH=C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin"
set "CMAKE_PATH=%USERPROFILE%\Desktop\cmake\bin"
set "NINJA_PATH=%USERPROFILE%\Desktop\ninja"
set "OPENOCD_PATH=C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.openocd.3.3.0.202405280907\tools\bin"

:: Gecici PATH Ayari
set "PATH=%TOOLCHAIN_PATH%;%CMAKE_PATH%;%NINJA_PATH%;%OPENOCD_PATH%;%PATH%"

:: Hedef Klasoru Belirle (Bir ust dizine temiz proje acar)
set "TARGET_DIR=..\VSCode_app_md5_2"

if exist "%TARGET_DIR%" rmdir /s /q "%TARGET_DIR%"
mkdir "%TARGET_DIR%"

echo Gerekli dosyalar kopyalaniyor...
xcopy "Core" "%TARGET_DIR%\Core\" /E /I /Y /Q
if exist "Drivers" xcopy "Drivers" "%TARGET_DIR%\Drivers\" /E /I /Y /Q
if exist "cmake" xcopy "cmake" "%TARGET_DIR%\cmake\" /E /I /Y /Q

:: Dosyalari kopyala
copy "CMakeLists.txt" "%TARGET_DIR%\" >nul
copy "*.ld" "%TARGET_DIR%\" >nul
copy "*.s" "%TARGET_DIR%\" >nul
copy "*.ioc" "%TARGET_DIR%\" >nul
copy "CMakePresets.json" "%TARGET_DIR%\" >nul

:: VS Code ayarlari icin bos klasor olustur
mkdir "%TARGET_DIR%\.vscode"

echo Islem basarili! VS Code aciliyor...
cd "%TARGET_DIR%"
code .