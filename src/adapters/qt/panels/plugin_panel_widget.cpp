// plugin_panel_widget.cpp — Qt widget that renders the portable plugin UI schema.

#include "plugin_panel_widget.hpp"

#include "ui/workspace/plugin_ui_schema.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <spectra/logger.hpp>

#include <algorithm>

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
        registry_->set_change_listener(
            [this]()
            {
                // Use QMetaObject to invoke on the GUI thread
                QMetaObject::invokeMethod(this,
                                          &QtPluginPanelWidget::refresh,
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
    auto* scroll = qobject_cast<QScrollArea*>(widget());
    if (!registry_ || !scroll)
        return;

    QWidget* old_content = scroll->takeWidget();
    scroll_content_      = new QWidget(scroll);
    scroll->setWidget(scroll_content_);
    if (old_content)
        old_content->deleteLater();

    auto* layout = new QVBoxLayout(scroll_content_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto schemas = registry_->registered_schemas();
    if (schemas.empty())
    {
        auto* label = new QLabel("No plugin panels registered.", scroll_content_);
        label->setStyleSheet("color: gray; padding: 20px;");
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        layout->addStretch();
        return;
    }

    for (const auto& registered : schemas)
        build_schema_widget(registered.id, registered.schema);

    layout->addStretch();
}

void QtPluginPanelWidget::build_schema_widget(const std::string&    schema_id,
                                              const PluginUISchema& schema)
{
    auto* group = new QGroupBox(QString::fromStdString(schema.panel_title), scroll_content_);
    auto* form  = new QFormLayout(group);
    form->setContentsMargins(8, 12, 8, 8);
    form->setSpacing(6);

    std::vector<bool> is_child(schema.elements.size(), false);
    for (const auto& element : schema.elements)
        if (element.type == PluginUIElementType::Group)
            for (const size_t child : element.children)
                if (child < is_child.size())
                    is_child[child] = true;

    std::vector<size_t> ancestry;
    for (size_t index = 0; index < schema.elements.size(); ++index)
        if (!is_child[index])
            build_element_widget(schema_id, schema, index, form, group, ancestry);

    if (scroll_content_->layout())
        static_cast<QVBoxLayout*>(scroll_content_->layout())->addWidget(group);
}

void QtPluginPanelWidget::build_element_widget(const std::string&    schema_id,
                                               const PluginUISchema& schema,
                                               size_t                element_index,
                                               QFormLayout*          form,
                                               QWidget*              parent,
                                               std::vector<size_t>&  ancestry)
{
    if (element_index >= schema.elements.size()
        || std::ranges::find(ancestry, element_index) != ancestry.end())
        return;
    ancestry.push_back(element_index);
    const auto& elem = schema.elements[element_index];
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
                        auto* cb = new QCheckBox(parent);
                        cb->setObjectName("plugin_ui_property_" + QString::fromStdString(prop.id));
                        cb->setChecked(prop.value == "true");
                        cb->setToolTip(QString::fromStdString(prop.tooltip));
                        if (prop.read_only)
                            cb->setEnabled(false);

                        if (!prop.id.empty())
                        {
                            QString prop_id = QString::fromStdString(prop.id);
                            QString sid     = QString::fromStdString(schema_id);
                            QObject::connect(cb,
                                             &QCheckBox::toggled,
                                             parent,
                                             [this, cb, sid, prop_id](bool checked)
                                             {
                                                 if (registry_)
                                                 {
                                                     const auto actual =
                                                         registry_->set_property_value(
                                                             sid.toStdString(),
                                                             prop_id.toStdString(),
                                                             checked ? "true" : "false");
                                                     const bool actual_checked = actual == "true";
                                                     if (actual_checked != cb->isChecked())
                                                     {
                                                         QSignalBlocker blocker(cb);
                                                         cb->setChecked(actual_checked);
                                                     }
                                                 }
                                             });
                        }
                        form->addRow(QString::fromStdString(prop.label), cb);
                        break;
                    }

                    case PluginUIPropertyType::Integer:
                    {
                        auto* sb = new QSpinBox(parent);
                        sb->setObjectName("plugin_ui_property_" + QString::fromStdString(prop.id));
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
                        sb->setToolTip(QString::fromStdString(prop.tooltip));

                        if (!prop.id.empty())
                        {
                            QString prop_id = QString::fromStdString(prop.id);
                            QString sid     = QString::fromStdString(schema_id);
                            QObject::connect(
                                sb,
                                QOverload<int>::of(&QSpinBox::valueChanged),
                                parent,
                                [this, sb, sid, prop_id](int val)
                                {
                                    if (registry_)
                                    {
                                        const auto actual =
                                            registry_->set_property_value(sid.toStdString(),
                                                                          prop_id.toStdString(),
                                                                          std::to_string(val));
                                        const int actual_value = std::atoi(actual.c_str());
                                        if (actual_value != sb->value())
                                        {
                                            QSignalBlocker blocker(sb);
                                            sb->setValue(actual_value);
                                        }
                                    }
                                });
                        }
                        form->addRow(QString::fromStdString(prop.label), sb);
                        break;
                    }

                    case PluginUIPropertyType::Float:
                    {
                        auto* sb = new QDoubleSpinBox(parent);
                        sb->setObjectName("plugin_ui_property_" + QString::fromStdString(prop.id));
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
                        sb->setToolTip(QString::fromStdString(prop.tooltip));

                        if (!prop.id.empty())
                        {
                            QString prop_id = QString::fromStdString(prop.id);
                            QString sid     = QString::fromStdString(schema_id);
                            QObject::connect(
                                sb,
                                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                                parent,
                                [this, sb, sid, prop_id](double val)
                                {
                                    if (registry_)
                                    {
                                        const auto actual =
                                            registry_->set_property_value(sid.toStdString(),
                                                                          prop_id.toStdString(),
                                                                          std::to_string(val));
                                        const double actual_value = std::atof(actual.c_str());
                                        if (actual_value != sb->value())
                                        {
                                            QSignalBlocker blocker(sb);
                                            sb->setValue(actual_value);
                                        }
                                    }
                                });
                        }
                        form->addRow(QString::fromStdString(prop.label), sb);
                        break;
                    }

                    case PluginUIPropertyType::String:
                    {
                        auto* le = new QLineEdit(parent);
                        le->setObjectName("plugin_ui_property_" + QString::fromStdString(prop.id));
                        le->setText(QString::fromStdString(prop.value));
                        if (prop.read_only)
                            le->setReadOnly(true);
                        le->setToolTip(QString::fromStdString(prop.tooltip));

                        if (!prop.id.empty())
                        {
                            QString prop_id = QString::fromStdString(prop.id);
                            QString sid     = QString::fromStdString(schema_id);
                            QObject::connect(
                                le,
                                &QLineEdit::textChanged,
                                parent,
                                [this, le, sid, prop_id](const QString& text)
                                {
                                    if (registry_)
                                    {
                                        const auto actual =
                                            registry_->set_property_value(sid.toStdString(),
                                                                          prop_id.toStdString(),
                                                                          text.toStdString());
                                        if (actual != le->text().toStdString())
                                        {
                                            QSignalBlocker blocker(le);
                                            le->setText(QString::fromStdString(actual));
                                        }
                                    }
                                });
                        }
                        form->addRow(QString::fromStdString(prop.label), le);
                        break;
                    }

                    case PluginUIPropertyType::Enum:
                    {
                        auto* combo = new QComboBox(parent);
                        combo->setObjectName("plugin_ui_property_"
                                             + QString::fromStdString(prop.id));
                        for (const auto& opt : prop.enum_options)
                            combo->addItem(QString::fromStdString(opt));

                        int idx = std::atoi(prop.value.c_str());
                        if (idx >= 0 && idx < static_cast<int>(prop.enum_options.size()))
                            combo->setCurrentIndex(idx);
                        if (prop.read_only)
                            combo->setEnabled(false);
                        combo->setToolTip(QString::fromStdString(prop.tooltip));

                        if (!prop.id.empty())
                        {
                            QString prop_id = QString::fromStdString(prop.id);
                            QString sid     = QString::fromStdString(schema_id);
                            QObject::connect(
                                combo,
                                QOverload<int>::of(&QComboBox::currentIndexChanged),
                                parent,
                                [this, combo, sid, prop_id](int idx)
                                {
                                    if (registry_)
                                    {
                                        const auto actual =
                                            registry_->set_property_value(sid.toStdString(),
                                                                          prop_id.toStdString(),
                                                                          std::to_string(idx));
                                        const int actual_index = std::atoi(actual.c_str());
                                        if (actual_index != combo->currentIndex())
                                        {
                                            QSignalBlocker blocker(combo);
                                            combo->setCurrentIndex(actual_index);
                                        }
                                    }
                                });
                        }
                        form->addRow(QString::fromStdString(prop.label), combo);
                        break;
                    }

                    case PluginUIPropertyType::Color:
                    {
                        auto* le = new QLineEdit(parent);
                        le->setObjectName("plugin_ui_property_" + QString::fromStdString(prop.id));
                        le->setText(QString::fromStdString(prop.value));
                        if (prop.read_only)
                            le->setReadOnly(true);
                        le->setToolTip(QString::fromStdString(prop.tooltip));

                        if (!prop.id.empty())
                        {
                            QString prop_id = QString::fromStdString(prop.id);
                            QString sid     = QString::fromStdString(schema_id);
                            QObject::connect(
                                le,
                                &QLineEdit::textChanged,
                                parent,
                                [this, le, sid, prop_id](const QString& text)
                                {
                                    if (registry_)
                                    {
                                        const auto actual =
                                            registry_->set_property_value(sid.toStdString(),
                                                                          prop_id.toStdString(),
                                                                          text.toStdString());
                                        if (actual != le->text().toStdString())
                                        {
                                            QSignalBlocker blocker(le);
                                            le->setText(QString::fromStdString(actual));
                                        }
                                    }
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
                auto*       btn = new QPushButton(QString::fromStdString(act.label), parent);
                btn->setObjectName("plugin_ui_action_" + QString::fromStdString(act.id));
                btn->setEnabled(act.enabled);
                if (!act.tooltip.empty())
                    btn->setToolTip(QString::fromStdString(act.tooltip));

                if (!act.id.empty())
                {
                    QString action_id = QString::fromStdString(act.id);
                    QString sid       = QString::fromStdString(schema_id);
                    QObject::connect(btn,
                                     &QPushButton::clicked,
                                     parent,
                                     [this, sid, action_id]()
                                     {
                                         if (registry_)
                                             registry_->trigger_action(sid.toStdString(),
                                                                       action_id.toStdString());
                                     });
                }
                form->addRow("", btn);
                break;
            }

            case PluginUIElementType::Label:
            {
                auto* label = new QLabel(QString::fromStdString(elem.label.text), parent);
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
            {
                auto* separator = new QFrame(parent);
                separator->setFrameShape(QFrame::HLine);
                separator->setFrameShadow(QFrame::Sunken);
                form->addRow("", separator);
                break;
            }

            case PluginUIElementType::Group:
            {
                auto* sub_group = new QGroupBox(QString::fromStdString(elem.group.title), parent);
                sub_group->setObjectName("plugin_ui_group_"
                                         + QString::number(static_cast<qulonglong>(element_index)));
                sub_group->setCheckable(true);
                sub_group->setChecked(!elem.group.collapsed);
                auto* sub_form = new QFormLayout(sub_group);
                sub_form->setContentsMargins(8, 12, 8, 8);
                sub_form->setSpacing(6);
                for (const size_t child : elem.children)
                    build_element_widget(schema_id, schema, child, sub_form, sub_group, ancestry);
                form->addRow("", sub_group);
                break;
            }
        }
    }
    ancestry.pop_back();
}

}   // namespace spectra::adapters::qt
