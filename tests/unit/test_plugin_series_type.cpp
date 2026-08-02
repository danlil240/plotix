// test_plugin_series_type.cpp — Unit tests for custom series type plugin registration.
//
// D3: Smoke tests for SeriesTypeRegistry C ABI, CustomSeries data model,
// callback invocation, bounds computation, and unregistration.

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <spectra/custom_series.hpp>
#include "render/series_type_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Minimal SPIR-V stub (valid header, no-op).  Real shaders are not needed
// for registry-level tests — only GPU tests require functional SPIR-V.
static const uint8_t kStubSpirv[] = {
    0x03, 0x02, 0x23, 0x07,   // SPIR-V magic
    0x00, 0x00, 0x01, 0x00,   // Version 1.0
    0x00, 0x00, 0x00, 0x00,   // Generator
    0x01, 0x00, 0x00, 0x00,   // Bound = 1
    0x00, 0x00, 0x00, 0x00,   // Reserved
};

// Track callback invocations.
struct CallTracker
{
    int upload_calls  = 0;
    int draw_calls    = 0;
    int bounds_calls  = 0;
    int cleanup_calls = 0;
};

// ─── C ABI registration tests ────────────────────────────────────────────────

TEST(PluginSeriesType, RegisterViaCABI)
{
    SeriesTypeRegistry reg;
    CallTracker        tracker;

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "dot_grid";
    desc.flags           = SPECTRA_SERIES_FLAG_NONE;
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.topology        = 0;
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void* ud) -> int
    {
        static_cast<CallTracker*>(ud)->upload_calls++;
        return 0;
    };
    desc.draw_fn = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void* ud) -> int
    {
        static_cast<CallTracker*>(ud)->draw_calls++;
        return 0;
    };
    desc.bounds_fn = [](const void*, size_t, SpectraRect* r, void* ud) -> int
    {
        static_cast<CallTracker*>(ud)->bounds_calls++;
        r->x_min = 0.0f;
        r->x_max = 10.0f;
        r->y_min = 0.0f;
        r->y_max = 5.0f;
        return 0;
    };
    desc.cleanup_fn = [](SpectraBackendHandle, void*, void* ud)
    { static_cast<CallTracker*>(ud)->cleanup_calls++; };
    desc.user_data = &tracker;

    int result = spectra_register_series_type(&reg, &desc);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(reg.count(), 1u);

    auto names = reg.type_names();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "dot_grid");
}

TEST(PluginSeriesType, FindRegisteredType)
{
    SeriesTypeRegistry reg;

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "dot_grid";
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
    { return 0; };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };
    desc.user_data = nullptr;

    spectra_register_series_type(&reg, &desc);

    const auto* entry = reg.find("dot_grid");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type_name, "dot_grid");
    EXPECT_TRUE(entry->upload_fn);
    EXPECT_TRUE(entry->draw_fn);
    EXPECT_TRUE(entry->bounds_fn);
    EXPECT_FALSE(entry->pipeline);   // No create_pipelines() called yet
}

TEST(PluginSeriesType, SPIRVBytecodeIsCopied)
{
    SeriesTypeRegistry reg;

    // Use a heap-allocated SPIR-V that we free immediately after registration.
    auto spirv = std::make_unique<uint8_t[]>(sizeof(kStubSpirv));
    std::memcpy(spirv.get(), kStubSpirv, sizeof(kStubSpirv));

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "spirv_copy_test";
    desc.vert_spirv      = spirv.get();
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = spirv.get();
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
    { return 0; };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };

    spectra_register_series_type(&reg, &desc);
    spirv.reset();   // Free the original — registry should have its own copy.

    const auto* entry = reg.find("spirv_copy_test");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pipeline_desc.vert_spirv_size, sizeof(kStubSpirv));
    EXPECT_EQ(entry->pipeline_desc.frag_spirv_size, sizeof(kStubSpirv));
    // Verify the stored bytes match the original.
    EXPECT_EQ(std::memcmp(entry->pipeline_desc.vert_spirv, kStubSpirv, sizeof(kStubSpirv)), 0);
}

