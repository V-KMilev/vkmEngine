#include "session/file_import.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "command/editor_commands.h"
#include "editor_state.h"
#include "import/audio_loaders.h"
#include "import/import_formats.h"
#include "import/model_loaders.h"
#include "import/texture_loaders.h"
#include "io/json_file.h"
#include "io/project_paths.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine::FileImport {

namespace fs = std::filesystem;

namespace {

/// Whether @p extension is one of @p list's.
template<typename List>
bool among(const List& list, const std::string& extension) {
    return std::find(list.begin(), list.end(), extension) != list.end();
}

/// @p file's path below @p dir, or empty when it lies elsewhere.
fs::path below(const fs::path& file, const fs::path& dir) {
    std::error_code ec;
    const fs::path inside = fs::weakly_canonical(file, ec).lexically_relative(fs::weakly_canonical(dir, ec));
    if (inside.empty() || *inside.begin() == "..") return {};
    return inside;
}

/// Whether two files hold the same bytes.
bool sameBytes(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    std::ifstream ia(a, std::ios::binary);
    std::ifstream ib(b, std::ios::binary);
    return std::equal(
        std::istreambuf_iterator<char>(ia),
        std::istreambuf_iterator<char>(),
        std::istreambuf_iterator<char>(ib)
    );
}

/// @p wanted, or the first of `stem_2`, `stem_3`... beside it that nothing holds.
fs::path freeName(const fs::path& wanted) {
    std::error_code ec;
    fs::path name = wanted;
    for (int n = 2; fs::exists(name, ec); ++n) {
        const std::string numbered = wanted.stem().string() + "_" + std::to_string(n);
        name = wanted.parent_path() / (numbered + wanted.extension().string());
    }
    return name;
}

/// A hex digit's value, or -1 for none.
int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// "%20" and the like, as a URI spells a character; a '%' not followed by two hex digits is itself.
std::string percentDecoded(const std::string& uri) {
    std::string out;
    for (size_t i = 0; i < uri.size(); ++i) {
        const int high = uri[i] == '%' && i + 2 < uri.size() ? hexValue(uri[i + 1]) : -1;
        const int low  = high >= 0 ? hexValue(uri[i + 2]) : -1;
        if (low >= 0) {
            out += static_cast<char>(high * 16 + low);
            i += 2;
        } else {
            out += uri[i];
        }
    }
    return out;
}

/// The maps a .mtl names, relative to its folder: the last word of each map line.
void mapsOf(const fs::path& library, const fs::path& from, std::vector<fs::path>& out) {
    std::ifstream in(library);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        const bool map = key.rfind("map_", 0) == 0 || key == "bump" || key == "disp" || key == "norm"
            || key == "decal" || key == "refl";
        if (!map) continue;
        std::string last;
        for (std::string word; words >> word;) last = word;
        if (!last.empty()) out.push_back(from / last);
    }
}

/**
 * @brief The files a model names, relative to its folder: a glTF's buffers and images, an OBJ's
 *        material libraries and their maps. A binary glTF or an FBX holds what it needs.
 */
std::vector<fs::path> companionsOf(const fs::path& model) {
    std::vector<fs::path> files;
    const std::string extension = lowercaseExtension(model);
    if (extension == ".gltf") {
        nlohmann::json doc;
        if (!detail::readJsonFile(model, doc, "Dropped glTF")) return files;
        for (const char* section : {"buffers", "images"}) {
            const auto list = doc.find(section);
            if (list == doc.end() || !list->is_array()) continue;
            // A file from anywhere: what is not as glTF spells it is passed over, not trusted.
            for (const nlohmann::json& item : *list) {
                const auto uri = item.is_object() ? item.find("uri") : item.end();
                if (uri == item.end() || !uri->is_string()) continue;
                const std::string named = uri->get<std::string>();
                if (!named.empty() && named.rfind("data:", 0) != 0) files.emplace_back(percentDecoded(named));
            }
        }
    } else if (extension == ".obj") {
        std::ifstream in(model);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream words(line);
            std::string key;
            words >> key;
            if (key != "mtllib") continue;
            for (std::string name; words >> name;) {
                files.emplace_back(name);
                mapsOf(model.parent_path() / name, fs::path(name).parent_path(), files);
            }
        }
    }
    return files;
}

/// Copy @p file into @p dir, or find it there already: the copy's path, or empty.
fs::path placeFile(const fs::path& file, const fs::path& dir, std::string& error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path target = dir / file.filename();
    if (fs::exists(target, ec)) {
        if (sameBytes(file, target)) return target;
        target = freeName(target);
    }
    if (!fs::copy_file(file, target, ec)) {
        error = "Could not copy " + file.string() + " into the project: " + ec.message();
        return {};
    }
    return target;
}

