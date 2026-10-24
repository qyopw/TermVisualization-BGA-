"""Battery temperature viewer using Pygame's software renderer."""

import csv
from pathlib import Path

import numpy as np
import pygame
import trimesh


from direct.showbase.ShowBase import ShowBase
from direct.task import Task
from direct.gui.DirectGui import (
    DirectFrame,
    DirectLabel,
    DirectScrolledFrame,
    DirectSlider,
)
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
    TransparencyAttrib,
    Vec4,
    WindowProperties,
)

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
STL_PATH = DATA_DIR / "model.stl"
CSV_PATH = DATA_DIR / "sensors.csv"

MODEL_SIZE = 10.0
MAX_RENDER_FACES = 25000
BACKGROUND = (25, 31, 41)
PANEL = (18, 23, 31)
TEXT = (225, 232, 241)
MUTED = (164, 176, 191)


def load_stl(path):
    """Load an STL mesh and normalize its coordinates."""
    if not path.exists():
        raise FileNotFoundError(f"Не найден STL-файл: {path}")
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


def create_demo_csv(mesh):
    """Create a sample sensor CSV if the file is missing."""
    points = np.random.default_rng(42).uniform(
        mesh.bounds[0], mesh.bounds[1], size=(102, 3)
    )
    with CSV_PATH.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(["id", "x", "y", "z", "temperature"])
        for index, point in enumerate(points, start=1):
            writer.writerow([
                index,
                *[round(float(value), 5) for value in point],
                round(25.0 + (index % 20) * 0.5, 1),
            ])
    print(f"Создан демонстрационный файл: {CSV_PATH.name}")