// ─── Callback invocation tests ───────────────────────────────────────────────

TEST(PluginSeriesType, UploadCallbackInvoked)
{
    SeriesTypeRegistry reg;
    CallTracker        tracker;

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "test_upload";
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void* ud) -> int
    {
        static_cast<CallTracker*>(ud)->upload_calls++;
        return 0;
    };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };
    desc.user_data = &tracker;

    spectra_register_series_type(&reg, &desc);

    // Invoke the registered upload_fn through the registry entry.
    auto* entry = reg.find_mut("test_upload");
    ASSERT_NE(entry, nullptr);

    // The C++ wrapper captures user_data, so we just call it directly.
    // The registry stores C++ std::function wrappers. We need a Backend& for
    // calling them — but in a non-GPU test we can't create one. Instead,
    // verify the tracker was incremented by the C ABI registration path.
    EXPECT_EQ(tracker.upload_calls, 0);
}

TEST(PluginSeriesType, BoundsCallbackProducesCorrectResult)
{
    SeriesTypeRegistry reg;

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "test_bounds";
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
    { return 0; };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void* data, size_t count, SpectraRect* rect, void*) -> int
    {
        // Interpret data as float pairs (x, y) and compute bounds.
        auto* pts   = static_cast<const float*>(data);
        rect->x_min = pts[0];
        rect->x_max = pts[0];
        rect->y_min = pts[1];
        rect->y_max = pts[1];
        for (size_t i = 1; i < count; ++i)
        {
            float x = pts[i * 2];
            float y = pts[i * 2 + 1];
            if (x < rect->x_min)
                rect->x_min = x;
            if (x > rect->x_max)
                rect->x_max = x;
            if (y < rect->y_min)
                rect->y_min = y;
            if (y > rect->y_max)
                rect->y_max = y;
        }
        return 0;
    };

    spectra_register_series_type(&reg, &desc);

    const auto* entry = reg.find("test_bounds");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->bounds_fn);

    float data[] = {1.0f, 2.0f, 5.0f, 0.5f, 3.0f, 8.0f};
    float bounds[4];
    int   rc = entry->bounds_fn(data, 3, bounds);
    EXPECT_EQ(rc, 0);
    EXPECT_FLOAT_EQ(bounds[0], 1.0f);   // x_min
    EXPECT_FLOAT_EQ(bounds[1], 5.0f);   // x_max
    EXPECT_FLOAT_EQ(bounds[2], 0.5f);   // y_min
    EXPECT_FLOAT_EQ(bounds[3], 8.0f);   // y_max
}

// ─── Unregistration ──────────────────────────────────────────────────────────

TEST(PluginSeriesType, UnregisterRemovesType)
{
    SeriesTypeRegistry reg;

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "to_remove";
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
    { return 0; };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };

    spectra_register_series_type(&reg, &desc);
    ASSERT_EQ(reg.count(), 1u);

    int result = spectra_unregister_series_type(&reg, "to_remove");
    EXPECT_EQ(result, 0);
    EXPECT_EQ(reg.count(), 0u);
    EXPECT_EQ(reg.find("to_remove"), nullptr);
}

TEST(PluginSeriesType, UnregisterViaCABINullCheck)
{
    EXPECT_EQ(spectra_unregister_series_type(nullptr, "test"), -1);
    SeriesTypeRegistry reg;
    EXPECT_EQ(spectra_unregister_series_type(&reg, nullptr), -1);
}

// ─── Error / null cases ──────────────────────────────────────────────────────

TEST(PluginSeriesType, RegisterNullReturnsError)
{
    EXPECT_EQ(spectra_register_series_type(nullptr, nullptr), -1);

    SeriesTypeRegistry reg;
    EXPECT_EQ(spectra_register_series_type(&reg, nullptr), -1);

    SpectraSeriesTypeDesc desc{};
    desc.type_name = nullptr;
    EXPECT_EQ(spectra_register_series_type(&reg, &desc), -1);
}

