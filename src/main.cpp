#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "embedded_assets.h"
#include "can_receiver.hpp"
#include "sensor_state.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr int kPanelX = 544;
constexpr int kPanelWidth = kWidth - kPanelX;
constexpr int kSliderX = kPanelX + 20;
constexpr int kSliderY = 78;
constexpr int kSliderWidth = kPanelWidth - 40;
constexpr double kModelSize = 10.0;
constexpr double kNearPlane = 0.1;

using Pixel = std::uint32_t;

constexpr Pixel rgb(int red, int green, int blue) {
    return 0xFF000000u |
           (static_cast<Pixel>(red & 0xFF) << 16) |
           (static_cast<Pixel>(green & 0xFF) << 8) |
           static_cast<Pixel>(blue & 0xFF);
}

Vec3 operator+(Vec3 left, Vec3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator-(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 operator*(Vec3 point, double amount) {
    return {point.x * amount, point.y * amount, point.z * amount};
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 normalized(Vec3 value) {
    const double length = std::sqrt(dot(value, value));
    if (length <= std::numeric_limits<double>::epsilon()) {
        return {};
    }
    return value * (1.0 / length);
}

struct Triangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
    Vec3 normal;
};

struct Bounds {
    Vec3 minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    Vec3 maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
};

struct Mesh {
    std::vector<Triangle> triangles;
    Bounds bounds;
};

void include(Bounds& bounds, Vec3 point) {
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
    bounds.maximum.z = std::max(bounds.maximum.z, point.z);
}

Triangle makeTriangle(Vec3 a, Vec3 b, Vec3 c) {
    return {a, b, c, normalized(cross(b - a, c - a))};
}

float readLittleEndianFloat(const std::vector<unsigned char>& bytes,
                            std::size_t offset) {
    const std::uint32_t bits =
        static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Mesh parseStl(const std::vector<unsigned char>& bytes, const std::string& source) {
    if (bytes.size() < 5) {
        throw std::runtime_error("STL file is empty or too short: " + source);
    }

    Mesh mesh;
    const auto add = [&mesh](Vec3 a, Vec3 b, Vec3 c) {
        for (const Vec3 point : {a, b, c}) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                !std::isfinite(point.z)) {
                throw std::runtime_error("STL contains a non-finite vertex.");
            }
            include(mesh.bounds, point);
        }
        mesh.triangles.push_back(makeTriangle(a, b, c));
    };

    bool binary = false;
    std::uint32_t triangleCount = 0;
    if (bytes.size() >= 84) {
        triangleCount =
            static_cast<std::uint32_t>(bytes[80]) |
            (static_cast<std::uint32_t>(bytes[81]) << 8) |
            (static_cast<std::uint32_t>(bytes[82]) << 16) |
            (static_cast<std::uint32_t>(bytes[83]) << 24);
        const std::uint64_t expectedSize = 84ULL + 50ULL * triangleCount;
        binary = expectedSize == bytes.size();
    }

    if (binary) {
        mesh.triangles.reserve(triangleCount);
        for (std::uint32_t face = 0; face < triangleCount; ++face) {
            const std::size_t offset = 84 + static_cast<std::size_t>(face) * 50 + 12;
            Vec3 points[3];
            for (int vertex = 0; vertex < 3; ++vertex) {
                const std::size_t vertexOffset = offset +
                    static_cast<std::size_t>(vertex) * 12;
                points[vertex] = {
                    readLittleEndianFloat(bytes, vertexOffset),
                    readLittleEndianFloat(bytes, vertexOffset + 4),
                    readLittleEndianFloat(bytes, vertexOffset + 8),
                };
            }
            add(points[0], points[1], points[2]);
        }
    } else {
        std::string contents(bytes.begin(), bytes.end());
        std::istringstream text(contents);
        std::string token;
        std::vector<Vec3> vertices;
        while (text >> token) {
            std::transform(token.begin(), token.end(), token.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            if (token != "vertex") {
                continue;
            }
            Vec3 point;
            if (!(text >> point.x >> point.y >> point.z)) {
                throw std::runtime_error("Malformed ASCII STL vertex.");
            }
            vertices.push_back(point);
            if (vertices.size() == 3) {
                add(vertices[0], vertices[1], vertices[2]);
                vertices.clear();
            }
        }
        if (!vertices.empty()) {
            throw std::runtime_error("ASCII STL has an incomplete triangle.");
        }
    }

    if (mesh.triangles.empty()) {
        throw std::runtime_error("STL contains no triangles: " + source);
    }
    return mesh;
}

Mesh loadStl(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open STL file: " + path.string());
    }
    const std::istreambuf_iterator<char> begin(input);
    const std::istreambuf_iterator<char> end;
    const std::vector<unsigned char> bytes(begin, end);
    return parseStl(bytes, path.string());
}

void normalizeMesh(Mesh& mesh, Vec3& center, double& scale) {
    center = (mesh.bounds.minimum + mesh.bounds.maximum) * 0.5;
    const Vec3 extent = mesh.bounds.maximum - mesh.bounds.minimum;
    const double largestExtent = std::max({extent.x, extent.y, extent.z});
    if (!std::isfinite(largestExtent) || largestExtent <= 0.0) {
        throw std::runtime_error("STL has zero or invalid dimensions.");
    }
    scale = kModelSize / largestExtent;

    for (Triangle& triangle : mesh.triangles) {
        triangle.a = (triangle.a - center) * scale;
        triangle.b = (triangle.b - center) * scale;
        triangle.c = (triangle.c - center) * scale;
        triangle.normal = normalized(cross(triangle.b - triangle.a,
                                            triangle.c - triangle.a));
    }
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> parseCsvLine(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) {
        throw std::runtime_error("CSV row contains an unclosed quote.");
    }
    fields.push_back(trim(field));
    return fields;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

double parseNumber(const std::string& text, const std::string& field,
                   const std::string& sensorId) {
    try {
        std::size_t parsed = 0;
        const double value = std::stod(text, &parsed);
        if (parsed != text.size() || !std::isfinite(value)) {
            throw std::invalid_argument("not a finite number");
        }
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid " + field + " for sensor " + sensorId +
                                 ": " + text);
    }
}

std::vector<Sensor> loadSensors(std::istream& input, const std::string& source,
                                Vec3 center, double scale,
                                const Bounds& modelBounds) {
    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("Sensor CSV is empty: " + source);
    }
    std::vector<std::string> headers = parseCsvLine(line);
    if (!headers.empty() && headers[0].size() >= 3 &&
        static_cast<unsigned char>(headers[0][0]) == 0xEF &&
        static_cast<unsigned char>(headers[0][1]) == 0xBB &&
        static_cast<unsigned char>(headers[0][2]) == 0xBF) {
        headers[0].erase(0, 3);
    }

    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        columns[lowercase(trim(headers[i]))] = i;
    }
    for (const std::string required : {"id", "x", "y", "z"}) {
        if (columns.find(required) == columns.end()) {
            throw std::runtime_error(
                "CSV must contain columns id,x,y,z.");
        }
    }

    std::vector<Sensor> sensors;
    std::size_t outsideModelBounds = 0;
    const Vec3 modelExtent = modelBounds.maximum - modelBounds.minimum;
    const double boundsMargin =
        0.02 * std::max({modelExtent.x, modelExtent.y, modelExtent.z});
    std::size_t rowNumber = 1;
    while (std::getline(input, line)) {
        ++rowNumber;
        if (trim(line).empty()) {
            continue;
        }
        const std::vector<std::string> fields = parseCsvLine(line);
        const auto field = [&fields, &columns, rowNumber](const std::string& name)
            -> const std::string& {
            const std::size_t column = columns.at(name);
            if (column >= fields.size()) {
                throw std::runtime_error("CSV row " + std::to_string(rowNumber) +
                                         " is missing a column.");
            }
            return fields[column];
        };

        const std::string id = field("id");
        const Vec3 point{
            parseNumber(field("x"), "x coordinate", id),
            parseNumber(field("y"), "y coordinate", id),
            parseNumber(field("z"), "z coordinate", id),
        };
        if (point.x < modelBounds.minimum.x - boundsMargin ||
            point.x > modelBounds.maximum.x + boundsMargin ||
            point.y < modelBounds.minimum.y - boundsMargin ||
            point.y > modelBounds.maximum.y + boundsMargin ||
            point.z < modelBounds.minimum.z - boundsMargin ||
            point.z > modelBounds.maximum.z + boundsMargin) {
            ++outsideModelBounds;
        }
        if (id.empty()) {
            throw std::runtime_error("CSV row " + std::to_string(rowNumber) +
                                     " has an empty sensor ID.");
        }
        sensors.push_back({id, (point - center) * scale});
    }

    if (sensors.empty()) {
        throw std::runtime_error("Sensor CSV has no data rows: " + source);
    }
    if (outsideModelBounds > 0) {
        std::cerr << "Warning: " << outsideModelBounds
                  << " sensor position(s) are outside the STL bounds. Check the "
                     "CSV coordinate origin and units.\n";
    }
    return sensors;
}

