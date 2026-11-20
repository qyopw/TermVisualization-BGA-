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
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

// Returns the executable directory using the platform API.
inline fs::path getExeDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return fs::current_path();
    }
    return fs::path(buffer).parent_path();
#else
    char* basePath = SDL_GetBasePath();
    if (basePath != nullptr) {
        const fs::path executableDirectory(basePath);
        SDL_free(basePath);
        return executableDirectory;
    }
    return fs::current_path();
#endif
}

// Finds the project root from the executable or working directory.
inline fs::path getProjectRootDir() {
    // Walks upward until the source-tree marker is found.
    const auto findRoot = [](fs::path directory) {
        std::error_code error;
        while (!directory.empty()) {
            if (fs::exists(directory / "CMakeLists.txt", error) && !error) {
                return directory;
            }
            error.clear();
            const fs::path parent = directory.parent_path();
            if (parent == directory) {
                break;
            }
            directory = parent;
        }
        return fs::path{};
    };

    const fs::path executableDirectory = getExeDir();
    fs::path root = findRoot(executableDirectory);
    if (!root.empty()) {
        return root;
    }

    std::error_code error;
    const fs::path workingDirectory = fs::current_path(error);
    if (!error) {
        root = findRoot(workingDirectory);
        if (!root.empty()) {
            return root;
        }
    }
    return executableDirectory.empty() ? fs::path(".") : executableDirectory;
}

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr int kPanelX = 544;
constexpr int kPanelWidth = kWidth - kPanelX;
constexpr double kModelSize = 10.0;
constexpr double kNearPlane = 0.1;

// Bottom control panel (lower third of the model area).
constexpr int kBottomPanelY = 340;
constexpr int kBottomPanelHeight = kHeight - kBottomPanelY;
constexpr int kGroupLabelY = kBottomPanelY + 10;
constexpr int kGroupCheckboxY = kBottomPanelY + 26;
constexpr int kGroupCheckboxSize = 16;
constexpr int kGroupCheckboxSpacingX = 22;
constexpr int kGroupCheckboxSpacingY = 20;
constexpr int kGroupStartX = 8;
constexpr int kModelTransparencyY = kBottomPanelY + 36;
constexpr int kModelZOffsetY = kBottomPanelY + 86;
constexpr int kBottomSliderX = 167;
constexpr int kBottomSliderWidth = 340;

// Auto-perspective button (top-left corner, on top of the viewport).
constexpr int kAutoPerspButtonX = 10;
constexpr int kAutoPerspButtonY = 10;
constexpr int kAutoPerspButtonW = 110;
constexpr int kAutoPerspButtonH = 24;

using Pixel = std::uint32_t;

// Packs RGB channels into the renderer's opaque ARGB format.
constexpr Pixel rgb(int red, int green, int blue) {
    return 0xFF000000u |
           (static_cast<Pixel>(red & 0xFF) << 16) |
           (static_cast<Pixel>(green & 0xFF) << 8) |
           static_cast<Pixel>(blue & 0xFF);
}

