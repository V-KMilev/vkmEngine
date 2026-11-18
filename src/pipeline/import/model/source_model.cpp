#include "import/model/source_model.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <future>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include <cgltf.h>
#include <ufbx.h>

#include "base/profile/profiler.h"
#include "import/import_formats.h"
#include "import/model/gltf_reader.h"
#include "import/model/ufbx_reader.h"

namespace Vkm::Engine {

std::unique_ptr<SourceModel> parseModel(const std::string& path, const std::string& ref) {
    PROFILE_SCOPE("ModelParse");
    const std::string extension = lowercaseExtension(path);
    if (extension == ".gltf" || extension == ".glb") return ModelReaders::parseGltf(path, ref);
    return ModelReaders::parseUfbx(path, ref);
}

namespace {

// A ufbx open that hands @p user, a callable, each path it is asked for before opening it.
template<typename Note>
bool openNoting(
    void* user,
    ufbx_stream* stream,
    const char* path,
    size_t length,
    const ufbx_open_file_info* info
) {
    (*static_cast<Note*>(user))(std::filesystem::path(std::string(path, length)));
    return ufbx_open_file(stream, path, length, nullptr, nullptr);
}

} // namespace

std::vector<std::filesystem::path> filesReadBy(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> files{path};
    auto add = [&](const std::filesystem::path& file) {
        const std::filesystem::path normal = file.lexically_normal();
        if (std::find(files.begin(), files.end(), normal) == files.end()) files.push_back(normal);
    };
    const std::string extension = lowercaseExtension(path);
    if (extension == ".glb") return files;

    if (extension == ".gltf") {
        // The buffers it keeps in files beside it, as cgltf_load_buffers finds them.
        cgltf_options options{};
        cgltf_data* data = nullptr;
        if (cgltf_parse_file(&options, path.string().c_str(), &data) == cgltf_result_success) {
            for (cgltf_size b = 0; b < data->buffers_count; ++b) {
                const char* uri = data->buffers[b].uri;
                if (!uri || !*uri || std::strncmp(uri, "data:", 5) == 0) continue;
                std::string decoded(uri);
                decoded.resize(cgltf_decode_uri(decoded.data()));
                add(path.parent_path() / decoded);
            }
        }
        cgltf_free(data);
        return files;
    }

    // What ufbx tries to open beside it under the options a parse takes - an OBJ's material
    // libraries, one it names and cannot find too, so its arrival moves a key - with nothing past
    // the file's references read.
    ufbx_load_opts options = ModelReaders::ufbxOptions();
    options.ignore_geometry   = true;
    options.ignore_animation  = true;
    options.ignore_embedded   = true;
    options.open_file_cb.user = &add;
    options.open_file_cb.fn   = &openNoting<decltype(add)>;
    ufbx_free_scene(ufbx_load_file(path.string().c_str(), &options, nullptr));
    return files;
}

namespace {

/**
 * @brief When one file on disk was last written, as far as the cache can tell.
 */
struct FileStamp {
    std::uintmax_t                  size = 0;
    std::filesystem::file_time_type written{};

    bool operator==(const FileStamp& other) const {
        return size == other.size && written == other.written;
    }
};

// Every file the import reads, stamped; an unstattable file is stamped zero, which no real one is.
std::vector<FileStamp> stampFiles(const std::vector<std::filesystem::path>& files) {
    std::vector<FileStamp> stamps;
    stamps.reserve(files.size());
    for (const std::filesystem::path& file : files) {
        std::error_code ec;
        FileStamp stamp;
        stamp.size = std::filesystem::file_size(file, ec);
        if (ec) stamp.size = 0;
        stamp.written = std::filesystem::last_write_time(file, ec);
        if (ec) stamp.written = {};
        stamps.push_back(stamp);
    }
    return stamps;
}

using ParsedModel = std::shared_ptr<const SourceModel>;

/**
 * @brief LRU cache of parsed model files, keyed by path and by the stamps of the
 *        files the parse read.
 *
 * Holds a future, so a request waits for another worker's parse of the same file while other
 * files parse outside the lock.
 */
class ModelCache {
    public:
        ModelCache() = default;
        ~ModelCache() = default;