struct ViewPoint {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;
};

class Viewer {
public:
    Viewer(Mesh mesh, SensorState& sensors)
        : mesh_(std::move(mesh)), sensors_(sensors),
          pixels_(static_cast<std::size_t>(kWidth) * kHeight),
          depthBuffer_(static_cast<std::size_t>(kWidth) * kHeight,
                       std::numeric_limits<double>::infinity()) {
        window_ = SDL_CreateWindow(
            "BGA Temperature Visualizer", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, kWidth, kHeight,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (window_ == nullptr) {
            throw std::runtime_error(std::string("SDL window error: ") + SDL_GetError());
        }

        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (renderer_ == nullptr) {
            throw std::runtime_error(std::string("SDL renderer error: ") + SDL_GetError());
        }
        if (SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight) != 0) {
            throw std::runtime_error(std::string("SDL logical size error: ") + SDL_GetError());
        }

        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
        if (texture_ == nullptr) {
            throw std::runtime_error(std::string("SDL texture error: ") + SDL_GetError());
        }
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE);
    }

    ~Viewer() {
        if (texture_ != nullptr) SDL_DestroyTexture(texture_);
        if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
        if (window_ != nullptr) SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    int run() {
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                running = handleEvent(event) && running;
            }
            drawFrame();
            SDL_Delay(16);
        }
        return 0;
    }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    Mesh mesh_;
    SensorState& sensors_;
    std::vector<Pixel> pixels_;
    std::vector<double> depthBuffer_;
    double heading_ = 35.0;
    double pitch_ = 20.0;
    double distance_ = 18.0;
    double transparency_ = 0.0;
    bool rotating_ = false;
    bool changingTransparency_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    int tableScroll_ = 0;

    static const std::array<std::uint8_t, 7>& glyph(char character) {
        static const std::unordered_map<char, std::array<std::uint8_t, 7>> font = {
            {' ', {0, 0, 0, 0, 0, 0, 0}},
            {'0', {14, 17, 19, 21, 25, 17, 14}},
            {'1', {4, 12, 4, 4, 4, 4, 14}},
            {'2', {14, 17, 1, 2, 4, 8, 31}},
            {'3', {30, 1, 1, 14, 1, 1, 30}},
            {'4', {2, 6, 10, 18, 31, 2, 2}},
            {'5', {31, 16, 16, 30, 1, 1, 30}},
            {'6', {14, 16, 16, 30, 17, 17, 14}},
            {'7', {31, 1, 2, 4, 8, 8, 8}},
            {'8', {14, 17, 17, 14, 17, 17, 14}},
            {'9', {14, 17, 17, 15, 1, 1, 14}},
            {'A', {14, 17, 17, 31, 17, 17, 17}},
            {'B', {30, 17, 17, 30, 17, 17, 30}},
            {'C', {14, 17, 16, 16, 16, 17, 14}},
            {'D', {30, 17, 17, 17, 17, 17, 30}},
            {'E', {31, 16, 16, 30, 16, 16, 31}},
            {'F', {31, 16, 16, 30, 16, 16, 16}},
            {'G', {14, 17, 16, 23, 17, 17, 15}},
            {'H', {17, 17, 17, 31, 17, 17, 17}},
            {'I', {14, 4, 4, 4, 4, 4, 14}},
            {'J', {7, 2, 2, 2, 2, 18, 12}},
            {'K', {17, 18, 20, 24, 20, 18, 17}},
            {'L', {16, 16, 16, 16, 16, 16, 31}},
            {'M', {17, 27, 21, 21, 17, 17, 17}},
            {'N', {17, 25, 25, 21, 19, 19, 17}},
            {'O', {14, 17, 17, 17, 17, 17, 14}},
            {'P', {30, 17, 17, 30, 16, 16, 16}},
            {'Q', {14, 17, 17, 17, 21, 18, 13}},
            {'R', {30, 17, 17, 30, 20, 18, 17}},
            {'S', {15, 16, 16, 14, 1, 1, 30}},
            {'T', {31, 4, 4, 4, 4, 4, 4}},
            {'U', {17, 17, 17, 17, 17, 17, 14}},
            {'V', {17, 17, 17, 17, 17, 10, 4}},
            {'W', {17, 17, 17, 21, 21, 21, 10}},
            {'X', {17, 17, 10, 4, 10, 17, 17}},
            {'Y', {17, 17, 10, 4, 4, 4, 4}},
            {'Z', {31, 1, 2, 4, 8, 16, 31}},
            {'.', {0, 0, 0, 0, 0, 12, 12}},
            {':', {0, 12, 12, 0, 12, 12, 0}},
            {'/', {1, 2, 4, 8, 16, 0, 0}},
            {'%', {17, 2, 4, 8, 17, 0, 0}},
            {'-', {0, 0, 0, 31, 0, 0, 0}},
            {'+', {0, 4, 4, 31, 4, 4, 0}},
        };
        static const std::array<std::uint8_t, 7> blank{0, 0, 0, 0, 0, 0, 0};
        const auto found = font.find(character);
        return found == font.end() ? blank : found->second;
    }

    static Pixel blend(Pixel background, Pixel foreground, int alpha) {
        const int inverse = 255 - alpha;
        const int red = (((background >> 16) & 0xFF) * inverse +
                         ((foreground >> 16) & 0xFF) * alpha) / 255;
        const int green = (((background >> 8) & 0xFF) * inverse +
                           ((foreground >> 8) & 0xFF) * alpha) / 255;
        const int blue = ((background & 0xFF) * inverse +
                          (foreground & 0xFF) * alpha) / 255;
        return rgb(red, green, blue);
    }

    void fillRect(int x, int y, int width, int height, Pixel color) {
        const int left = std::clamp(x, 0, kWidth);
        const int top = std::clamp(y, 0, kHeight);
        const int right = std::clamp(x + width, 0, kWidth);
        const int bottom = std::clamp(y + height, 0, kHeight);
        for (int row = top; row < bottom; ++row) {
            const auto start = pixels_.begin() +
                static_cast<std::ptrdiff_t>(row) * kWidth + left;
            std::fill(start, start + (right - left), color);
        }
    }

    void fillCircle(int centerX, int centerY, int radius, Pixel color) {
        for (int y = -radius; y <= radius; ++y) {
            const int remaining = radius * radius - y * y;
            const int halfWidth = static_cast<int>(std::sqrt(remaining));
            fillRect(centerX - halfWidth, centerY + y, 2 * halfWidth + 1, 1, color);
        }
    }

    void drawText(std::string text, int x, int y, Pixel color, int scale = 1) {
        for (char& character : text) {
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - 'a' + 'A');
            }
        }
        int penX = x;
        for (const char character : text) {
            const auto& rows = glyph(character);
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if ((rows[row] & (1u << (4 - column))) != 0) {
                        fillRect(penX + column * scale, y + row * scale,
                                 scale, scale, color);
                    }
                }
            }
            penX += 6 * scale;
        }
    }

    ViewPoint transform(Vec3 point) const {
        const double heading = heading_ * 3.14159265358979323846 / 180.0;
        const double pitch = pitch_ * 3.14159265358979323846 / 180.0;
        const double cosHeading = std::cos(heading);
        const double sinHeading = std::sin(heading);
        const double cosPitch = std::cos(pitch);
        const double sinPitch = std::sin(pitch);

        const double x = point.x * cosHeading - point.y * sinHeading;
        const double horizontalY = point.x * sinHeading + point.y * cosHeading;
        const double y = horizontalY * cosPitch - point.z * sinPitch;
        const double z = horizontalY * sinPitch + point.z * cosPitch;
        const double depth = distance_ - z;
        const double focalLength = 410.0;
        return {
            272.0 + focalLength * x / std::max(depth, kNearPlane),
            240.0 - focalLength * y / std::max(depth, kNearPlane),
            depth,
        };
    }

    Vec3 transformDirection(Vec3 direction) const {
        const double heading = heading_ * 3.14159265358979323846 / 180.0;
        const double pitch = pitch_ * 3.14159265358979323846 / 180.0;
        const double x = direction.x * std::cos(heading) -
                         direction.y * std::sin(heading);
        const double horizontalY = direction.x * std::sin(heading) +
                                   direction.y * std::cos(heading);
        return normalized({
            x,
            horizontalY * std::cos(pitch) - direction.z * std::sin(pitch),
            horizontalY * std::sin(pitch) + direction.z * std::cos(pitch),
        });
    }

    static double edge(double ax, double ay, double bx, double by,
                       double px, double py) {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    }

    void rasterTriangle(const std::array<ViewPoint, 3>& points, Pixel color,
                        int alpha, bool useDepth) {
        if (points[0].depth <= kNearPlane || points[1].depth <= kNearPlane ||
            points[2].depth <= kNearPlane) {
            return;
        }
        const double area = edge(points[0].x, points[0].y,
                                 points[1].x, points[1].y,
                                 points[2].x, points[2].y);
        if (std::abs(area) < 1e-8) {
            return;
        }
        const int minX = std::clamp(static_cast<int>(std::floor(
            std::min({points[0].x, points[1].x, points[2].x}))), 0, kPanelX - 1);
        const int maxX = std::clamp(static_cast<int>(std::ceil(
            std::max({points[0].x, points[1].x, points[2].x}))), 0, kPanelX - 1);
        const int minY = std::clamp(static_cast<int>(std::floor(
            std::min({points[0].y, points[1].y, points[2].y}))), 0, kHeight - 1);
        const int maxY = std::clamp(static_cast<int>(std::ceil(
            std::max({points[0].y, points[1].y, points[2].y}))), 0, kHeight - 1);

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const double px = x + 0.5;
                const double py = y + 0.5;
                const double w0 = edge(points[1].x, points[1].y,
                                       points[2].x, points[2].y, px, py) / area;
                const double w1 = edge(points[2].x, points[2].y,
                                       points[0].x, points[0].y, px, py) / area;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) {
                    continue;
                }
                const double depth = w0 * points[0].depth +
                                     w1 * points[1].depth +
                                     w2 * points[2].depth;
                const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
                if (useDepth) {
                    if (depth >= depthBuffer_[index]) {
                        continue;
                    }
                    depthBuffer_[index] = depth;
                    pixels_[index] = color;
                } else {
                    pixels_[index] = blend(pixels_[index], color, alpha);
                }
            }
        }
    }

    Pixel temperatureColor(double temperature) const {
        const double amount = std::clamp((temperature - 20.0) / 60.0, 0.0, 1.0);
        if (amount < 0.5) {
            const double fraction = amount * 2.0;
            return rgb(0, static_cast<int>(255 * fraction),
                       static_cast<int>(255 * (1.0 - fraction)));
        }
        const double fraction = (amount - 0.5) * 2.0;
        return rgb(static_cast<int>(255 * fraction),
                   static_cast<int>(255 * (1.0 - fraction)), 0);
    }

    void drawModel() {
        std::fill(depthBuffer_.begin(), depthBuffer_.end(),
                  std::numeric_limits<double>::infinity());
        struct ProjectedTriangle {
            std::array<ViewPoint, 3> points;
            double averageDepth;
            Pixel color;
        };
        std::vector<ProjectedTriangle> projected;
        projected.reserve(mesh_.triangles.size());
        const Vec3 light = normalized({-0.35, -0.45, 0.82});

        for (const Triangle& triangle : mesh_.triangles) {
            std::array<ViewPoint, 3> points{
                transform(triangle.a), transform(triangle.b), transform(triangle.c)};
            if (points[0].depth <= kNearPlane && points[1].depth <= kNearPlane &&
                points[2].depth <= kNearPlane) {
                continue;
            }
            const Vec3 normal = transformDirection(triangle.normal);
            const double brightness = 0.38 + 0.62 * std::abs(dot(normal, light));
            const Pixel color = rgb(static_cast<int>(154 * brightness),
                                    static_cast<int>(177 * brightness),
                                    static_cast<int>(202 * brightness));
            const double averageDepth =
                (points[0].depth + points[1].depth + points[2].depth) / 3.0;
            projected.push_back({points, averageDepth, color});
        }

        const bool opaque = transparency_ <= 0.001;
        if (!opaque) {
            std::sort(projected.begin(), projected.end(),
                      [](const ProjectedTriangle& a, const ProjectedTriangle& b) {
                          return a.averageDepth > b.averageDepth;
                      });
        }
        const int alpha = static_cast<int>(255.0 * (1.0 - transparency_));
        for (const ProjectedTriangle& triangle : projected) {
            rasterTriangle(triangle.points, triangle.color, alpha, opaque);
        }
    }

    void drawSensors(const std::vector<Sensor>& sensors) {
        for (const Sensor& sensor : sensors) {
            const ViewPoint point = transform(sensor.position);
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                point.depth <= kNearPlane || point.x < -6.0 ||
                point.x >= kPanelX + 6.0 || point.y < -6.0 ||
                point.y >= kHeight + 6.0) {
                continue;
            }
            const int x = static_cast<int>(std::lround(point.x));
            const int y = static_cast<int>(std::lround(point.y));
            if (x < 0 || x >= kPanelX || y < 0 || y >= kHeight) {
                continue;
            }
            fillCircle(x, y, 6, rgb(242, 246, 250));
            const Pixel markerColor = sensor.hasCurrentReading
                ? temperatureColor(sensor.temperature)
                : rgb(116, 132, 151);
            fillCircle(x, y, 4, markerColor);
        }
    }

    void drawPanel(const std::vector<Sensor>& sensors) {
        const Pixel panel = rgb(19, 25, 34);
        const Pixel border = rgb(54, 67, 84);
        const Pixel text = rgb(225, 232, 241);
        const Pixel muted = rgb(155, 170, 188);
        fillRect(kPanelX, 0, kPanelWidth, kHeight, panel);
        fillRect(kPanelX, 0, 1, kHeight, border);
        drawText("BGA VIEWER", kPanelX + 20, 16, text, 2);
        drawText("MODEL TRANSPARENCY", kPanelX + 20, 49, muted);

        const int percent = static_cast<int>(std::lround(transparency_ * 100.0));
        drawText(std::to_string(percent) + "%", kPanelX + 205, 48, text);
        fillRect(kSliderX, kSliderY, kSliderWidth, 8, rgb(58, 72, 90));
        const int knobX = kSliderX + static_cast<int>(std::lround(
            (transparency_ / 0.95) * kSliderWidth));
        fillCircle(knobX, kSliderY + 4, 8, rgb(91, 183, 240));

        fillRect(kPanelX + 14, 103, kPanelWidth - 28, 1, border);
        drawText("SENSORS: " + std::to_string(sensors.size()),
                 kPanelX + 20, 115, muted);
        drawText("ID", kPanelX + 20, 151, muted);
        drawText("TEMP C", kPanelX + 158, 151, muted);
        fillRect(kPanelX + 14, 166, kPanelWidth - 28, 1, border);

        constexpr int rowHeight = 22;
        const int tableTop = 174;
        const int visibleRows = (kHeight - tableTop - 24) / rowHeight;
        const int maximumScroll = std::max(
            0, static_cast<int>(sensors.size()) - visibleRows);
        tableScroll_ = std::clamp(tableScroll_, 0, maximumScroll);
        for (int row = 0; row < visibleRows; ++row) {
            const int index = tableScroll_ + row;
            if (index >= static_cast<int>(sensors.size())) {
                break;
            }
            const Sensor& sensor = sensors[static_cast<std::size_t>(index)];
            const int y = tableTop + row * rowHeight;
            if (row % 2 == 0) {
                fillRect(kPanelX + 12, y, kPanelWidth - 24, rowHeight,
                         rgb(27, 35, 47));
            }
            drawText(sensor.id.substr(0, 14), kPanelX + 20, y + 7, text);
            if (sensor.hasCurrentReading) {
                std::ostringstream temperature;
                temperature << std::fixed << std::setprecision(1)
                            << sensor.temperature;
                drawText(temperature.str(), kPanelX + 158, y + 7,
                         temperatureColor(sensor.temperature));
            } else {
                drawText("N/A", kPanelX + 158, y + 7, muted);
            }
        }
        drawText("PGUP PGDN SCROLL", kPanelX + 20, 462, muted);
    }

    void drawFrame() {
        std::fill(pixels_.begin(), pixels_.end(), rgb(25, 31, 41));
        const std::vector<Sensor> sensors = sensors_.snapshot();
        drawModel();
        drawSensors(sensors);
        drawPanel(sensors);
        void* texturePixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(texture_, nullptr, &texturePixels, &pitch) != 0) {
            throw std::runtime_error(std::string("SDL texture lock error: ") +
                                     SDL_GetError());
        }
        const auto* source = reinterpret_cast<const unsigned char*>(pixels_.data());
        auto* destination = static_cast<unsigned char*>(texturePixels);
        const std::size_t rowBytes = kWidth * sizeof(Pixel);
        for (int row = 0; row < kHeight; ++row) {
            std::memcpy(destination + static_cast<std::size_t>(row) * pitch,
                        source + static_cast<std::size_t>(row) * rowBytes,
                        rowBytes);
        }
        SDL_UnlockTexture(texture_);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

    void setTransparency(int mouseX) {
        const double relative = static_cast<double>(mouseX - kSliderX) /
                                static_cast<double>(kSliderWidth);
        transparency_ = std::clamp(relative * 0.95, 0.0, 0.95);
    }

    void changeZoom(double factor) {
        distance_ = std::clamp(distance_ * factor, 7.0, 60.0);
    }

    bool handleEvent(const SDL_Event& event) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            const int x = event.button.x;
            const int y = event.button.y;
            if (x >= kSliderX - 10 && x <= kSliderX + kSliderWidth + 10 &&
                y >= kSliderY - 10 && y <= kSliderY + 18) {
                changingTransparency_ = true;
                setTransparency(x);
            } else if (x < kPanelX) {
                rotating_ = true;
                lastMouseX_ = x;
                lastMouseY_ = y;
            }
        } else if (event.type == SDL_MOUSEBUTTONUP &&
                   event.button.button == SDL_BUTTON_LEFT) {
            rotating_ = false;
            changingTransparency_ = false;
        } else if (event.type == SDL_MOUSEMOTION) {
            if (changingTransparency_) {
                setTransparency(event.motion.x);
            } else if (rotating_) {
                heading_ -= (event.motion.x - lastMouseX_) * 0.6;
                pitch_ = std::clamp(
                    pitch_ + (event.motion.y - lastMouseY_) * 0.6, -85.0, 85.0);
                lastMouseX_ = event.motion.x;
                lastMouseY_ = event.motion.y;
            }
        } else if (event.type == SDL_MOUSEWHEEL) {
            int x = 0;
            int y = 0;
            SDL_GetMouseState(&x, &y);
            if (x >= kPanelX && y >= 174 && y < kHeight - 18) {
                tableScroll_ -= event.wheel.y * 3;
            } else if (event.wheel.y > 0) {
                if (x < kPanelX) changeZoom(0.9);
            } else if (event.wheel.y < 0) {
                if (x < kPanelX) changeZoom(1.1);
            }
        } else if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    return false;
                case SDLK_PAGEUP:
                    tableScroll_ -= 8;
                    break;
                case SDLK_PAGEDOWN:
                    tableScroll_ += 8;
                    break;
                case SDLK_PLUS:
                case SDLK_KP_PLUS:
                case SDLK_EQUALS:
                    changeZoom(0.9);
                    break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS:
                    changeZoom(1.1);
                    break;
                default:
                    break;
            }
        }
        return true;
    }
};

