// qt_automation_adapter.cpp — QtAutomationAdapter implementation.

#include "qt_automation_adapter.hpp"

#include "app/application_services.hpp"
#include "app/frontend_services.hpp"
#include "ui/automation/automation_dispatch.hpp"
#include "ui/automation/automation_json.hpp"
#include "ui/automation/automation_figure_ops.hpp"
#include "ui/automation/automation_server.hpp"
#include "ui/automation/mcp_server.hpp"
#include "ui/commands/command_registry.hpp"

#include <spectra/figure_registry.hpp>
#include <spectra/axes.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <sstream>

namespace spectra::adapters::qt
{

namespace
{

struct InputTarget
{
    QPointer<QWidget> widget;
    QPointer<QWindow> window;

    bool     valid() const { return widget || window; }
    QObject* object() const
    {
        return widget ? static_cast<QObject*>(widget.data()) : window.data();
    }
};

Qt::KeyboardModifiers to_qt_modifiers(int modifiers)
{
    Qt::KeyboardModifiers result = Qt::NoModifier;
    if ((modifiers & 0x0001) != 0)
        result |= Qt::ShiftModifier;
    if ((modifiers & 0x0002) != 0)
        result |= Qt::ControlModifier;
    if ((modifiers & 0x0004) != 0)
        result |= Qt::AltModifier;
    if ((modifiers & 0x0008) != 0)
        result |= Qt::MetaModifier;
    return result;
}

std::optional<Qt::MouseButton> to_qt_button(int button)
{
    switch (button)
    {
        case 0:
            return Qt::LeftButton;
        case 1:
            return Qt::RightButton;
        case 2:
            return Qt::MiddleButton;
        default:
            return std::nullopt;
    }
}

int to_qt_key(int key)
{
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
        return key;
    if (key >= 'a' && key <= 'z')
        return key - 'a' + Qt::Key_A;
    if ((key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde))
        return key;
    if (key >= 290 && key <= 314)
        return Qt::Key_F1 + (key - 290);

    switch (key)
    {
        case 256:
            return Qt::Key_Escape;
        case 257:
            return Qt::Key_Return;
        case 258:
            return Qt::Key_Tab;
        case 259:
            return Qt::Key_Backspace;
        case 260:
            return Qt::Key_Insert;
        case 261:
            return Qt::Key_Delete;
        case 262:
            return Qt::Key_Right;
        case 263:
            return Qt::Key_Left;
        case 264:
            return Qt::Key_Down;
        case 265:
            return Qt::Key_Up;
        case 266:
            return Qt::Key_PageUp;
        case 267:
            return Qt::Key_PageDown;
        case 268:
            return Qt::Key_Home;
        case 269:
            return Qt::Key_End;
        case 280:
            return Qt::Key_CapsLock;
        case 281:
            return Qt::Key_ScrollLock;
        case 282:
            return Qt::Key_NumLock;
        case 283:
            return Qt::Key_Print;
        case 284:
            return Qt::Key_Pause;
        case 340:
        case 344:
            return Qt::Key_Shift;
        case 341:
        case 345:
            return Qt::Key_Control;
        case 342:
        case 346:
            return Qt::Key_Alt;
        case 343:
        case 347:
            return Qt::Key_Meta;
        case 348:
            return Qt::Key_Menu;
        default:
            return Qt::Key_unknown;
    }
}

QString key_text(int key, Qt::KeyboardModifiers modifiers)
{
    if (key < Qt::Key_Space || key > Qt::Key_AsciiTilde
        || (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
    {
        return {};
    }

    QChar character(static_cast<char16_t>(key));
    if (character.isLetter() && !(modifiers & Qt::ShiftModifier))
        character = character.toLower();
    else if (modifiers & Qt::ShiftModifier)
    {
        switch (key)
        {
            case '1':
                character = '!';
                break;
            case '2':
                character = '@';
                break;
            case '3':
                character = '#';
                break;
            case '4':
                character = '$';
                break;
            case '5':
                character = '%';
                break;
            case '6':
                character = '^';
                break;
            case '7':
                character = '&';
                break;
            case '8':
                character = '*';
                break;
            case '9':
                character = '(';
                break;
            case '0':
                character = ')';
                break;
            case '-':
                character = '_';
                break;
            case '=':
                character = '+';
                break;
            case '[':
                character = '{';
                break;
            case ']':
                character = '}';
                break;
            case '\\':
                character = '|';
                break;
            case ';':
                character = ':';
                break;
            case '\'':
                character = '"';
                break;
            case ',':
                character = '<';
                break;
            case '.':
                character = '>';
                break;
            case '/':
                character = '?';
                break;
            case '`':
                character = '~';
                break;
            default:
                break;
        }
    }
    return QString(character);
}

QPointF global_position(QWidget* root, const QPointF& root_position)
{
    const QPoint integral = root_position.toPoint();
    const QPoint mapped   = root->mapToGlobal(integral);
    return QPointF(mapped) + (root_position - QPointF(integral));
}

QPointF local_position(const InputTarget& target, const QPointF& global)
{
    const QPoint integral = global.toPoint();
    const QPoint mapped   = target.widget ? target.widget->mapFromGlobal(integral)
                                          : target.window->mapFromGlobal(integral);
    return QPointF(mapped) + (global - QPointF(integral));
}

InputTarget resolve_pointer_target(QWidget* root, QWindow* canvas, const QPointF& position)
{
    if (!root || !std::isfinite(position.x()) || !std::isfinite(position.y())
        || !QRectF(QPointF(0.0, 0.0), QSizeF(root->size())).contains(position))
    {
        return {};
    }

    const QPointF global = global_position(root, position);
    if (canvas && canvas->isVisible())
    {
        const QPoint canvas_origin = canvas->mapToGlobal(QPoint(0, 0));
        const QRectF canvas_rect(QPointF(canvas_origin), QSizeF(canvas->size()));
        if (canvas_rect.contains(global))
            return {.window = canvas};
    }

    QWidget* widget = root->childAt(position.toPoint());
    return {.widget = widget ? widget : root};
}

InputTarget resolve_keyboard_target(QWidget* root, QWindow* canvas)
{
    if (canvas && QGuiApplication::focusWindow() == canvas)
        return {.window = canvas};
    QWidget* focused = QApplication::focusWidget();
    if (focused && root && (focused == root || root->isAncestorOf(focused)))
        return {.widget = focused};
    if (canvas)
        return {.window = canvas};
    if (root)
        return {.widget = root};
    return {};
}

bool send_mouse_event(const InputTarget&    target,
                      QEvent::Type          type,
                      const QPointF&        global,
                      Qt::MouseButton       button,
                      Qt::MouseButtons      buttons,
                      Qt::KeyboardModifiers modifiers)
{
    if (!target.valid())
        return false;
    const QPointF local = local_position(target, global);
    QMouseEvent   event(type, local, local, global, button, buttons, modifiers);
    return QCoreApplication::sendEvent(target.object(), &event);
}

bool is_qt_fuzz_denied_command(const std::string& id)
{
    static constexpr std::array<const char*, 12> kDenied = {
        "figure.close",
        "app.quit",
        "file.save_figure",
        "file.load_figure",
        "file.export_png",
        "file.export_svg",
        "file.copy_to_clipboard",
        "file.save_workspace",
        "file.load_workspace",
        "help.show",
        "accessibility.sonify_series",
        "data.export_html_table",
    };
    return std::ranges::any_of(kDenied, [&id](const char* denied) { return id == denied; });
}

void focus_pointer_target(const InputTarget& target)
{
    if (target.widget && (target.widget->focusPolicy() & Qt::ClickFocus))
        target.widget->setFocus(Qt::MouseFocusReason);
    else if (target.window)
        target.window->requestActivate();
}

}   // namespace

QtAutomationAdapter::QtAutomationAdapter(QObject* parent) : QObject(parent) {}

QtAutomationAdapter::~QtAutomationAdapter()
{
    stop();
}

bool QtAutomationAdapter::start(ApplicationServices* services, uint16_t port)
{
    if (running_.load(std::memory_order_relaxed))
        return true;

    if (!services)
        return false;

    services_ = services;

    // Determine port: explicit > env > default 9837
    if (port == 0)
    {
        const char* env_port = std::getenv("SPECTRA_MCP_PORT");
        if (env_port && *env_port)
            port = static_cast<uint16_t>(std::atoi(env_port));
        else
            port = 9837;
    }

    const bool automation_was_running = services_->automation() != nullptr;
    if (!automation_was_running)
    {
        if (!services_->start_automation("", "127.0.0.1", port))
        {
            SPECTRA_LOG_WARN("qt_automation", "Failed to start automation server");
            return false;
        }
        owns_automation_ = true;
    }

    if (!services_->automation() || !services_->mcp() || !services_->mcp()->is_running())
    {
        SPECTRA_LOG_WARN("qt_automation", "Application services did not provide a live MCP server");
        if (owns_automation_)
            services_->stop_automation();
        owns_automation_ = false;
        services_        = nullptr;
        return false;
    }

    // Drive the service-owned pending queue from the Qt event loop.
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(16);   // ~60fps
    connect(poll_timer_, &QTimer::timeout, this, &QtAutomationAdapter::on_poll_timeout);
    poll_timer_->start();

    last_frame_count_ = get_frame_count_fn_ ? get_frame_count_fn_() : 0;

    running_.store(true, std::memory_order_release);
    SPECTRA_LOG_INFO("qt_automation", "Using MCP server on " + services_->mcp()->endpoint());
    return true;
}

void QtAutomationAdapter::stop()
{
    if (!running_.load(std::memory_order_relaxed))
        return;

    running_.store(false, std::memory_order_release);

    if (poll_timer_)
    {
        poll_timer_->stop();
        delete poll_timer_;
        poll_timer_ = nullptr;
    }

    if (owns_automation_ && services_)
        services_->stop_automation();

    owns_automation_ = false;
    services_        = nullptr;
}

void QtAutomationAdapter::on_poll_timeout()
{
    if (!services_ || !services_->automation())
        return;

    const uint64_t frame_count = get_frame_count_fn_ ? get_frame_count_fn_() : last_frame_count_;
    const uint64_t frame_delta =
        frame_count >= last_frame_count_ ? frame_count - last_frame_count_ : 0;
    last_frame_count_ = frame_count;
    services_->automation()->poll(
        [this](AutomationRequest& request) { handle_request(request); },
        static_cast<uint32_t>(std::min<uint64_t>(frame_delta, UINT32_MAX)));
}

void QtAutomationAdapter::set_input_target(QWidget* root, GetCanvasWindowFn canvas_window)
{
    input_root_       = root;
    canvas_window_fn_ = std::move(canvas_window);
}

bool QtAutomationAdapter::handle_input_request(AutomationRequest& request)
{
    const bool is_input_method = request.method == "mouse_move" || request.method == "mouse_click"
                                 || request.method == "mouse_drag" || request.method == "scroll"
                                 || request.method == "key_press" || request.method == "text_input"
                                 || request.method == "double_click";
    if (!is_input_method)
        return false;

    QWidget* root   = input_root_.data();
    QWindow* canvas = canvas_window_fn_ ? canvas_window_fn_() : nullptr;
    if (!root)
    {
        request.response_json = json_error(request.id, "Qt automation input target is unavailable");
        return true;
    }

    if (request.method == "key_press")
    {
        if (!json_has_key(request.params_json, "key"))
        {
            request.response_json = json_error(request.id, "Missing parameter: key");
            return true;
        }
        const int key    = json_get_int(request.params_json, "key");
        const int qt_key = to_qt_key(key);
        if (qt_key == Qt::Key_unknown)
        {
            request.response_json =
                json_error(request.id, "Unsupported automation key code: " + std::to_string(key));
            return true;
        }

        InputTarget target = resolve_keyboard_target(root, canvas);
        if (!target.valid())
        {
            request.response_json = json_error(request.id, "Keyboard input target is unavailable");
            return true;
        }

        const auto modifiers = to_qt_modifiers(json_get_int(request.params_json, "modifiers", 0));
        const QString text   = key_text(qt_key, modifiers);
        QKeyEvent     press(QEvent::KeyPress, qt_key, modifiers, text);
        QCoreApplication::sendEvent(target.object(), &press);
        if (target.valid())
        {
            QKeyEvent release(QEvent::KeyRelease, qt_key, modifiers, text);
            QCoreApplication::sendEvent(target.object(), &release);
        }
        request.response_json = json_ok(request.id);
        return true;
    }

    if (request.method == "text_input")
    {
        if (!json_has_key(request.params_json, "text"))
        {
            request.response_json = json_error(request.id, "Missing parameter: text");
            return true;
        }
        InputTarget target = resolve_keyboard_target(root, canvas);
        if (!target.valid())
        {
            request.response_json = json_error(request.id, "Text input target is unavailable");
            return true;
        }

        const QString text = QString::fromStdString(json_get_string(request.params_json, "text"));
        QInputMethodEvent event;
        event.setCommitString(text);
        QCoreApplication::sendEvent(target.object(), &event);
        request.response_json =
            json_ok(request.id, "{\"chars\":" + std::to_string(text.size()) + "}");
        return true;
    }

    auto read_position = [&request](const char* x_key, const char* y_key, QPointF& position) -> bool
    {
        if (!json_has_key(request.params_json, x_key) || !json_has_key(request.params_json, y_key))
        {
            return false;
        }
        position = QPointF(json_get_number(request.params_json, x_key),
                           json_get_number(request.params_json, y_key));
        return std::isfinite(position.x()) && std::isfinite(position.y());
    };

    if (request.method == "mouse_drag")
    {
        QPointF start;
        QPointF end;
        if (!read_position("x1", "y1", start) || !read_position("x2", "y2", end))
        {
            request.response_json = json_error(request.id, "Missing or invalid drag coordinates");
            return true;
        }
        InputTarget target = resolve_pointer_target(root, canvas, start);
        if (!target.valid())
        {
            request.response_json = json_error(request.id, "Drag start is outside the Qt window");
            return true;
        }

        const int  button_code = json_get_int(request.params_json, "button", 0);
        const auto button      = to_qt_button(button_code);
        if (!button)
        {
            request.response_json =
                json_error(request.id,
                           "Unsupported automation mouse button: " + std::to_string(button_code));
            return true;
        }
        const auto modifiers = to_qt_modifiers(json_get_int(request.params_json, "modifiers", 0));
        const int  steps     = std::clamp(json_get_int(request.params_json, "steps", 10), 2, 100);

        focus_pointer_target(target);
        send_mouse_event(target,
                         QEvent::MouseMove,
                         global_position(root, start),
                         Qt::NoButton,
                         Qt::NoButton,
                         modifiers);
        send_mouse_event(target,
                         QEvent::MouseButtonPress,
                         global_position(root, start),
                         *button,
                         *button,
                         modifiers);
        for (int i = 1; i <= steps && target.valid(); ++i)
        {
            const qreal   t        = static_cast<qreal>(i) / static_cast<qreal>(steps);
            const QPointF position = start + (end - start) * t;
            send_mouse_event(target,
                             QEvent::MouseMove,
                             global_position(root, position),
                             Qt::NoButton,
                             *button,
                             modifiers);
        }
        if (target.valid())
        {
            send_mouse_event(target,
                             QEvent::MouseButtonRelease,
                             global_position(root, end),
                             *button,
                             Qt::NoButton,
                             modifiers);
        }
        request.response_json = json_ok(request.id);
        return true;
    }

    QPointF position;
    if (!read_position("x", "y", position))
    {
        request.response_json = json_error(request.id, "Missing or invalid pointer coordinates");
        return true;
    }
    InputTarget target = resolve_pointer_target(root, canvas, position);
    if (!target.valid())
    {
        request.response_json = json_error(request.id, "Pointer position is outside the Qt window");
        return true;
    }
    const QPointF global = global_position(root, position);

    if (request.method == "mouse_move")
    {
        send_mouse_event(target,
                         QEvent::MouseMove,
                         global,
                         Qt::NoButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        request.response_json = json_ok(request.id);
        return true;
    }

    if (request.method == "scroll")
    {
        const double dx = json_get_number(request.params_json, "dx", 0.0);
        const double dy = json_get_number(request.params_json, "dy", 1.0);
        if (!std::isfinite(dx) || !std::isfinite(dy))
        {
            request.response_json = json_error(request.id, "Invalid scroll delta");
            return true;
        }
        const QPointF local = local_position(target, global);
        const QPoint  angle_delta(static_cast<int>(std::lround(dx * 120.0)),
                                 static_cast<int>(std::lround(dy * 120.0)));
        QWheelEvent   event(local,
                          global,
                          QPoint(),
                          angle_delta,
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::ScrollUpdate,
                          false);
        QCoreApplication::sendEvent(target.object(), &event);
        request.response_json = json_ok(request.id);
        return true;
    }

    const int  button_code = json_get_int(request.params_json, "button", 0);
    const auto button      = to_qt_button(button_code);
    if (!button)
    {
        request.response_json =
            json_error(request.id,
                       "Unsupported automation mouse button: " + std::to_string(button_code));
        return true;
    }
    const auto modifiers = to_qt_modifiers(json_get_int(request.params_json, "modifiers", 0));
    focus_pointer_target(target);
    send_mouse_event(target, QEvent::MouseMove, global, Qt::NoButton, Qt::NoButton, modifiers);
    send_mouse_event(target, QEvent::MouseButtonPress, global, *button, *button, modifiers);
    if (target.valid())
    {
        send_mouse_event(target,
                         QEvent::MouseButtonRelease,
                         global,
                         *button,
                         Qt::NoButton,
                         modifiers);
    }

    if (request.method == "double_click" && target.valid())
    {
        send_mouse_event(target, QEvent::MouseButtonDblClick, global, *button, *button, modifiers);
        if (target.valid())
        {
            send_mouse_event(target,
                             QEvent::MouseButtonRelease,
                             global,
                             *button,
                             Qt::NoButton,
                             modifiers);
        }
    }
    request.response_json = json_ok(request.id);
    return true;
}

bool QtAutomationAdapter::handle_fuzz_request(AutomationRequest& request)
{
    if (request.method == "list_fuzz_actions")
    {
        std::ostringstream result;
        result << R"({"actions":[)";
        for (size_t i = 0; i < automation::kFuzzActionWeights.size(); ++i)
        {
            if (i > 0)
                result << ',';
            const auto& entry = automation::kFuzzActionWeights[i];
            result << R"({"action":")" << automation::fuzz_action_name(entry.action)
                   << R"(","weight":)" << entry.weight << '}';
        }
        result << "]}";
        request.response_json = json_ok(request.id, result.str());
        return true;
    }

    if (request.method == "fuzz_reset")
    {
        fuzz_step_index_ = 0;
        if (json_has_key(request.params_json, "seed"))
        {
            fuzz_base_seed_ = static_cast<uint64_t>(json_get_int(request.params_json, "seed", 0));
            fuzz_rng_       = std::mt19937(static_cast<uint32_t>(fuzz_base_seed_));
            fuzz_seed_explicit_ = true;
        }
        else
        {
            fuzz_rng_           = std::mt19937(std::random_device{}());
            fuzz_seed_explicit_ = false;
        }
        request.response_json =
            json_ok(request.id,
                    R"({"reset":true,"seed":)" + std::to_string(fuzz_base_seed_)
                        + R"(,"seed_explicit":)" + (fuzz_seed_explicit_ ? "true" : "false") + '}');
        return true;
    }

    if (request.method != "fuzz_step")
        return false;

    if (json_has_key(request.params_json, "seed"))
    {
        fuzz_base_seed_     = static_cast<uint64_t>(json_get_int(request.params_json, "seed", 0));
        fuzz_rng_           = std::mt19937(static_cast<uint32_t>(fuzz_base_seed_));
        fuzz_seed_explicit_ = true;
        fuzz_step_index_    = 0;
    }

    automation::FuzzAction action = automation::pick_weighted_fuzz_action(fuzz_rng_);
    const std::string      forced = json_get_string(request.params_json, "action");
    if (!forced.empty() && !automation::parse_fuzz_action(forced, action))
    {
        request.response_json = json_error(request.id, "Unknown fuzz action: " + forced);
        return true;
    }

    int               pump_frames = 1;
    const std::string details     = run_fuzz_action(action, pump_frames);
    ++fuzz_step_index_;

    std::ostringstream result;
    result << R"({"action":")" << automation::fuzz_action_name(action) << R"(","step":)"
           << fuzz_step_index_ << R"(,"seed":)" << (fuzz_seed_explicit_ ? fuzz_base_seed_ : 0)
           << R"(,"details":)" << details << R"(,"pump_frames":)" << pump_frames << '}';
    request.response_json = json_ok(request.id, result.str());
    return true;
}

std::string QtAutomationAdapter::run_fuzz_action(automation::FuzzAction action, int& pump_frames)
{
    using automation::FuzzAction;
    pump_frames = 1;
    std::ostringstream details;
    details << '{';

    const auto execute_command = [this](const std::string& id)
    {
        if (!services_)
            return false;
        return execute_cmd_fn_ ? execute_cmd_fn_(id) : services_->commands().execute(id);
    };
    const auto window_size = [this]()
    {
        if (input_root_ && input_root_->width() > 0 && input_root_->height() > 0)
            return std::pair{input_root_->width(), input_root_->height()};
        if (get_size_fn_)
        {
            const auto [width, height] = get_size_fn_();
            if (width > 0 && height > 0)
                return std::pair{static_cast<int>(width), static_cast<int>(height)};
        }
        return std::pair{1280, 720};
    };
    const auto send_fuzz_input = [this](const std::string& method, std::string params)
    {
        AutomationRequest input;
        input.method      = method;
        input.params_json = std::move(params);
        return handle_input_request(input)
               && input.response_json.find(R"("isError":true)") == std::string::npos;
    };

    switch (action)
    {
        case FuzzAction::ExecuteCommand:
        {
            if (!services_)
            {
                details << R"("skipped":"services_unavailable")";
                break;
            }
            std::vector<std::string> command_ids;
            for (const auto* command : services_->commands().all_commands())
                if (command)
                    command_ids.push_back(command->id);
            if (command_ids.empty())
            {
                details << R"("skipped":"no_commands")";
                break;
            }
            std::uniform_int_distribution<size_t> pick(0, command_ids.size() - 1);
            const std::string&                    id = command_ids[pick(fuzz_rng_)];
            if (is_qt_fuzz_denied_command(id))
                details << R"("skipped_command":")" << json_escape(id) << '"';
            else
                details << R"("command_id":")" << json_escape(id) << R"(","executed":)"
                        << (execute_command(id) ? "true" : "false");
            break;
        }
        case FuzzAction::MouseClick:
        {
            const auto [width, height] = window_size();
            std::uniform_real_distribution<double> x(0.0, std::max(0, width - 1));
            std::uniform_real_distribution<double> y(0.0, std::max(0, height - 1));
            std::uniform_int_distribution<int>     button(0, 1);
            const double                           px = x(fuzz_rng_);
            const double                           py = y(fuzz_rng_);
            const int                              b  = button(fuzz_rng_);
            std::ostringstream                     params;
            params << R"({"x":)" << px << R"(,"y":)" << py << R"(,"button":)" << b << '}';
            details << R"("x":)" << px << R"(,"y":)" << py << R"(,"button":)" << b
                    << R"(,"dispatched":)"
                    << (send_fuzz_input("mouse_click", params.str()) ? "true" : "false");
            break;
        }
        case FuzzAction::MouseDrag:
        {
            const auto [width, height] = window_size();
            std::uniform_real_distribution<double> x(0.0, std::max(0, width - 1));
            std::uniform_real_distribution<double> y(0.0, std::max(0, height - 1));
            const double                           x1 = x(fuzz_rng_);
            const double                           y1 = y(fuzz_rng_);
            const double                           x2 = x(fuzz_rng_);
            const double                           y2 = y(fuzz_rng_);
            std::ostringstream                     params;
            params << R"({"x1":)" << x1 << R"(,"y1":)" << y1 << R"(,"x2":)" << x2 << R"(,"y2":)"
                   << y2 << R"(,"steps":5})";
            details << R"("x1":)" << x1 << R"(,"y1":)" << y1 << R"(,"x2":)" << x2 << R"(,"y2":)"
                    << y2 << R"(,"dispatched":)"
                    << (send_fuzz_input("mouse_drag", params.str()) ? "true" : "false");
            break;
        }
        case FuzzAction::MouseScroll:
        {
            const auto [width, height] = window_size();
            std::uniform_real_distribution<double> x(0.0, std::max(0, width - 1));
            std::uniform_real_distribution<double> y(0.0, std::max(0, height - 1));
            std::uniform_real_distribution<double> delta(-3.0, 3.0);
            const double                           px = x(fuzz_rng_);
            const double                           py = y(fuzz_rng_);
            const double                           dy = delta(fuzz_rng_);
            std::ostringstream                     params;
            params << R"({"x":)" << px << R"(,"y":)" << py << R"(,"dy":)" << dy << '}';
            details << R"("x":)" << px << R"(,"y":)" << py << R"(,"dy":)" << dy
                    << R"(,"dispatched":)"
                    << (send_fuzz_input("scroll", params.str()) ? "true" : "false");
            break;
        }
        case FuzzAction::KeyPress:
        {
            std::uniform_int_distribution<int> key(32, 126);
            const int                          value = key(fuzz_rng_);
            details << R"("key":)" << value << R"(,"dispatched":)"
                    << (send_fuzz_input("key_press", "{\"key\":" + std::to_string(value) + '}')
                            ? "true"
                            : "false");
            break;
        }
        case FuzzAction::CreateFigure:
        {
            if (!services_ || !create_figure_fn_ || services_->figures().all_ids().size() >= 20)
            {
                details << R"("skipped":"max_figures_or_unavailable")";
                break;
            }
            std::uniform_int_distribution<int> size(640, 1600);
            const FigureId id     = create_figure_fn_(static_cast<uint32_t>(size(fuzz_rng_)),
                                                  static_cast<uint32_t>(size(fuzz_rng_)));
            Figure*        figure = services_->figures().get(id);
            if (!figure)
            {
                details << R"("created":false)";
                break;
            }
            auto&                                 axes = figure->subplot(1, 1, 1);
            std::uniform_int_distribution<int>    points(20, 200);
            std::uniform_real_distribution<float> value(-10.0f, 10.0f);
            const int                             count = points(fuzz_rng_);
            std::vector<float>                    xs(static_cast<size_t>(count));
            std::vector<float>                    ys(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                xs[static_cast<size_t>(i)] = static_cast<float>(i);
                ys[static_cast<size_t>(i)] = value(fuzz_rng_);
            }
            axes.line(xs, ys);
            details << R"("created":true,"figure_id":)" << id << R"(,"points":)" << count;
            break;
        }
        case FuzzAction::CloseFigure:
        {
            const auto ids = services_ ? services_->figures().all_ids() : std::vector<FigureId>{};
            if (ids.size() <= 1 || !switch_figure_fn_)
            {
                details << R"("skipped":"insufficient_figures_or_unavailable")";
                break;
            }
            std::uniform_int_distribution<size_t> pick(0, ids.size() - 1);
            const FigureId                        id       = ids[pick(fuzz_rng_)];
            const bool                            switched = switch_figure_fn_(id);
            const bool                            closed =
                switched
                && (close_figure_fn_ ? close_figure_fn_(id) : execute_command("figure.close"));
            details << R"("figure_id":)" << id << R"(,"closed":)" << (closed ? "true" : "false");
            break;
        }
        case FuzzAction::SwitchTab:
        {
            const auto ids = services_ ? services_->figures().all_ids() : std::vector<FigureId>{};
            if (ids.empty() || !switch_figure_fn_)
            {
                details << R"("skipped":"no_figures_or_unavailable")";
                break;
            }
            std::uniform_int_distribution<size_t> pick(0, ids.size() - 1);
            const FigureId                        id = ids[pick(fuzz_rng_)];
            details << R"("figure_id":)" << id << R"(,"switched":)"
                    << (switch_figure_fn_(id) ? "true" : "false");
            break;
        }
        case FuzzAction::AddSeries:
        {
            const auto ids = services_ ? services_->figures().all_ids() : std::vector<FigureId>{};
            if (ids.empty())
            {
                details << R"("skipped":"no_figures")";
                break;
            }
            std::uniform_int_distribution<size_t> figure_pick(0, ids.size() - 1);
            const FigureId                        id     = ids[figure_pick(fuzz_rng_)];
            Figure*                               figure = services_->figures().get(id);
            if (!figure)
            {
                details << R"("skipped":"figure_missing")";
                break;
            }
            auto&                                 axes = figure->subplot(1, 1, 1);
            std::uniform_int_distribution<int>    points(10, 200);
            std::uniform_real_distribution<float> value(-50.0f, 50.0f);
            const int                             count = points(fuzz_rng_);
            std::vector<float>                    xs(static_cast<size_t>(count));
            std::vector<float>                    ys(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                xs[static_cast<size_t>(i)] = static_cast<float>(i);
                ys[static_cast<size_t>(i)] = value(fuzz_rng_);
            }
            std::uniform_int_distribution<int> type(0, 1);
            if (type(fuzz_rng_) == 0)
                axes.line(xs, ys);
            else
                axes.scatter(xs, ys);
            details << R"("figure_id":)" << id << R"(,"points":)" << count;
            break;
        }
        case FuzzAction::UpdateData:
        {
            const auto ids = services_ ? services_->figures().all_ids() : std::vector<FigureId>{};
            if (ids.empty())
            {
                details << R"("skipped":"no_figures")";
                break;
            }
            std::uniform_int_distribution<size_t> pick(0, ids.size() - 1);
            Figure*     figure = services_->figures().get(ids[pick(fuzz_rng_)]);
            LineSeries* line   = nullptr;
            if (figure && !figure->axes().empty() && !figure->axes()[0]->series().empty())
                line = dynamic_cast<LineSeries*>(figure->axes_mut()[0]->series_mut()[0].get());
            if (!line)
            {
                details << R"("skipped":"no_line_series")";
                break;
            }
            std::vector<float>                    ys(line->x_data().size());
            std::uniform_real_distribution<float> value(-50.0f, 50.0f);
            for (float& item : ys)
                item = value(fuzz_rng_);
            line->set_y(ys);
            details << R"("points":)" << ys.size();
            break;
        }
        case FuzzAction::LargeDataset:
        {
            const auto ids = services_ ? services_->figures().all_ids() : std::vector<FigureId>{};
            if (ids.empty())
            {
                details << R"("skipped":"no_figures")";
                break;
            }
            std::uniform_int_distribution<size_t> pick(0, ids.size() - 1);
            Figure* figure = services_->figures().get(ids[pick(fuzz_rng_)]);
            if (!figure)
            {
                details << R"("skipped":"figure_missing")";
                break;
            }
            std::uniform_int_distribution<int> points(100000, 500000);
            const int                          count = points(fuzz_rng_);
            std::vector<float>                 xs(static_cast<size_t>(count));
            std::vector<float>                 ys(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                xs[static_cast<size_t>(i)] = static_cast<float>(i);
                ys[static_cast<size_t>(i)] = std::sin(static_cast<float>(i) * 0.001f);
            }
            figure->subplot(1, 1, 1).line(xs, ys);
            details << R"("points":)" << count;
            break;
        }
        case FuzzAction::SplitDock:
        {
            std::uniform_int_distribution<int> direction(0, 1);
            const std::string                  command =
                direction(fuzz_rng_) == 0 ? "view.split_right" : "view.split_down";
            details << R"("command_id":")" << command << R"(","executed":)"
                    << (execute_command(command) ? "true" : "false");
            break;
        }
        case FuzzAction::WaitFrames:
        {
            std::uniform_int_distribution<int> frames(1, 10);
            pump_frames = frames(fuzz_rng_);
            details << R"("requested_frames":)" << pump_frames;
            break;
        }
        case FuzzAction::WindowResize:
        {
            std::uniform_int_distribution<int> dimension(200, 1920);
            const int                          width  = dimension(fuzz_rng_);
            const int                          height = dimension(fuzz_rng_);
            if (resize_fn_)
                resize_fn_(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            details << R"("width":)" << width << R"(,"height":)" << height << R"(,"resized":)"
                    << (resize_fn_ ? "true" : "false");
            break;
        }
        case FuzzAction::WindowDrag:
        {
            std::uniform_int_distribution<int> x(0, 1600);
            std::uniform_int_distribution<int> y(0, 900);
            const int                          px = x(fuzz_rng_);
            const int                          py = y(fuzz_rng_);
            if (input_root_)
                input_root_->move(px, py);
            details << R"("x":)" << px << R"(,"y":)" << py << R"(,"moved":)"
                    << (input_root_ ? "true" : "false");
            break;
        }
        case FuzzAction::TabDetach:
        {
            const auto ids = services_ ? services_->figures().all_ids() : std::vector<FigureId>{};
            if (ids.size() < 2 || !switch_figure_fn_)
            {
                details << R"("skipped":"insufficient_figures_or_unavailable")";
                break;
            }
            std::uniform_int_distribution<size_t> pick(0, ids.size() - 1);
            const FigureId                        id       = ids[pick(fuzz_rng_)];
            const bool                            switched = switch_figure_fn_(id);
            const bool                            detached = switched
                                  && (detach_figure_fn_ ? detach_figure_fn_(id)
                                                        : execute_command("figure.move_to_window"));
            details << R"("figure_id":)" << id << R"(,"detached":)"
                    << (detached ? "true" : "false");
            break;
        }
    }

    if (services_ && services_->redraw_request())
        services_->redraw_request()->request_redraw();
    while (QWidget* popup = QApplication::activePopupWidget())
        popup->close();
    details << '}';
    return details.str();
}

void QtAutomationAdapter::handle_request(AutomationRequest& request)
{
    if (request.method == "ping")
    {
        request.response_json = json_ok(request.id, R"({"pong":true})");
        return;
    }

    if (request.method == "pump_frames")
    {
        const int count = std::clamp(json_get_int(request.params_json, "count", 1), 1, 600);
        if (!pump_frames_fn_)
        {
            request.response_json = json_error(request.id, "Qt frame pump is unavailable");
            return;
        }

        const uint32_t pumped = pump_frames_fn_(static_cast<uint32_t>(count));
        if (pumped != static_cast<uint32_t>(count))
        {
            request.response_json =
                json_error(request.id,
                           "Qt frame pump rendered " + std::to_string(pumped) + " of "
                               + std::to_string(count) + " requested frames");
            return;
        }
        request.response_json = json_ok(request.id, "{\"pumped\":" + std::to_string(pumped) + "}");
        return;
    }

    if (request.method == "wait_frames")
    {
        request.response_json = json_ok(request.id, R"({"waited":true})");
        return;
    }

    if (request.method == "dismiss_ui_capture")
    {
        bool popup_cleared = false;
        while (QWidget* popup = QApplication::activePopupWidget())
        {
            popup_cleared = true;
            popup->close();
            QCoreApplication::sendPostedEvents(popup, QEvent::DeferredDelete);
        }

        QWidget* mouse_grabber = QWidget::mouseGrabber();
        if (mouse_grabber)
            mouse_grabber->releaseMouse();

        QWidget* keyboard_grabber = QWidget::keyboardGrabber();
        if (keyboard_grabber)
            keyboard_grabber->releaseKeyboard();

        bool native_mouse_release = false;
        if (QWindow* canvas = canvas_window_fn_ ? canvas_window_fn_() : nullptr)
            native_mouse_release = canvas->setMouseGrabEnabled(false);

        request.response_json = json_ok(
            request.id,
            std::string{R"({"cleared":{"popup":)"} + (popup_cleared ? "true" : "false")
                + R"(,"mouse_grab":)" + (mouse_grabber ? "true" : "false") + R"(,"keyboard_grab":)"
                + (keyboard_grabber ? "true" : "false") + R"(,"native_mouse_release":)"
                + (native_mouse_release ? "true" : "false") + "}}");
        return;
    }

    if (request.method == "get_state")
    {
        if (!get_state_fn_)
        {
            request.response_json = json_error(request.id, "Qt application state is unavailable");
            return;
        }
        request.response_json = json_ok(request.id, get_state_fn_());
        return;
    }

    if (request.method == "list_commands")
    {
        if (!services_)
        {
            request.response_json = json_error(request.id, "Application services are unavailable");
            return;
        }

        const auto         commands = services_->commands().all_commands();
        std::ostringstream result;
        result << R"({"commands":[)";
        for (size_t i = 0; i < commands.size(); ++i)
        {
            if (i > 0)
                result << ',';
            const auto* command = commands[i];
            result << R"({"id":")" << json_escape(command->id) << R"(","label":")"
                   << json_escape(command->label) << R"(","category":")"
                   << json_escape(command->category) << R"(","shortcut":")"
                   << json_escape(command->shortcut) << R"(","enabled":)"
                   << (command->enabled ? "true" : "false") << '}';
        }
        result << "]}";
        request.response_json = json_ok(request.id, result.str());
        return;
    }

