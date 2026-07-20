// plugin_panel_widget.cpp — Qt widget that renders the portable plugin UI schema.

#include "plugin_panel_widget.hpp"

#include "ui/workspace/plugin_ui_schema.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <spectra/logger.hpp>

namespace spectra::adapters::qt
{

QtPluginPanelWidget::QtPluginPanelWidget(PluginUIRegistry* registry, QWidget* parent)
    : QDockWidget("Plugin Panels", parent), registry_(registry)
{
    setObjectName("plugin_panel_dock");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll_content_ = new QWidget(scroll);
    scroll->setWidget(scroll_content_);

    setWidget(scroll);

    // Set up change listener so we refresh when schemas change
    if (registry_)
    {
        registry_->set_change_listener([this]() {
            // Use QMetaObject to invoke on the GUI thread
            QMetaObject::invokeMethod(this, &QtPluginPanelWidget::refresh,
                                      Qt::QueuedConnection);
        });
    }

    refresh();
}

QtPluginPanelWidget::~QtPluginPanelWidget()
{
    if (registry_)
        registry_->set_change_listener(nullptr);
}

void QtPluginPanelWidget::refresh()
{
    if (!registry_ || !scroll_content_)
        return;

    // Clear existing content
    auto* old_layout = scroll_content_->layout();
    delete old_layout;

    auto* layout = new QVBoxLayout(scroll_content_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto schemas = registry_->schemas();
    if (schemas.empty())
    {
        auto* label = new QLabel("No plugin panels registered.", scroll_content_);
        label->setStyleSheet("color: gray; padding: 20px;");
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        layout->addStretch();
        return;
    }

    for (auto& schema : schemas)
        build_schema_widget(schema);

    layout->addStretch();
}

void QtPluginPanelWidget::build_schema_widget(const PluginUISchema& schema)
{
    auto* group = new QGroupBox(QString::fromStdString(schema.panel_title), scroll_content_);
    auto* form  = new QFormLayout(group);
    form->setContentsMargins(8, 12, 8, 8);
    form->setSpacing(6);

    for (const auto& elem : schema.elements)
    {
        switch (elem.type)
        {
        case PluginUIElementType::Property:
        {
            const auto& prop = elem.property;

            switch (prop.type)
            {
            case PluginUIPropertyType::Boolean:
            {
                auto* cb = new QCheckBox(group);
                cb->setChecked(prop.value == "true");
                if (prop.read_only)
                    cb->setEnabled(false);

                if (!prop.id.empty())
                {
                    QString prop_id = QString::fromStdString(prop.id);
                    QString prop_val = QString::fromStdString(prop.value);
                    QObject::connect(cb, &QCheckBox::toggled, group,
                        [this, prop_id, prop_val](bool checked) {
                            if (registry_)
                                registry_->set_property_value(
                                    "", prop_id.toStdString(),
                                    checked ? "true" : "false");
                        });
                }
                form->addRow(QString::fromStdString(prop.label), cb);
                break;
            }

            case PluginUIPropertyType::Integer:
            {
                auto* sb = new QSpinBox(group);
                sb->setValue(std::atoi(prop.value.c_str()));
                if (!prop.min_value.empty())
                    sb->setMinimum(std::atoi(prop.min_value.c_str()));
                else
                    sb->setMinimum(-2147483647);
                if (!prop.max_value.empty())
                    sb->setMaximum(std::atoi(prop.max_value.c_str()));
                else
                    sb->setMaximum(2147483647);
                if (prop.read_only)
                    sb->setReadOnly(true);

                if (!prop.id.empty())
                {
                    QString prop_id = QString::fromStdString(prop.id);
                    QObject::connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
                        group, [this, prop_id](int val) {
                            if (registry_)
                                registry_->set_property_value(
                                    "", prop_id.toStdString(),
                                    std::to_string(val));
                        });
                }
                form->addRow(QString::fromStdString(prop.label), sb);
                break;
            }

            case PluginUIPropertyType::Float:
            {
                auto* sb = new QDoubleSpinBox(group);
                sb->setDecimals(6);
                sb->setValue(std::atof(prop.value.c_str()));
                if (!prop.min_value.empty())
                    sb->setMinimum(std::atof(prop.min_value.c_str()));
                else
                    sb->setMinimum(-1e18);
                if (!prop.max_value.empty())
                    sb->setMaximum(std::atof(prop.max_value.c_str()));
                else
                    sb->setMaximum(1e18);
                if (prop.read_only)
                    sb->setReadOnly(true);

                if (!prop.id.empty())
                {
                    QString prop_id = QString::fromStdString(prop.id);
                    QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        group, [this, prop_id](double val) {
                            if (registry_)
                                registry_->set_property_value(
                                    "", prop_id.toStdString(),
                                    std::to_string(val));
                        });
                }
                form->addRow(QString::fromStdString(prop.label), sb);
                break;
            }

            case PluginUIPropertyType::String:
            {
                auto* le = new QLineEdit(group);
                le->setText(QString::fromStdString(prop.value));
                if (prop.read_only)
                    le->setReadOnly(true);

                if (!prop.id.empty())
                {
                    QString prop_id = QString::fromStdString(prop.id);
                    QObject::connect(le, &QLineEdit::textChanged, group,
                        [this, prop_id](const QString& text) {
                            if (registry_)
                                registry_->set_property_value(
                                    "", prop_id.toStdString(),
                                    text.toStdString());
                        });
                }
                form->addRow(QString::fromStdString(prop.label), le);
                break;
            }

            case PluginUIPropertyType::Enum:
            {
                auto* combo = new QComboBox(group);
                for (const auto& opt : prop.enum_options)
                    combo->addItem(QString::fromStdString(opt));

                int idx = std::atoi(prop.value.c_str());
                if (idx >= 0 && idx < static_cast<int>(prop.enum_options.size()))
                    combo->setCurrentIndex(idx);
                if (prop.read_only)
                    combo->setEnabled(false);

                if (!prop.id.empty())
                {
                    QString prop_id = QString::fromStdString(prop.id);
                    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                        group, [this, prop_id](int idx) {
                            if (registry_)
                                registry_->set_property_value(
                                    "", prop_id.toStdString(),
                                    std::to_string(idx));
                        });
                }
                form->addRow(QString::fromStdString(prop.label), combo);
                break;
            }

            case PluginUIPropertyType::Color:
            {
                auto* le = new QLineEdit(group);
                le->setText(QString::fromStdString(prop.value));
                if (prop.read_only)
                    le->setReadOnly(true);

                if (!prop.id.empty())
                {
                    QString prop_id = QString::fromStdString(prop.id);
                    QObject::connect(le, &QLineEdit::textChanged, group,
                        [this, prop_id](const QString& text) {
                            if (registry_)
                                registry_->set_property_value(
                                    "", prop_id.toStdString(),
                                    text.toStdString());
                        });
                }
                form->addRow(QString::fromStdString(prop.label), le);
                break;
            }
            }
            break;
        }

        case PluginUIElementType::Action:
        {
            const auto& act = elem.action;
            auto* btn = new QPushButton(QString::fromStdString(act.label), group);
            btn->setEnabled(act.enabled);
            if (!act.tooltip.empty())
                btn->setToolTip(QString::fromStdString(act.tooltip));

            if (!act.id.empty())
            {
                QString action_id = QString::fromStdString(act.id);
                QObject::connect(btn, &QPushButton::clicked, group,
                    [this, action_id]() {
                        if (registry_)
                            registry_->trigger_action("", action_id.toStdString());
                    });
            }
            form->addRow("", btn);
            break;
        }

        case PluginUIElementType::Label:
        {
            auto* label = new QLabel(QString::fromStdString(elem.label.text), group);
            if (elem.label.style_hint == "heading")
            {
                QFont f = label->font();
                f.setBold(true);
                label->setFont(f);
            }
            else if (elem.label.style_hint == "warning")
                label->setStyleSheet("color: orange;");
            else if (elem.label.style_hint == "error")
                label->setStyleSheet("color: red;");
            else if (elem.label.style_hint == "status")
                label->setStyleSheet("color: gray;");

            form->addRow("", label);
            break;
        }

        case PluginUIElementType::Separator:
            form->addRow("", new QWidget(group));  // spacer
            break;

        case PluginUIElementType::Group:
        {
            auto* sub_group = new QGroupBox(QString::fromStdString(elem.group.title), group);
            sub_group->setCheckable(true);
            sub_group->setChecked(!elem.group.collapsed);
            form->addRow("", sub_group);
            break;
        }
        }
    }

    if (scroll_content_->layout())
        static_cast<QVBoxLayout*>(scroll_content_->layout())->addWidget(group);
}

}   // namespace spectra::adapters::qt