struct ProgramOptions {
    std::string modelPath;
    std::string sensorCsvPath;
    std::string canInterface = "can0";
    bool canEnabled = true;
};

ProgramOptions parseArguments(int argc, char* argv[]) {
    ProgramOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--no-can") {
            options.canEnabled = false;
        } else if (argument == "--can-interface") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--can-interface requires an interface name.");
            }
            options.canInterface = argv[++index];
        } else if (argument.rfind("--can-interface=", 0) == 0) {
            options.canInterface = argument.substr(std::string("--can-interface=").size());
        } else if (argument.rfind("--", 0) == 0) {
            throw std::runtime_error("Unknown option: " + argument);
        } else if (options.modelPath.empty()) {
            options.modelPath = argument;
        } else if (options.sensorCsvPath.empty()) {
            options.sensorCsvPath = argument;
        } else {
            throw std::runtime_error("Too many positional arguments.");
        }
    }
    if (options.canInterface.empty()) {
        throw std::runtime_error("CAN interface name cannot be empty.");
    }
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    ProgramOptions options;
    try {
        options = parseArguments(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n'
                  << "Usage: bga_visualizer [model.stl] [sensors.csv] "
                     "[--can-interface NAME] [--no-can]\n";
        return 2;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL initialization error: " << SDL_GetError() << '\n';
        return 1;
    }

    try {
        Mesh mesh;
        if (!options.modelPath.empty()) {
            mesh = loadStl(options.modelPath);
        } else {
            const std::vector<unsigned char> modelBytes(
                embedded_assets::model.begin(), embedded_assets::model.end());
            mesh = parseStl(modelBytes, "embedded model.stl");
        }
        const Bounds originalBounds = mesh.bounds;
        Vec3 center;
        double scale = 1.0;
        normalizeMesh(mesh, center, scale);

        std::vector<Sensor> sensors;
        if (!options.sensorCsvPath.empty()) {
            std::ifstream sensorFile(options.sensorCsvPath);
            if (!sensorFile) {
                throw std::runtime_error("Cannot open sensor CSV file: " +
                                         options.sensorCsvPath);
            }
            sensors = loadSensors(sensorFile, options.sensorCsvPath, center, scale,
                                  originalBounds);
        } else {
            const std::string embeddedCsv(embedded_assets::sensors.begin(),
                                          embedded_assets::sensors.end());
            std::istringstream sensorStream(embeddedCsv);
            sensors = loadSensors(sensorStream, "embedded sensors.csv", center,
                                  scale, originalBounds);
        }

        std::cout << "Loaded " << mesh.triangles.size() << " STL triangles and "
                  << sensors.size() << " sensors.\n";

        SensorState sensorState(std::move(sensors));
        CanReceiver canReceiver;
        if (options.canEnabled) {
            std::string canError;
            std::vector<std::uint32_t> sensorCanIds;
            for (const Sensor& sensor : sensorState.snapshot()) {
                std::uint32_t canId = 0;
                if (!parseSensorCanId(sensor.id, canId)) {
                    canError = "Sensor ID '" + sensor.id +
                               "' must be a canonical decimal value from 0 to 2047 "
                               "to receive CAN data.";
                    break;
                }
                sensorCanIds.push_back(canId);
            }
            std::unordered_set<std::string> unmatchedSensorIds;
            const bool receiverStarted = !canError.empty()
                ? false
                : canReceiver.start(
                      options.canInterface, sensorCanIds,
                      [&sensorState, &unmatchedSensorIds](
                          const SensorReading& reading) {
                          if (!sensorState.updateTemperature(reading.sensorId,
                                                             reading.temperatureC) &&
                              unmatchedSensorIds.insert(reading.sensorId).second) {
                              std::cerr << "Ignoring CAN ID " << reading.sensorId
                                        << ": no sensor with that ID exists in the CSV.\n";
                          }
                      },
                      canError);
            if (!receiverStarted) {
                std::cerr << "Warning: " << canError
                          << " The visualizer will continue without CAN data.\n";
            } else {
                std::cout << "Listening for CAN data on " << options.canInterface
                          << ".\n";
            }
        } else {
            std::cout << "CAN reception disabled (--no-can).\n";
        }

        Viewer viewer(std::move(mesh), sensorState);
        return viewer.run();
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
