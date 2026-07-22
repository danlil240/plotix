// spectra_design_tokens.cpp — Font loading and token implementation.

#include "spectra_design_tokens.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFileInfo>
#include <QStandardPaths>

namespace spectra::adapters::qt
{

SpectraFontManager& SpectraFontManager::instance()
{
    static SpectraFontManager mgr;
    return mgr;
}

void SpectraFontManager::load_from_file(const QString& path, bool is_icon)
{
    int id = QFontDatabase::addApplicationFont(path);
    if (id < 0)
        return;

    QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty())
        return;

    if (is_icon)
    {
        icon_family_ = families.first();
        has_icon_font_ = true;
    }
    else
    {
        // Determine which weight based on filename
        QString fname = QFileInfo(path).fileName().toLower();
        if (fname.contains("medium"))
        {
            inter_medium_family_ = families.first();
            has_inter_medium_ = true;
        }
        else if (fname.contains("semibold") || fname.contains("semibd"))
        {
            inter_semibold_family_ = families.first();
            has_inter_semibold_ = true;
        }
        else
        {
            inter_family_ = families.first();
            has_inter_ = true;
        }
    }
}

void SpectraFontManager::load_embedded()
{
    // Try loading from embedded font data if file loading fails
    // The icon font data is in third_party/icon_font_data.hpp but requires
    // raw TTF bytes — we use file-based loading instead.
}

void SpectraFontManager::load_fonts()
{
    if (loaded_)
        return;
    loaded_ = true;

    // Search paths for third_party fonts
    QStringList search_paths;
    QString app_dir = QApplication::applicationDirPath();

    // Common relative paths from build directory to third_party
    search_paths << app_dir + "/../third_party"
                 << app_dir + "/../../third_party"
                 << app_dir + "/../../../third_party"
                 << QDir::currentPath() + "/third_party"
                 << QDir::currentPath() + "/../third_party";

    // Also check source tree paths
    search_paths << "/home/daniel/projects/Spectra/third_party";

    QString third_party_dir;
    for (const auto& p : search_paths)
    {
        if (QFile::exists(p + "/Inter-Regular.ttf"))
        {
            third_party_dir = p;
            break;
        }
    }

    if (third_party_dir.isEmpty())
    {
        // Fallback: use system Inter if available
        inter_family_ = "Inter";
        has_inter_ = true;
        return;
    }

    // Load Inter Regular
    load_from_file(third_party_dir + "/Inter-Regular.ttf");

    // Inter Medium and Semibold — we only have Regular, so we synthesize
    // weight via QFont::weight() if the family doesn't have separate files.
    // Check for additional Inter weights
    if (QFile::exists(third_party_dir + "/Inter-Medium.ttf"))
        load_from_file(third_party_dir + "/Inter-Medium.ttf");
    if (QFile::exists(third_party_dir + "/Inter-SemiBold.ttf"))
        load_from_file(third_party_dir + "/Inter-SemiBold.ttf");

    // If we don't have separate Medium/Semibold files, use the Regular family
    // with weight modifiers
    if (!has_inter_medium_ && has_inter_)
    {
        inter_medium_family_ = inter_family_;
        has_inter_medium_ = true;
    }
    if (!has_inter_semibold_ && has_inter_)
    {
        inter_semibold_family_ = inter_family_;
        has_inter_semibold_ = true;
    }

    // Load Spectra icon font
    if (QFile::exists(third_party_dir + "/SpectraIcons.ttf"))
        load_from_file(third_party_dir + "/SpectraIcons.ttf", true);
    else if (QFile::exists(third_party_dir + "/SpectraIcons.otf"))
        load_from_file(third_party_dir + "/SpectraIcons.otf", true);

    // Set default application font
    if (has_inter_)
        QApplication::setFont(QFont(inter_family_, spectra_typography().font_base));
}

// ─── Font Role Getters ───────────────────────────────────────────────────────

QFont SpectraFontManager::font_base() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter", spectra_typography().font_base);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_small() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter", spectra_typography().font_sm);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_medium() const
{
    QFont f(has_inter_medium_ ? inter_medium_family_ :
            (has_inter_ ? inter_family_ : "Inter"),
            spectra_typography().font_base);
    f.setWeight(QFont::Medium);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_semibold() const
{
    QFont f(has_inter_semibold_ ? inter_semibold_family_ :
            (has_inter_ ? inter_family_ : "Inter"),
            spectra_typography().font_base);
    f.setWeight(QFont::DemiBold);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_title() const
{
    QFont f(has_inter_semibold_ ? inter_semibold_family_ :
            (has_inter_ ? inter_family_ : "Inter"),
            spectra_typography().font_xl);
    f.setWeight(QFont::DemiBold);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_wordmark() const
{
    QFont f(has_inter_semibold_ ? inter_semibold_family_ :
            (has_inter_ ? inter_family_ : "Inter"),
            spectra_typography().font_2xl);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 2.5f);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_status() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter", spectra_typography().font_xs);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_icon(int pixel_size) const
{
    QFont f(has_icon_font_ ? icon_family_ : "SpectraIcons", pixel_size);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont SpectraFontManager::font_mono() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter", spectra_typography().font_base);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setFixedPitch(true);
    return f;
}

QString SpectraFontManager::icon_codepoint(uint32_t code)
{
    // Convert Unicode codepoint to QString
    return QString::fromUcs4(&code, 1);
}

}   // namespace spectra::adapters::qt
