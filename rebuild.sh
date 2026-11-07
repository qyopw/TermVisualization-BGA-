#!/bin/bash

# Останавливаем скрипт, если любая команда завершилась ошибкой.
set -e

# Переходим в каталог, в котором находится сам скрипт.
cd "$(dirname "$0")"

echo "=== Конфигурация проекта через CMake ==="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

echo "=== Сборка проекта ==="
cmake --build build --parallel 1

echo "=== Сборка успешно завершена ==="
echo "Исполняемый файл: $(pwd)/build/bga_visualizer"
