#include "cleanup.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfPixelType.h>
#include <OpenEXR/ImfTestFile.h>

namespace exr_cleanup {
namespace {

constexpr std::array<const char*, 3> kVarianceLayers = {
    "diffuse_mse",
    "specular_mse",
    "normal_var",
};

std::atomic<std::uint64_t> temporary_file_counter{0};

class TemporaryFile final {
public:
    explicit TemporaryFile(std::filesystem::path path)
        : path_(std::move(path)) {}

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    ~TemporaryFile() {
        if (!active_) {
            return;
        }

        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    void release() noexcept { active_ = false; }

private:
    std::filesystem::path path_;
    bool active_ = true;
};

struct ChannelBuffer {
    std::string name;
    OPENEXR_IMF_NAMESPACE::PixelType type = OPENEXR_IMF_NAMESPACE::HALF;
    int x_sampling = 1;
    int y_sampling = 1;
    std::size_t x_stride = 0;
    std::size_t y_stride = 0;
    std::vector<char> data;
};

[[nodiscard]] std::string lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

[[nodiscard]] bool belongs_to_variance_layer(const std::string& channel_name) {
    return std::any_of(
        kVarianceLayers.begin(),
        kVarianceLayers.end(),
        [&channel_name](const char* layer) {
            const std::string layer_name(layer);
            const bool exact_match = channel_name == layer_name;
            const bool is_layer_channel =
                channel_name.size() > layer_name.size() &&
                channel_name.compare(
                    0, layer_name.size(), layer_name) == 0 &&
                channel_name[layer_name.size()] == '.';
            return exact_match || is_layer_channel;
        });
}

[[nodiscard]] std::size_t pixel_size(
    const OPENEXR_IMF_NAMESPACE::PixelType type) {
    switch (type) {
        case OPENEXR_IMF_NAMESPACE::UINT:
            return sizeof(std::uint32_t);
        case OPENEXR_IMF_NAMESPACE::HALF:
            return sizeof(std::uint16_t);
        case OPENEXR_IMF_NAMESPACE::FLOAT:
            return sizeof(float);
        default:
            throw std::runtime_error("Unsupported OpenEXR pixel type");
    }
}

[[nodiscard]] std::int64_t floor_div(
    const std::int64_t numerator,
    const std::int64_t denominator) {
    const std::int64_t quotient = numerator / denominator;
    const std::int64_t remainder = numerator % denominator;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] std::size_t sampled_value_count(
    const int minimum,
    const int maximum,
    const int sampling) {
    if (sampling <= 0) {
        throw std::runtime_error("Invalid channel sampling factor");
    }

    const auto minimum64 = static_cast<std::int64_t>(minimum);
    const auto maximum64 = static_cast<std::int64_t>(maximum);
    const auto sampling64 = static_cast<std::int64_t>(sampling);
    const auto count =
        floor_div(maximum64, sampling64) -
        floor_div(minimum64 - 1, sampling64);

    if (count < 0) {
        throw std::runtime_error("Invalid OpenEXR data window");
    }

    return static_cast<std::size_t>(count);
}

[[nodiscard]] std::size_t checked_multiply(
    const std::size_t left,
    const std::size_t right) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::runtime_error("OpenEXR channel buffer is too large");
    }
    return left * right;
}

[[nodiscard]] bool directory_entry_exists(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (!error) {
        return status.type() != std::filesystem::file_type::not_found;
    }
    if (error == std::errc::no_such_file_or_directory) {
        return false;
    }

    throw std::runtime_error(
        "Unable to inspect path " + path.string() + ": " + error.message());
}

[[nodiscard]] std::filesystem::path unique_sibling_path(
    const std::filesystem::path& source,
    const std::string& label) {
    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();

    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto counter = temporary_file_counter.fetch_add(1);
        const auto filename =
            "." + source.filename().string() + "." + label + "-" +
            std::to_string(timestamp) + "-" + std::to_string(counter);
        const auto candidate = source.parent_path() / filename;

        if (!directory_entry_exists(candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error(
        "Unable to allocate a temporary file next to " + source.string());
}

[[nodiscard]] std::uintmax_t file_size_or_throw(
    const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error(
            "Unable to read file size for " + path.string() + ": " +
            error.message());
    }
    return size;
}

void validate_supported_file(const std::filesystem::path& path) {
    bool is_tiled = false;
    bool is_deep = false;
    bool is_multipart = false;

    if (!OPENEXR_IMF_NAMESPACE::isOpenExrFile(
        path.string().c_str(), is_tiled, is_deep, is_multipart)) {
        throw std::runtime_error("Not a valid OpenEXR file");
    }
    if (is_tiled || is_deep || is_multipart) {
        throw std::runtime_error(
            "Only single-part scanline OpenEXR files are supported");
    }
}

[[nodiscard]] std::vector<ChannelBuffer> make_channel_buffers(
    const OPENEXR_IMF_NAMESPACE::ChannelList& channels,
    const IMATH_NAMESPACE::Box2i& data_window) {
    std::vector<ChannelBuffer> buffers;

    for (auto iterator = channels.begin();
        iterator != channels.end();
        ++iterator) {
        const std::string name(iterator.name());
        if (belongs_to_variance_layer(name)) {
            continue;
        }

        const auto& channel = iterator.channel();
        ChannelBuffer buffer;
        buffer.name = name;
        buffer.type = channel.type;
        buffer.x_sampling = channel.xSampling;
        buffer.y_sampling = channel.ySampling;
        buffer.x_stride = pixel_size(channel.type);

        const auto sampled_width = sampled_value_count(
            data_window.min.x, data_window.max.x, channel.xSampling);
        const auto sampled_height = sampled_value_count(
            data_window.min.y, data_window.max.y, channel.ySampling);
        buffer.y_stride = checked_multiply(sampled_width, buffer.x_stride);

        const auto byte_count =
            checked_multiply(buffer.y_stride, sampled_height);
        buffer.data.resize(std::max<std::size_t>(byte_count, 1));
        buffers.push_back(std::move(buffer));
    }

    return buffers;
}

[[nodiscard]] OPENEXR_IMF_NAMESPACE::FrameBuffer make_frame_buffer(
    std::vector<ChannelBuffer>& buffers,
    const IMATH_NAMESPACE::Box2i& data_window) {
    OPENEXR_IMF_NAMESPACE::FrameBuffer frame_buffer;

    for (auto& buffer : buffers) {
        frame_buffer.insert(
            buffer.name,
            OPENEXR_IMF_NAMESPACE::Slice::Make(
                buffer.type,
                buffer.data.data(),
                data_window,
                buffer.x_stride,
                buffer.y_stride,
                buffer.x_sampling,
                buffer.y_sampling));
    }

    return frame_buffer;
}

[[nodiscard]] OPENEXR_IMF_NAMESPACE::ChannelList retained_channels(
    const OPENEXR_IMF_NAMESPACE::ChannelList& channels,
    std::vector<std::string>& removed_channels) {
    OPENEXR_IMF_NAMESPACE::ChannelList retained;

    for (auto iterator = channels.begin();
        iterator != channels.end();
        ++iterator) {
        const std::string name(iterator.name());
        if (belongs_to_variance_layer(name)) {
            removed_channels.push_back(name);
        } else {
            retained.insert(name, iterator.channel());
        }
    }

    return retained;
}

void write_cleaned_file(
    OPENEXR_IMF_NAMESPACE::InputFile& input,
    const std::filesystem::path& destination,
    const OPENEXR_IMF_NAMESPACE::ChannelList& output_channels) {
    const auto data_window = input.header().dataWindow();
    auto buffers = make_channel_buffers(input.header().channels(), data_window);
    if (buffers.empty()) {
        throw std::runtime_error(
            "Cleanup would remove every channel; the source was left "
            "unchanged");
    }

    auto frame_buffer = make_frame_buffer(buffers, data_window);
    input.setFrameBuffer(frame_buffer);
    input.readPixels(data_window.min.y, data_window.max.y);

    OPENEXR_IMF_NAMESPACE::Header output_header(input.header());
    output_header.channels() = output_channels;

    {
        OPENEXR_IMF_NAMESPACE::OutputFile output(
            destination.string().c_str(), output_header);
        output.setFrameBuffer(frame_buffer);
        output.writePixels(data_window.max.y - data_window.min.y + 1);
    }

    OPENEXR_IMF_NAMESPACE::InputFile validation(destination.string().c_str());
    if (!validation.isComplete()) {
        throw std::runtime_error("The cleaned OpenEXR failed validation");
    }
}

[[nodiscard]] std::filesystem::path replace_source(
    const std::filesystem::path& source,
    TemporaryFile& temporary,
    const bool keep_backup) {
    std::filesystem::path backup;
    if (keep_backup) {
        backup = source;
        backup += ".bak";

        if (directory_entry_exists(backup)) {
            throw std::runtime_error(
                "Backup path already exists: " + backup.string());
        }
    } else {
        backup = unique_sibling_path(source, "rollback");
    }

    std::error_code error;
    std::filesystem::rename(source, backup, error);
    if (error) {
        throw std::runtime_error(
            "Unable to prepare source replacement: " + error.message());
    }

    std::filesystem::rename(temporary.path(), source, error);
    if (error) {
        std::error_code rollback_error;
        std::filesystem::rename(backup, source, rollback_error);

        std::string message =
            "Unable to install the cleaned file: " + error.message();
        if (rollback_error) {
            message +=
                ". Automatic rollback also failed; the original remains at " +
                backup.string();
        }
        throw std::runtime_error(message);
    }

    temporary.release();

    if (!keep_backup) {
        std::filesystem::remove(backup, error);
        if (error) {
            return backup;
        }
        return {};
    }

    return backup;
}

}  // namespace

bool has_exr_extension(const std::filesystem::path& path) {
    return lowercase(path.extension().string()) == ".exr";
}

CleanupResult clean_file(
    const std::filesystem::path& path,
    const CleanupOptions& options) {
    CleanupResult result;
    result.path = path;

    std::error_code error;
    const bool is_symlink = std::filesystem::is_symlink(path, error);
    if (error) {
        throw std::runtime_error(
            "Unable to inspect the input path: " + error.message());
    }
    if (is_symlink) {
        throw std::runtime_error(
            "Symbolic links are not modified; pass the target file "
            "explicitly");
    }

    error.clear();
    if (!std::filesystem::is_regular_file(path, error) || error) {
        throw std::runtime_error("Input is not a readable regular file");
    }

    const auto source_permissions =
        std::filesystem::status(path, error).permissions();
    if (error) {
        throw std::runtime_error(
            "Unable to read source permissions: " + error.message());
    }

    result.original_size = file_size_or_throw(path);
    validate_supported_file(path);

    TemporaryFile temporary(unique_sibling_path(path, "tmp"));
    {
        // Keep the input handle in this scope so Windows can rename the source
        // after OpenEXR has closed it.
        OPENEXR_IMF_NAMESPACE::InputFile input(path.string().c_str());
        if (!input.isComplete()) {
            throw std::runtime_error("The source OpenEXR file is incomplete");
        }

        auto output_channels = retained_channels(
            input.header().channels(), result.removed_channels);
        if (result.removed_channels.empty()) {
            result.status = FileStatus::no_matching_channels;
            result.cleaned_size = result.original_size;
            return result;
        }

        if (options.dry_run) {
            result.status = FileStatus::would_clean;
            result.cleaned_size = result.original_size;
            return result;
        }

        write_cleaned_file(input, temporary.path(), output_channels);
    }

    std::filesystem::permissions(
        temporary.path(),
        source_permissions,
        std::filesystem::perm_options::replace,
        error);
    if (error) {
        throw std::runtime_error(
            "Unable to preserve source permissions: " + error.message());
    }

    result.cleaned_size = file_size_or_throw(temporary.path());
    result.backup_path = replace_source(path, temporary, options.keep_backup);
    result.status = FileStatus::cleaned;
    return result;
}

}  // namespace exr_cleanup
