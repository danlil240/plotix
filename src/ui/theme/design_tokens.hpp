#pragma once

namespace spectra::ui::tokens
{

// Spacing Scale (base: 4px)
constexpr float SPACE_0  = 0.0f;    // No space
constexpr float SPACE_1  = 4.0f;    // Tight
constexpr float SPACE_2  = 8.0f;    // Compact
constexpr float SPACE_3  = 12.0f;   // Default
constexpr float SPACE_4  = 16.0f;   // Comfortable
constexpr float SPACE_5  = 20.0f;   // Spacious
constexpr float SPACE_6  = 24.0f;   // Section gap
constexpr float SPACE_8  = 32.0f;   // Panel padding
constexpr float SPACE_10 = 40.0f;   // Zone gap
constexpr float SPACE_12 = 48.0f;   // Large gap
constexpr float SPACE_16 = 64.0f;   // Extra large gap

// Radius Scale — unified r4/r8/r12 system
constexpr float RADIUS_SM   = 4.0f;     // r4: small controls, checkboxes, badges
constexpr float RADIUS_MD   = 8.0f;     // r8: inputs, tooltips, popovers, buttons
constexpr float RADIUS_LG   = 12.0f;    // r12: panels, floating toolbars, containers
constexpr float RADIUS_XL   = 16.0f;    // Large panels (rarely used)
constexpr float RADIUS_PILL = 999.0f;   // Pill buttons, scrollbar thumbs

// Font Scale (Inter) — typography hierarchy
constexpr float FONT_XS   = 10.5f;   // Status bar, badges, dimmer chrome
constexpr float FONT_SM   = 11.5f;   // Inspector labels, secondary text
constexpr float FONT_BASE = 13.0f;   // Body text, controls, menu items
constexpr float FONT_MD   = 14.0f;   // Input values, tree items
constexpr float FONT_LG   = 15.0f;   // Section headers
constexpr float FONT_XL   = 17.0f;   // Panel titles
constexpr float FONT_2XL  = 20.0f;   // Plot titles (via Vulkan text renderer)
constexpr float FONT_MONO = 12.0f;   // Data readout, coordinates

// Layout Rhythm (8px grid)
constexpr float PANEL_PADDING      = 16.0f;   // Panel internal padding
constexpr float SECTION_GAP        = 12.0f;   // Gap between inspector sections
constexpr float ROW_PADDING_V      = 4.0f;    // Vertical padding inside rows
constexpr float ROW_PADDING_H      = 8.0f;    // Horizontal padding inside rows
constexpr float SERIES_ROW_HEIGHT  = 30.0f;   // Series list row height
constexpr float ICON_BUTTON_HITBOX = 32.0f;   // Standard icon button size
constexpr float INSPECTOR_HEADER_H = 32.0f;   // Inspector section header strip height

// Layout Constants
constexpr float COMMAND_BAR_HEIGHT      = 48.0f;   // Premium top bar
constexpr float NAV_RAIL_WIDTH          = 56.0f;
constexpr float NAV_RAIL_WIDTH_EXPANDED = 200.0f;
constexpr float INSPECTOR_WIDTH         = 320.0f;
constexpr float INSPECTOR_WIDTH_MIN     = 240.0f;
constexpr float INSPECTOR_WIDTH_MAX     = 480.0f;
constexpr float STATUS_BAR_HEIGHT       = 34.0f;   // Roomy status pills
constexpr float FLOATING_TOOLBAR_HEIGHT = 48.0f;

// Animation Durations (in seconds)
constexpr float DURATION_INSTANT     = 0.0f;
constexpr float DURATION_FAST        = 0.1f;
constexpr float DURATION_NORMAL      = 0.15f;
constexpr float DURATION_SLOW        = 0.2f;
constexpr float DURATION_SLOWER      = 0.3f;
constexpr float DURATION_HOVER       = 0.08f;   // Hover transitions
constexpr float DURATION_TOOLTIP_IN  = 0.05f;   // Tooltip appear
constexpr float DURATION_TOOLTIP_OUT = 0.10f;   // Tooltip disappear
constexpr float DURATION_ZOOM            = 0.12f;   // Plot zoom animation
constexpr float DURATION_INSPECTOR_OPEN  = 0.22f;   // Inspector slide-in (ease-out)
constexpr float DURATION_INSPECTOR_CLOSE = 0.16f;   // Inspector slide-out (ease-in)
constexpr float DURATION_SECTION_EXPAND  = 0.18f;   // Inspector section unfold
constexpr float DURATION_SECTION_COLLAPSE = 0.14f;  // Inspector section fold

// Glow / Accent Effect Tokens
constexpr float GLOW_RADIUS_SM = 2.0f;   // Subtle inner glow
constexpr float GLOW_RADIUS_MD = 4.0f;   // Standard glow (active elements)
constexpr float GLOW_RADIUS_LG = 8.0f;   // Emphasis glow (selected items)

// Grid Tokens — per-theme alpha (Night / Dark / Light)
constexpr float GRID_MAJOR_ALPHA_NIGHT = 0.07f;
constexpr float GRID_MAJOR_ALPHA_DARK  = 0.14f;
constexpr float GRID_MAJOR_ALPHA_LIGHT = 0.20f;
constexpr float GRID_MINOR_ALPHA_NIGHT = 0.025f;
constexpr float GRID_MINOR_ALPHA_DARK  = 0.05f;
constexpr float GRID_MINOR_ALPHA_LIGHT = 0.10f;

// Inspector Rhythm
constexpr float SECTION_HEADER_HEIGHT  = 32.0f;
constexpr float SECTION_CONTENT_INSET  = 12.0f;   // Left indent for section content
constexpr float INSPECTOR_LABEL_WIDTH  = 80.0f;   // Fixed label column width
constexpr float INSPECTOR_INPUT_HEIGHT = 28.0f;   // Compact input height

// ─── Panel Component Language ────────────────────────────────────────────────
// Shared geometry for the reusable panel/card/chip system so every surface
// (inspector sections, modal groups, timeline header) reads as one design.
constexpr float CARD_RADIUS       = RADIUS_LG;   // r12: secondary grouped card
constexpr float CARD_PADDING      = 12.0f;       // internal padding of a card
constexpr float CARD_GAP          = 10.0f;       // vertical gap between cards
constexpr float CHIP_HEIGHT       = 24.0f;       // interactive chip / pill height
constexpr float CHIP_PADDING_H    = 10.0f;       // chip horizontal padding
constexpr float SEGMENT_TAB_H     = 30.0f;       // segmented control tab height
constexpr float SEGMENT_TRACK_PAD = 3.0f;        // inset of tabs inside the track
constexpr float MODAL_RADIUS      = 14.0f;       // floating dialog corner radius
constexpr float MODAL_PADDING     = 20.0f;       // floating dialog content padding
constexpr float MODAL_HEADER_H    = 48.0f;       // floating dialog header strip

// ─── Timeline Editing Surface ────────────────────────────────────────────────
constexpr float TIMELINE_RULER_HEIGHT = 26.0f;    // time ruler strip
constexpr float TIMELINE_TRACK_HEIGHT = 30.0f;    // single lane / track row
constexpr float TIMELINE_LABEL_WIDTH  = 150.0f;   // track-name gutter width

// Focus Ring
constexpr float FOCUS_RING_WIDTH  = 2.0f;
constexpr float FOCUS_RING_OFFSET = 2.0f;   // Offset from element edge

// Icon Sizes
constexpr float ICON_XS = 12.0f;
constexpr float ICON_SM = 16.0f;
constexpr float ICON_MD = 20.0f;
constexpr float ICON_LG = 24.0f;
constexpr float ICON_XL = 32.0f;

// Border Widths
constexpr float BORDER_WIDTH_THIN   = 0.5f;
constexpr float BORDER_WIDTH_NORMAL = 1.0f;
constexpr float BORDER_WIDTH_THICK  = 2.0f;

// Opacity Values
constexpr float OPACITY_HIDDEN  = 0.0f;
constexpr float OPACITY_FAINT   = 0.1f;
constexpr float OPACITY_SUBTLE  = 0.3f;
constexpr float OPACITY_MID     = 0.5f;
constexpr float OPACITY_STRONG  = 0.7f;
constexpr float OPACITY_VISIBLE = 0.9f;
constexpr float OPACITY_OPAQUE  = 1.0f;

// Shadow Intensity
constexpr float SHADOW_INTENSITY_SUBTLE   = 0.12f;
constexpr float SHADOW_INTENSITY_NORMAL   = 0.15f;
constexpr float SHADOW_INTENSITY_STRONG   = 0.20f;
constexpr float SHADOW_INTENSITY_STRONGER = 0.25f;

// Elevation System — shadow spread for depth hierarchy
constexpr float ELEVATION_0_SPREAD = 0.0f;    // Flat on surface (canvas)
constexpr float ELEVATION_1_SPREAD = 4.0f;    // Subtle lift (nav rail, status bar)
constexpr float ELEVATION_2_SPREAD = 8.0f;    // Floating panel (inspector, command bar)
constexpr float ELEVATION_3_SPREAD = 16.0f;   // Popup/dropdown
constexpr float ELEVATION_4_SPREAD = 24.0f;   // Modal overlay

// Panel gap — breathing room between floating panels and canvas
constexpr float PANEL_GAP = 2.0f;   // Subtle gap to reinforce floating feel

// Interaction Values
constexpr float HOVER_DISTANCE_THRESHOLD    = 8.0f;    // For data point snapping
constexpr float DRAG_THRESHOLD              = 4.0f;    // Minimum drag distance
constexpr float DOUBLE_CLICK_TIME           = 0.5f;    // Seconds between clicks
constexpr float TOOLTIP_DELAY               = 0.05f;   // Seconds before tooltip appears
constexpr float FLOATING_TOOLBAR_HIDE_DELAY = 2.0f;    // Seconds of inactivity before hiding

// Performance Targets
constexpr float FRAME_TIME_BUDGET_MS    = 16.67f;   // 60 FPS
constexpr float UI_FRAME_TIME_TARGET_MS = 2.0f;     // UI rendering budget
constexpr float TOOLTIP_TIME_TARGET_MS  = 0.1f;     // Tooltip query budget

// ─── Micro-Polish Component Language ─────────────────────────────────────────
// Shared geometry/effect constants for the app shell so toolbar, tab bar,
// status bar, command bar, and canvas frame stay visually consistent.

// Nav rail (left toolbar)
constexpr float NAV_RAIL_ICON_ALPHA_INACTIVE   = 0.86f;   // readable, not disabled
constexpr float NAV_RAIL_ICON_ALPHA_HOVER      = 0.96f;
constexpr float NAV_RAIL_ICON_ALPHA_ACTIVE     = 1.00f;
constexpr float NAV_RAIL_LABEL_ALPHA_INACTIVE  = 0.78f;   // readable tiny labels
constexpr float NAV_RAIL_LABEL_ALPHA_HOVER     = 0.95f;
constexpr float NAV_RAIL_LABEL_ALPHA_ACTIVE    = 1.00f;
constexpr float NAV_RAIL_ICON_SIZE_BASE        = 20.0f;   // matches ICON_MD
// Drawn via font_heading_ (baked at 11.5px); keep the requested draw size close
// to that baked size so ImGui's bitmap glyphs stay crisp (oversampled 4x/2x, but
// still degrades if rescaled too far). 13px reads clearly while staying <15%
// off the baked size.
constexpr float NAV_RAIL_LABEL_SIZE_BASE       = 13.0f;
// Floors applied after the rail's height-driven compression scale so labels/icons
// never shrink into illegibility on short windows (see nav_rail_scale_for_height()).
constexpr float NAV_RAIL_ICON_SIZE_MIN         = 14.0f;
constexpr float NAV_RAIL_LABEL_SIZE_MIN        = 11.0f;
constexpr float NAV_RAIL_GLOW_ALPHA_ACTIVE     = 0.14f;   // refined active glow
constexpr float NAV_RAIL_GLOW_ALPHA_HOVER      = 0.06f;
constexpr float NAV_RAIL_SURFACE_ALPHA_INACTIVE = 0.00f;  // no bg until interaction
constexpr float NAV_RAIL_SURFACE_ALPHA_ACTIVE  = 0.90f;
constexpr float NAV_RAIL_SURFACE_ALPHA_HOVER   = 0.72f;
constexpr float NAV_RAIL_BORDER_ALPHA_ACTIVE   = 0.55f;
constexpr float NAV_RAIL_BORDER_ALPHA_HOVER    = 0.36f;

// Tab bar
constexpr float TAB_BAR_SELECTED_HEIGHT_EXTRA  = 3.0f;    // selected tab grows upward
constexpr float TAB_BAR_HORIZONTAL_PADDING     = 22.0f;   // title side padding
constexpr float TAB_BAR_CLOSE_BTN_SIZE         = 14.0f;
constexpr float TAB_BAR_CLOSE_BTN_PAD_RIGHT    = 10.0f;
constexpr float TAB_BAR_PLUS_SIZE              = 6.0f;    // half-bar of plus sign
constexpr float TAB_BAR_UNDERLINE_HEIGHT       = 2.5f;
constexpr float TAB_BAR_RADIUS                 = 8.0f;    // r8
constexpr float TAB_BAR_MIN_WIDTH              = 104.0f;
constexpr float TAB_BAR_MAX_WIDTH              = 220.0f;
constexpr float TAB_BAR_ADD_BTN_WIDTH          = 34.0f;
constexpr float TAB_BAR_ICON_TITLE_GAP         = 4.0f;    // if icon+title

// Command bar / top navigation
constexpr float COMMAND_BAR_ITEM_SPACING       = 14.0f;   // tighten menu gaps
constexpr float COMMAND_BAR_HOME_ICON_SCALE    = 0.76f;   // make Home less dominant
constexpr float COMMAND_BAR_HOME_ALPHA         = 0.80f;   // quieter home icon
constexpr float COMMAND_BAR_MENU_TEXT_ALPHA    = 0.94f;   // readable menus
constexpr float COMMAND_BAR_BRAND_TO_HOME_GAP  = 10.0f;   // cohesive group
constexpr float COMMAND_BAR_HOME_TO_MENU_GAP   = 16.0f;

// Status bar
constexpr float STATUS_BAR_PADDING_H           = 16.0f;   // left/right room
constexpr float STATUS_BAR_PILL_HEIGHT         = 22.0f;   // fixed pill height
constexpr float STATUS_BAR_PILL_PAD_H          = 9.0f;    // horizontal inside pill
constexpr float STATUS_BAR_PILL_PAD_V          = 3.0f;    // vertical inside pill
constexpr float STATUS_BAR_PILL_RADIUS         = 11.0f;   // full pill
constexpr float STATUS_BAR_GROUP_GAP           = 18.0f;   // breathing room between left items
constexpr float STATUS_BAR_PERF_GAP            = 16.0f;   // gap between fps and gpu
constexpr float STATUS_BAR_TEXT_ALPHA          = 0.92f;
constexpr float STATUS_BAR_FPS_PILL_PAD_H      = 10.0f;   // Vision-scale lime badge
constexpr float STATUS_BAR_FPS_PILL_PAD_V      = 4.0f;

// Canvas / plot frame
constexpr float CANVAS_FRAME_ROUNDING          = 14.0f;
constexpr float CANVAS_FRAME_INSET             = 1.5f;    // inner rim inset
constexpr float CANVAS_FRAME_BORDER_ALPHA      = 0.55f;
constexpr float CANVAS_FRAME_INNER_ALPHA       = 0.35f;
constexpr float CANVAS_FRAME_GLOW_ALPHA        = 0.10f;
constexpr float CANVAS_VIGNETTE_ALPHA          = 0.16f;

// ROS / product panel component geometry
constexpr float TOOLBAR_BUTTON_SIZE            = 28.0f;
constexpr float TOOLBAR_BUTTON_GAP             = 4.0f;
constexpr float TOOLBAR_HEIGHT                 = 36.0f;
constexpr float STAT_CARD_MIN_WIDTH            = 120.0f;
constexpr float STAT_CARD_HEIGHT               = 64.0f;
constexpr float STAT_CARD_GAP                  = 8.0f;
constexpr float EMPTY_STATE_ICON_SIZE          = 40.0f;
constexpr float EMPTY_STATE_MAX_WIDTH          = 360.0f;
constexpr float EMPTY_STATE_ACTION_GAP         = 8.0f;
constexpr float DROP_ZONE_BORDER_WIDTH         = 2.0f;
constexpr float DROP_ZONE_DASH_LEN             = 8.0f;
constexpr float DROP_ZONE_ALPHA_VALID          = 0.22f;
constexpr float DROP_ZONE_ALPHA_INVALID        = 0.14f;
constexpr float PANEL_HEADER_HEIGHT            = 40.0f;
constexpr float SEARCH_BOX_HEIGHT              = 28.0f;
constexpr float SEARCH_CLEAR_BTN_SIZE          = 18.0f;
constexpr float LEGEND_ROW_HEIGHT              = 22.0f;
constexpr float LEGEND_SWATCH_SIZE             = 10.0f;
constexpr float TIME_PRESET_BTN_WIDTH          = 36.0f;

}   // namespace spectra::ui::tokens
