# TermVisualization-BGA

Небольшое приложение на C++17 и SDL2 для просмотра STL-модели и температурных датчиков. STL загружается и растеризуется на GPU; SDL2 создаёт окно и передаёт
события мыши и клавиатуры. Данные о температуре датчиков передаюся по CAN 2.0 и обрабатываюся приложением в реальном времени. Приложение рассчитано на работу на Raspberry Pi Zero W v1.1 с PiOS 32-bit в связке с MCP2515 в качестве can-трансивера.

## Данные

- Модель: `data/model.stl`.
- Датчики: `data/sensors.csv`, столбцы `id,x,y,z`.
- Координаты CSV должны совпадать с STL по системе координат и единицам.
  Приложение одинаково центрирует и масштабирует модель и датчики.
- При запуске программа предупреждает о точках датчиков за пределами габаритов STL.
- При сборке модель STL и CSV встраиваются в программу; запуск не требует отдельных файлов с данными. Показывается столько датчиков, сколько записей в CSV.
- Данные с датчиков передаюся по CAN 2.0 на MCP2515 с can-трансивером, подключённым к PaspberryPi Zero W v1.1. SPI включён, приём CAN-пакетов работает на штатном Linux SocketCAN через сетевой интерфейс can0.

## Управление

- Зажмите левую кнопку мыши над моделью и двигайте мышь, чтобы вращать её.
- Перетаскивайте ползунок справа, чтобы менять прозрачность модели.
- Клавиши `+` и `-` или колёсико над моделью меняют масштаб.
- `Page Up` и `Page Down` прокручивают список датчиков; колёсико над списком
  также прокручивает его.
- `Esc` закрывает приложение.

## Raspberry Pi OS (32-bit)

Инструкция предназначена для Raspberry Pi Zero W v1.1 и 32-битной PiOS (ARMHF). У Zero W нет Ethernet, а Wi-Fi работает в диапазоне 2,4 ГГц.

### Сборка проекта

Откройте Terminal (ctrl+alt+T) в рабочем столе PiOS, обновите систему при необходимости и установите зависимости: Git, компилятор, CMake и SDL2:

```bash
sudo apt update
sudo apt full-upgrade -y
sudo apt install -y git build-essential cmake pkg-config libsdl2-dev
```

Склонируйте репозиторий в каталог `/home/user/git/TermVisualization-BGA-`. (или любой другой по вашему желанию).
Необходимо включить SSH-сервер в raspi-config.
Для SSH-клонирования на Raspberry Pi заранее добавьте публичный SSH-ключ Pi в настройки GitHub (**Settings → SSH and GPG keys → New SSH key**, тип **Authentication Key**):

```bash
mkdir -p /home/user/git
cd /home/user/git
git clone git@github.com:qyopw/TermVisualization-BGA-.git
cd TermVisualization-BGA-
```

### Обновление проекта и пересборка

После отправки изменений с компьютера разработки обновите исходники на Pi и
пересоберите приложение:

```bash
cd /home/cpt/git/TermVisualization-BGA-
git pull
./rebuild.sh
```

Скрипт `rebuild.sh` находится в корне репозитория. Он переходит в каталог
проекта, конфигурирует сборку CMake в режиме `Release` и компилирует приложение
в один поток. Если сборка завершилась ошибкой, скрипт останавливается.

Если скрипт не запускается с ошибкой `Permission denied`, добавьте право
исполнения:

```bash
chmod +x rebuild.sh
```

Запускайте программу из Terminal, открытого в графическом рабочем столе, не
через SSH или текстовую консоль: для SDL2 требуется активный графический сеанс.
При конфигурации CMake файлы `data/model.stl` и `data/sensors.csv` встраиваются
в исполняемый файл.

После изменения файлов в `data/` повторите конфигурацию и сборку CMake, чтобы
обновить данные внутри исполняемого файла.

Запуск программы производится с исполняемого файла `bga_visualizer`в директории проекта.

```bash
./build/bga_visualizer
```

## Работа с Git

Основной рабочий процесс: разработка на компьютере, отправка изменений в GitHub,
затем получение изменений и пересборка на Raspberry Pi.

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
