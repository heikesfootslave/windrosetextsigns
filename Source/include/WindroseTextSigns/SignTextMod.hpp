#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Mod/CppUserModBase.hpp>
#include <Input/Handler.hpp>
#include <Input/KeyDef.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>

namespace WindroseTextSigns
{
    struct LabelRecord
    {
        std::string stable_id{};
        std::string world_id{};
        std::string text{};
        std::string asset{};
        float surface_axis{0.00f}; // 0.00=forward(X), 1.00=right(Y), blend supported
        int surface_sign{1}; // -1 or +1
        float depth_offset{12.00f}; // outward from label face
        float align_x{0.00f}; // along label surface horizontal axis
        float align_y{1.50f}; // along label surface vertical axis (local Z)
        float font_size{18.00f};
        float color_r{0.393822f};
        float color_g{0.393822f};
        float color_b{0.393822f};
        float color_a{1.00f};
        std::string last_seen_utc{};
    };

    struct SelectionCandidate
    {
        RC::Unreal::AActor* actor{};
        std::string stable_id{};
        std::string world_id{};
        std::string asset{};
        double score{-1.0};
        double distance{0.0};
    };

    class SignTextMod : public RC::CppUserModBase
    {
      public:
        SignTextMod();
        ~SignTextMod() override;

        auto on_unreal_init() -> void override;
        auto on_update() -> void override;

        auto render_ui() -> void;

      private:
        auto resolve_mod_root() -> std::filesystem::path;
        auto resolve_data_root() const -> std::filesystem::path;
        auto migrate_legacy_sidecar_if_needed() -> void;
        auto open_log() -> void;
        auto log_line(const std::string& line) -> void;
        auto now_utc() const -> std::string;

        auto register_input_hotkey() -> void;
        auto probe_phase7_native_ui_capabilities() -> void;
        auto open_phase7_native_editor_for_selection() -> bool;
        auto close_phase7_native_editor(bool restore_game_input) -> void;
        auto set_phase7_game_and_ui_input_mode(bool enable_ui_mode) -> bool;
        auto install_process_event_probe() -> void;
        auto install_static_construct_probe() -> void;
        auto uninstall_process_event_probe() -> void;

        auto tick_pending_hotkey() -> void;
        auto tick_pending_fallback_hotkeys() -> void;
        auto tick_file_triggers() -> void;
        auto run_six_sign_targeting_test() -> void;
        auto ensure_selected_label_for_action(const std::string& action_name) -> bool;
        auto is_actor_pointer_live(RC::Unreal::AActor* actor) const -> bool;
        auto ensure_selected_actor_valid(const std::string& reason) -> bool;
        auto try_select_label_from_camera() -> std::optional<SelectionCandidate>;
        auto try_get_primary_player_controller() -> RC::Unreal::UObject*;
        auto is_probable_marker_ui_widget(RC::Unreal::UObject* object) const -> bool;
        auto log_marker_widget_snapshot(RC::Unreal::UObject* widget) -> void;
        auto read_marker_popup_text(RC::Unreal::UObject* popup_widget) -> std::string;
        auto count_live_user_marker_widgets() -> int32_t;
        auto tick_marker_ui_state_probe() -> void;

        auto is_probable_label_actor(RC::Unreal::AActor* actor) const -> bool;
        auto detect_label_asset(RC::Unreal::AActor* actor) const -> std::string;

        auto build_world_id_for_actor(RC::Unreal::AActor* actor) const -> std::string;
        auto build_storage_key(const std::string& world_id, const std::string& stable_id) const -> std::string;

        auto extract_stable_id(RC::Unreal::UObject* object) -> std::string;
        auto try_extract_guid_from_object(RC::Unreal::UObject* object) -> std::optional<std::string>;
        auto try_extract_guid_from_params(RC::Unreal::UFunction* function, void* params) -> std::optional<std::string>;

