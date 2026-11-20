## Разработка на macOS (Visual Studio Code)

Установите Xcode Command Line Tools, Homebrew и расширение C/C++ для VS Code.
В терминале установите SDL2 и CMake:

```bash
xcode-select --install
brew install cmake sdl2
```

Из терминала VS Code, открытого в корне проекта, соберите и запустите:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bga_visualizer --no-can
# For sensors that transmit temperature bytes most-significant first:
./build/bga_visualizer --can-byte-order big
```

## Разработка на Windows (Visual Studio Code)

### Предварительные требования

Установите следующее ПО:

1. **Visual Studio 2022 Community** (или Visual Studio Build Tools) с нагрузкой
   **Разработка классических приложений на C++** (*Desktop development with C++*).
   Это даст компилятор MSVC, линковщик и генератор CMake для Visual Studio.
2. **CMake** (https://cmake.org/download/) — добавьте `cmake.exe` в PATH.
3. **Git** (https://git-scm.com/download/win) — добавьте `git.exe` в PATH.
4. **VS Code** с расширением **C/C++** (опционально).

### Установка SDL2 через vcpkg

Откройте PowerShell и выполните:

```powershell
git clone https://github.com/microsoft/vcpkg "$env:USERPROFILE/vcpkg"
& "$env:USERPROFILE/vcpkg/bootstrap-vcpkg.bat"
& "$env:USERPROFILE/vcpkg/vcpkg.exe" install sdl2:x64-windows
```

### Сборка приложения

Из корня проекта выполните настройку и сборку через CMake:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE/vcpkg/scripts/buildsystems/vcpkg.cmake" -A x64
cmake --build build --config Release
```

Ключ `-A x64` явно указывает 64-битную сборку (совместимую с `sdl2:x64-windows`).

### Запуск приложения

Запустите собранный исполняемый файл с отключённым CAN-приёмом (SocketCAN доступен
только в Linux-сборке):

```powershell
.\build\Release\bga_visualizer.exe --no-can
```

### Важно: SDL2.dll

Для запуска `bga_visualizer.exe` рядом с ним должен находиться `SDL2.dll`.
Скопируйте его из каталога установки vcpkg:

```powershell
Copy-Item "$env:USERPROFILE\vcpkg\installed\x64-windows\bin\SDL2.dll" ".\build\Release\SDL2.dll"
```

Если вы запускаете программу из другой рабочей директории, убедитесь, что
`SDL2.dll` доступна в PATH или лежит рядом с `.exe`.

### Параметры командной строки

```
bga_visualizer.exe [model.stl] [sensors.csv] [--can-interface NAME] [--can-byte-order little|big] [--no-can] [--forced-can]
```

Без аргументов используются встроенные в исполняемый файл модель и список датчиков.
На Windows всегда запускайте с `--no-can`.

### Пересборка после изменений

После изменения исходников или файлов в `data/` можно пересобрать проект
вручную двумя командами CMake (настройку и сборку), либо использовать
готовый скрипт `rebuild.ps1`, который делает всё автоматически:

```powershell
.\rebuild.ps1
```

Скрипт:

1. Закрывает запущенный экземпляр `bga_visualizer.exe` (если есть),
2. Выполняет конфигурацию CMake с vcpkg toolchain-файлом,
3. Собирает проект в режиме Release,
4. Копирует `SDL2.dll` из vcpkg рядом с исполняемым файлом.

Скрипт `rebuild.sh` предназначен только для Linux.

## Внешние данные

По умолчанию приложение использует модель STL и CSV, встроенные при сборке.
Их можно заменить, передав оба пути:

```bash
./build/bga_visualizer path/to/model.stl path/to/sensors.csv --no-can
```
