
import csv
from pathlib import Path

import numpy as np
import trimesh

from direct.showbase.ShowBase import ShowBase
from direct.task import Task
from panda3d.core import (
    AmbientLight,
    DirectionalLight,
    Geom,
    GeomNode,
    GeomTriangles,
    GeomVertexData,
    GeomVertexFormat,
    GeomVertexWriter,
    NodePath,
    Point3,
    Vec4,
)

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
STL_PATH = DATA_DIR / "model.stl"
CSV_PATH = DATA_DIR / "sensors.csv"
EXPECTED_SENSOR_COUNT = 102

# Максимальный размер модели в координатах Panda3D.
MODEL_SIZE = 10.0
SENSOR_RADIUS = 0.075


def load_stl(path):
    """Загрузить STL и привести модель к удобному масштабу."""
    if not path.exists():
        raise FileNotFoundError(
            f"Не найден файл {path.name}. "
            f"Положи STL-модель в каталог {path.parent.name}/."
        )

    if path.stat().st_size == 0:
        raise ValueError(f"Файл STL пустой: {path}")

    mesh = trimesh.load(path, force="mesh")

    if not isinstance(mesh, trimesh.Trimesh) or len(mesh.faces) == 0:
        raise ValueError("STL не содержит корректной треугольной сетки.")

    if not np.isfinite(mesh.vertices).all():
        raise ValueError("В STL обнаружены некорректные координаты.")

    center = (mesh.bounds[0] + mesh.bounds[1]) / 2.0
    extent = float(np.max(mesh.extents))

    if extent <= 0:
        raise ValueError("У модели нулевые габариты.")

    scale = MODEL_SIZE / extent
    vertices = (mesh.vertices - center) * scale

    return mesh, vertices, center, scale


def create_mesh_node(mesh, vertices):
    """Создать геометрию Panda3D непосредственно из STL."""
    fmt = GeomVertexFormat.getV3n3()
    data = GeomVertexData(
        "battery-mesh", fmt, Geom.UHStatic
    )
    data.setNumRows(len(vertices))

    vertex_writer = GeomVertexWriter(data, "vertex")
    normal_writer = GeomVertexWriter(data, "normal")

    # Нормали вершин нужны для корректного освещения.
    normals = mesh.vertex_normals

    for vertex, normal in zip(vertices, normals):
        vertex_writer.addData3f(*vertex)
        normal_writer.addData3f(*normal)

    triangles = GeomTriangles(Geom.UHStatic)

    for face in mesh.faces:
        triangles.addVertices(
            int(face[0]), int(face[1]), int(face[2])
        )

    triangles.closePrimitive()

    geom = Geom(data)
    geom.addPrimitive(triangles)

    node = GeomNode("battery")
    node.addGeom(geom)

    model = NodePath(node)
    model.setColor(0.68, 0.72, 0.78, 1.0)
    model.setTwoSided(True)
    model.setLightOff(False)

    return model


def create_demo_csv(mesh):
    """Создать пример из 102 точек, если CSV отсутствует."""
    bounds = mesh.bounds
    low = bounds[0]
    high = bounds[1]

    # Демонстрационные точки распределены по габаритному
    # объёму модели. Они НЕ гарантированно находятся внутри неё.
    rng = np.random.default_rng(42)
    points = rng.uniform(low, high, size=(102, 3))

    with CSV_PATH.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(["id", "x", "y", "z", "temperature"])

        for i, point in enumerate(points, start=1):
            writer.writerow([
                i,
                *[round(float(v), 5) for v in point],
                round(25.0 + (i % 20) * 0.5, 1),
            ])

    print(f"Создан демонстрационный файл: {CSV_PATH.name}")


