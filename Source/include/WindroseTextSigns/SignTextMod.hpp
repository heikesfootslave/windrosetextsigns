#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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

#include <WindroseTextSigns/NativeBridge.hpp>

namespace WindroseTextSigns
{
    struct LabelRecord
    {
        std::string stable_id{};
        std::string world_id{};
        std::string text{};
        std::string asset{};
        std::string kind{"UnverifiedWoodenLabel"};
        std::string backing_asset{};
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
        auto configure_data_root() -> void;
        auto migrate_legacy_sidecar_if_needed() -> void;
        auto open_log() -> void;
        auto log_line(const std::string& line) -> void;
        auto now_utc() const -> std::string;

        auto register_input_hotkey() -> void;
        auto probe_phase7_native_ui_capabilities() -> void;
        auto open_phase7_native_editor_for_selection() -> bool;
        auto close_phase7_native_editor(bool restore_game_input) -> void;
        auto set_phase7_game_and_ui_input_mode(bool enable_ui_mode) -> bool;
        auto open_phase7_umg_editor_for_selection() -> bool;
        auto close_phase7_umg_editor(bool restore_game_input) -> void;
        auto tick_phase7_umg_editor() -> void;
        auto install_phase7_keyboard_capture_hook() -> void;
        auto uninstall_phase7_keyboard_capture_hook() -> void;
        auto install_process_event_probe() -> void;
        auto install_static_construct_probe() -> void;
        auto uninstall_process_event_probe() -> void;
        auto is_static_construct_probe_enabled() const -> bool;
        auto is_process_event_probe_enabled() const -> bool;
        auto is_phase5_placement_probe_enabled() const -> bool;
        auto is_phase5_build_menu_selection_probe_enabled() const -> bool;
        auto is_phase5_visual_patch_probe_enabled() const -> bool;
        auto is_phase5_visual_patch_hide_icon_components_enabled() const -> bool;
        auto config_bool_value(std::string_view key, bool fallback) const -> bool;

        auto tick_pending_hotkey() -> void;
        auto tick_pending_fallback_hotkeys() -> void;
        auto tick_file_triggers() -> void;
        auto run_six_sign_targeting_test() -> void;
        auto run_buildmenu_asset_probe() -> void;
        auto tick_phase5_build_menu_selection_probe() -> void;
        auto is_restore_scan_world_active() -> bool;
        auto ensure_selected_label_for_action(const std::string& action_name) -> bool;
        auto is_actor_pointer_live(RC::Unreal::AActor* actor) const -> bool;
        auto ensure_selected_actor_valid(const std::string& reason) -> bool;
        auto try_select_label_from_camera() -> std::optional<SelectionCandidate>;
        auto try_get_primary_player_controller() -> RC::Unreal::UObject*;

        auto is_probable_label_actor(RC::Unreal::AActor* actor) const -> bool;
        auto detect_label_asset(RC::Unreal::AActor* actor) const -> std::string;
        auto is_dedicated_runtime_process() const -> bool;
        auto is_world_authoritative(RC::Unreal::UObject* world_object) const -> bool;
        auto is_local_hosted_runtime() const -> bool;
        auto configure_sidecar_for_actor(RC::Unreal::AActor* actor, const std::string& world_id) -> void;
        auto active_storage_world_id(const std::string& actor_world_id) const -> std::string;
        auto set_sidecar_route(
            const std::filesystem::path& data_root,
            const std::string& runtime_role,
            const std::string& data_mode,
            const std::string& authority_mode,
            const std::string& sidecar_kind,
            bool authoritative,
            const std::filesystem::path& profile_root,
            const std::string& world_folder_id,
            const std::string& reason) -> void;

        auto build_world_id_for_actor(RC::Unreal::AActor* actor) const -> std::string;
        auto build_storage_key(const std::string& world_id, const std::string& stable_id) const -> std::string;

        auto extract_stable_id(RC::Unreal::UObject* object) -> std::string;
        auto try_extract_guid_from_object(RC::Unreal::UObject* object) -> std::optional<std::string>;
        auto try_extract_guid_from_params(RC::Unreal::UFunction* function, void* params) -> std::optional<std::string>;

