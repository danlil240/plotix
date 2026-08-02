#include "ui/native_dialog_policy.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace
{

void set_test_env(const char* name, const char* value)
{
#ifdef _WIN32
    const std::string assignment = std::string(name) + "=" + value;
    _putenv(assignment.c_str());
#else
    setenv(name, value, 1);
#endif
}

void unset_test_env(const char* name)
{
#ifdef _WIN32
    const std::string assignment = std::string(name) + "=";
    _putenv(assignment.c_str());
#else
    unsetenv(name);
#endif
}

class NativeDialogPolicyTest : public ::testing::Test
{
   protected:
    void SetUp() override { saved_enabled_ = spectra::native_dialogs_enabled(); }

    void TearDown() override { spectra::set_native_dialogs_enabled(saved_enabled_); }

    bool saved_enabled_ = true;
};

TEST_F(NativeDialogPolicyTest, StripsNoNativeDialogsFlag)
{
    char  arg0[] = "spectra";
    char  arg1[] = "--no-native-dialogs";
    char  arg2[] = "--help";
    char* argv[] = {arg0, arg1, arg2, nullptr};
    int   argc   = 3;

    spectra::set_native_dialogs_enabled(true);
    spectra::init_native_dialog_policy(argc, argv);

    EXPECT_EQ(argc, 2);
    EXPECT_STREQ(argv[1], "--help");
    EXPECT_FALSE(spectra::native_dialogs_enabled());
}

TEST_F(NativeDialogPolicyTest, EnvVarDisablesDialogs)
{
    spectra::set_native_dialogs_enabled(true);
    set_test_env("SPECTRA_NO_NATIVE_DIALOGS", "1");

    char  arg0[] = "spectra";
    char* argv[] = {arg0, nullptr};
    int   argc   = 1;
    spectra::init_native_dialog_policy(argc, argv);

    EXPECT_FALSE(spectra::native_dialogs_enabled());
    unset_test_env("SPECTRA_NO_NATIVE_DIALOGS");
}

}   // namespace
