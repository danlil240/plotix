#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <thread>

#include "ui/workspace/workspace_autosave.hpp"

using namespace spectra;
using namespace std::chrono_literals;

namespace
{

// Build a unique temp path for autosave tests
std::string test_autosave_path(const std::string& suffix = "")
{
    return (std::filesystem::temp_directory_path()
            / ("spectra_autosave_test" + suffix + ".spectra"))
        .string();
}

}   // namespace

// ─── Constructor defaults ─────────────────────────────────────────────────────

TEST(WorkspaceAutosave, DefaultState)
{
    WorkspaceAutosave a;
    const std::string path = test_autosave_path("_default_state");
    std::error_code   ec;
    std::filesystem::remove(path, ec);
    a.set_autosave_path(path);
    EXPECT_FALSE(a.has_unsaved_changes());
    EXPECT_FALSE(a.has_autosave());
    EXPECT_EQ(a.last_saved_path(), "");
    EXPECT_EQ(a.interval(), std::chrono::seconds(60));
    EXPECT_EQ(a.debounce(), std::chrono::seconds(5));
    EXPECT_EQ(a.time_since_last_save(), WorkspaceAutosave::Duration::max());
}

// ─── mark_dirty ───────────────────────────────────────────────────────────────

TEST(WorkspaceAutosave, MarkDirtySetsDirtyFlag)
{
    WorkspaceAutosave a;
    EXPECT_FALSE(a.has_unsaved_changes());
    a.mark_dirty();
    EXPECT_TRUE(a.has_unsaved_changes());
}

// ─── tick does not save before debounce ──────────────────────────────────────

TEST(WorkspaceAutosave, TickDoesNotSaveBeforeDebounce)
{
    std::string       path = test_autosave_path("_nodebounce");
    WorkspaceAutosave a;
    a.set_autosave_path(path);
    a.set_debounce(std::chrono::seconds(60));   // very long debounce
    a.set_serialize_fn([] { return R"({"test":true})"; });

    a.mark_dirty();
    a.tick();   // debounce not expired

    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(a.has_unsaved_changes());
}

// ─── save_now writes file ─────────────────────────────────────────────────────

class AutosaveSaveTest : public ::testing::Test
{
   protected:
    std::string path_;

    void SetUp() override { path_ = test_autosave_path("_savenow"); }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
};

TEST_F(AutosaveSaveTest, SaveNowWritesFile)
{
    WorkspaceAutosave a;
    a.set_autosave_path(path_);
    a.set_serialize_fn([] { return R"({"saved":true})"; });

    a.mark_dirty();
    bool ok = a.save_now();

    EXPECT_TRUE(ok);
    EXPECT_TRUE(std::filesystem::exists(path_));
    EXPECT_FALSE(a.has_unsaved_changes());
}

TEST_F(AutosaveSaveTest, SaveNowMarksNotDirty)
{
    WorkspaceAutosave a;
    a.set_autosave_path(path_);
    a.set_serialize_fn([] { return "{}"; });

    a.mark_dirty();
    EXPECT_TRUE(a.has_unsaved_changes());
    a.save_now();
    EXPECT_FALSE(a.has_unsaved_changes());
}

TEST_F(AutosaveSaveTest, LastSavedPathAfterSave)
{
    WorkspaceAutosave a;
    a.set_autosave_path(path_);
    a.set_serialize_fn([] { return "{}"; });
    a.save_now();

    EXPECT_EQ(a.last_saved_path(), path_);
}

TEST_F(AutosaveSaveTest, TimeSinceLastSaveAfterSave)
{
    WorkspaceAutosave a;
    a.set_autosave_path(path_);
    a.set_serialize_fn([] { return "{}"; });
    a.save_now();

    // Should be a very small duration right after saving
    EXPECT_LT(a.time_since_last_save(), std::chrono::seconds(5));
}

TEST_F(AutosaveSaveTest, SaveNowWithoutSerializeFn)
{
    WorkspaceAutosave a;
    a.set_autosave_path(path_);
    // No serialize_fn set
    bool ok = a.save_now();
    EXPECT_FALSE(ok);
}

// ─── has_autosave ─────────────────────────────────────────────────────────────

TEST_F(AutosaveSaveTest, HasAutosaveAfterSave)
{
    WorkspaceAutosave a;
    a.set_autosave_path(path_);
    a.set_serialize_fn([] { return "{}"; });

    EXPECT_FALSE(a.has_autosave());
    a.save_now();
    EXPECT_TRUE(a.has_autosave());
}

TEST_F(AutosaveSaveTest, ClearAutosaveUsesConfiguredPathAndIsIdempotent)
{
    WorkspaceAutosave a;
    a.set_autosave_path(path_);
    a.set_serialize_fn([] { return "{}"; });
    ASSERT_TRUE(a.save_now());
    ASSERT_TRUE(a.has_autosave());

    EXPECT_TRUE(a.clear_autosave());
    EXPECT_FALSE(a.has_autosave());
    EXPECT_TRUE(a.clear_autosave());
}

// ─── autosave_is_newer_than ───────────────────────────────────────────────────

class AutosaveNewerTest : public ::testing::Test
{
   protected:
    std::string autosave_path_;
    std::string reference_path_;

    void SetUp() override
    {
        autosave_path_  = test_autosave_path("_newer_auto");
        reference_path_ = test_autosave_path("_newer_ref");
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(autosave_path_, ec);
        std::filesystem::remove(reference_path_, ec);
    }
};

TEST_F(AutosaveNewerTest, ReturnsFalseWhenNoAutosave)
{
    WorkspaceAutosave a;
    a.set_autosave_path(autosave_path_);
    EXPECT_FALSE(a.autosave_is_newer_than(reference_path_));
}

TEST_F(AutosaveNewerTest, ReturnsTrueWhenReferenceDoesNotExist)
{
    WorkspaceAutosave a;
    a.set_autosave_path(autosave_path_);
    a.set_serialize_fn([] { return "{}"; });
    a.save_now();

    // Reference file does not exist → autosave is "newer"
    EXPECT_TRUE(a.autosave_is_newer_than(reference_path_));
}

TEST_F(AutosaveNewerTest, ComparesTimestampsCorrectly)
{
    // Create reference file first
    {
        std::ofstream f(reference_path_);
        f << "old";
    }

    // Small sleep so filesystem timestamps differ
    std::this_thread::sleep_for(10ms);

    WorkspaceAutosave a;
    a.set_autosave_path(autosave_path_);
    a.set_serialize_fn([] { return "{}"; });
    a.save_now();

    EXPECT_TRUE(a.autosave_is_newer_than(reference_path_));
}

// ─── tick with short debounce/interval saves ─────────────────────────────────

class AutosaveTickTest : public ::testing::Test
{
   protected:
    std::string path_;

    void SetUp() override { path_ = test_autosave_path("_tick"); }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
};

TEST_F(AutosaveTickTest, TickSavesAfterDebounce)
{
    WorkspaceAutosave a;
    a.set_autosave_path(path_);
    a.set_debounce(std::chrono::milliseconds(0));
    a.set_interval(std::chrono::milliseconds(0));
    a.set_serialize_fn([] { return R"({"tick":true})"; });

    a.mark_dirty();
    a.tick();

    EXPECT_TRUE(std::filesystem::exists(path_));
    EXPECT_FALSE(a.has_unsaved_changes());
}

// ─── default_autosave_path ────────────────────────────────────────────────────

TEST(WorkspaceAutosave, DefaultAutosavePath)
{
    std::string path = WorkspaceAutosave::default_autosave_path();
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("autosave.spectra"), std::string::npos);
}