def load_sensors(center, scale):
    """Прочитать датчики и преобразовать координаты как у STL."""
    sensors = []

    with CSV_PATH.open(newline="", encoding="utf-8-sig") as file:
        reader = csv.DictReader(file)

        required = {"id", "x", "y", "z", "temperature"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError(
                "CSV должен содержать столбцы: "
                "id,x,y,z,temperature"
            )

        for row in reader:
            point = np.array(
                [float(row["x"]), float(row["y"]), float(row["z"])],
                dtype=float,
            )

            if not np.isfinite(point).all():
                raise ValueError(
                    f"Некорректные координаты датчика {row['id']}"
                )

            # Та же нормализация, что применена к STL.
            point = (point - center) * scale

            sensors.append({
                "id": row["id"],
                "position": point,
                "temperature": _parse_temperature(row),
            })

    if len(sensors) != EXPECTED_SENSOR_COUNT:
        raise ValueError(
            f"В CSV найдено {len(sensors)} датчиков, "
            f"ожидается {EXPECTED_SENSOR_COUNT}."
        )

    return sensors


def _parse_temperature(row):
    """Прочитать конечное числовое значение температуры."""
    try:
        temperature = float(row["temperature"])
    except (TypeError, ValueError) as exc:
        raise ValueError(
            f"Некорректная температура датчика {row.get('id', '?')}"
        ) from exc

    if not np.isfinite(temperature):
        raise ValueError(
            f"Некорректная температура датчика {row.get('id', '?')}"
        )
    return temperature


def temperature_color(temp):
    """Простая цветовая шкала: синий -> зелёный -> красный."""
    t = float(np.clip((temp - 20.0) / 60.0, 0.0, 1.0))

    if t < 0.5:
        k = t * 2.0
        return Vec4(0.0, k, 1.0 - k, 1.0)

    k = (t - 0.5) * 2.0
    return Vec4(k, 1.0 - k, 0.0, 1.0)


class BatteryViewer(ShowBase):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("Battery Visualizer — Prototype")
        self.setBackgroundColor(0.10, 0.12, 0.16, 1.0)

        self.disableMouse()

        mesh, vertices, center, scale = load_stl(STL_PATH)

        self.model = create_mesh_node(mesh, vertices)
        self.model.reparentTo(self.render)

        if not CSV_PATH.exists():
            create_demo_csv(mesh)

        self.sensors = load_sensors(center, scale)
        self.sensor_nodes = []

        # Встроенная сферическая модель Panda3D.
        sphere = self.loader.loadModel("models/misc/sphere")
        sphere.setScale(SENSOR_RADIUS)

        for sensor in self.sensors:
            marker = sphere.copyTo(self.render)
            marker.setPos(*sensor["position"])
            marker.setColor(
                temperature_color(sensor["temperature"])
            )
            marker.setTag("sensor_id", str(sensor["id"]))
            self.sensor_nodes.append(marker)

        self.setup_lighting()
        self.setup_camera()
        self.setup_controls()

        print(f"Модель загружена: {len(mesh.faces)} треугольников")
        print(f"Датчиков: {len(self.sensors)}")
        print("Управление: левая кнопка мыши — вращение, колесо — масштаб.")

    def setup_lighting(self):
        ambient = AmbientLight("ambient")
        ambient.setColor(Vec4(0.45, 0.45, 0.48, 1.0))
        self.render.setLight(
            self.render.attachNewNode(ambient)
        )

        directional = DirectionalLight("directional")
        directional.setColor(Vec4(0.9, 0.9, 0.9, 1.0))
        light_node = self.render.attachNewNode(directional)
        light_node.setHpr(-35, -55, 0)
        self.render.setLight(light_node)

    def setup_camera(self):
        self.heading = 35.0
        self.pitch = 20.0
        self.distance = 16.0

        self.update_camera()
        self.accept("wheel_up", self.zoom, [-1])
        self.accept("wheel_down", self.zoom, [1])

        self.dragging = False
        self.last_mouse = None
        self.accept("mouse1", self.start_drag)
        self.accept("mouse1-up", self.stop_drag)
        self.taskMgr.add(self.camera_task, "camera-task")

    def update_camera(self):
        h = np.radians(self.heading)
        p = np.radians(self.pitch)

        x = self.distance * np.cos(p) * np.sin(h)
        y = -self.distance * np.cos(p) * np.cos(h)
        z = self.distance * np.sin(p)

        self.camera.setPos(x, y, z)
        self.camera.lookAt(Point3(0, 0, 0))

    def zoom(self, direction):
        self.distance = float(np.clip(
            self.distance * (1.12 ** direction), 2.0, 100.0
        ))
        self.update_camera()

    def start_drag(self):
        self.dragging = True
        self.last_mouse = None

    def stop_drag(self):
        self.dragging = False
        self.last_mouse = None

    def camera_task(self, task):
        if self.dragging and self.mouseWatcherNode.hasMouse():
            mouse = self.mouseWatcherNode.getMouse()
            current = (mouse.getX(), mouse.getY())

            if self.last_mouse is not None:
                dx = current[0] - self.last_mouse[0]
                dy = current[1] - self.last_mouse[1]

                self.heading -= dx * 180.0
                self.pitch = float(np.clip(
                    self.pitch + dy * 180.0, -85.0, 85.0
                ))

                self.update_camera()

            self.last_mouse = current

        return Task.cont


if __name__ == "__main__":
    BatteryViewer().run()
