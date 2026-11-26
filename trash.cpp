#include "helperlib.h"

#include <QDateTime>
#include <QFile>

#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{
QDateTime deletionDate(const fs::path &metadata)
{
    const int fd = ::open(metadata.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    QFile info;
    if (!info.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return {};
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 65536) {
        return {};
    }
    const auto lines = info.read(65537).split('\n');
    if (lines.isEmpty() || lines.first().trimmed() != "[Trash Info]") {
        return {};
    }
    for (const auto &line : lines.mid(1)) {
        if (line.startsWith('[')) {
            break;
        }
        if (line.startsWith("DeletionDate=")) {
            return QDateTime::fromString(QString::fromLatin1(line.mid(13).trimmed()), "yyyy-MM-dd'T'HH:mm:ss");
        }
    }
    return {};
}

quint64 allocatedBlocks(const fs::path &path)
{
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) {
        throw fs::filesystem_error("lstat", path, std::error_code(errno, std::generic_category()));
    }
    return static_cast<quint64>(st.st_blocks);
}
}

bool cleanTrashItems(const QString &trashDir, bool remove, int days, quint64 *sizeKiB)
{
    const fs::path root(QFile::encodeName(trashDir).constData());
    const fs::path infoDir = root / "info";
    const fs::path filesDir = root / "files";
    const auto cutoff = QDateTime::currentDateTime().addDays(-days);
    quint64 blocks = 0;
    bool ok = true;
    auto process = [&](const fs::path &path) {
        if (remove) {
            fs::remove_all(path);
            return;
        }
        blocks += allocatedBlocks(path);
        if (fs::is_directory(fs::symlink_status(path))) {
            for (const auto &child : fs::recursive_directory_iterator(path)) {
                blocks += allocatedBlocks(child.path());
            }
        }
    };
    auto forEachEntry = [&](const fs::path &dir, const auto &visit) {
        if (!fs::exists(fs::symlink_status(dir))) {
            return;
        }
        for (const auto &entry : fs::directory_iterator(dir)) {
            try {
                visit(entry.path());
            } catch (const fs::filesystem_error &error) {
                printError(QString::fromLocal8Bit(error.what()));
                ok = false;
            }
        }
    };
    try {
        for (const auto &dir : {root, filesDir, infoDir}) {
            const auto status = fs::symlink_status(dir);
            if (fs::exists(status) && !fs::is_directory(status)) {
                printError(QString("Invalid trash directory: %1").arg(QFile::decodeName(dir.c_str())));
                return false;
            }
        }
        forEachEntry(filesDir, [&](const fs::path &payload) {
            const auto metadata = infoDir / (payload.filename().native() + ".trashinfo");
            if (days > 0) {
                const auto date = deletionDate(metadata);
                if (!date.isValid() || date > cutoff) {
                    return;
                }
            }
            process(payload);
            if (fs::exists(fs::symlink_status(metadata))) {
                process(metadata);
            }
        });
        if (days == 0) {
            // An interrupted trash operation can leave metadata without a payload.
            // Keep the metadata for any payload whose removal failed.
            forEachEntry(infoDir, [&](const fs::path &metadata) {
                const std::string name = metadata.filename().native();
                constexpr std::string_view suffix = ".trashinfo";
                if (name.ends_with(suffix)) {
                    const auto id = name.substr(0, name.size() - suffix.size());
                    if (!id.empty() && id != "." && id != ".." && fs::exists(fs::symlink_status(filesDir / id))) {
                        return;
                    }
                }
                process(metadata);
            });
        }
    } catch (const fs::filesystem_error &error) {
        printError(QString::fromLocal8Bit(error.what()));
        ok = false;
    }
    *sizeKiB = (blocks + 1) / 2;
    return ok;
}