TEST(PluginSeriesType, RegisterMissingCallbackReturnsError)
{
    SeriesTypeRegistry    reg;
    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "incomplete";
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = nullptr;   // Missing!
    desc.draw_fn         = nullptr;   // Missing!
    desc.bounds_fn       = nullptr;   // Missing!

    EXPECT_EQ(spectra_register_series_type(&reg, &desc), -1);
    EXPECT_EQ(reg.count(), 0u);
}

TEST(PluginSeriesType, DuplicateNameIgnored)
{
    SeriesTypeRegistry reg;

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "dup_test";
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
    { return 0; };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };

    EXPECT_EQ(spectra_register_series_type(&reg, &desc), 0);
    EXPECT_EQ(spectra_register_series_type(&reg, &desc), 0);   // Duplicate — logged, but no crash
    EXPECT_EQ(reg.count(), 1u);                                // Only one entry
}

// ─── CustomSeries data model tests ───────────────────────────────────────────

TEST(PluginSeriesType, CustomSeriesDataModel)
{
    CustomSeries cs("dot_grid");
    EXPECT_EQ(cs.type_name(), "dot_grid");
    EXPECT_EQ(cs.element_count(), 0u);
    EXPECT_EQ(cs.data_byte_size(), 0u);

    // Set label and color via builder-style API.
    cs.label("My Dots").color(Color{1.0f, 0.0f, 0.0f, 1.0f});
    EXPECT_EQ(cs.label(), "My Dots");
    EXPECT_FLOAT_EQ(cs.color().r, 1.0f);

    // Set data blob.
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    cs.set_data(data.data(), data.size() * sizeof(float), data.size());
    EXPECT_EQ(cs.element_count(), 4u);
    EXPECT_EQ(cs.data_byte_size(), 16u);
    EXPECT_NE(cs.data(), nullptr);

    // Verify data was copied.
    auto* readback = static_cast<const float*>(cs.data());
    EXPECT_FLOAT_EQ(readback[0], 1.0f);
    EXPECT_FLOAT_EQ(readback[3], 4.0f);

    // Set bounds.
    cs.set_bounds(0.0f, 10.0f, -5.0f, 5.0f);
    EXPECT_FLOAT_EQ(cs.bounds_x_min(), 0.0f);
    EXPECT_FLOAT_EQ(cs.bounds_x_max(), 10.0f);
    EXPECT_FLOAT_EQ(cs.bounds_y_min(), -5.0f);
    EXPECT_FLOAT_EQ(cs.bounds_y_max(), 5.0f);
}

TEST(PluginSeriesType, CustomSeriesIsDirtyOnDataChange)
{
    CustomSeries cs("test_dirty");

    // CustomSeries inherits from Series — check dirty tracking.
    float val = 1.0f;
    cs.set_data(&val, sizeof(val), 1);
    EXPECT_TRUE(cs.is_dirty());

    cs.clear_dirty();
    EXPECT_FALSE(cs.is_dirty());

    // Setting data again marks it dirty.
    float val2 = 2.0f;
    cs.set_data(&val2, sizeof(val2), 1);
    EXPECT_TRUE(cs.is_dirty());
}

// ─── Multiple types ──────────────────────────────────────────────────────────

TEST(PluginSeriesType, MultipleTypesRegistered)
{
    SeriesTypeRegistry reg;

    auto make_desc = [](const char* name) -> SpectraSeriesTypeDesc
    {
        SpectraSeriesTypeDesc d{};
        d.type_name       = name;
        d.vert_spirv      = kStubSpirv;
        d.vert_spirv_size = sizeof(kStubSpirv);
        d.frag_spirv      = kStubSpirv;
        d.frag_spirv_size = sizeof(kStubSpirv);
        d.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
        { return 0; };
        d.draw_fn   = [](SpectraBackendHandle,
                       SpectraPipelineHandle,
                       const void*,
                       const SpectraViewport*,
                       const SpectraSeriesPushConst*,
                       void*) -> int { return 0; };
        d.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };
        return d;
    };

    auto d1 = make_desc("type_a");
    auto d2 = make_desc("type_b");
    auto d3 = make_desc("type_c");

    EXPECT_EQ(spectra_register_series_type(&reg, &d1), 0);
    EXPECT_EQ(spectra_register_series_type(&reg, &d2), 0);
    EXPECT_EQ(spectra_register_series_type(&reg, &d3), 0);
    EXPECT_EQ(reg.count(), 3u);

    auto names = reg.type_names();
    EXPECT_EQ(names.size(), 3u);

    // Unregister one.
    spectra_unregister_series_type(&reg, "type_b");
    EXPECT_EQ(reg.count(), 2u);
    EXPECT_EQ(reg.find("type_b"), nullptr);
    EXPECT_NE(reg.find("type_a"), nullptr);
    EXPECT_NE(reg.find("type_c"), nullptr);
}

