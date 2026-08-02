#include "platform/clipboard_image.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(ClipboardImage, CopyDoesNotCrash)
{
    // Minimal valid PNG header — must not terminate the process when the helper
    // pipe breaks (missing xclip) or when the helper accepts partial data.
    static const uint8_t kTinyPng[] = {
        0x89,
        0x50,
        0x4E,
        0x47,
        0x0D,
        0x0A,
        0x1A,
        0x0A,
    };
    (void)spectra::platform::copy_image_to_clipboard(kTinyPng, sizeof(kTinyPng));
    SUCCEED();
}

TEST(ClipboardImage, RejectsEmptyPayload)
{
    EXPECT_FALSE(spectra::platform::copy_image_to_clipboard(nullptr, 0));
    const uint8_t byte = 0;
    EXPECT_FALSE(spectra::platform::copy_image_to_clipboard(&byte, 0));
}