        ModelCache(const ModelCache& other) = delete;
        ModelCache& operator=(const ModelCache& other) = delete;

        ModelCache(ModelCache && other) = delete;
        ModelCache& operator=(ModelCache && other) = delete;

    public:
        /**
         * @brief The parsed file, parsed now if these bytes have not been.
         *
         * @param path The model file on this machine.
         * @param ref Its project reference, which the log lines name.
         * @return The model, or null when the file does not parse (logged).
         */
        ParsedModel get(const std::string& path, const std::string& ref) {
            std::error_code ec;
            const auto canonicalPath    = std::filesystem::weakly_canonical(path, ec);
            const std::string canonical = ec ? path : canonicalPath.string();

            std::promise<ParsedModel>       promise;
            std::shared_future<ParsedModel> cached;
            uint64_t ticket = 0;
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                const auto it = std::find_if(
                    m_entries.begin(),
                    m_entries.end(),
                    [&](const Entry& e) { return e.path == canonical; }
                );
                // Against every file the parse listed, not only the .gltf's own bytes. An entry with
                // none is a parse still running, to wait for.
                if (it != m_entries.end() && (it->files.empty() || stampFiles(it->files) == it->stamps)) {
                    cached = it->model;
                    std::rotate(m_entries.begin(), it, it + 1);
                } else {
                    if (it != m_entries.end()) m_entries.erase(it);
                    ticket = ++m_lastTicket;
                    Entry entry{canonical, {}, {}, ticket, promise.get_future().share()};
                    m_entries.insert(m_entries.begin(), std::move(entry));
                    if (m_entries.size() > MAX_CACHED) {
                        m_entries.erase(m_entries.begin() + MAX_CACHED, m_entries.end());
                    }
                }
            }
            // Outside the lock: the parse takes it again to record its stamps.
            if (cached.valid()) return cached.get();

            // Stamped before the parse, so a write during it leaves a stale stamp, not a fresh one
            // over old bytes.
            const std::vector<std::filesystem::path> files = filesReadBy(path);
            std::vector<FileStamp> stamps = stampFiles(files);
            ParsedModel model;
            try {
                model = parseModel(path, ref);
            } catch (...) {
                forget(ticket);
                promise.set_exception(std::current_exception());
                throw;
            }

            if (!model) {
                forget(ticket);
            } else {
                const std::lock_guard<std::mutex> lock(m_mutex);
                for (Entry& entry : m_entries) {
                    if (entry.ticket != ticket) continue;
                    entry.files  = files;
                    entry.stamps = std::move(stamps);
                }
            }
            promise.set_value(model);
            return model;
        }

    private:
        struct Entry {
            std::string                        path;
            std::vector<std::filesystem::path> files;   ///< Empty while its parse runs.
            std::vector<FileStamp>             stamps;  ///< One per file, as the parse found them.
            uint64_t                           ticket = 0;
            std::shared_future<ParsedModel>    model;
        };

        // Sized for an import batch while keeping retained memory bounded.
        static constexpr size_t MAX_CACHED = 8;

    private:
        // A file that did not parse is not cached: the next request tries again.
        void forget(uint64_t ticket) {
            const std::lock_guard<std::mutex> lock(m_mutex);
            const auto finished = std::remove_if(
                m_entries.begin(),
                m_entries.end(),
                [&](const Entry& e) { return e.ticket == ticket; }
            );
            m_entries.erase(finished, m_entries.end());
        }

    private:
        std::vector<Entry> m_entries;
        uint64_t           m_lastTicket = 0;
        std::mutex         m_mutex;
};

} // namespace

std::shared_ptr<const SourceModel> loadSourceModel(const std::string& path, const std::string& ref) {
    static ModelCache s_cache;
    return s_cache.get(path, ref);
}

} // namespace Vkm::Engine