/// Copy a model and the files it names into a folder of its own under @p models.
fs::path placeModel(const fs::path& model, const fs::path& models, std::string& error) {
    std::error_code ec;
    fs::path folder = models / model.stem();
    if (fs::exists(folder, ec) && !sameBytes(model, folder / model.filename())) folder = freeName(folder);
    fs::create_directories(folder, ec);

    const fs::path placed = folder / model.filename();
    if (!fs::exists(placed, ec) && !fs::copy_file(model, placed, ec)) {
        error = "Could not copy " + model.string() + " into the project: " + ec.message();
        return {};
    }
    // Each where the model looks for it: the same path below the folder.
    size_t left = 0;
    for (const fs::path& companion : companionsOf(model)) {
        const fs::path from = model.parent_path() / companion;
        const fs::path to   = (folder / companion).lexically_normal();
        if (below(to, folder).empty() || !fs::exists(from, ec)) {
            ++left;
            continue;
        }
        fs::create_directories(to.parent_path(), ec);
        if (!fs::exists(to, ec)) fs::copy_file(from, to, ec);
    }
    if (left > 0) {
        error = std::to_string(left) + " file(s) " + model.filename().string()
            + " names were missing or outside its folder; it may import without them";
    }
    return placed;
}

} // namespace

bool importModel(const std::string& path, Scene& scene, ResourceManager& resources, EditorState& state) {
    const ModelImport imported = importModelIntoScene(path, resources, scene);
    if (imported.root) {
        // An undo step: the root may have taken the slot a deleted entity's undo is waiting to
        // restore into.
        SubtreeSnapshot made = SubtreeSnapshot::capture(scene, imported.root);
        state.pushStep(std::make_unique<CreateSubtreeCommand>(std::move(made), "Import Model"));
        state.selectEntity(imported.root);
    } else if (imported.ok) {
        // Clips and no mesh: nothing to select.
        const std::string message = "Imported " + std::to_string(imported.clips)
            + " clip(s). The file has no mesh, so nothing was added to the scene.";
        state.pushToast(ToastKind::Info, message);
    } else {
        state.pushToast(ToastKind::Error, "Could not import '" + path + "'");
    }
    return imported.ok;
}

bool importTexture(const std::string& path, ResourceManager& resources, EditorState& state) {
    // Asked first: loadTexture returns a held name's texture silently, hiding which outcome it was.
    const std::string ref = ProjectPaths::toProjectRelative(path);
    if (resources.findByName<TextureAsset>(ref)) {
        state.pushToast(ToastKind::Info, ref + " is already imported");
        return true;
    }
    // Color: a hand-picked texture is art. Data maps come with a model or a Material Editor slot.
    if (!loadTexture(path, resources, TextureUsage::Color)) {
        state.pushToast(ToastKind::Error, "Could not decode " + path);
        return false;
    }
    state.markSceneDirty();
    return true;
}

bool importSound(const std::string& path, ResourceManager& resources, EditorState& state) {
    // Asked first: loadAudioClip returns a held name's clip silently, hiding which outcome it was.
    const std::string ref = ProjectPaths::toProjectRelative(path);
    const bool alreadyHeld = static_cast<bool>(resources.findByName<AudioClipAsset>(ref));
    if (!loadAudioClip(path, resources)) {
        state.pushToast(ToastKind::Error, "Could not decode " + path);
        return false;
    }
    if (alreadyHeld) state.pushToast(ToastKind::Info, ref + " is already imported");
    else             state.markSceneDirty();
    return true;
}

fs::path intoProject(const fs::path& file, std::string& error) {
    const fs::path assets = ProjectPaths::assets();
    if (!below(file, assets).empty()) return file;

    const std::string extension = lowercaseExtension(file);
    if (among(MODEL_EXTENSIONS, extension)) return placeModel(file, assets / "models", error);
    if (among(IMAGE_EXTENSIONS, extension)) return placeFile(file, assets / "textures", error);
    if (among(AUDIO_EXTENSIONS, extension)) return placeFile(file, assets / "audio", error);
    error = file.filename().string() + ": not a model, an image or a sound the editor reads";
    return {};
}

void importDropped(
    const std::vector<std::string>& paths,
    Scene& scene,
    ResourceManager& resources,
    EditorState& state
) {
    for (const std::string& dropped : paths) {
        std::error_code ec;
        if (!fs::is_regular_file(dropped, ec)) {
            state.pushToast(ToastKind::Error, dropped + " is not a file; drop the files themselves");
            continue;
        }
        std::string error;
        fs::path    inside;
        // Read from anywhere, so whatever it holds costs this file, not the session.
        try {
            inside = intoProject(dropped, error);
        } catch (const std::exception& e) {
            error = "Could not read " + dropped + ": " + e.what();
        }
        if (!error.empty()) state.pushToast(inside.empty() ? ToastKind::Error : ToastKind::Info, error);
        if (inside.empty()) continue;

        const std::string path      = inside.string();
        const std::string extension = lowercaseExtension(inside);
        try {
            if (among(MODEL_EXTENSIONS, extension))      importModel(path, scene, resources, state);
            else if (among(IMAGE_EXTENSIONS, extension)) importTexture(path, resources, state);
            else                                         importSound(path, resources, state);
        } catch (const std::exception& e) {
            state.pushToast(ToastKind::Error, "Could not import " + path + ": " + e.what());
        }
    }
}

} // namespace Vkm::Engine::FileImport
