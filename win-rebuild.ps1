# Скрипт пересборки проекта TermVisualization-BGA для Windows.
# Аналог rebuild.sh для Linux.

# Останавливаем выполнение при первой ошибке.
$ErrorActionPreference = "Stop"

# Переходим в каталог, в котором находится сам скрипт.
cd "$PSScriptRoot"

Write-Host "=== Закрываем запущенный экземпляр приложения (если есть) ===" -ForegroundColor Cyan
Get-Process bga_visualizer -ErrorAction SilentlyContinue | Stop-Process

Write-Host "=== Конфигурация проекта через CMake ===" -ForegroundColor Cyan
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE/vcpkg/scripts/buildsystems/vcpkg.cmake" -A x64

Write-Host "=== Сборка проекта ===" -ForegroundColor Cyan
cmake --build build --config Release

Write-Host "=== Копируем SDL2.dll рядом с исполняемым файлом ===" -ForegroundColor Cyan
Copy-Item "$env:USERPROFILE\vcpkg\installed\x64-windows\bin\SDL2.dll" ".\build\Release\SDL2.dll" -Force

Write-Host "=== Сборка успешно завершена ===" -ForegroundColor Green
Write-Host "Исполняемый файл: $(Resolve-Path "build/Release/bga_visualizer.exe")" -ForegroundColor Green
Write-Host "Запуск: .\build\Release\bga_visualizer.exe --no-can" -ForegroundColor Green
