#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace exr_cleanup {

enum class FileStatus {
    cleaned,
    would_clean,
    no_matching_channels,
};

struct CleanupOptions {
    bool dry_run = false;
    bool keep_backup = false;
};

struct CleanupResult {
    FileStatus status = FileStatus::no_matching_channels;
    std::filesystem::path path;
    std::vector<std::string> removed_channels;
    std::uintmax_t original_size = 0;
    std::uintmax_t cleaned_size = 0;
    std::filesystem::path backup_path;
};

[[nodiscard]] bool has_exr_extension(const std::filesystem::path& path);

[[nodiscard]] CleanupResult clean_file(
    const std::filesystem::path& path,
    const CleanupOptions& options = {});

}  // namespace exr_cleanup