        auto apply_text_to_selected_label(const std::string& text_value) -> void;
        auto clear_text_on_selected_label() -> void;
        auto restore_known_text_if_any(RC::Unreal::AActor* actor, const std::string& stable_id) -> void;
        auto apply_text_to_actor_component(RC::Unreal::AActor* actor, const std::string& text_value) -> bool;
        auto make_managed_component_name(const std::string& storage_key) const -> std::string;
        auto find_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key) -> RC::Unreal::UObject*;
        auto create_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key, const RC::Unreal::FVector& relative_location) -> RC::Unreal::UObject*;
        auto destroy_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key) -> bool;

        auto process_event_probe(RC::Unreal::UObject* context, RC::Unreal::UFunction* function, void* params) -> void;
        auto static_construct_probe(const RC::Unreal::FStaticConstructObjectParameters& params, RC::Unreal::UObject* constructed_object) -> void;

        auto load_sidecar_json() -> void;
        auto save_sidecar_json(const std::string& reason, const std::string& key, const std::string& stable_id, const std::string& world_id) -> void;
        auto maybe_write_backup_snapshot(const std::string& reason, const std::string& payload) -> void;
        auto sanitize_backup_reason(std::string reason) const -> std::string;

        auto escape_json(std::string_view s) const -> std::string;
        auto unescape_json(std::string_view s) const -> std::string;

        auto narrow_ascii(const RC::StringType& value) const -> std::string;
        auto lower_ascii(std::string value) const -> std::string;

      private:
        std::filesystem::path m_mod_root{};
        std::filesystem::path m_data_root{};
        std::filesystem::path m_log_path{};
        std::filesystem::path m_sidecar_path{};
        std::filesystem::path m_legacy_sidecar_path{};
        std::filesystem::path m_backup_root{};
        std::ofstream m_log{};

        std::atomic<bool> m_hotkey_requested{false};
        std::atomic<bool> m_clear_hotkey_requested{false};
        std::atomic<bool> m_six_sign_test_requested{false};
        bool m_ui_open{false};
        bool m_unreal_ready{false};
        bool m_phase7_native_probe_ran{false};
        bool m_phase7_native_supported{false};
        bool m_phase7_native_editor_open{false};
        bool m_phase7_imgui_fallback_enabled{true};
        std::string m_phase7_native_probe_summary{};
        RC::Unreal::UObject* m_phase7_native_widget{};

        std::optional<SelectionCandidate> m_selected{};
        std::array<char, 257> m_text_buffer{};

        RC::Unreal::Hook::GlobalCallbackId m_process_event_probe_id{RC::Unreal::Hook::ERROR_ID};
        RC::Unreal::Hook::GlobalCallbackId m_static_construct_probe_id{RC::Unreal::Hook::ERROR_ID};

        std::unordered_map<std::string, LabelRecord> m_labels{};
        std::chrono::steady_clock::time_point m_last_restore_scan{};
        std::chrono::steady_clock::time_point m_last_probe_status{};
        std::chrono::steady_clock::time_point m_last_ui_tick_log{};

        uint64_t m_probe_event_count{0};
        uint64_t m_probe_label_hit_count{0};
        uint64_t m_construct_event_count{0};
        uint64_t m_construct_label_hit_count{0};
        std::unordered_map<std::string, std::string> m_rendered_text_cache{};
        std::unordered_map<std::string, std::string> m_component_name_cache{};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_phase4_next_retry{};
        std::unordered_set<std::string> m_seen_live_label_keys{};
        std::unordered_map<std::string, uint32_t> m_missing_label_scan_counts{};
        uint32_t m_consecutive_empty_label_scans{0};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_construct_probe_last_seen{};
        std::chrono::steady_clock::time_point m_last_backup_snapshot{};
        std::string m_last_backup_signature{};
        std::unordered_set<std::string> m_marker_widget_logged{};
        std::unordered_set<std::string> m_marker_widget_active{};
        std::unordered_set<std::string> m_marker_widget_callable_logged{};
        std::chrono::steady_clock::time_point m_last_marker_probe_scan{};
        bool m_cursor_state_known{false};
        bool m_last_show_mouse_cursor{false};
        bool m_marker_popup_open{false};
        std::string m_marker_popup_active_name{};
        std::string m_marker_popup_text_on_open{};
        std::string m_marker_popup_last_text{};
        int32_t m_marker_popup_user_markers_on_open{0};
        bool m_marker_popup_confirm_clicked{false};
        bool m_marker_popup_cancel_clicked{false};
        bool m_marker_popup_last_confirm_pressed{false};
        bool m_marker_popup_last_cancel_pressed{false};
        bool m_marker_popup_last_visibility{false};
        std::string m_marker_popup_last_direct_text{};
        std::chrono::steady_clock::time_point m_marker_popup_last_telemetry_log{};
        bool m_marker_popup_gettext_fn_available{false};
        bool m_marker_popup_settext_fn_available{false};
        bool m_marker_popup_confirm_fn_available{false};
        bool m_marker_popup_cancel_fn_available{false};
        bool m_in_process_event_probe{false};
    };
}
