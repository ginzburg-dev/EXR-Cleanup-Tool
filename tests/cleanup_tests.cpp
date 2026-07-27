#include "cleanup.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <Imath/half.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfStringAttribute.h>

namespace {

constexpr int kWidth = 4;
constexpr int kHeight = 3;
constexpr std::size_t kPixelCount = static_cast<std::size_t>(kWidth * kHeight);

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("exr-cleanup-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_fixture(
    const std::filesystem::path& path,
    const bool add_variance_channels) {
    OPENEXR_IMF_NAMESPACE::Header header(kWidth, kHeight);
    header.insert(
        "fixture",
        OPENEXR_IMF_NAMESPACE::StringAttribute(
            "metadata must survive cleanup"));
    header.channels().insert(
        "R",
        OPENEXR_IMF_NAMESPACE::Channel(OPENEXR_IMF_NAMESPACE::FLOAT));
    header.channels().insert(
        "G",
        OPENEXR_IMF_NAMESPACE::Channel(OPENEXR_IMF_NAMESPACE::HALF));
    header.channels().insert(
        "beauty.Z",
        OPENEXR_IMF_NAMESPACE::Channel(OPENEXR_IMF_NAMESPACE::FLOAT));

    if (add_variance_channels) {
        header.channels().insert(
            "diffuse_mse.R",
            OPENEXR_IMF_NAMESPACE::Channel(OPENEXR_IMF_NAMESPACE::FLOAT));
        header.channels().insert(
            "normal_var.X",
            OPENEXR_IMF_NAMESPACE::Channel(OPENEXR_IMF_NAMESPACE::HALF));
        header.channels().insert(
            "specular_mse.B",
            OPENEXR_IMF_NAMESPACE::Channel(OPENEXR_IMF_NAMESPACE::FLOAT));
    }

    std::array<float, kPixelCount> red{};
    std::array<IMATH_NAMESPACE::half, kPixelCount> green{};
    std::array<float, kPixelCount> depth{};
    std::array<float, kPixelCount> diffuse{};
    std::array<IMATH_NAMESPACE::half, kPixelCount> normal{};
    std::array<float, kPixelCount> specular{};

    for (std::size_t index = 0; index < kPixelCount; ++index) {
        const auto value = static_cast<float>(index);
        red[index] = 10.0F + value;
        green[index] = IMATH_NAMESPACE::half(20.0F + value);
        depth[index] = 30.0F + value;
        diffuse[index] = 40.0F + value;
        normal[index] = IMATH_NAMESPACE::half(50.0F + value);
        specular[index] = 60.0F + value;
    }

    OPENEXR_IMF_NAMESPACE::FrameBuffer frame_buffer;
    const auto insert_float = [&frame_buffer](
        const std::string& name,
        std::array<float, kPixelCount>& values) {
        frame_buffer.insert(
            name,
            OPENEXR_IMF_NAMESPACE::Slice(
                OPENEXR_IMF_NAMESPACE::FLOAT,
                reinterpret_cast<char*>(values.data()),
                sizeof(float),
                sizeof(float) * static_cast<std::size_t>(kWidth)));
    };
    const auto insert_half = [&frame_buffer](
        const std::string& name,
        std::array<IMATH_NAMESPACE::half, kPixelCount>& values) {
        frame_buffer.insert(
            name,
            OPENEXR_IMF_NAMESPACE::Slice(
                OPENEXR_IMF_NAMESPACE::HALF,
                reinterpret_cast<char*>(values.data()),
                sizeof(IMATH_NAMESPACE::half),
                sizeof(IMATH_NAMESPACE::half) *
                    static_cast<std::size_t>(kWidth)));
    };

    insert_float("R", red);
    insert_half("G", green);
    insert_float("beauty.Z", depth);
    if (add_variance_channels) {
        insert_float("diffuse_mse.R", diffuse);
        insert_half("normal_var.X", normal);
        insert_float("specular_mse.B", specular);
    }

    OPENEXR_IMF_NAMESPACE::OutputFile output(path.string().c_str(), header);
    output.setFrameBuffer(frame_buffer);
    output.writePixels(kHeight);
}

[[nodiscard]] bool has_channel(
    const OPENEXR_IMF_NAMESPACE::ChannelList& channels,
    const std::string& name) {
    return channels.findChannel(name) != nullptr;
}

void expect_clean_file(const std::filesystem::path& path) {
    OPENEXR_IMF_NAMESPACE::InputFile input(path.string().c_str());
    expect(input.isComplete(), "Cleaned fixture is incomplete");

    const auto& channels = input.header().channels();
    expect(has_channel(channels, "R"), "R channel was not preserved");
    expect(has_channel(channels, "G"), "G channel was not preserved");
    expect(
        has_channel(channels, "beauty.Z"),
        "Unrelated layered channel was not preserved");
    expect(
        !has_channel(channels, "diffuse_mse.R"),
        "diffuse_mse channel was not removed");
    expect(
        !has_channel(channels, "normal_var.X"),
        "normal_var channel was not removed");
    expect(
        !has_channel(channels, "specular_mse.B"),
        "specular_mse channel was not removed");

    const auto& fixture =
        input.header()
            .typedAttribute<OPENEXR_IMF_NAMESPACE::StringAttribute>("fixture")
            .value();
    expect(
        fixture == "metadata must survive cleanup",
        "Custom header metadata was not preserved");

    std::array<float, kPixelCount> red{};
    OPENEXR_IMF_NAMESPACE::FrameBuffer frame_buffer;
    frame_buffer.insert(
        "R",
        OPENEXR_IMF_NAMESPACE::Slice(
            OPENEXR_IMF_NAMESPACE::FLOAT,
            reinterpret_cast<char*>(red.data()),
            sizeof(float),
            sizeof(float) * static_cast<std::size_t>(kWidth)));
    input.setFrameBuffer(frame_buffer);
    input.readPixels(0, kHeight - 1);

    for (std::size_t index = 0; index < kPixelCount; ++index) {
        expect(
            red[index] == 10.0F + static_cast<float>(index),
            "Retained pixel data changed");
    }
}

void test_dry_run_and_cleanup() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "render_variance.exr";
    write_fixture(path, true);