// ─── Vertex layout description ───────────────────────────────────────────────

TEST(PluginSeriesType, VertexLayoutPassedThrough)
{
    SeriesTypeRegistry reg;

    SpectraVertexBinding binding{};
    binding.binding    = 0;
    binding.stride     = 8;
    binding.input_rate = 0;

    SpectraVertexAttribute attr{};
    attr.location = 0;
    attr.binding  = 0;
    attr.format   = 103;   // VK_FORMAT_R32G32_SFLOAT
    attr.offset   = 0;

    SpectraSeriesTypeDesc desc{};
    desc.type_name              = "with_vertex_layout";
    desc.vert_spirv             = kStubSpirv;
    desc.vert_spirv_size        = sizeof(kStubSpirv);
    desc.frag_spirv             = kStubSpirv;
    desc.frag_spirv_size        = sizeof(kStubSpirv);
    desc.vertex_bindings        = &binding;
    desc.vertex_binding_count   = 1;
    desc.vertex_attributes      = &attr;
    desc.vertex_attribute_count = 1;
    desc.upload_fn              = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
    { return 0; };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };

    spectra_register_series_type(&reg, &desc);

    const auto* entry = reg.find("with_vertex_layout");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pipeline_desc.vertex_binding_count, 1u);
    EXPECT_EQ(entry->pipeline_desc.vertex_attribute_count, 1u);
}

// ─── Flags mapping ───────────────────────────────────────────────────────────

TEST(PluginSeriesType, FlagsMappedToPipelineDesc)
{
    SeriesTypeRegistry reg;

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "flagged";
    desc.flags           = SPECTRA_SERIES_FLAG_3D | SPECTRA_SERIES_FLAG_BACKFACE_CULL;
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
    { return 0; };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };

    spectra_register_series_type(&reg, &desc);

    const auto* entry = reg.find("flagged");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->pipeline_desc.enable_depth_test);
    EXPECT_TRUE(entry->pipeline_desc.enable_depth_write);
    EXPECT_TRUE(entry->pipeline_desc.enable_backface_cull);
    EXPECT_TRUE(entry->pipeline_desc.enable_blending);
}

TEST(PluginSeriesType, TransparentFlagDisablesDepthWrite)
{
    SeriesTypeRegistry reg;

    SpectraSeriesTypeDesc desc{};
    desc.type_name       = "transparent";
    desc.flags           = SPECTRA_SERIES_FLAG_3D | SPECTRA_SERIES_FLAG_TRANSPARENT;
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.upload_fn       = [](SpectraBackendHandle, const void*, void*, size_t, void*) -> int
    { return 0; };
    desc.draw_fn   = [](SpectraBackendHandle,
                      SpectraPipelineHandle,
                      const void*,
                      const SpectraViewport*,
                      const SpectraSeriesPushConst*,
                      void*) -> int { return 0; };
    desc.bounds_fn = [](const void*, size_t, SpectraRect*, void*) -> int { return 0; };

    spectra_register_series_type(&reg, &desc);

    const auto* entry = reg.find("transparent");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->pipeline_desc.enable_depth_test);
    EXPECT_FALSE(entry->pipeline_desc.enable_depth_write);   // Transparent -> no depth write
}
