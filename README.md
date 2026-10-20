# TermVisualization-BGA-

3D-визуализация модели БГА и температурных датчиков на Panda3D.

## Данные

- STL-модель: `data/model.stl`.
- Датчики: `data/sensors.csv` со столбцами `id,x,y,z,temperature`.
- Координаты датчиков должны быть в той же системе координат и единицах,
  что и STL. Приложение центрирует и масштабирует модель и датчики вместе.
- Для запуска ожидается ровно 102 датчика. Если CSV отсутствует, приложение
  создаст демонстрационный CSV из 102 точек в габаритном объёме модели.

## Запуск на macOS

Из корня репозитория выполните:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
python main.py
```
