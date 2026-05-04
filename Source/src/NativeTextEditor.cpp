#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <WindroseTextSigns/NativeTextEditor.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

namespace WindroseTextSigns
{
    namespace
    {
        constexpr int k_id_edit = 101;
        constexpr int k_id_apply = 102;
        constexpr int k_id_clear = 103;
        constexpr int k_id_cancel = 104;

        auto utf8_to_wide(const std::string& value) -> std::wstring
        {
            if (value.empty())
            {
                return {};
            }
            const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
            if (needed <= 0)
            {
                return std::wstring(value.begin(), value.end());
            }
            std::wstring out(static_cast<size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), needed);
            return out;
        }

        auto wide_to_utf8(const std::wstring& value) -> std::string
        {
            if (value.empty())
            {
                return {};
            }
            const int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (needed <= 0)
            {
                return std::string(value.begin(), value.end());
            }
            std::string out(static_cast<size_t>(needed), '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), needed, nullptr, nullptr);
            return out;
        }

        auto control_text_utf8(HWND hwnd) -> std::string
        {
            if (!hwnd)
            {
                return {};
            }
            const int length = GetWindowTextLengthW(hwnd);
            if (length <= 0)
            {
                return {};
            }
            std::wstring value(static_cast<size_t>(length + 1), L'\0');
            GetWindowTextW(hwnd, value.data(), length + 1);
            value.resize(static_cast<size_t>(length));
            return wide_to_utf8(value);
        }

        auto find_game_window() -> HWND
        {
            const DWORD current_pid = GetCurrentProcessId();
            HWND foreground = GetForegroundWindow();
            if (foreground)
            {
                DWORD pid = 0;
                GetWindowThreadProcessId(foreground, &pid);
                if (pid == current_pid)
                {
                    return foreground;
                }
            }

            struct EnumState
            {
                DWORD pid{};
                HWND hwnd{};
            } state{current_pid, nullptr};

            EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
                auto* s = reinterpret_cast<EnumState*>(lparam);
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == s->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr)
                {
                    s->hwnd = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&state));
            return state.hwnd;
        }

        auto center_rect_near_owner(HWND owner, int width, int height) -> RECT
        {
            RECT base{};
            if (!owner || !GetWindowRect(owner, &base))
            {
                base.left = 100;
                base.top = 100;
                base.right = base.left + 1280;
                base.bottom = base.top + 720;
            }
            const int base_width = static_cast<int>(std::max<long>(1, base.right - base.left));
            const int base_height = static_cast<int>(std::max<long>(1, base.bottom - base.top));
            const int left = base.left + std::max(0, (base_width - width) / 2);
            const int top = base.top + std::max(0, (base_height - height) / 2);
            return RECT{left, top, left + width, top + height};
        }
    }

    struct NativeTextEditor::Impl
    {
        HWND owner{};
        HWND window{};
        HWND edit{};
        HWND apply{};
        HWND clear{};
        HWND cancel{};
        NativeTextEditorResult pending{};
        bool has_pending{false};

        static auto class_name() -> const wchar_t*
        {
            return L"WindroseTextSignsNativeTextEditor";
        }

        static auto window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) -> LRESULT
        {
            auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE)
            {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
                self = reinterpret_cast<Impl*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }

            if (!self)
            {
                return DefWindowProcW(hwnd, msg, wparam, lparam);
            }

            switch (msg)
            {
            case WM_COMMAND:
            {
                const int id = LOWORD(wparam);
                if (id == k_id_apply)
                {
                    self->pending = {NativeTextEditorAction::Apply, control_text_utf8(self->edit)};
                    self->has_pending = true;
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
                if (id == k_id_clear)
                {
                    SetWindowTextW(self->edit, L"");
                    self->pending = {NativeTextEditorAction::Clear, {}};
                    self->has_pending = true;
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
                if (id == k_id_cancel)
                {
                    self->pending = {NativeTextEditorAction::Cancel, {}};
                    self->has_pending = true;
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
                break;
            }
            case WM_CLOSE:
                self->pending = {NativeTextEditorAction::Closed, {}};
                self->has_pending = true;
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            case WM_DESTROY:
                self->window = nullptr;
                self->edit = nullptr;
                self->apply = nullptr;
                self->clear = nullptr;
                self->cancel = nullptr;
                return 0;
            default:
                break;
            }

            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        auto register_class_if_needed() -> bool
        {
            WNDCLASSEXW wc{};
            if (GetClassInfoExW(GetModuleHandleW(nullptr), class_name(), &wc))
            {
                return true;
            }

            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &Impl::window_proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = class_name();
            return RegisterClassExW(&wc) != 0;
        }

        auto create_window(const std::wstring& title, const std::wstring& initial_text) -> bool
        {
            if (!register_class_if_needed())
            {
                return false;
            }

            owner = find_game_window();
            const int width = 560;
            const int height = 300;
            const auto rect = center_rect_near_owner(owner, width, height);

            window = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                class_name(),
                title.c_str(),
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                rect.left,
                rect.top,
                width,
                height,
                owner,
                nullptr,
                GetModuleHandleW(nullptr),
                this);
            if (!window)
            {
                return false;
            }

            CreateWindowExW(0, L"STATIC", L"Sign text", WS_CHILD | WS_VISIBLE,
                18, 14, 200, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);

            edit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                initial_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
                18,
                40,
                508,
                160,
                window,
                reinterpret_cast<HMENU>(static_cast<intptr_t>(k_id_edit)),
                GetModuleHandleW(nullptr),
                nullptr);
            apply = CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                246, 218, 88, 30, window, reinterpret_cast<HMENU>(static_cast<intptr_t>(k_id_apply)), GetModuleHandleW(nullptr), nullptr);
            clear = CreateWindowExW(0, L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                342, 218, 88, 30, window, reinterpret_cast<HMENU>(static_cast<intptr_t>(k_id_clear)), GetModuleHandleW(nullptr), nullptr);
            cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                438, 218, 88, 30, window, reinterpret_cast<HMENU>(static_cast<intptr_t>(k_id_cancel)), GetModuleHandleW(nullptr), nullptr);

            if (!edit || !apply || !clear || !cancel)
            {
                DestroyWindow(window);
                return false;
            }

            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HWND children[] = {edit, apply, clear, cancel};
            for (HWND child : children)
            {
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }

            SendMessageW(edit, EM_SETLIMITTEXT, 256, 0);
            ShowWindow(window, SW_SHOWNORMAL);
            SetForegroundWindow(window);
            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return true;
        }
    };

    NativeTextEditor::NativeTextEditor() : m_impl(new Impl{})
    {
    }

    NativeTextEditor::~NativeTextEditor()
    {
        close();
        delete m_impl;
        m_impl = nullptr;
    }

    auto NativeTextEditor::open(const std::string& title, const std::string& initial_text) -> bool
    {
        if (!m_impl)
        {
            return false;
        }
        m_impl->has_pending = false;
        m_impl->pending = {};
        const auto title_w = utf8_to_wide(title);
        const auto text_w = utf8_to_wide(initial_text);
        if (m_impl->window && IsWindow(m_impl->window))
        {
            SetWindowTextW(m_impl->window, title_w.c_str());
            SetWindowTextW(m_impl->edit, text_w.c_str());
            ShowWindow(m_impl->window, SW_SHOWNORMAL);
            SetForegroundWindow(m_impl->window);
            SetFocus(m_impl->edit);
            SendMessageW(m_impl->edit, EM_SETSEL, 0, -1);
            return true;
        }
        return m_impl->create_window(title_w, text_w);
    }

    auto NativeTextEditor::close() -> void
    {
        if (!m_impl)
        {
            return;
        }
        if (m_impl->window && IsWindow(m_impl->window))
        {
            DestroyWindow(m_impl->window);
        }
        m_impl->window = nullptr;
        m_impl->edit = nullptr;
        m_impl->apply = nullptr;
        m_impl->clear = nullptr;
        m_impl->cancel = nullptr;
        m_impl->has_pending = false;
    }

    auto NativeTextEditor::is_open() const -> bool
    {
        return m_impl && m_impl->window && IsWindow(m_impl->window) && IsWindowVisible(m_impl->window);
    }

    auto NativeTextEditor::pump() -> std::optional<NativeTextEditorResult>
    {
        if (!m_impl)
        {
            return std::nullopt;
        }

        MSG msg{};
        if (m_impl->window && IsWindow(m_impl->window) && IsWindowVisible(m_impl->window))
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (m_impl->window && IsWindow(m_impl->window) && IsDialogMessageW(m_impl->window, &msg))
                {
                    continue;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (!m_impl->has_pending)
        {
            return std::nullopt;
        }
        m_impl->has_pending = false;
        return m_impl->pending;
    }
}