    const auto original_size = std::filesystem::file_size(path);
    const auto dry_run = exr_cleanup::clean_file(
        path, exr_cleanup::CleanupOptions{true, false});

    expect(
        dry_run.status == exr_cleanup::FileStatus::would_clean,
        "Dry-run status is incorrect");
    expect(
        dry_run.removed_channels.size() == 3,
        "Dry-run did not report all variance channels");
    expect(
        std::filesystem::file_size(path) == original_size,
        "Dry-run modified the source file");

    OPENEXR_IMF_NAMESPACE::InputFile unchanged(path.string().c_str());
    expect(
        has_channel(unchanged.header().channels(), "diffuse_mse.R"),
        "Dry-run removed a channel");

    const auto result = exr_cleanup::clean_file(
        path, exr_cleanup::CleanupOptions{false, true});
    expect(
        result.status == exr_cleanup::FileStatus::cleaned,
        "Cleanup status is incorrect");
    expect(
        result.backup_path == std::filesystem::path(path.string() + ".bak"),
        "Backup path is incorrect");
    expect(std::filesystem::exists(result.backup_path), "Backup was not created");

    expect_clean_file(path);

    OPENEXR_IMF_NAMESPACE::InputFile backup(result.backup_path.string().c_str());
    expect(
        has_channel(backup.header().channels(), "diffuse_mse.R"),
        "Backup does not contain the original channels");
}

void test_file_without_variance_channels_is_unchanged() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "beauty.exr";
    write_fixture(path, false);
    const auto original_size = std::filesystem::file_size(path);

    const auto result = exr_cleanup::clean_file(path);
    expect(
        result.status == exr_cleanup::FileStatus::no_matching_channels,
        "Unchanged file status is incorrect");
    expect(result.removed_channels.empty(), "Unexpected channels were removed");
    expect(
        std::filesystem::file_size(path) == original_size,
        "File without variance channels was rewritten");
}

void test_cleanup_without_backup_leaves_no_artifacts() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "render_variance.exr";
    write_fixture(path, true);

    const auto result = exr_cleanup::clean_file(path);
    expect(
        result.status == exr_cleanup::FileStatus::cleaned,
        "Cleanup without backup failed");
    expect(
        result.backup_path.empty(),
        "Cleanup reported an unexpected rollback artifact");

    std::size_t entry_count = 0;
    for (const auto& entry :
        std::filesystem::directory_iterator(directory.path())) {
        static_cast<void>(entry);
        ++entry_count;
    }
    expect(entry_count == 1, "Cleanup left a temporary or rollback artifact");
    expect_clean_file(path);
}

void test_existing_backup_prevents_replacement() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "render_variance.exr";
    const auto backup_path = std::filesystem::path(path.string() + ".bak");
    write_fixture(path, true);
    write_fixture(backup_path, false);
    const auto backup_size = std::filesystem::file_size(backup_path);

    bool rejected = false;
    try {
        static_cast<void>(exr_cleanup::clean_file(
            path, exr_cleanup::CleanupOptions{false, true}));
    } catch (const std::exception&) {
        rejected = true;
    }

    expect(rejected, "Existing backup path did not prevent replacement");
    expect(
        std::filesystem::file_size(backup_path) == backup_size,
        "Existing backup was modified");

    OPENEXR_IMF_NAMESPACE::InputFile source(path.string().c_str());
    expect(
        has_channel(source.header().channels(), "diffuse_mse.R"),
        "Source changed after backup collision");
}

void test_extension_detection() {
    expect(
        exr_cleanup::has_exr_extension("image.EXR"),
        "Uppercase EXR extension was not accepted");
    expect(
        !exr_cleanup::has_exr_extension("image.exr.bak"),
        "Backup file was accepted as an EXR candidate");
}

}  // namespace

int main() {
    try {
        test_dry_run_and_cleanup();
        test_file_without_variance_channels_is_unchanged();
        test_cleanup_without_backup_leaves_no_artifacts();
        test_existing_backup_prevents_replacement();
        test_extension_detection();
        std::cout << "All EXR Cleanup Tool tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return 1;
    }
}
