#include "cleanup.hpp"
#include "exr_cleanup/version.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct CliOptions {
    bool dry_run = false;
    bool keep_backup = false;
    bool recursive = true;
    bool verbose = false;
    std::filesystem::path input;
};

struct RunSummary {
    std::size_t scanned = 0;
    std::size_t cleaned = 0;
    std::size_t would_clean = 0;
    std::size_t skipped = 0;
    std::size_t failed = 0;
    std::uintmax_t bytes_saved = 0;
};

void print_usage(std::ostream& output) {
    output
        << "EXR Cleanup Tool " << EXR_CLEANUP_VERSION << '\n'
        << "CLI for removing RenderMan variance channels from OpenEXR files.\n\n"
        << "Usage:\n"
        << "  exr-cleanup [options] <file-or-directory>\n\n"
        << "Options:\n"
        << "  -n, --dry-run       Report changes without writing files\n"
        << "  -b, --keep-backup   Keep each original as <name>.bak\n"
        << "      --no-recursive  Do not descend into subdirectories\n"
        << "  -v, --verbose       Show files that do not need cleanup\n"
        << "  -h, --help          Show this help message\n"
        << "      --version       Show the version\n";
}

[[nodiscard]] CliOptions parse_arguments(const int argc, char* argv[]) {
    CliOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);

        if (argument == "-n" || argument == "--dry-run") {
            options.dry_run = true;
        } else if (argument == "-b" || argument == "--keep-backup") {
            options.keep_backup = true;
        } else if (argument == "--no-recursive") {
            options.recursive = false;
        } else if (argument == "-v" || argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "-h" || argument == "--help") {
            print_usage(std::cout);
            std::exit(0);
        } else if (argument == "--version") {
            std::cout << EXR_CLEANUP_VERSION << '\n';
            std::exit(0);
        } else if (!argument.empty() && argument.front() == '-') {
            throw std::runtime_error("Unknown option: " + argument);
        } else if (!options.input.empty()) {
            throw std::runtime_error("Only one input path can be specified");
        } else {
            options.input = std::filesystem::path(argument);
        }
    }

    if (options.input.empty()) {
        throw std::runtime_error("An input file or directory is required");
    }

    return options;
}

[[nodiscard]] std::vector<std::filesystem::path> find_candidates(
    const CliOptions& options) {
    std::error_code error;
    if (std::filesystem::is_regular_file(options.input, error) && !error) {
        return {options.input};
    }

    error.clear();
    if (!std::filesystem::is_directory(options.input, error) || error) {
        throw std::runtime_error(
            "Input path is neither a readable file nor a directory");
    }

    std::set<std::filesystem::path> candidates;
    const auto add_candidate = [&candidates](
        const std::filesystem::directory_entry& entry) {
        std::error_code entry_error;
        const bool is_symlink = entry.is_symlink(entry_error);
        if (!entry_error && !is_symlink &&
            entry.is_regular_file(entry_error) && !entry_error &&
            exr_cleanup::has_exr_extension(entry.path())) {
            candidates.insert(entry.path());
        }
    };

    if (options.recursive) {
        std::filesystem::recursive_directory_iterator iterator(
            options.input,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end;

        while (iterator != end) {
            if (error) {
                throw std::runtime_error(
                    "Unable to scan input directory: " + error.message());
            }
            add_candidate(*iterator);
            iterator.increment(error);
        }
    } else {
        std::filesystem::directory_iterator iterator(
            options.input,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::directory_iterator end;

        while (iterator != end) {
            if (error) {
                throw std::runtime_error(
                    "Unable to scan input directory: " + error.message());
            }
            add_candidate(*iterator);
            iterator.increment(error);
        }
    }

    if (error) {
        throw std::runtime_error(
            "Unable to finish scanning input directory: " + error.message());
    }

    return {candidates.begin(), candidates.end()};
}

[[nodiscard]] std::string human_size(const std::uintmax_t bytes) {
    constexpr std::uintmax_t scale = 1024;
    constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};

    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= static_cast<double>(scale) && unit < 4) {
        value /= static_cast<double>(scale);
        ++unit;
    }

    std::ostringstream output;
    output
        << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value
        << ' ' << units[unit];
    return output.str();
}

[[nodiscard]] std::string channel_summary(
    const std::vector<std::string>& channels) {
    std::ostringstream output;
    for (std::size_t index = 0; index < channels.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << channels[index];
    }
    return output.str();
}

void print_result(
    const exr_cleanup::CleanupResult& result,
    const CliOptions& options,
    RunSummary& summary) {
    ++summary.scanned;

    switch (result.status) {
        case exr_cleanup::FileStatus::cleaned: {
            ++summary.cleaned;
            const auto saved = result.original_size > result.cleaned_size
                ? result.original_size - result.cleaned_size
                : 0;
            summary.bytes_saved += saved;

            std::cout
                << "[cleaned] " << result.path.string() << '\n'
                << "  removed: "
                << channel_summary(result.removed_channels) << '\n'
                << "  size: " << human_size(result.original_size) << " -> "
                << human_size(result.cleaned_size) << '\n';
            if (!result.backup_path.empty()) {
                std::cout
                    << "  backup: " << result.backup_path.string() << '\n';
            }
            break;
        }
        case exr_cleanup::FileStatus::would_clean:
            ++summary.would_clean;
            std::cout
                << "[would clean] " << result.path.string() << '\n'
                << "  remove: "
                << channel_summary(result.removed_channels) << '\n';
            break;
        case exr_cleanup::FileStatus::no_matching_channels:
            ++summary.skipped;
            if (options.verbose) {
                std::cout
                    << "[skip] " << result.path.string()
                    << " (no variance channels)\n";
            }
            break;
    }
}

void print_summary(const RunSummary& summary, const bool dry_run) {
    std::cout
        << '\n'
        << "Scanned " << summary.scanned << " file(s): ";
    if (dry_run) {
        std::cout << summary.would_clean << " would change, ";
    } else {
        std::cout << summary.cleaned << " cleaned, ";
    }
    std::cout
        << summary.skipped << " unchanged, " << summary.failed << " failed";
    if (!dry_run && summary.bytes_saved > 0) {
        std::cout << ", " << human_size(summary.bytes_saved) << " saved";
    }
    std::cout << ".\n";
}

}  // namespace

int main(const int argc, char* argv[]) {
    try {
        const auto options = parse_arguments(argc, argv);
        const auto candidates = find_candidates(options);

        RunSummary summary;
        for (const auto& candidate : candidates) {
            try {
                const exr_cleanup::CleanupOptions cleanup_options{
                    options.dry_run,
                    options.keep_backup,
                };
                const auto result =
                    exr_cleanup::clean_file(candidate, cleanup_options);
                print_result(result, options, summary);
            } catch (const std::exception& exception) {
                ++summary.scanned;
                ++summary.failed;
                std::cerr
                    << "[error] " << candidate.string() << ": "
                    << exception.what() << '\n';
            }
        }

        print_summary(summary, options.dry_run);
        return summary.failed == 0 ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << "\n\n";
        print_usage(std::cerr);
        return 2;
    }
}