def load_sensors(center, scale):
    """Read sensors and apply the same normalization as the STL mesh."""
    sensors = []
    with CSV_PATH.open(newline="", encoding="utf-8-sig") as file:
        reader = csv.DictReader(file)
        required = {"id", "x", "y", "z", "temperature"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("CSV должен содержать столбцы id,x,y,z,temperature")

        for row in reader:
            try:
                point = np.array([float(row[key]) for key in ("x", "y", "z")])
            except (TypeError, ValueError) as exc:
                raise ValueError(
                    f"Некорректные координаты датчика {row.get('id', '?')}"
                ) from exc
            if not np.isfinite(point).all():
                raise ValueError(
                    f"Некорректные координаты датчика {row.get('id', '?')}"
                )
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

            sensors.append({
                "id": row["id"],
                "position": (point - center) * scale,
                "temperature": temperature,
            })
    return sensors


def temperature_color(temperature):
    """Return an RGB color from blue through green to red."""
    value = float(np.clip((temperature - 20.0) / 60.0, 0.0, 1.0))
    if value < 0.5:
        fraction = value * 2
        return (0, round(255 * fraction), round(255 * (1 - fraction)))
    fraction = (value - 0.5) * 2
    return (round(255 * fraction), round(255 * (1 - fraction)), 0)


class BatteryViewer:
    """Interactive 3D mesh viewer drawn with Pygame software surfaces."""

    def __init__(self):
        pygame.init()
        self.screen = pygame.display.set_mode((800, 480), pygame.RESIZABLE)
        pygame.display.set_caption("Battery Visualizer")
        self.clock = pygame.time.Clock()
        self.font = pygame.font.Font(None, 22)
        self.small_font = pygame.font.Font(None, 18)

        mesh, self.vertices, center, scale = load_stl(STL_PATH)
        if not CSV_PATH.exists():
            create_demo_csv(mesh)
        self.sensors = load_sensors(center, scale)

        if len(mesh.faces) > MAX_RENDER_FACES:
            indices = np.linspace(
                0, len(mesh.faces) - 1, MAX_RENDER_FACES, dtype=np.int32
            )
            self.faces = mesh.faces[indices]
            normals = mesh.face_normals[indices]
        else:
            self.faces = mesh.faces
            normals = mesh.face_normals

        light = np.array([0.35, -0.45, 0.82])
        light /= np.linalg.norm(light)
        brightness = 0.42 + 0.58 * np.abs(normals @ light)
        base_color = np.array([158, 177, 199])
        self.face_colors = np.clip(
            brightness[:, None] * base_color, 0, 255
        ).astype(np.uint8)

        self.heading = 35.0
        self.pitch = 20.0
        self.distance = 16.0
        self.transparency = 0.0
        self.dragging = False
        self.slider_dragging = False
        self.last_mouse = None
        self.table_scroll = 0
        print(f"Модель: {len(mesh.faces)} треугольников")
        print(f"Датчиков: {len(self.sensors)}")

    def panel_geometry(self):
        width, height = self.screen.get_size()
        panel_width = min(310, max(230, width // 3))
        panel_x = width - panel_width
        table_top = 174
        table_bottom = height - 14
        return panel_x, panel_width, table_top, table_bottom

    def draw_text(self, text, x, y, color=TEXT, font=None):
        image = (font or self.font).render(str(text), True, color)
        self.screen.blit(image, (x, y))

    def draw_panel(self):
        width, height = self.screen.get_size()
        panel_x, panel_width, table_top, table_bottom = self.panel_geometry()
        pygame.draw.rect(self.screen, PANEL, (panel_x, 0, panel_width, height))
        pygame.draw.line(self.screen, (56, 68, 84), (panel_x, 0), (panel_x, height))
        self.draw_text("Настройки и датчики", panel_x + 18, 15)
        self.draw_text("Прозрачность модели", panel_x + 18, 50, MUTED, self.small_font)
        self.draw_text(f"{round(self.transparency * 100)}%", panel_x + 18, 72)

        self.slider_rect = pygame.Rect(panel_x + 18, 106, panel_width - 48, 10)
        pygame.draw.rect(self.screen, (66, 78, 94), self.slider_rect, border_radius=5)
        slider_x = self.slider_rect.left + round(
            self.transparency / 0.95 * self.slider_rect.width
        )
        pygame.draw.circle(self.screen, (90, 180, 240), (slider_x, 111), 9)

        self.draw_text(f"Датчики: {len(self.sensors)}", panel_x + 18, 133, MUTED, self.small_font)
        pygame.draw.line(self.screen, (56, 68, 84), (panel_x + 12, 164), (width - 12, 164))
        self.draw_text("ID", panel_x + 18, 148, MUTED, self.small_font)
        self.draw_text("Температура", panel_x + panel_width - 122, 148, MUTED, self.small_font)

        self.table_rect = pygame.Rect(
            panel_x + 10, table_top, panel_width - 20, max(0, table_bottom - table_top)
        )
        pygame.draw.rect(self.screen, (23, 29, 39), self.table_rect)
        row_height = 28
        visible_rows = max(1, self.table_rect.height // row_height)
        max_scroll = max(0, len(self.sensors) - visible_rows)
        self.table_scroll = min(self.table_scroll, max_scroll)
        for row_index, sensor in enumerate(
            self.sensors[self.table_scroll:self.table_scroll + visible_rows]
        ):
            row_y = table_top + row_index * row_height
            if row_index % 2 == 0:
                pygame.draw.rect(
                    self.screen, (31, 39, 51),
                    (self.table_rect.x, row_y, self.table_rect.width, row_height),
                )
            self.draw_text(sensor["id"], panel_x + 18, row_y + 5, TEXT, self.small_font)
            self.draw_text(
                f"{sensor['temperature']:g} °C",
                panel_x + panel_width - 122,
                row_y + 5,
                temperature_color(sensor["temperature"]),
                self.small_font,
            )

    def project(self, points, viewport, focal_length):
        heading = np.radians(self.heading)
        pitch = np.radians(self.pitch)
        cos_h, sin_h = np.cos(heading), np.sin(heading)
        cos_p, sin_p = np.cos(pitch), np.sin(pitch)

        x = points[:, 0] * cos_h - points[:, 1] * sin_h
        y = points[:, 0] * sin_h + points[:, 1] * cos_h
        z = points[:, 2]
        rotated_y = y * cos_p - z * sin_p
        rotated_z = y * sin_p + z * cos_p
        depth = self.distance - rotated_z
        safe_depth = np.maximum(depth, 0.1)
        projected_x = viewport.centerx + focal_length * x / safe_depth
        projected_y = viewport.centery - focal_length * rotated_y / safe_depth
        projected = np.column_stack((projected_x, projected_y))
        return projected, depth

    def draw_model(self, viewport):
        if viewport.width <= 0 or viewport.height <= 0:
            return
        focal_length = min(viewport.width, viewport.height) * 1.25
        points = self.vertices[self.faces]
        projected, depth = self.project(points.reshape(-1, 3), viewport, focal_length)
        triangles_2d = projected.reshape(-1, 3, 2)
        triangle_depth = depth.reshape(-1, 3).mean(axis=1)
        order = np.argsort(triangle_depth)[::-1]

        clip_rect = viewport.clip(self.screen.get_rect())
        self.screen.set_clip(clip_rect)
        if self.transparency > 0:
            mesh_layer = pygame.Surface(viewport.size, pygame.SRCALPHA)
            target = mesh_layer
            offset_x, offset_y = viewport.left, viewport.top
            alpha = round(255 * (1 - self.transparency))
        else:
            target = self.screen
            offset_x = offset_y = 0
            alpha = 255

        for triangle_index in order:
            if triangle_depth[triangle_index] <= 0.1:
                continue
            polygon = triangles_2d[triangle_index]
            if np.any(polygon[:, 0] < viewport.left - 300) or np.any(polygon[:, 0] > viewport.right + 300):
                continue
            if np.any(polygon[:, 1] < viewport.top - 300) or np.any(polygon[:, 1] > viewport.bottom + 300):
                continue
            color = (*self.face_colors[triangle_index].tolist(), alpha)
            points_2d = [
                (round(point[0] - offset_x), round(point[1] - offset_y))
                for point in polygon
            ]
            pygame.draw.polygon(target, color, points_2d)

        if self.transparency > 0:
            self.screen.blit(mesh_layer, viewport.topleft)
        self.screen.set_clip(None)

        sensor_points = np.array([sensor["position"] for sensor in self.sensors])
        if len(sensor_points):
            projected_sensors, sensor_depth = self.project(sensor_points, viewport, focal_length)
            for sensor, point, distance in zip(self.sensors, projected_sensors, sensor_depth):
                if distance <= 0.1 or not viewport.collidepoint(point):
                    continue
                pygame.draw.circle(
                    self.screen,
                    temperature_color(sensor["temperature"]),
                    (round(point[0]), round(point[1])),
                    5,
                )
                pygame.draw.circle(
                    self.screen, (245, 248, 252),
                    (round(point[0]), round(point[1])), 5, 1,
                )

    def set_transparency_from_mouse(self, x):
        width = max(1, self.slider_rect.width)
        self.transparency = float(np.clip(
            (x - self.slider_rect.left) / width * 0.95, 0, 0.95
        ))

    def handle_event(self, event):
        panel_x, _, _, _ = self.panel_geometry()
        viewport = pygame.Rect(0, 0, panel_x, self.screen.get_height())

        if event.type == pygame.QUIT:
            return False
        if event.type == pygame.VIDEORESIZE:
            self.screen = pygame.display.set_mode(event.size, pygame.RESIZABLE)
        elif event.type == pygame.MOUSEBUTTONDOWN:
            if event.button == 1:
                if hasattr(self, "slider_rect") and self.slider_rect.inflate(12, 18).collidepoint(event.pos):
                    self.slider_dragging = True
                    self.set_transparency_from_mouse(event.pos[0])
                elif viewport.collidepoint(event.pos):
                    self.dragging = True
                    self.last_mouse = event.pos
            elif event.button in (4, 5):
                if hasattr(self, "table_rect") and self.table_rect.collidepoint(event.pos):
                    self.table_scroll += -3 if event.button == 4 else 3
                elif viewport.collidepoint(event.pos):
                    self.distance = float(np.clip(
                        self.distance * (0.9 if event.button == 4 else 1.1),
                        7.0, 60.0,
                    ))
        elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
            self.dragging = False
            self.slider_dragging = False
            self.last_mouse = None
        elif event.type == pygame.MOUSEMOTION:
            if self.slider_dragging:
                self.set_transparency_from_mouse(event.pos[0])
            elif self.dragging:
                dx, dy = event.rel
                self.heading -= dx * 0.6
                self.pitch = float(np.clip(self.pitch + dy * 0.6, -85, 85))
        elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
            return False
        return True

    def run(self):
        running = True
        while running:
            for event in pygame.event.get():
                running = self.handle_event(event) and running
            self.screen.fill(BACKGROUND)
            panel_x, _, _, _ = self.panel_geometry()
            viewport = pygame.Rect(0, 0, panel_x, self.screen.get_height())
            self.draw_model(viewport)
            self.draw_panel()
            pygame.display.flip()
            self.clock.tick(15)
        pygame.quit()


if __name__ == "__main__":
    BatteryViewer().run()
