# TermVisualization-BGA

Небольшое приложение на C++17 и SDL2 для просмотра STL-модели и температурных
датчиков. STL загружается и растеризуется на CPU; SDL2 создаёт окно и передаёт
события мыши и клавиатуры. Приложение рассчитано в том числе на Raspberry Pi
Zero W v1.1 с Raspberry Pi OS 32-bit.

## Данные

- Модель: `data/model.stl`.
- Датчики: `data/sensors.csv`, столбцы `id,x,y,z,temperature`.
- Координаты CSV должны совпадать с STL по системе координат и единицам.
  Приложение одинаково центрирует и масштабирует модель и датчики.
- При запуске программа предупреждает о точках датчиков за пределами габаритов STL.
- При сборке модель STL и CSV встраиваются в программу; запуск не требует
  отдельных файлов с данными. Показывается столько датчиков, сколько записей в CSV.
- Другие STL и CSV можно передать программе как аргументы командной строки.
- Поддерживаются бинарный и текстовый STL.

## Управление

- Зажмите левую кнопку мыши над моделью и двигайте мышь, чтобы вращать её.
- Перетаскивайте ползунок справа, чтобы менять прозрачность модели.
- Клавиши `+` и `-` или колёсико над моделью меняют масштаб.
- `Page Up` и `Page Down` прокручивают список датчиков; колёсико над списком
  также прокручивает его.
- `Esc` закрывает приложение.

## Raspberry Pi OS (32-bit)

Инструкция предназначена для Raspberry Pi Zero W v1.1 и 32-битной Raspberry Pi
OS (ARMHF, в том числе Release 5.6). Для первого запуска понадобятся microSD,
карта на 16 ГБ или больше, блок питания, монитор с переходником mini-HDMI,
USB OTG-переходник, клавиатура и мышь. У Zero W нет Ethernet, а Wi-Fi работает в диапазоне 2,4 ГГц.

### Сборка и запуск

Откройте Terminal в рабочем столе Pi, обновите систему и установите Git,
компилятор, CMake и SDL2:

```bash
sudo apt update
sudo apt full-upgrade -y
sudo apt install -y git build-essential cmake pkg-config libsdl2-dev
```

Проверьте версию ОС и архитектуру: `cat` должен показать нужный выпуск ОС,
а `dpkg` — вывести `armhf`.

```bash
cat /etc/os-release
dpkg --print-architecture
```

Скачайте опубликованную C++/SDL2-версию проекта и перейдите в её каталог. Если
обновления ещё не отправлены в GitHub, сначала опубликуйте их из компьютера,
на котором ведётся разработка.

```bash
git clone https://github.com/qyopw/TermVisualization-BGA-.git
cd TermVisualization-BGA-
```

Соберите его непосредственно на Pi Zero W, чтобы получить ARMHF-файл. Сборка
в один поток подходит для этой маломощной платы:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
./build/bga_visualizer
```

Запускайте программу из Terminal, открытого в графическом рабочем столе, не
через SSH или текстовую консоль: для SDL2 требуется активный графический сеанс.
При конфигурации CMake файлы `data/model.stl` и `data/sensors.csv` встраиваются
в исполняемый файл. Чтобы перенести его на другую Raspberry Pi с такой же
32-битной ОС, скопируйте только `build/bga_visualizer`; на целевой Pi должны быть
установлены SDL2 runtime-библиотека и системные библиотеки.

Чтобы заменить встроенные данные, передайте пути к модели и CSV:

```bash
./build/bga_visualizer /path/to/model.stl /path/to/sensors.csv
```

После изменения файлов в `data/` повторите конфигурацию и сборку CMake, чтобы
обновить данные внутри исполняемого файла.

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
./build/bga_visualizer
```

## Разработка на Windows (Visual Studio Code)

Установите VS Code, CMake, Git и Visual Studio Build Tools с компонентом
**Desktop development with C++**. Затем откройте PowerShell и установите SDL2
через vcpkg:

```powershell
git clone https://github.com/microsoft/vcpkg "$env:USERPROFILE/vcpkg"
& "$env:USERPROFILE/vcpkg/bootstrap-vcpkg.bat"
& "$env:USERPROFILE/vcpkg/vcpkg.exe" install sdl2:x64-windows
```

Из корня проекта настройте и соберите приложение:

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

Запустите `build\Release\bga_visualizer.exe` из Проводника или PowerShell.

## Внешние данные

По умолчанию приложение использует модель STL и CSV, встроенные при сборке.
Их можно заменить, передав оба пути:

```bash
./build/bga_visualizer path/to/model.stl path/to/sensors.csv
```
