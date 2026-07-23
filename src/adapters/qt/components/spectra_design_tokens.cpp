// spectra_design_tokens.cpp — Font loading and token implementation.

#include "spectra_design_tokens.hpp"

#include "../../../../third_party/fa_solid_900.hpp"
#include "inter_font_embedded.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFileInfo>
#include <QStandardPaths>
#include <QByteArray>

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
        icon_family_   = families.first();
        has_icon_font_ = true;
    }
    else
    {
        // Determine which weight based on filename
        QString fname = QFileInfo(path).fileName().toLower();
        if (fname.contains("medium"))
        {
            inter_medium_family_ = families.first();
            has_inter_medium_    = true;
        }
        else if (fname.contains("semibold") || fname.contains("semibd"))
        {
            inter_semibold_family_ = families.first();
            has_inter_semibold_    = true;
        }
        else
        {
            inter_family_ = families.first();
            has_inter_    = true;
        }
    }
}

void SpectraFontManager::load_embedded()
{
    const QByteArray inter_data =
        QByteArray::fromRawData(reinterpret_cast<const char*>(InterFont_ttf_data),
                                static_cast<int>(InterFont_ttf_size));
    const int         inter_id       = QFontDatabase::addApplicationFontFromData(inter_data);
    const QStringList inter_families = QFontDatabase::applicationFontFamilies(inter_id);
    if (!inter_families.isEmpty())
    {
        inter_family_          = inter_families.first();
        inter_medium_family_   = inter_family_;
        inter_semibold_family_ = inter_family_;
        has_inter_             = true;
        has_inter_medium_      = true;
        has_inter_semibold_    = true;
    }

    // This is the exact FA6 Solid font merged into every legacy ImGui font.
    const QByteArray icon_data =
        QByteArray::fromRawData(reinterpret_cast<const char*>(fa_solid_900_data),
                                static_cast<int>(fa_solid_900_size));
    const int         icon_id       = QFontDatabase::addApplicationFontFromData(icon_data);
    const QStringList icon_families = QFontDatabase::applicationFontFamilies(icon_id);
    if (!icon_families.isEmpty())
    {
        icon_family_   = icon_families.first();
        has_icon_font_ = true;
    }
}

void SpectraFontManager::load_fonts()
{
    if (loaded_)
        return;
    loaded_ = true;

    // Use the same embedded bytes as the legacy frontend first. Filesystem
    // lookup remains only as a development fallback.
    load_embedded();

    // Search paths for third_party fonts
    QStringList search_paths;
    QString     app_dir = QApplication::applicationDirPath();

    // Common relative paths from build directory to third_party
    search_paths << app_dir + "/../third_party" << app_dir + "/../../third_party"
                 << app_dir + "/../../../third_party" << QDir::currentPath() + "/third_party"
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
        if (!has_inter_)
        {
            inter_family_ = "Inter";
            has_inter_    = true;
        }
        QApplication::setFont(font_base());
        return;
    }

    // Load Inter Regular only when the embedded copy was unavailable.
    if (!has_inter_)
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
        has_inter_medium_    = true;
    }
    if (!has_inter_semibold_ && has_inter_)
    {
        inter_semibold_family_ = inter_family_;
        has_inter_semibold_    = true;
    }

    // Set default application font
    if (has_inter_)
        QApplication::setFont(font_base());
}

// ─── Font Role Getters ───────────────────────────────────────────────────────

QFont SpectraFontManager::font_base() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter");
    f.setPixelSize(spectra_typography().font_base);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_small() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter");
    f.setPixelSize(spectra_typography().font_sm);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_medium() const
{
    QFont f(has_inter_medium_ ? inter_medium_family_ : (has_inter_ ? inter_family_ : "Inter"));
    f.setPixelSize(spectra_typography().font_base);
    f.setWeight(QFont::Medium);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_semibold() const
{
    QFont f(has_inter_semibold_ ? inter_semibold_family_ : (has_inter_ ? inter_family_ : "Inter"));
    f.setPixelSize(spectra_typography().font_base);
    f.setWeight(QFont::DemiBold);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_title() const
{
    QFont f(has_inter_semibold_ ? inter_semibold_family_ : (has_inter_ ? inter_family_ : "Inter"));
    f.setPixelSize(spectra_typography().font_xl);
    f.setWeight(QFont::DemiBold);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_wordmark() const
{
    QFont f(has_inter_semibold_ ? inter_semibold_family_ : (has_inter_ ? inter_family_ : "Inter"));
    f.setPixelSize(spectra_typography().font_2xl);
    f.setWeight(QFont::Normal);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 2.2f);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_menubar() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter");
    f.setPixelSize(spectra_typography().font_menubar);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_status() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter");
    f.setPixelSize(11);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont SpectraFontManager::font_icon(int pixel_size) const
{
    QFont f(has_icon_font_ ? icon_family_ : spectra_typography().family_icon);
    f.setPixelSize(pixel_size);
    f.setWeight(QFont::Black);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont SpectraFontManager::font_mono() const
{
    QFont f(has_inter_ ? inter_family_ : "Inter");
    f.setPixelSize(spectra_typography().font_base);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setFixedPitch(true);
    return f;
}

QString SpectraFontManager::icon_codepoint(uint32_t code)
{
    // Convert Unicode codepoint to QString
    const char32_t codepoint = static_cast<char32_t>(code);
    return QString::fromUcs4(&codepoint, 1);
}

}   // namespace spectra::adapters::qt