    if (request.method == "list_menus")
    {
        if (!list_menus_fn_)
        {
            request.response_json = json_error(request.id, "Qt menu state is unavailable");
            return;
        }
        request.response_json = json_ok(request.id, list_menus_fn_());
        return;
    }

    if (request.method == "list_methods")
    {
        if (!services_ || !services_->automation())
        {
            request.response_json =
                json_error(request.id, "Automation method catalog is unavailable");
            return;
        }
        request.response_json =
            json_ok(request.id,
                    "{\"methods\":" + services_->automation()->handler_catalog_json() + "}");
        return;
    }

    if (request.method == "execute_command")
    {
        const std::string command_id = json_get_string(request.params_json, "command_id");
        if (command_id.empty())
        {
            request.response_json = json_error(request.id, "Missing parameter: command_id");
            return;
        }

        const bool executed = execute_cmd_fn_ ? execute_cmd_fn_(command_id)
                                              : services_->commands().execute(command_id);
        if (!executed)
        {
            request.response_json =
                json_error(request.id, "Command not found or disabled: " + command_id);
            return;
        }
        if (services_->redraw_request())
            services_->redraw_request()->request_redraw();
        request.response_json =
            json_ok(request.id, R"({"executed":")" + json_escape(command_id) + "\"}");
        return;
    }

    if (request.method == "create_figure")
    {
        const int width  = json_get_int(request.params_json, "width", 1280);
        const int height = json_get_int(request.params_json, "height", 720);
        if (width <= 0 || height <= 0)
        {
            request.response_json = json_error(request.id, "Figure dimensions must be positive");
            return;
        }
        if (!create_figure_fn_)
        {
            request.response_json = json_error(request.id, "Figure creation is unavailable");
            return;
        }
        const FigureId figure_id =
            create_figure_fn_(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        if (figure_id == INVALID_FIGURE_ID)
        {
            request.response_json = json_error(request.id, "Figure creation failed");
            return;
        }
        request.response_json =
            json_ok(request.id, "{\"figure_id\":" + std::to_string(figure_id) + "}");
        return;
    }

    if (request.method == "switch_figure")
    {
        if (!json_has_key(request.params_json, "figure_id"))
        {
            request.response_json = json_error(request.id, "Missing parameter: figure_id");
            return;
        }
        const auto figure_id =
            static_cast<FigureId>(json_get_uint64(request.params_json, "figure_id", 0));
        if (!services_ || !services_->figures().get(figure_id))
        {
            request.response_json = json_error(request.id, "Figure not found");
            return;
        }
        if (!switch_figure_fn_ || !switch_figure_fn_(figure_id))
        {
            request.response_json = json_error(request.id, "Figure activation failed");
            return;
        }
        request.response_json = json_ok(request.id);
        return;
    }

    if (request.method == "add_series")
    {
        if (!services_)
        {
            request.response_json = json_error(request.id, "Application services are unavailable");
            return;
        }
        const bool added = automation_add_series(request, services_->figures());
        if (added && services_->redraw_request())
        {
            services_->redraw_request()->request_redraw();
        }
        return;
    }

    if (request.method == "get_figure_info")
    {
        if (!services_)
        {
            request.response_json = json_error(request.id, "Application services are unavailable");
            return;
        }
        automation_get_figure_info(request, services_->figures());
        return;
    }

    if (request.method == "get_window_size")
    {
        if (!get_size_fn_)
        {
            request.response_json = json_error(request.id, "Window size is unavailable");
            return;
        }
        const auto [width, height] = get_size_fn_();
        request.response_json      = json_ok(
            request.id,
            "{\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height) + "}");
        return;
    }

    if (request.method == "resize_window")
    {
        const int width  = json_get_int(request.params_json, "width", 1280);
        const int height = json_get_int(request.params_json, "height", 720);
        if (width <= 0 || height <= 0 || !resize_fn_)
        {
            request.response_json = json_error(request.id, "Window resize is unavailable");
            return;
        }
        resize_fn_(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        request.response_json = json_ok(request.id);
        return;
    }

    if (request.method == "capture_screenshot" || request.method == "capture_window")
    {
        std::string path = json_get_string(request.params_json, "path");
        if (path.empty())
        {
            path = request.method == "capture_window" ? "/tmp/spectra_auto_window.png"
                                                      : "/tmp/spectra_auto_screenshot.png";
        }
        const QtCaptureScope scope =
            request.method == "capture_window" ? QtCaptureScope::Window : QtCaptureScope::Canvas;
        const QtCaptureResult capture = capture_fn_ ? capture_fn_(scope, path) : QtCaptureResult{};
        if (!capture.valid() || capture.path.empty())
        {
            request.response_json = json_error(request.id, "Screenshot capture failed");
            return;
        }
        request.response_json =
            json_ok(request.id,
                    R"({"path":")" + json_escape(capture.path)
                        + "\",\"width\":" + std::to_string(capture.width)
                        + ",\"height\":" + std::to_string(capture.height) + R"(,"scope":")"
                        + (scope == QtCaptureScope::Window ? "window" : "canvas") + "\"}");
        return;
    }

    if (request.method == "get_screenshot_base64")
    {
        const QtCaptureResult capture =
            capture_fn_ ? capture_fn_(QtCaptureScope::Window, {}) : QtCaptureResult{};
        if (!capture.valid() || capture.png_base64.empty())
        {
            request.response_json = json_error(request.id, "Screenshot capture failed");
            return;
        }
        request.response_json = json_ok(request.id,
                                        "{\"width\":" + std::to_string(capture.width)
                                            + ",\"height\":" + std::to_string(capture.height)
                                            + R"(,"format":"png","scope":"window","data":")"
                                            + capture.png_base64 + "\"}");
        return;
    }

    if (handle_input_request(request))
        return;

    if (handle_fuzz_request(request))
        return;

    request.response_json =
        json_error(request.id, "Qt automation method is not implemented: " + request.method);
}

}   // namespace spectra::adapters::qt