        auto apply_text_to_selected_label(const std::string& text_value) -> void;
        auto clear_text_on_selected_label() -> void;
        auto restore_known_text_if_any(RC::Unreal::AActor* actor, const std::string& stable_id) -> void;
        auto apply_text_to_actor_component(RC::Unreal::AActor* actor, const std::string& text_value) -> bool;
        auto should_render_world_text_components() const -> bool;
        auto diagnose_or_patch_label_visual(RC::Unreal::AActor* actor, const std::string& storage_key, const std::string& reason) -> bool;
        auto resolve_world_text_font_asset() -> RC::Unreal::UObject*;
        auto apply_world_text_font(RC::Unreal::UObject* text_component) -> bool;
        auto make_managed_component_name(const std::string& storage_key) const -> std::string;
        auto find_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key) -> RC::Unreal::UObject*;
        auto create_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key, const RC::Unreal::FVector& relative_location) -> RC::Unreal::UObject*;
        auto destroy_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key) -> bool;

        auto process_event_probe(RC::Unreal::UObject* context, RC::Unreal::UFunction* function, void* params) -> void;
        auto static_construct_probe(const RC::Unreal::FStaticConstructObjectParameters& params, RC::Unreal::UObject* constructed_object) -> void;

        auto load_sidecar_json() -> void;
        auto save_sidecar_json(const std::string& reason, const std::string& key, const std::string& stable_id, const std::string& world_id) -> void;
        auto sidecar_record_count_on_disk() const -> std::optional<size_t>;
        auto is_authoritative_write_allowed() const -> bool;
        auto is_cache_path_allowed() const -> bool;
        auto maybe_write_backup_snapshot(const std::string& reason, const std::string& payload) -> void;
        auto sanitize_backup_reason(std::string reason) const -> std::string;

        auto configure_bridge_role(const std::string& reason) -> void;
        auto tick_bridge() -> void;
        auto send_bridge_snapshot_request(const std::string& reason) -> void;
        auto send_bridge_record_request(const std::string& request_type, const LabelRecord& rec) -> bool;
        auto broadcast_bridge_record(const LabelRecord& rec, const std::string& reason) -> void;
        auto broadcast_bridge_clear(const std::string& stable_id, const std::string& world_id, const std::string& reason) -> void;
        auto broadcast_bridge_snapshot(const std::string& reason) -> void;
        auto handle_bridge_payload(const std::string& payload) -> void;
        auto handle_bridge_server_set(const std::unordered_map<std::string, std::string>& fields) -> void;
        auto handle_bridge_server_clear(const std::unordered_map<std::string, std::string>& fields) -> void;
        auto handle_bridge_client_upsert(const std::unordered_map<std::string, std::string>& fields) -> void;
        auto handle_bridge_client_clear(const std::unordered_map<std::string, std::string>& fields) -> void;
        auto server_has_label_stable_id(const std::string& stable_id) -> bool;
        auto reconcile_bridge_snapshot(const std::string& reason) -> void;
        auto write_recovery_candidate(
            const std::string& reason,
            const std::unordered_map<std::string, LabelRecord>& records) -> void;

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
        std::vector<std::filesystem::path> m_legacy_sidecar_paths{};
        std::filesystem::path m_backup_root{};
        std::ofstream m_log{};
        std::string m_runtime_role{"Unknown"};
        std::string m_data_mode{"Unknown"};
        std::string m_authority_mode{"Unknown"};
        std::string m_sidecar_kind{"unknown"};
        std::string m_save_profile_root{};
        std::string m_world_folder_id{};
        bool m_sidecar_authoritative{false};
        uint64_t m_revision{0};
        std::string m_session_id{};
        BridgeRole m_bridge_role{BridgeRole::Unknown};
        std::chrono::steady_clock::time_point m_bridge_next_snapshot_request{};
        std::chrono::steady_clock::time_point m_bridge_last_status{};
        bool m_bridge_snapshot_received{false};
        bool m_bridge_snapshot_active{false};
        std::unordered_set<std::string> m_bridge_snapshot_seen_keys{};

        std::atomic<bool> m_hotkey_requested{false};
        std::atomic<bool> m_six_sign_test_requested{false};
        std::atomic<bool> m_buildmenu_probe_requested{false};
        std::atomic<bool> m_phase7_enter_requested{false};
        std::atomic<bool> m_phase7_escape_requested{false};
        uint32_t m_hotkey_retry_remaining{0};
        std::chrono::steady_clock::time_point m_hotkey_retry_next{};
        bool m_ui_open{false};
        bool m_unreal_ready{false};
        bool m_phase7_native_probe_ran{false};
        bool m_phase7_native_supported{false};
        bool m_phase7_native_editor_open{false};
        bool m_phase7_imgui_fallback_enabled{true};
        bool m_static_construct_probe_enabled{false};
        bool m_phase5_placement_probe_enabled{false};
        bool m_phase5_build_menu_selection_probe_enabled{false};
        bool m_phase5_visual_patch_probe_enabled{false};
        bool m_phase5_visual_patch_hide_icon_components_enabled{false};
        bool m_f8_poll_was_down{false};
        bool m_phase7_enter_was_down{false};
        bool m_phase7_escape_was_down{false};
        std::atomic<bool> m_phase7_keyboard_capture_active{false};
        std::atomic<bool> m_phase7_keyboard_hook_stop{false};
        std::atomic<bool> m_phase7_keyboard_hook_installed{false};
        std::atomic<uint32_t> m_phase7_keyboard_hook_thread_id{0};
        std::thread m_phase7_keyboard_hook_thread{};
        std::string m_phase7_umg_last_text{};
        std::string m_phase7_native_probe_summary{};
        RC::Unreal::UObject* m_phase7_native_widget{};
        RC::Unreal::UObject* m_phase7_umg_widget{};
        RC::Unreal::UObject* m_phase7_umg_text_box{};
        RC::Unreal::UObject* m_phase7_umg_apply_button{};
        RC::Unreal::UObject* m_phase7_umg_clear_button{};
        RC::Unreal::UObject* m_phase7_umg_cancel_button{};

        std::optional<SelectionCandidate> m_selected{};
        std::array<char, 257> m_text_buffer{};
        std::string m_text_buffer_bound_key{};

        RC::Unreal::Hook::GlobalCallbackId m_process_event_probe_id{RC::Unreal::Hook::ERROR_ID};
        RC::Unreal::Hook::GlobalCallbackId m_static_construct_probe_id{RC::Unreal::Hook::ERROR_ID};

        std::unordered_map<std::string, LabelRecord> m_labels{};
        std::chrono::steady_clock::time_point m_last_restore_scan{};
        std::chrono::steady_clock::time_point m_last_restore_scan_diag{};
        std::chrono::steady_clock::time_point m_last_probe_status{};
        std::chrono::steady_clock::time_point m_last_ui_tick_log{};

        uint64_t m_probe_event_count{0};
        uint64_t m_probe_label_hit_count{0};
        uint64_t m_construct_event_count{0};
        uint64_t m_construct_label_hit_count{0};
        std::unordered_map<std::string, std::string> m_rendered_text_cache{};
        std::unordered_set<std::string> m_visual_patch_probe_logged_keys{};
        std::unordered_map<std::string, std::string> m_component_name_cache{};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_phase4_next_retry{};
        std::unordered_set<std::string> m_seen_live_label_keys{};
        std::unordered_map<std::string, uintptr_t> m_live_label_actor_ptrs{};
        std::unordered_map<std::string, uint32_t> m_missing_label_scan_counts{};
        uint32_t m_consecutive_empty_label_scans{0};
        bool m_restore_scan_has_seen_live_labels{false};
        bool m_restore_scan_wait_logged{false};
        bool m_prune_deferred_logged{false};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_construct_probe_last_seen{};
        std::chrono::steady_clock::time_point m_phase5_build_menu_selection_probe_next{};
        std::chrono::steady_clock::time_point m_phase5_build_menu_selection_probe_last_summary{};
        std::unordered_map<std::string, std::string> m_phase5_build_menu_selection_probe_last{};
        std::chrono::steady_clock::time_point m_last_backup_snapshot{};
        std::string m_last_backup_signature{};
        bool m_runtime_text_label_patch_applied{false};
        RC::Unreal::UObject* m_world_text_font_asset{};
        bool m_world_text_font_resolved{false};
        bool m_world_text_font_missing_logged{false};
        bool m_in_process_event_probe{false};
    };
}
