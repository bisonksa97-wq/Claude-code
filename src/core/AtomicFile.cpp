#include "core/AtomicFile.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace up {
namespace {

// Flushes file data to stable storage so a crash after rename cannot yield an empty file.
bool syncFile(const std::filesystem::path& path) {
#ifdef _WIN32
    FILE* f = _wfopen(path.c_str(), L"rb+");
    if (!f) return false;
    const bool ok = _commit(_fileno(f)) == 0;
    std::fclose(f);
    return ok;
#else
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
#endif
}

}  // namespace

Status writeFileAtomically(const std::filesystem::path& path, std::string_view contents, bool keepBackup) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.has_parent_path() && !fs::exists(path.parent_path(), ec)) {
        return makeError(ErrorCode::IoError, "project",
                         "The folder '" + path.parent_path().string() + "' does not exist.",
                         "Choose an existing folder or create it first.");
    }
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return makeError(ErrorCode::IoError, "project",
                             "Unable to write '" + path.string() + "' because the file could not be created.",
                             "Check that the folder is writable and the disk is not full.");
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            fs::remove(temp, ec);
            return makeError(ErrorCode::IoError, "project",
                             "Unable to write '" + path.string() + "' because writing the data failed.",
                             "Check free disk space and try again.");
        }
    }
    if (!syncFile(temp)) {
        fs::remove(temp, ec);
        return makeError(ErrorCode::IoError, "project",
                         "Unable to write '" + path.string() + "' because the data could not be flushed to disk.",
                         "Check the storage device and try again.");
    }
    if (keepBackup && fs::exists(path, ec)) {
        fs::path backup = path;
        backup += ".bak";
        fs::copy_file(path, backup, fs::copy_options::overwrite_existing, ec);
        // A failed backup is not fatal: the primary file is still replaced atomically.
    }
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        return makeError(ErrorCode::IoError, "project",
                         "Unable to replace '" + path.string() + "'. The previous version was left untouched.",
                         "Close other programs that may be using the file and try again.", ec.message());
    }
    return Status::success();
}

Result<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return makeError(ErrorCode::NotFound, "project", "Unable to open '" + path.string() + "'.",
                         "Check that the file exists and that you have permission to read it.");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace up