// Adds two three-dimensional vectors component-wise.
Vec3 operator+(Vec3 left, Vec3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

// Subtracts one three-dimensional vector from another.
Vec3 operator-(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

// Scales a three-dimensional vector by a scalar.
Vec3 operator*(Vec3 point, double amount) {
    return {point.x * amount, point.y * amount, point.z * amount};
}

// Computes the cross product of two vectors.
Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

// Computes the dot product of two vectors.
double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Returns a unit vector, or zero for a near-zero input.
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

// Expands a bounding box to include one point.
void include(Bounds& bounds, Vec3 point) {
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
    bounds.maximum.z = std::max(bounds.maximum.z, point.z);
}

// Creates a triangle and derives its surface normal.
Triangle makeTriangle(Vec3 a, Vec3 b, Vec3 c) {
    return {a, b, c, normalized(cross(b - a, c - a))};
}

// Reads one IEEE-754 float stored in a binary STL's Little Endian layout.
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

// Parses binary or ASCII STL data and computes mesh bounds.
Mesh parseStl(const std::vector<unsigned char>& bytes, const std::string& source) {
    if (bytes.size() < 5) {
        throw std::runtime_error("STL file is empty or too short: " + source);
    }

    Mesh mesh;
    // Validates every vertex and updates bounds before adding a triangle.
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

// Reads an STL file and delegates format parsing to parseStl.
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

// Centers and scales the mesh while updating its triangle normals.
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

// Removes surrounding whitespace from a string.
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// Splits one CSV row while honoring quoted fields and escaped quotes.
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

// Converts ASCII letters to lowercase for case-insensitive headers.
std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

// Parses a finite numeric CSV/config value with a useful error message.
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

// Loads sensor IDs, coordinates, and optional display groups from CSV.
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
    const bool hasKernelColumn = columns.find("kernel") != columns.end();

    for (const std::string required : {"id", "x", "y", "z"}) {
        if (columns.find(required) == columns.end()) {
            throw std::runtime_error(
                "CSV must contain columns id,x,y,z.");
        }
    }

    std::vector<Sensor> sensors;
    std::size_t outsideModelBounds = 0;
    const Vec3 modelExtent = modelBounds.maximum - modelBounds.minimum;
    const Vec3 centeredMinimum = modelBounds.minimum - center;
    const Vec3 centeredMaximum = modelBounds.maximum - center;
    const double boundsMargin =
        0.02 * std::max({modelExtent.x, modelExtent.y, modelExtent.z});
    std::size_t rowNumber = 1;
    while (std::getline(input, line)) {
        ++rowNumber;
        if (trim(line).empty()) {
            continue;
        }
        const std::vector<std::string> fields = parseCsvLine(line);
        // Пропускаем строки, состоящие только из разделителей (например "...,,,"),
        // которые могут встречаться в конце CSV-файла.
        bool allEmpty = true;
        for (const auto& f : fields) {
            if (!f.empty()) {
                allEmpty = false;
                break;
            }
        }
        if (allEmpty) {
            continue;
        }
        // Looks up one named CSV field and reports short rows clearly.
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
        if (point.x < centeredMinimum.x - boundsMargin ||
            point.x > centeredMaximum.x + boundsMargin ||
            point.y < centeredMinimum.y - boundsMargin ||
            point.y > centeredMaximum.y + boundsMargin ||
            point.z < centeredMinimum.z - boundsMargin ||
            point.z > centeredMaximum.z + boundsMargin) {
            ++outsideModelBounds;
        }
        if (id.empty()) {
            throw std::runtime_error("CSV row " + std::to_string(rowNumber) +
                                     " has an empty sensor ID.");
        }

        int group = -1;
        if (hasKernelColumn) {
            const double kernel = parseNumber(field("kernel"), "kernel", id);
            if (std::trunc(kernel) != kernel || kernel < -1.0 || kernel > 34.0) {
                throw std::runtime_error(
                    "Invalid kernel for sensor " + id +
                    ": expected an integer from -1 to 34.");
            }
            group = static_cast<int>(kernel);
        }
        sensors.push_back({id, point * scale, group});
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
    // Loads viewer settings and creates all SDL resources.
    Viewer(Mesh mesh, SensorState& sensors, bool canEnabled)
        : mesh_(std::move(mesh)), sensors_(sensors), canEnabled_(canEnabled),
          pixels_(static_cast<std::size_t>(kWidth) * kHeight),
          depthBuffer_(static_cast<std::size_t>(kWidth) * kHeight,
                       std::numeric_limits<double>::infinity()) {
        try {
            groupVisible_.fill(true);
            groupExpanded_.fill(true);
            loadConfig();
            window_ = SDL_CreateWindow(
                "BGA Temperature Visualizer", SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED, kWidth, kHeight,
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
            if (window_ == nullptr) {
                throw std::runtime_error(
                    std::string("SDL window error: ") + SDL_GetError());
            }

            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
            if (renderer_ == nullptr) {
                throw std::runtime_error(
                    std::string("SDL renderer error: ") + SDL_GetError());
            }
            if (SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight) != 0) {
                throw std::runtime_error(
                    std::string("SDL logical size error: ") + SDL_GetError());
            }

            texture_ = SDL_CreateTexture(
                renderer_, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
            if (texture_ == nullptr) {
                throw std::runtime_error(
                    std::string("SDL texture error: ") + SDL_GetError());
            }
            if (SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE) != 0) {
                throw std::runtime_error(
                    std::string("SDL texture blend error: ") + SDL_GetError());
            }
        } catch (...) {
            releaseSdlResources();
            SDL_Quit();
            throw;
        }
    }

    // Keeps SDL resource ownership unique to one Viewer instance.
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;

    // Releases SDL resources after normal shutdown.
    ~Viewer() {
        releaseSdlResources();
        SDL_Quit();
    }

    // Runs the SDL event and rendering loop until the user exits.
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
        saveConfig();
        return 0;
    }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    Mesh mesh_;
    SensorState& sensors_;
    bool canEnabled_ = false;
    std::vector<Pixel> pixels_;
    std::vector<double> depthBuffer_;
    double heading_ = 35.0;
    double pitch_ = 20.0;
    double distance_ = 18.0;
    double autoPerspHeading_ = 70.0;
    double autoPerspPitch_ = -44.5;
    double autoPerspDistance_ = 18.0;
    double transparency_ = 0.0;
    double modelZOffset_ = 0.0;
    bool rotating_ = false;
    bool changingTransparency_ = false;
    bool changingModelZOffset_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    int tableScroll_ = 0;
    std::array<bool, 35> groupVisible_{};
    std::array<bool, 35> groupExpanded_{};
    std::vector<SDL_Rect> groupCheckboxes_;

    struct DisplayEntry {
        bool isGroup = false;
        int groupId = -1;
        std::size_t sensorIndex = 0;
    };
    std::vector<DisplayEntry> displayEntries_;

    // Destroys partially or fully initialized SDL objects safely.
    void releaseSdlResources() noexcept {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
    }

    // Toggles visibility of one valid kernel group.
    void toggleGroupVisibility(int group) {
        if (group >= 0 && group < 35) {
            groupVisible_[group] = !groupVisible_[group];
        }
    }

    // Expands or collapses one valid kernel group.
    void toggleGroupExpanded(int group) {
        if (group >= 0 && group < 35) {
            groupExpanded_[group] = !groupExpanded_[group];
        }
    }

    // Expands every group or collapses every group together.
    void toggleAllGroupsExpanded() {
        bool allExpanded = true;
        for (int g = 0; g < 35; ++g) {
            if (!groupExpanded_[g]) {
                allExpanded = false;
                break;
            }
        }
        for (int g = 0; g < 35; ++g) {
            groupExpanded_[g] = !allExpanded;
        }
    }

    // Loads persisted camera, display, and group settings.
    void loadConfig() {
        const fs::path configPath = getProjectRootDir() / "config.txt";
        std::ifstream file(configPath);
        if (!file) {
            return;  // Конфига нет — используем значения по умолчанию.
        }
        // Loads a numeric setting only when it is finite and in range.
        const auto loadNumber = [](const std::string& key,
                                   const std::string& value, double minimum,
                                   double maximum, double& destination) {
            try {
                const double parsed = parseNumber(value, key, "config");
                if (parsed < minimum || parsed > maximum) {
                    std::cerr << "Warning: Ignoring out-of-range config value for "
                              << key << ".\n";
                    return;
                }
                destination = parsed;
            } catch (const std::exception& error) {
                std::cerr << "Warning: Ignoring invalid config value for "
                          << key << ": " << error.what() << '\n';
            }
        };
        std::string line;
        while (std::getline(file, line)) {
            // Пропускаем пустые строки и комментарии.
            const auto trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') {
                continue;
            }
            const auto eqPos = trimmed.find('=');
            if (eqPos == std::string::npos) {
                continue;
            }
            const std::string key = trim(trimmed.substr(0, eqPos));
            const std::string value = trim(trimmed.substr(eqPos + 1));

            if (key == "auto-perspective-heading") {
                loadNumber(key, value, -360.0, 360.0, autoPerspHeading_);
            } else if (key == "auto-perspective-pitch") {
                loadNumber(key, value, -85.0, 85.0, autoPerspPitch_);
            } else if (key == "auto-perspective-distance") {
                loadNumber(key, value, 7.0, 60.0, autoPerspDistance_);
            } else if (key == "model-transparency") {
                loadNumber(key, value, 0.0, 0.95, transparency_);
            } else if (key == "model-z-offset") {
                loadNumber(key, value, -10.0, 10.0, modelZOffset_);
            } else if (key == "disabled-kernels") {
                parseDisabledKernels(value);
            }
        }
    }

    // Saves the current settings to the project config file.
    void saveConfig() {
        const fs::path configPath = getProjectRootDir() / "config.txt";
        std::ofstream file(configPath);
        if (!file) {
            return;
        }
        file << "# Конфигурация приложения TermVisualization-BGA\n";
        file << "# Строки, начинающиеся с #, являются комментариями.\n\n";
        file << "# 1. Параметры запуска программы\n";
        file << "# no-can=1  - отключить приём CAN-данных\n";
        file << "# forced-can=1 - принудительно включить приём CAN-данных\n";
        file << "no-can=" << (canEnabled_ ? 0 : 1) << "\n\n";
        file << "# 2. Углы просмотра для кнопки \"AUTO PERSPECTIVE\" (в градусах)\n";
        file << "auto-perspective-heading=" << autoPerspHeading_ << "\n";
        file << "auto-perspective-pitch=" << autoPerspPitch_ << "\n";
        file << "auto-perspective-distance=" << autoPerspDistance_ << "\n\n";
        file << "# 3. Начальные значения ползунков (0.0 .. 0.95)\n";
        file << "model-transparency=" << transparency_ << "\n";
        file << "model-z-offset=" << modelZOffset_ << "\n\n";
        file << "# 4. Список выключенных kernel (номера от 0 до 34, через запятую).\n";
        file << "# Пустой список означает, что все kernel включены.\n";
        file << "disabled-kernels=";
        bool first = true;
        for (int g = 0; g < 35; ++g) {
            if (!groupVisible_[g]) {
                if (!first) {
                    file << ",";
                }
                file << g;
                first = false;
            }
        }
        file << "\n";
    }

    // Parses the comma-separated list of hidden groups.
    void parseDisabledKernels(const std::string& value) {
        std::string trimmed = trim(value);
        if (trimmed.empty()) {
            return;
        }
        std::size_t start = 0;
        while (start < trimmed.size()) {
            const auto comma = trimmed.find(',', start);
            const std::string token =
                (comma == std::string::npos) ? trimmed.substr(start)
                                             : trimmed.substr(start, comma - start);
            const auto tkn = trim(token);
            if (!tkn.empty()) {
                try {
                    const int kernel = std::stoi(tkn);
                    if (kernel >= 0 && kernel < 35) {
                        groupVisible_[kernel] = false;
                    }
                } catch (...) {
                    // Игнорируем некорректные значения.
                }
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
    }

    // Returns the seven-row bitmap for one supported character.
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

    // Blends foreground color over background using an 8-bit alpha value.
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

    // Fills a clipped rectangle in the CPU pixel buffer.
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

    // Draws a filled circle using clipped horizontal spans.
    void fillCircle(int centerX, int centerY, int radius, Pixel color) {
        for (int y = -radius; y <= radius; ++y) {
            const int remaining = radius * radius - y * y;
            const int halfWidth = static_cast<int>(std::sqrt(remaining));
            fillRect(centerX - halfWidth, centerY + y, 2 * halfWidth + 1, 1, color);
        }
    }

    // Draws text from the built-in bitmap font.
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

    // Rotates and projects a model point into screen coordinates.
    ViewPoint transform(Vec3 point) const {
        const double heading = heading_ * 3.14159265358979323846 / 180.0;
        const double pitch = pitch_ * 3.14159265358979323846 / 180.0;
        const double cosHeading = std::cos(heading);
        const double sinHeading = std::sin(heading);
        const double cosPitch = std::cos(pitch);
        const double sinPitch = std::sin(pitch);

        const double x = point.x * cosHeading - point.y * sinHeading;
        const double horizontalY = point.x * sinHeading + point.y * cosHeading;
        const double zPoint = point.z + modelZOffset_;
        const double y = horizontalY * cosPitch - zPoint * sinPitch;
        const double z = horizontalY * sinPitch + zPoint * cosPitch;
        const double depth = distance_ - z;
        const double focalLength = 410.0;
        return {
            272.0 + focalLength * x / std::max(depth, kNearPlane),
            220.0 - focalLength * y / std::max(depth, kNearPlane),
            depth,
        };
    }

    // Rotates a direction vector without applying translation.
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

    // Computes the signed edge function used for triangle rasterization.
    static double edge(double ax, double ay, double bx, double by,
                       double px, double py) {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    }

    // Rasterizes one projected triangle with optional depth testing.
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

    // Maps a temperature to the viewer's existing heat-map colors.
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

    // Projects and rasterizes the complete STL mesh.
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
            // Sorts back-to-front so translucent faces blend in draw order.
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

    // Draws visible sensor markers from a thread-safe state snapshot.
    void drawSensors(const std::vector<Sensor>& sensors) {
        for (const Sensor& sensor : sensors) {
            if (sensor.group >= 0) {
                if (sensor.group >= static_cast<int>(groupVisible_.size()) ||
                    !groupVisible_[static_cast<std::size_t>(sensor.group)]) {
                    continue;
                }
            }
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
            const Pixel markerColor = sensor.hasCurrentReading
                ? temperatureColor(sensor.temperature)
                : rgb(116, 132, 151);
            fillCircle(x, y, 4, rgb(242, 246, 250));
            fillCircle(x, y, 3, markerColor);
        }
    }

    // Builds and draws the sensor list and its group headers.
    void drawPanel(const std::vector<Sensor>& sensors) {
        const Pixel panel = rgb(19, 25, 34);
        const Pixel border = rgb(54, 67, 84);
        const Pixel text = rgb(225, 232, 241);
        const Pixel muted = rgb(155, 170, 188);
        fillRect(kPanelX, 0, kPanelWidth, kHeight, panel);
        fillRect(kPanelX, 0, 1, kHeight, border);
        drawText("BGA VIEWER", kPanelX + 20, 16, text, 2);

        // Collapse all kernels button at the top of the list.
        const SDL_Rect collapseAllButton{kPanelX + 20, 42, 170, 18};
        const Pixel buttonColor = rgb(58, 72, 90);
        fillRect(collapseAllButton.x, collapseAllButton.y,
                 collapseAllButton.w, collapseAllButton.h, buttonColor);
        drawText("COLLAPSE ALL KERNELS", collapseAllButton.x + 6,
                 collapseAllButton.y + 4, text);

        fillRect(kPanelX + 14, 66, kPanelWidth - 28, 1, border);
        drawText("ID", kPanelX + 20, 78, muted);
        drawText("TEMP C", kPanelX + 158, 78, muted);
        // Build flat list of display entries: group headers + sensors.
        displayEntries_.clear();
        for (int g = 0; g < 35; ++g) {
            bool hasSensorsInGroup = false;
            for (const auto& s : sensors) {
                if (s.group == g) {
                    hasSensorsInGroup = true;
                    break;
                }
            }
            if (!hasSensorsInGroup) {
                continue;
            }
            displayEntries_.push_back({true, g, 0});
            if (groupExpanded_[g]) {
                for (std::size_t i = 0; i < sensors.size(); ++i) {
                    if (sensors[i].group == g) {
                        displayEntries_.push_back({false, g, i});
                    }
                }
            }
        }
        // Ungrouped sensors (group == -1).
        bool hasUngrouped = false;
        for (const auto& s : sensors) {
            if (s.group < 0) {
                hasUngrouped = true;
                break;
            }
        }
        if (hasUngrouped) {
            displayEntries_.push_back({true, -1, 0});
            for (std::size_t i = 0; i < sensors.size(); ++i) {
                if (sensors[i].group < 0) {
                    displayEntries_.push_back({false, -1, i});
                }
            }
        }

        constexpr int rowHeight = 20;
        const int tableTop = 102;
        const int visibleRows = (kHeight - 30 - tableTop) / rowHeight;
        const int maximumScroll = std::max(
            0, static_cast<int>(displayEntries_.size()) - visibleRows);
        tableScroll_ = std::clamp(tableScroll_, 0, maximumScroll);
        for (int row = 0; row < visibleRows; ++row) {
            const int index = tableScroll_ + row;
            if (index >= static_cast<int>(displayEntries_.size())) {
                break;
            }
            const DisplayEntry& entry =
                displayEntries_[static_cast<std::size_t>(index)];
            const int y = tableTop + row * rowHeight;
            if (row % 2 == 0) {
                fillRect(kPanelX + 12, y, kPanelWidth - 24, rowHeight,
                         rgb(27, 35, 47));
            }
            if (entry.isGroup) {
                const bool groupValid = entry.groupId >= 0 && entry.groupId < 35;
                const char arrow = !groupValid || groupExpanded_[entry.groupId]
                    ? '+'
                    : '-';
                drawText(std::string(1, arrow) + " Kernel " +
                             std::to_string(entry.groupId),
                         kPanelX + 20, y + 5, text);
            } else {
                const Sensor& sensor =
                    sensors[entry.sensorIndex];
                drawText("----" + sensor.id.substr(0, 10), kPanelX + 28,
                         y + 5, text);
                if (sensor.hasCurrentReading) {
                    std::ostringstream temperature;
                    temperature << std::fixed << std::setprecision(1)
                                << sensor.temperature;
                    drawText(temperature.str(), kPanelX + 158, y + 5,
                             temperatureColor(sensor.temperature));
                } else {
                    drawText("N/A", kPanelX + 158, y + 5, muted);
                }
            }
        }
        drawText("Use PageUp/Down to Scroll", kPanelX + 20, kHeight - 24,
                 muted);
        drawText("sensors: " + std::to_string(sensors.size()),
                 kPanelX + kPanelWidth - 110, kHeight - 8, muted);
    }

    // Draws kernel visibility controls and model adjustment sliders.
    void drawBottomControls() {
        const Pixel panel = rgb(19, 25, 34);
        const Pixel border = rgb(54, 67, 84);
        const Pixel text = rgb(225, 232, 241);
        const Pixel muted = rgb(155, 170, 188);

        // Bottom panel background.
        fillRect(0, kBottomPanelY, kPanelX, kBottomPanelHeight, panel);
        fillRect(0, kBottomPanelY, kPanelX, 1, border);

        // KERNEL label.
        drawText("KERNEL", 10, kGroupLabelY, muted);

        // Group visibility checkboxes: 7 columns x 5 rows (35 groups).
        groupCheckboxes_.clear();
        for (int g = 0; g < 35; ++g) {
            const int col = g % 7;
            const int row = g / 7;
            SDL_Rect rect{
                kGroupStartX + col * kGroupCheckboxSpacingX,
                kGroupCheckboxY + row * kGroupCheckboxSpacingY,
                kGroupCheckboxSize,
                kGroupCheckboxSize,
            };
            groupCheckboxes_.push_back(rect);

            const Pixel checkColor = groupVisible_[g]
                ? rgb(91, 183, 240)
                : rgb(58, 72, 90);
            fillRect(rect.x, rect.y, rect.w, rect.h, checkColor);
            fillRect(rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6,
                     rgb(19, 25, 34));
            drawText(std::to_string(g), rect.x + 5, rect.y + 3, text);
        }

        // MODEL TRANSPARENCY slider.
        drawText("MODEL TRANSPARENCY", kBottomSliderX, kModelTransparencyY,
                 muted);
        const int modelPercent =
            static_cast<int>(std::lround(transparency_ * 100.0));
        drawText(std::to_string(modelPercent) + "%",
                 kBottomSliderX + kBottomSliderWidth + 10,
                 kModelTransparencyY, text);
        const SDL_Rect modelSlider{kBottomSliderX, kModelTransparencyY + 14,
                                   kBottomSliderWidth, 12};
        fillRect(modelSlider.x, modelSlider.y,
                 modelSlider.w, modelSlider.h,
                 rgb(58, 72, 90));
        const int modelKnobX = kBottomSliderX +
            static_cast<int>(std::lround(
                (transparency_ / 0.95) * kBottomSliderWidth));
        fillCircle(modelKnobX, modelSlider.y + 6, 7,
                   rgb(91, 183, 240));

        // MODEL Z OFFSET slider.
        drawText("MODEL Z OFFSET", kBottomSliderX, kModelZOffsetY, muted);
        const int zOffsetPercent =
            static_cast<int>(std::lround((modelZOffset_ + 10.0) / 20.0 * 100.0));
        drawText(std::to_string(zOffsetPercent) + "%",
                 kBottomSliderX + kBottomSliderWidth + 10,
                 kModelZOffsetY, text);
        const SDL_Rect zOffsetSlider{kBottomSliderX, kModelZOffsetY + 14,
                                     kBottomSliderWidth, 12};
        fillRect(zOffsetSlider.x, zOffsetSlider.y,
                 zOffsetSlider.w, zOffsetSlider.h,
                 rgb(58, 72, 90));
        const int zOffsetKnobX = kBottomSliderX +
            static_cast<int>(std::lround(
                ((modelZOffset_ + 10.0) / 20.0) * kBottomSliderWidth));
        fillCircle(zOffsetKnobX, zOffsetSlider.y + 6, 7,
                   rgb(91, 183, 240));
    }

    // Renders one complete frame using SDL's streaming texture.
    void drawFrame() {
        std::fill(pixels_.begin(), pixels_.end(), rgb(25, 31, 41));
        const std::vector<Sensor> sensors = sensors_.snapshot();
        drawModel();
        drawSensors(sensors);
        drawBottomControls();
        drawPanel(sensors);
        drawAutoPerspButton();
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
        if (SDL_RenderClear(renderer_) != 0) {
            throw std::runtime_error(std::string("SDL clear error: ") +
                                     SDL_GetError());
        }
        if (SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0) {
            throw std::runtime_error(std::string("SDL copy error: ") +
                                     SDL_GetError());
        }
        SDL_RenderPresent(renderer_);
    }

    // Sets model transparency from the horizontal slider position.
    void setTransparency(int mouseX) {
        const double relative = static_cast<double>(mouseX - kBottomSliderX) /
                                static_cast<double>(kBottomSliderWidth);
        transparency_ = std::clamp(relative * 0.95, 0.0, 0.95);
    }

    // Sets model vertical offset from the horizontal slider position.
    void setModelZOffset(int mouseX) {
        const double relative = static_cast<double>(mouseX - kBottomSliderX) /
                                static_cast<double>(kBottomSliderWidth);
        modelZOffset_ = std::clamp(relative * 20.0 - 10.0, -10.0, 10.0);
    }

    // Applies the persisted auto-perspective camera values.
    void setAutoPerspective() {
        heading_ = autoPerspHeading_;
        pitch_ = autoPerspPitch_;
        distance_ = autoPerspDistance_;
    }

    // Draws the auto-perspective button over the viewport.
    void drawAutoPerspButton() {
        const SDL_Rect autoPerspButton{kAutoPerspButtonX, kAutoPerspButtonY,
                                       kAutoPerspButtonW, kAutoPerspButtonH};
        const Pixel buttonColor = rgb(58, 72, 90);
        fillRect(autoPerspButton.x, autoPerspButton.y,
                 autoPerspButton.w, autoPerspButton.h, buttonColor);
        fillRect(autoPerspButton.x + 2, autoPerspButton.y + 2,
                 autoPerspButton.w - 4, autoPerspButton.h - 4,
                 rgb(19, 25, 34));
        drawText("AUTO PERSPECTIVE", autoPerspButton.x + 8,
                 autoPerspButton.y + 6, rgb(225, 232, 241));
    }

    // Changes camera distance while keeping the supported zoom range.
    void changeZoom(double factor) {
        distance_ = std::clamp(distance_ * factor, 7.0, 60.0);
    }

    // Handles window, mouse, wheel, and keyboard events.
    bool handleEvent(const SDL_Event& event) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            const int x = event.button.x;
            const int y = event.button.y;
            if (x >= kBottomSliderX - 10 &&
                x <= kBottomSliderX + kBottomSliderWidth + 10 &&
                y >= kModelTransparencyY + 4 && y <= kModelTransparencyY + 22) {
                changingTransparency_ = true;
                setTransparency(x);
            } else if (x >= kBottomSliderX - 10 &&
                x <= kBottomSliderX + kBottomSliderWidth + 10 &&
                y >= kModelZOffsetY + 4 && y <= kModelZOffsetY + 22) {
                changingModelZOffset_ = true;
                setModelZOffset(x);
            } else if (x >= kPanelX && y >= 42 && y < 60) {
                // Collapse all button click.
                toggleAllGroupsExpanded();
            } else if (x >= kPanelX && y >= 102 && y < kHeight - 30) {
                // Group expand/collapse arrow click in table.
                const int row = (y - 102) / 20;
                const int entryIndex = tableScroll_ + row;
                if (entryIndex >= 0 &&
                    entryIndex < static_cast<int>(displayEntries_.size())) {
                    const DisplayEntry& entry =
                        displayEntries_[static_cast<std::size_t>(entryIndex)];
                    if (entry.isGroup) {
                        toggleGroupExpanded(entry.groupId);
                    }
                }
            } else if (y >= kBottomPanelY && y < kHeight && x < kPanelX) {
                // Bottom group checkboxes click (lower left).
                for (int g = 0; g < 35 && g < static_cast<int>(groupCheckboxes_.size()); ++g) {
                    const SDL_Rect& rect = groupCheckboxes_[static_cast<std::size_t>(g)];
                    if (x >= rect.x && x < rect.x + rect.w &&
                        y >= rect.y && y < rect.y + rect.h) {
                        toggleGroupVisibility(g);
                        break;
                    }
                }
            } else if (x >= kAutoPerspButtonX && x < kAutoPerspButtonX + kAutoPerspButtonW &&
                       y >= kAutoPerspButtonY && y < kAutoPerspButtonY + kAutoPerspButtonH) {
                // Auto-perspective button click.
                setAutoPerspective();
            } else if (x < kPanelX) {
                rotating_ = true;
                lastMouseX_ = x;
                lastMouseY_ = y;
            }
        } else if (event.type == SDL_MOUSEBUTTONUP &&
                   event.button.button == SDL_BUTTON_LEFT) {
            rotating_ = false;
            changingTransparency_ = false;
            changingModelZOffset_ = false;
        } else if (event.type == SDL_MOUSEMOTION) {
            if (changingTransparency_) {
                setTransparency(event.motion.x);
            } else if (changingModelZOffset_) {
                setModelZOffset(event.motion.x);
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
            if (x >= kPanelX && y >= 102 && y < kHeight - 30) {
                tableScroll_ -= event.wheel.y;
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
                    tableScroll_ -= 1;
                    break;
                case SDLK_PAGEDOWN:
                    tableScroll_ += 1;
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
    CanByteOrder canByteOrder = CanByteOrder::LittleEndian;
#ifdef _WIN32
    bool canEnabled = false;  // Windows: CAN выключен по умолчанию
#else
    bool canEnabled = true;   // Linux: CAN включен по умолчанию
#endif
    bool forcedCanRequested = false;
    bool noCanRequested = false;
};

// Parses the byte-order values accepted by the CAN command-line option.
CanByteOrder parseCanByteOrder(const std::string& value) {
    const std::string normalized = lowercase(value);
    if (normalized == "little" || normalized == "little-endian") {
        return CanByteOrder::LittleEndian;
    }
    if (normalized == "big" || normalized == "big-endian") {
        return CanByteOrder::BigEndian;
    }
    throw std::runtime_error("CAN byte order must be 'little' or 'big'.");
}

// Converts command-line arguments into validated application options.
ProgramOptions parseArguments(int argc, char* argv[]) {
    ProgramOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--forced-can") {
            options.canEnabled = true;
            options.forcedCanRequested = true;
        } else if (argument == "--no-can") {
            options.canEnabled = false;
            options.noCanRequested = true;
        } else if (argument == "--can-interface") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--can-interface requires an interface name.");
            }
            options.canInterface = argv[++index];
        } else if (argument.rfind("--can-interface=", 0) == 0) {
            options.canInterface = argument.substr(std::string("--can-interface=").size());
        } else if (argument == "--can-byte-order") {
            if (index + 1 >= argc) {
                throw std::runtime_error(
                    "--can-byte-order requires 'little' or 'big'.");
            }
            options.canByteOrder = parseCanByteOrder(argv[++index]);
        } else if (argument.rfind("--can-byte-order=", 0) == 0) {
            options.canByteOrder = parseCanByteOrder(
                argument.substr(std::string("--can-byte-order=").size()));
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

// Reads the configured CAN state; forced-can=1 overrides no-can in the file.
std::optional<bool> readCanEnabledFromConfig() {
    const fs::path configPath = getProjectRootDir() / "config.txt";
    std::ifstream file(configPath);
    if (!file) {
        return std::nullopt;
    }
    std::optional<bool> canEnabled;
    std::string line;
    while (std::getline(file, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const auto eqPos = trimmed.find('=');
        if (eqPos == std::string::npos) {
            continue;
        }
        const std::string key = trim(trimmed.substr(0, eqPos));
        const std::string value = trim(trimmed.substr(eqPos + 1));
        if (key == "forced-can" && value == "1") {
            return true;
        }
        if (key == "no-can" && (value == "0" || value == "1")) {
            canEnabled = value == "0";
        }
    }
    return canEnabled;
}

// Initializes SDL, loads project data, starts CAN, and runs the viewer.
int main(int argc, char* argv[]) {
    ProgramOptions options;
    try {
        options = parseArguments(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n'
                  << "Usage: bga_visualizer [model.stl] [sensors.csv] "
                     "[--can-interface NAME] [--can-byte-order little|big] "
                     "[--no-can] [--forced-can]\n";
        return 2;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL initialization error: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    try {
        // Explicit CLI flags take priority over CAN settings from the config.
        if (options.noCanRequested) {
            options.canEnabled = false;
        } else if (options.forcedCanRequested) {
            options.canEnabled = true;
        } else if (const auto configuredCan = readCanEnabledFromConfig()) {
            options.canEnabled = *configuredCan;
        }

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
            const bool receiverStarted = canReceiver.start(
                options.canInterface, options.canByteOrder,
                [&sensorState](const SensorReading& reading) {
                    sensorState.updateTemperature(reading.sensorId,
                                                  reading.temperatureC);
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
            std::cout << "CAN reception disabled.\n";
        }

        Viewer viewer(std::move(mesh), sensorState, options.canEnabled);
        return viewer.run();
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
