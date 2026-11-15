#include "grpp/mechanisms.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace grpp {
void Scores::saveAtomic(const std::filesystem::path &path) const {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    save(temporary);
    Scores verified;
    verified.load(temporary);
    // Keep the last good document when a user-edited/truncated file is repaired.
    if (std::filesystem::exists(path)) {
        bool valid = false;
        try {
            Scores previous;
            previous.load(path);
            valid = true;
        } catch (const std::runtime_error &) {
        }
        if (valid) {
            auto backup = path;
            backup += ".bak";
            std::filesystem::copy_file(path, backup,
                                       std::filesystem::copy_options::overwrite_existing);
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not commit score/profile file");
#else
    std::filesystem::rename(temporary, path);
#endif
}
} // namespace grpp
