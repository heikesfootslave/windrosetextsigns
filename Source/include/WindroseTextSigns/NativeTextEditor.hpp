#pragma once

#include <optional>
#include <string>

namespace WindroseTextSigns
{
    enum class NativeTextEditorAction
    {
        None,
        Apply,
        Clear,
        Cancel,
        Closed
    };

    struct NativeTextEditorResult
    {
        NativeTextEditorAction action{NativeTextEditorAction::None};
        std::string text{};
    };

    class NativeTextEditor
    {
      public:
        NativeTextEditor();
        ~NativeTextEditor();

        auto open(const std::string& title, const std::string& initial_text) -> bool;
        auto close() -> void;
        auto is_open() const -> bool;
        auto pump() -> std::optional<NativeTextEditorResult>;

      private:
        struct Impl;
        Impl* m_impl{};
    };
}
