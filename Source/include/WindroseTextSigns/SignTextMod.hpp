#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
#include <WindroseTextSigns/UpnpNat.hpp>

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
        enum class BridgeUpnpMode : uint32_t
        {
            Off = 0,
            On = 1,
            Auto = 2
        };
        enum class WorldIdBindPhase : uint32_t
        {
            Unbound = 0,
            ProvisionalIdSeen = 1,
            StableIdLatched = 2
        };

        auto resolve_mod_root() -> std::filesystem::path;
        auto configure_data_root() -> void;
        auto open_log() -> void;
        auto compact_log_line(std::string line) const -> std::string;
        auto write_log_row(const std::string& row) -> void;
        auto flush_log_repeat_summary() -> void;
        auto log_line(const std::string& line) -> void;
        auto trace_behavior_sm(const std::string& event, const std::string& fields = {}) -> void;
        auto now_utc() const -> std::string;

        auto register_input_hotkey() -> void;
        auto probe_phase7_native_ui_capabilities() -> void;
        auto open_phase7_native_editor_for_selection() -> bool;
        auto close_phase7_native_editor(bool restore_game_input) -> void;
        auto set_phase7_game_and_ui_input_mode(bool enable_ui_mode) -> bool;
        auto cache_phase7_umg_class_pointers() -> bool;
        auto cache_phase7_umg_function_pointers() -> void;
        auto apply_phase7_umg_debug_scales(const char* reason = nullptr) -> void;
        auto invoke_phase7_set_visibility(uint8_t value, const char* source_tag = nullptr) -> bool;
        auto invalidate_phase7_umg_widget_cache(const std::string& reason) -> void;
        auto ensure_phase7_umg_widget_built() -> bool;
        auto maybe_prewarm_phase7_umg_editor() -> void;
        auto open_phase7_umg_editor_for_selection() -> bool;
        auto tick_phase7_umg_open_pending() -> void;
        auto close_phase7_umg_editor(bool restore_game_input) -> void;
        auto force_close_phase7_for_teardown(const std::string& reason) -> void;
        auto arm_phase7_definitive_teardown(const std::string& reason) -> void;
        auto maybe_run_phase7_bootstrap_sanitize() -> void;
        auto reset_phase7_runtime_state() -> void;
        auto is_phase7_runtime_interaction_safe(std::string* out_reason = nullptr) -> bool;
        auto mark_phase7_status_dirty(const char* reason = nullptr) -> void;
        auto refresh_phase7_umg_status(bool force, const char* reason = nullptr) -> void;
        auto build_phase7_role_status_text() const -> std::string;
        auto build_phase7_network_status_text() const -> std::string;
        auto is_current_world_snapshot_ready() const -> bool;
        auto tick_phase7_umg_editor() -> void;
        auto install_phase7_keyboard_capture_hook() -> void;
        auto uninstall_phase7_keyboard_capture_hook() -> void;
        auto is_hide_native_label_icon_enabled() const -> bool;
        auto is_label_text_visual_diagnostics_enabled() const -> bool;
        auto is_native_transport_inventory_probe_enabled() const -> bool;
        auto config_bool_value(std::string_view key, bool fallback) const -> bool;
        auto config_string_value(std::string_view key, std::string fallback) const -> std::string;

        auto tick_pending_hotkey() -> void;
        auto request_hotkey_press(const char* source) -> bool;
        auto tick_pending_fallback_hotkeys() -> void;
        auto tick_file_triggers() -> void;
        auto run_native_transport_inventory_probe(const std::string& reason) -> void;
        auto is_restore_scan_world_active() -> bool;
        auto is_localclient_prune_ready(bool authority_source_resolved, std::string* out_reason = nullptr) -> bool;
        auto tick_localclient_role_resolution() -> void;
        auto tick_r5_readiness_markers() -> void;
        auto refresh_recent_destroy_signals_from_r5_log() -> void;
        auto has_recent_destroy_confirmation(const std::string& stable_id, const std::string& expected_world_id) -> bool;
        enum class SuspectRebuildDecision
        {
            None,
            PromotePrune,
            UnsuppressRestoreOnce
        };
        auto mark_suspect_rebuild(
            const std::string& key,
            const std::string& stable_id,
            const std::string& world_id,
            uintptr_t old_actor_ptr,
            uintptr_t new_actor_ptr,
            std::chrono::steady_clock::time_point now) -> void;
        auto expire_suspect_rebuild_states(std::chrono::steady_clock::time_point now) -> void;
        auto maybe_promote_suspect_rebuild_to_prune(
            const std::string& key,
            const std::string& stable_id,
            const std::string& world_id,
            std::chrono::steady_clock::time_point now) -> SuspectRebuildDecision;
        auto maybe_handle_new_construct_overrides_stale_record(
            RC::Unreal::AActor* actor,
            const std::string& key,
            const std::string& stable_id,
            const std::string& world_id,
            bool first_seen_live_after_ready,
            bool is_ready_baseline_key) -> bool;
        auto should_hold_restore_for_first_seen_post_ready(
            const std::string& key,
            const std::string& world_id,
            bool first_seen_live_after_ready,
            bool is_ready_baseline_key) -> bool;
        auto reset_localclient_role_lock_restore_pass_state() -> void;
        auto schedule_localclient_role_lock_restore_passes(const std::string& reason) -> void;
        auto maybe_run_localclient_role_lock_restore_passes(std::chrono::steady_clock::time_point now) -> void;
        auto maybe_run_hosted_post_ready_reconcile() -> void;
        auto is_localclient_runtime_stable_for_post_ready(std::string* out_reason = nullptr) -> bool;
        auto is_session_ready_for_role_resolution(std::string* out_reason = nullptr) -> bool;
        auto is_session_window_active_for_gameplay(std::string* out_reason = nullptr) const -> bool;
        auto open_session_window(const std::string& signal, const std::filesystem::path& log_path, uintmax_t offset) -> void;
        auto close_session_window(const std::string& signal, const std::filesystem::path& log_path, uintmax_t offset) -> void;
        auto is_worldid_bound_for_current_epoch() const -> bool;
        auto try_resolve_worldid_for_ready_bind(std::string* out_world_id, std::string* out_source) -> bool;
        auto bind_worldid_for_epoch_if_ready(const std::string& reason) -> bool;
        auto is_world_bound_operation_allowed(const std::string& op, std::string* out_reason = nullptr) -> bool;
        auto note_world_bound_operation_resumed(const std::string& op) -> void;
        auto is_world_id_latched_for_authoritative_localclient_bind(std::string* out_reason = nullptr) -> bool;
        auto reset_session_state(const std::string& reason) -> void;
        auto reset_visual_verify_debug_state() -> void;
        auto tick_localclient_visual_verify_debug(std::chrono::steady_clock::time_point now) -> void;
        auto run_localclient_visual_verify_pass(
            int pass_number,
            bool apply_before_verify,
            bool force_reapply,
            const std::string& reason) -> void;
        auto ensure_selected_label_for_action(const std::string& action_name) -> bool;
        auto is_actor_pointer_live(RC::Unreal::AActor* actor) const -> bool;
        auto is_actor_ready_for_restore_retry(RC::Unreal::AActor* actor, std::string* out_reason = nullptr) -> bool;
        auto ensure_selected_actor_valid(const std::string& reason) -> bool;
        auto try_select_label_from_camera() -> std::optional<SelectionCandidate>;
        auto try_get_primary_player_controller() -> RC::Unreal::UObject*;

        auto is_probable_label_actor(RC::Unreal::AActor* actor) const -> bool;
        auto detect_label_asset(RC::Unreal::AActor* actor) const -> std::string;
        auto is_dedicated_runtime_process() const -> bool;
        auto is_world_authoritative(RC::Unreal::UObject* world_object) const -> bool;
        auto is_local_hosted_runtime() const -> bool;
        auto treat_as_local_authoritative_for_routing(RC::Unreal::UObject* world_object) const -> bool;
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
        auto restore_known_text_if_any(
            RC::Unreal::AActor* actor,
            const std::string& stable_id,
            bool force_bypass_retry_guard = false) -> void;
        auto apply_text_to_actor_component(RC::Unreal::AActor* actor, const std::string& text_value) -> bool;
        auto should_render_world_text_components() const -> bool;
        auto diagnose_or_patch_label_visual(RC::Unreal::AActor* actor, const std::string& storage_key, const std::string& reason) -> bool;
        auto resolve_world_text_font_asset() -> RC::Unreal::UObject*;
        auto apply_world_text_font(RC::Unreal::UObject* text_component) -> bool;
        auto has_world_text_font_override_pak() -> bool;
        auto apply_autosize_defaults_for_font_profile(bool has_override_pak, const std::string& reason) -> void;
        auto refresh_world_text_font_profile(const std::string& reason, bool force_recheck) -> void;
        auto replay_cached_label_text_after_ready(const std::string& reason) -> std::pair<size_t, size_t>;
        auto queue_first_authoritative_render_pass(const std::string& source, const std::string& world_id) -> void;
        auto flush_deferred_bridge_payloads_after_world_bind(const std::string& reason) -> void;
        auto maybe_run_first_authoritative_render_pass(const std::string& trigger) -> void;
        auto resolve_world_text_font_size_limits() -> std::pair<float, float>;
        auto make_managed_component_name(const std::string& storage_key) const -> std::string;
        auto make_managed_row_storage_key(const std::string& storage_key, int row_index) const -> std::string;
        auto find_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key) -> RC::Unreal::UObject*;
        auto create_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key, const RC::Unreal::FVector& relative_location) -> RC::Unreal::UObject*;
        auto destroy_managed_text_component(RC::Unreal::AActor* actor, const std::string& storage_key) -> bool;

        auto load_sidecar_json() -> void;
        auto save_sidecar_json(const std::string& reason, const std::string& key, const std::string& stable_id, const std::string& world_id) -> void;
        auto sidecar_record_count_on_disk() const -> std::optional<size_t>;
        auto is_authoritative_write_allowed() const -> bool;
        auto is_cache_path_allowed() const -> bool;
        auto maybe_write_backup_snapshot(const std::string& reason, const std::string& payload) -> void;
        auto sanitize_backup_reason(std::string reason) const -> std::string;

        auto configure_bridge_role(const std::string& reason) -> void;
        auto reset_server_role_classification_state(const std::string& reason) -> void;
        auto maybe_begin_server_role_classification(const std::filesystem::path& log_path, uintmax_t window_start_offset) -> void;
        auto maybe_observe_server_role_signal(const std::string& line_lower, uintmax_t line_window_start, uintmax_t line_window_end) -> void;
        auto maybe_commit_server_role_classification(std::chrono::steady_clock::time_point now, const std::string& reason) -> void;
        auto apply_server_role_classification(bool hosted_server_relay, const std::string& reason) -> void;
        auto bridge_upnp_mode_name() const -> std::string;
        auto maybe_start_bridge_upnp_attempt(const std::string& reason) -> void;
        auto tick_bridge_upnp() -> void;
        auto tick_bridge_route_discovery() -> void;
        auto reset_bridge_snapshot_payload_state(const std::string& reason) -> void;
        auto reset_bridge_snapshot_state(const std::string& reason) -> void;
        auto mark_bridge_healthy(const std::string& reason) -> void;
        auto has_viable_remote_route_for_snapshot() const -> bool;
        auto reset_route_probe_state(const std::string& reason) -> void;
        auto build_route_probe_candidates() -> std::vector<std::pair<std::string, std::string>>;
        auto start_next_route_probe(std::chrono::steady_clock::time_point now) -> bool;
        auto commit_locked_route_from_probe(const std::string& host, const std::string& source) -> void;
        auto update_bridge_health(std::chrono::steady_clock::time_point now) -> void;
        auto is_bootstrap_resolution_window_active(std::chrono::steady_clock::time_point now) const -> bool;
        auto maybe_acquire_role_lock(std::chrono::steady_clock::time_point now, const std::string& reason) -> void;
        auto reset_role_route_locks(const std::string& reason) -> void;
        auto is_remoteclient_ready_and_world_bound() const -> bool;
        auto should_ignore_remote_inbound_before_ready(
            const std::string& type,
            std::string* out_reason = nullptr,
            std::string* out_authority_path = nullptr) const -> bool;
        auto start_remote_post_ready_resync(const std::string& reason) -> void;
        auto stop_remote_post_ready_resync(const std::string& reason, bool success) -> void;
        auto tick_remote_post_ready_resync(std::chrono::steady_clock::time_point now) -> void;
        auto next_snapshot_retry_delay() const -> std::chrono::seconds;
        auto tick_bridge() -> void;
        auto send_bridge_snapshot_request(const std::string& reason) -> void;
        auto send_bridge_record_request(const std::string& request_type, const LabelRecord& rec) -> bool;
        auto broadcast_bridge_record(
            const LabelRecord& rec,
            const std::string& reason,
            const std::string& snapshot_id = {},
            int snapshot_count = -1) -> void;
        auto broadcast_bridge_clear(const std::string& stable_id, const std::string& world_id, const std::string& reason) -> void;
        auto broadcast_bridge_snapshot(const std::string& reason) -> void;
        auto handle_bridge_payload(const std::string& payload) -> void;
        auto handle_bridge_server_set(const std::unordered_map<std::string, std::string>& fields) -> void;
        auto handle_bridge_server_clear(const std::unordered_map<std::string, std::string>& fields) -> void;
        auto handle_bridge_client_upsert(const std::unordered_map<std::string, std::string>& fields) -> void;
        auto handle_bridge_client_clear(const std::unordered_map<std::string, std::string>& fields) -> void;
        auto is_definitive_ready_signal_observed(
            std::string* out_signal = nullptr,
            std::string* out_reason = nullptr) const -> bool;
        auto is_hosted_client_authority_context() const -> bool;
        auto is_hosted_server_relay_context() const -> bool;
        auto ensure_hosted_client_authority_route(const std::string& reason) -> bool;
        auto send_hosted_authority_payload_to_server(
            const std::string& payload,
            const std::string& type,
            const std::string& stable_id,
            const std::string& world_id,
            const std::string& reason) -> bool;
        auto ensure_hosted_server_authority_route(const std::string& reason) -> bool;
        auto send_hosted_server_control_message(const std::string& type, const std::string& reason) -> bool;
        auto resolve_hosted_server_advertise_host() const -> std::string;
        auto resolve_hosted_server_endpoint_path() const -> std::filesystem::path;
        auto publish_hosted_server_endpoint(const std::string& reason) -> bool;
        auto consume_hosted_server_endpoint(const std::string& reason) -> bool;
        auto apply_bridge_remote_endpoint(const std::string& host, uint16_t port, const std::string& reason) -> bool;
        auto relay_payload_to_hosted_authority(
            const std::string& payload,
            const std::string& type,
            const std::string& stable_id,
            const std::string& world_id,
            const std::string& reason) -> bool;
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
        std::filesystem::path m_backup_root{};
        std::ofstream m_log{};
        size_t m_log_bytes_written{0};
        bool m_log_size_cap_hit{false};
        std::chrono::steady_clock::time_point m_bootstrap_started{};
        std::string m_last_log_payload{};
        uint32_t m_last_log_repeat_count{0};
        bool m_bootstrap_begin_logged{false};
        bool m_bootstrap_end_logged{false};
        bool m_bootstrap_prune_phase_observed{false};
        uint64_t m_session_epoch{1};
        bool m_session_ready_latched{false};
        std::string m_session_ready_world_id{};
        bool m_role_lock_acquired{false};
        std::string m_role_lock_runtime_role{};
        std::string m_role_lock_bridge_role{};
        std::string m_role_lock_world_id{};
        std::string m_role_lock_start_signal{};
        std::string m_runtime_role{"Unknown"};
        std::string m_data_mode{"Unknown"};
        std::string m_authority_mode{"Unknown"};
        std::string m_sidecar_kind{"unknown"};
        std::string m_save_profile_root{};
        std::string m_world_folder_id{};
        bool m_sidecar_authoritative{false};
        bool m_last_sidecar_load_ok{false};
        bool m_last_sidecar_save_ok{true};
        WorldIdBindPhase m_worldid_bind_phase{WorldIdBindPhase::Unbound};
        std::string m_worldid_provisional_id{};
        std::string m_worldid_latched_id{};
        uint32_t m_worldid_stability_seen_count{0};
        std::chrono::steady_clock::time_point m_worldid_stability_last_observed{};
        std::chrono::steady_clock::time_point m_worldid_generation_last_signal{};
        std::string m_worldid_generation_last_marker_id{};
        bool m_worldid_generation_in_progress{false};
        std::string m_worldid_last_defer_reason{};
        std::chrono::steady_clock::time_point m_worldid_last_defer_log{};
        bool m_worldid_bound_for_epoch{false};
        uint64_t m_worldid_bound_epoch{0};
        std::string m_worldid_bound_source{};
        bool m_session_action_defer_connecting_bl{false};
        bool m_deferred_definitive_start_pending{false};
        std::string m_deferred_definitive_start_signal{};
        uintmax_t m_deferred_definitive_start_offset{0};
        bool m_deferred_definitive_end_pending{false};
        uintmax_t m_deferred_definitive_end_offset{0};
        struct WorldBoundDeferLogState
        {
            std::string reason{};
            std::chrono::steady_clock::time_point last_log{};
        };
        std::unordered_map<std::string, WorldBoundDeferLogState> m_world_bound_defer_logs_by_op{};
        std::unordered_set<std::string> m_world_bound_resumed_ops{};
        bool m_f8_latency_breakdown_enabled{true};
        bool m_behavior_trace_enabled{false};
        bool m_create_null_short_retry_enabled{true};
        std::array<uint32_t, 3> m_create_null_retry_delays_ms{250, 750, 1500};
        bool m_force_local_only{false};
        bool m_force_local_only_bridge_logged{false};
        std::chrono::steady_clock::time_point m_force_local_only_last_ignored_log{};
        std::string m_force_local_only_last_ignored_type{};
        uint64_t m_revision{0};
        std::string m_session_id{};
        BridgeRole m_bridge_role{BridgeRole::Unknown};
        std::string m_bridge_remote_server_host{"127.0.0.1"};
        std::string m_bridge_remote_server_host_config{"127.0.0.1"};
        bool m_bridge_route_auto_enabled{false};
        std::filesystem::path m_bridge_route_log_path{};
        std::chrono::steady_clock::time_point m_bridge_route_next_check{};
        std::string m_bridge_route_last_discovered_host{};
        std::vector<std::string> m_bridge_route_last_candidates{};
        bool m_bridge_route_lock_acquired{false};
        bool m_route_lock_failed_no_reachable_endpoint{false};
        std::string m_bridge_route_locked_host{};
        bool m_bridge_route_loopback_same_machine_ok{false};
        bool m_bridge_route_retry_consumed{false};
        bool m_bridge_route_force_non_loopback{false};
        bool m_bridge_route_recovery_logged{false};
        bool m_bridge_route_gate_open{false};
        bool m_bridge_route_gate_pending_logged{false};
        bool m_bridge_route_probe_active{false};
        bool m_bridge_route_probe_waiting_ack{false};
        uint32_t m_bridge_route_post_ready_retry_stage{0};
        std::chrono::steady_clock::time_point m_bridge_route_post_ready_next_probe_due{};
        std::vector<std::pair<std::string, std::string>> m_bridge_route_probe_candidates{};
        size_t m_bridge_route_probe_index{0};
        std::string m_bridge_route_probe_token{};
        std::string m_bridge_route_probe_host{};
        std::string m_bridge_route_probe_source{};
        bool m_bridge_route_staged_active{false};
        std::string m_bridge_route_staged_host{};
        std::string m_bridge_route_staged_source{};
        std::chrono::steady_clock::time_point m_bridge_route_probe_deadline{};
        std::unordered_set<std::string> m_bridge_route_rejected_candidates_logged{};
        std::unordered_set<std::string> m_bridge_route_fallback_candidates_logged{};
        bool m_bridge_route_bootstrap_pause_logged{false};
        std::chrono::steady_clock::time_point m_bridge_route_wait_last_log{};
        std::string m_bridge_route_wait_last_reason{};
        int m_bridge_udp_port{45801};
        bool m_bridge_upnp_enabled{false};
        BridgeUpnpMode m_bridge_upnp_mode{BridgeUpnpMode::Off};
        bool m_bridge_upnp_attempted{false};
        bool m_bridge_upnp_mapped{false};
        bool m_bridge_upnp_timeout_logged{false};
        uint32_t m_bridge_upnp_attempt_count{0};
        std::chrono::steady_clock::time_point m_bridge_upnp_last_attempt{};
        std::chrono::steady_clock::time_point m_bridge_upnp_attempt_started{};
        std::string m_bridge_upnp_last_policy{};
        struct BridgeUpnpJobState
        {
            std::atomic<bool> done{false};
            std::mutex mutex{};
            UpnpNatResult result{};
        };
        std::shared_ptr<BridgeUpnpJobState> m_bridge_upnp_job{};
        std::chrono::steady_clock::time_point m_bridge_next_snapshot_request{};
        std::chrono::steady_clock::time_point m_bridge_sync_wait_started{};
        std::chrono::steady_clock::time_point m_bridge_last_snapshot_request{};
        std::chrono::steady_clock::time_point m_bridge_last_authoritative_rx{};
        std::chrono::steady_clock::time_point m_bridge_last_status{};
        bool m_bridge_snapshot_request_in_flight{false};
        bool m_bridge_snapshot_received{false};
        std::string m_bridge_snapshot_world_id{};
        bool m_bridge_health_unhealthy{false};
        bool m_bridge_health_warning_logged{false};
        uint32_t m_bridge_snapshot_retry_attempts{0};
        bool m_bridge_snapshot_active{false};
        bool m_bridge_snapshot_end_seen{false};
        int m_bridge_snapshot_expected_count{-1};
        std::string m_bridge_snapshot_id{};
        std::unordered_set<std::string> m_bridge_snapshot_seen_keys{};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_bridge_pending_request_keys{};
        struct DeferredBridgePayload
        {
            bool is_clear{false};
            std::unordered_map<std::string, std::string> fields{};
        };
        std::vector<DeferredBridgePayload> m_bridge_deferred_unbound_world_payloads{};
        std::unordered_set<std::string> m_hosted_authority_local_apply_deferred_keys{};
        bool m_hosted_authority_route_active{false};
        bool m_hosted_server_authority_route_configured{false};
        bool m_hosted_server_cache_initialized{false};
        uint64_t m_hosted_server_cache_revision{0};
        bool m_hosted_server_endpoint_advertised{false};
        std::string m_hosted_server_endpoint_host{};
        uint16_t m_hosted_server_endpoint_port{0};
        std::chrono::steady_clock::time_point m_hosted_server_endpoint_last_read{};
        bool m_bridge_bind_success_logged{false};
        bool m_bridge_bind_failed_logged{false};
        struct RemoteDeltaApplyState
        {
            uint64_t revision{0};
            uint64_t content_hash{0};
        };
        std::unordered_map<std::string, RemoteDeltaApplyState> m_remote_delta_applied_state{};
        struct SnapshotRequesterState
        {
            uint64_t last_requester_revision_seen{0};
            uint64_t last_snapshot_served_revision{0};
            std::chrono::steady_clock::time_point last_snapshot_served_at{};
            std::chrono::steady_clock::time_point last_request_at{};
        };
        std::unordered_map<std::string, SnapshotRequesterState> m_snapshot_requester_state_by_session{};
        std::chrono::steady_clock::time_point m_hosted_server_next_hello{};
        std::chrono::steady_clock::time_point m_hosted_server_next_resync_request{};
        std::chrono::steady_clock::time_point m_hosted_server_last_resync_request_sent{};
        bool m_hosted_server_resync_in_flight{false};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_hosted_server_snapshot_unavailable_last_by_session{};
        std::chrono::steady_clock::time_point m_remote_snapshot_no_cache_backoff_until{};
        bool m_remote_post_ready_resync_active{false};
        bool m_remote_post_ready_resync_in_flight{false};
        bool m_remote_post_ready_resync_bootstrap_done{false};
        uint32_t m_remote_post_ready_resync_attempts{0};
        std::chrono::steady_clock::time_point m_remote_post_ready_resync_started{};
        std::chrono::steady_clock::time_point m_remote_post_ready_resync_next_due{};
        std::chrono::steady_clock::time_point m_remote_post_ready_resync_last_send{};
        std::string m_remote_post_ready_resync_last_reason{};
        bool m_server_role_classification_pending{false};
        bool m_server_role_signal_executable_seen{false};
        bool m_server_role_signal_hosted_ini_seen{false};
        bool m_server_role_signal_host_ready_seen{false};
        uintmax_t m_server_role_window_start_offset{0};
        uintmax_t m_server_role_window_end_offset{0};
        std::filesystem::path m_server_role_log_path{};
        std::chrono::steady_clock::time_point m_server_role_classification_started{};
        std::chrono::steady_clock::time_point m_server_role_pending_last_log{};

        std::atomic<bool> m_hotkey_requested{false};
        std::atomic<bool> m_native_transport_inventory_requested{false};
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
        bool m_hide_native_label_icon_enabled{true};
        bool m_label_text_visual_diagnostics_enabled{false};
        bool m_native_transport_inventory_probe_enabled{false};
        bool m_native_transport_inventory_probe_ran{false};
        int m_hotkey_vk{0x77};
        std::string m_hotkey_name{"F8"};
        bool m_hotkey_poll_was_down{false};
        bool m_hotkey_action_in_flight{false};
        bool m_hotkey_require_release_before_next_press{false};
        bool m_phase7_enter_was_down{false};
        bool m_phase7_escape_was_down{false};
        bool m_phase7_shift_was_down{false};
        bool m_phase7_ui_input_mode_active{false};
        std::atomic<bool> m_phase7_keyboard_capture_active{false};
        std::atomic<uint64_t> m_phase7_mouse_capture_arm_until_ms{0};
        std::atomic<bool> m_phase7_mouse_first_down_consumed{false};
        std::atomic<bool> m_phase7_force_full_mouse_consume{false};
        std::atomic<bool> m_phase7_keyboard_hook_stop{false};
        std::atomic<bool> m_phase7_keyboard_hook_installed{false};
        std::atomic<uint32_t> m_phase7_keyboard_hook_thread_id{0};
        std::thread m_phase7_keyboard_hook_thread{};
        std::string m_phase7_umg_last_text{};
        std::string m_phase7_native_probe_summary{};
        struct F8LatencyTraceState
        {
            bool active{false};
            bool target_seen{false};
            bool construct_seen{false};
            std::chrono::steady_clock::time_point edge{};
            std::chrono::steady_clock::time_point target{};
            std::chrono::steady_clock::time_point construct{};
            uint64_t press_id{0};
        } m_f8_latency_trace{};
        RC::Unreal::UObject* m_phase7_native_widget{};
        RC::Unreal::UObject* m_phase7_umg_widget{};
        RC::Unreal::UObject* m_phase7_umg_text_box{};
        RC::Unreal::UObject* m_phase7_umg_title{};
        RC::Unreal::UObject* m_phase7_umg_hint{};
        RC::Unreal::UObject* m_phase7_umg_status{};
        RC::Unreal::UObject* m_phase7_umg_apply_button{};
        RC::Unreal::UObject* m_phase7_umg_clear_button{};
        RC::Unreal::UObject* m_phase7_umg_cancel_button{};
        RC::Unreal::UClass* m_phase7_class_user_widget{};
        RC::Unreal::UClass* m_phase7_class_widget_tree{};
        RC::Unreal::UClass* m_phase7_class_canvas_panel{};
        RC::Unreal::UClass* m_phase7_class_border{};
        RC::Unreal::UClass* m_phase7_class_size_box{};
        RC::Unreal::UClass* m_phase7_class_text_block{};
        RC::Unreal::UClass* m_phase7_class_text_box{};
        RC::Unreal::UFunction* m_phase7_fn_add_to_viewport{};
        RC::Unreal::UFunction* m_phase7_fn_remove_from_parent{};
        RC::Unreal::UFunction* m_phase7_fn_set_keyboard_focus{};
        RC::Unreal::UFunction* m_phase7_fn_set_focus{};
        RC::Unreal::UFunction* m_phase7_fn_set_visibility{};
        float m_phase7_debug_status_render_scale{0.45f};
        float m_phase7_debug_hint_render_scale{0.4f};
        bool m_phase7_umg_prewarm_attempted{false};
        bool m_phase7_umg_prewarm_succeeded{false};
        bool m_phase7_umg_in_viewport{false};
        bool m_phase7_umg_open_pending{false};
        bool m_phase7_open_sla_violation_logged{false};
        bool m_phase7_set_visibility_param_unhandled_logged{false};
        bool m_phase7_teardown_skip_logged{false};
        std::chrono::steady_clock::time_point m_phase7_umg_prewarm_next_try{};
        std::chrono::steady_clock::time_point m_phase7_open_pending_since{};
        bool m_phase7_teardown_pending{false};
        std::string m_phase7_teardown_pending_reason{};
        bool m_phase7_definitive_teardown_armed{false};
        std::string m_phase7_definitive_teardown_reason{};
        uint64_t m_phase7_bootstrap_sanitize_epoch{0};
        std::chrono::steady_clock::time_point m_phase7_teardown_suppressed_last_log{};
        std::string m_phase7_teardown_suppressed_last_reason{};
        uint64_t m_phase7_open_epoch{0};
        uint64_t m_phase7_active_epoch{0};
        std::chrono::steady_clock::time_point m_phase7_opened_at{};
        std::chrono::steady_clock::time_point m_phase7_last_interaction_at{};
        bool m_phase7_watchdog_logged{false};
        bool m_phase7_last_close_removed{false};
        std::chrono::steady_clock::time_point m_phase7_guard_fail_started{};
        std::string m_phase7_guard_fail_reason{};
        bool m_phase7_guard_hysteresis_logged{false};
        std::chrono::steady_clock::time_point m_phase7_stale_epoch_last_log{};
        std::string m_phase7_stale_epoch_last_detail{};
        bool m_phase7_status_dirty{true};
        std::chrono::steady_clock::time_point m_phase7_last_status_ui_refresh{};
        std::string m_phase7_last_status_role_text{};
        std::string m_phase7_last_status_network_text{};
        std::chrono::steady_clock::time_point m_phase7_last_status_log{};

        std::optional<SelectionCandidate> m_selected{};
        std::array<char, 257> m_text_buffer{};
        std::string m_text_buffer_bound_key{};

        std::unordered_map<std::string, LabelRecord> m_labels{};
        std::chrono::steady_clock::time_point m_last_restore_scan{};
        std::chrono::steady_clock::time_point m_last_restore_scan_diag{};
        std::chrono::steady_clock::time_point m_last_ui_tick_log{};
        std::unordered_map<std::string, std::string> m_rendered_text_cache{};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_phase4_last_apply_success_at{};
        std::unordered_map<std::string, std::string> m_phase4_last_failure_reason{};
        std::unordered_set<std::string> m_label_text_visual_logged_keys{};
        std::unordered_map<std::string, std::string> m_component_name_cache{};
        std::unordered_map<std::string, int> m_last_row_count_by_key{};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_phase4_next_retry{};
        struct CreateNullRetryState
        {
            std::string stable_id{};
            std::string world_id{};
            uint64_t session_epoch{0};
            uintptr_t actor_ptr{0};
            uint32_t attempt_idx{1};
            std::chrono::steady_clock::time_point next_due{};
        };
        std::unordered_map<std::string, CreateNullRetryState> m_create_null_retry_states{};
        std::unordered_set<std::string> m_seen_live_label_keys{};
        std::unordered_map<std::string, uintptr_t> m_live_label_actor_ptrs{};
        std::unordered_map<std::string, uint32_t> m_missing_label_scan_counts{};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_recent_destroy_guid_signals{};
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_recent_destroy_slot_confirmations{};
        struct RecentConstructSignal
        {
            std::chrono::steady_clock::time_point seen_at{};
            std::string world_id{};
            uint64_t session_epoch{0};
        };
        std::unordered_map<std::string, RecentConstructSignal> m_recent_construct_slot_signals{};
        std::unordered_set<std::string> m_ready_baseline_live_keys{};
        uint32_t m_ready_baseline_capture_remaining_scans{0};
        struct FirstSeenConstructHoldState
        {
            std::string world_id{};
            uint64_t session_epoch{0};
            uint64_t first_seen_scan_cycle{0};
            bool hold_logged{false};
        };
        std::unordered_map<std::string, FirstSeenConstructHoldState> m_first_seen_construct_hold_states{};
        bool m_pending_world_inactive_ignored_logged{false};
        bool m_locked_world_inactive_ignored_logged{false};
        std::chrono::steady_clock::time_point m_world_inactive_since{};
        struct SuspectRebuildState
        {
            std::string stable_id{};
            std::string world_id{};
            uint64_t session_epoch{0};
            uintptr_t old_actor_ptr{0};
            uintptr_t replacement_actor_ptr{0};
            uint32_t stable_scan_hits{0};
            uint32_t live_scans_post_ready{0};
            uint64_t last_live_scan_cycle{0};
            bool unsuppress_fallback_issued{false};
            std::chrono::steady_clock::time_point first_detected{};
            std::chrono::steady_clock::time_point last_seen{};
            std::chrono::steady_clock::time_point suppress_until{};
        };
        std::unordered_map<std::string, SuspectRebuildState> m_suspect_rebuild_states{};
        std::unordered_map<std::string, uint32_t> m_restore_skip_guard_log_buckets{};
        std::filesystem::path m_destroy_signal_log_path{};
        uintmax_t m_destroy_signal_log_offset{0};
        bool m_destroy_signal_log_initialized{false};
        std::chrono::steady_clock::time_point m_destroy_signal_last_poll{};
        uint32_t m_destroy_confirm_ttl_sec{10};
        std::chrono::steady_clock::time_point m_definitive_session_reset_last_trigger{};
        std::string m_definitive_session_reset_last_signature{};
        std::string m_definitive_session_reset_last_category{};
        std::chrono::steady_clock::time_point m_definitive_session_last_end_seen{};
        std::filesystem::path m_definitive_session_last_end_log_path{};
        uintmax_t m_definitive_session_last_end_offset{0};
        bool m_definitive_session_start_candidate_active{false};
        std::string m_definitive_session_start_candidate_signal{};
        std::string m_definitive_session_start_candidate_world_hint{};
        bool m_session_window_open{false};
        bool m_definitive_session_start_seen{false};
        std::string m_definitive_session_start_signal{};
        std::string m_definitive_session_exit_signal{};
        std::filesystem::path m_session_window_log_path{};
        uintmax_t m_session_window_start_offset{0};
        uintmax_t m_session_window_end_offset{0};
        std::chrono::steady_clock::time_point m_session_window_blocked_last_log{};
        std::string m_session_window_blocked_last_signature{};
        std::chrono::steady_clock::time_point m_ready_latch_blocked_last_log{};
        std::string m_ready_latch_blocked_last_signature{};
        std::chrono::steady_clock::time_point m_ready_latch_blocked_first_seen{};
        uint64_t m_ready_latch_blocked_count{0};
        std::chrono::steady_clock::time_point m_last_player_activity{};
        std::chrono::steady_clock::time_point m_pending_role_watchdog_started{};
        bool m_pending_role_watchdog_logged{false};
        std::string m_pending_resolution_last_block_reason{};
        std::string m_pending_resolution_last_controller_signature{};
        bool m_hosted_ready_world_client_seen{false};
        bool m_hosted_ready_player_ready_seen{false};
        bool m_hosted_ready_datakeeper_seen{false};
        bool m_hosted_ready_hide_loading_seen{false};
        bool m_hosted_ready_sequence_complete{false};
        bool m_def_ready_hide_loading_datakeeper_seen{false};
        bool m_def_ready_hide_loading_coopproxy_wait_seen{false};
        bool m_def_ready_hosted_secondary_verifying_seen{false};
        bool m_def_ready_hosted_secondary_waiting_island_ready_seen{false};
        bool m_def_ready_server_waiting_client_ready_seen{false};
        bool m_hosted_post_ready_reconcile_done{false};
        bool m_remote_cached_replay_pending_after_ready{false};
        bool m_first_authoritative_render_pending{false};
        bool m_first_authoritative_render_completed{false};
        uint64_t m_first_authoritative_render_epoch{0};
        std::string m_first_authoritative_render_world_id{};
        std::string m_first_authoritative_render_source{};
        uint32_t m_first_authoritative_render_attempts{0};
        std::chrono::steady_clock::time_point m_first_authoritative_render_last_log{};
        std::string m_first_authoritative_render_last_reason{};
        uint32_t m_consecutive_empty_label_scans{0};
        bool m_restore_scan_has_seen_live_labels{false};
        bool m_restore_scan_wait_logged{false};
        std::chrono::steady_clock::time_point m_dedicated_restore_active_since{};
        std::chrono::steady_clock::time_point m_dedicated_restore_stable_since{};
        uint32_t m_dedicated_last_probable_label_count{0};
        bool m_prune_deferred_logged{false};
        std::string m_last_prune_defer_reason{};
        std::chrono::steady_clock::time_point m_last_backup_snapshot{};
        std::string m_last_backup_signature{};
        bool m_runtime_text_label_patch_applied{false};
        RC::Unreal::UObject* m_world_text_font_asset{};
        bool m_world_text_font_resolved{false};
        bool m_world_text_font_missing_logged{false};
        bool m_world_text_font_override_pak_detected{false};
        bool m_world_text_font_override_pak_checked{false};
        bool m_world_text_font_profile_ready_latched{false};
        bool m_autosize_profile_initialized{false};
        bool m_autosize_profile_has_override_pak{false};
        float m_autosize_char_width_factor{0.85f};
        float m_row_gap_factor{1.50f};
        float m_row_gap_factor_2{1.50f};
        float m_row_gap_factor_3{1.25f};
        float m_row_gap_factor_4{1.00f};
        std::array<float, 1> m_row_offsets_1{{0.0f}};
        std::array<float, 2> m_row_offsets_2{{8.0f, -5.0f}};
        std::array<float, 3> m_row_offsets_3{{12.0f, 2.0f, -8.0f}};
        std::array<float, 4> m_row_offsets_4{{17.0f, 7.0f, -3.0f, -13.0f}};
        float m_localclient_controller_probe_interval_sec{0.2f};
        std::chrono::steady_clock::time_point m_localclient_controller_probe_last{};
        RC::Unreal::UObject* m_localclient_controller_probe_cached{};
        bool m_localclient_controller_probe_cache_valid{false};
        std::chrono::steady_clock::time_point m_localclient_stability_skip_last_log{};
        std::string m_localclient_stability_skip_last_reason{};
        std::chrono::steady_clock::time_point m_localclient_stability_unstable_last{};
        std::string m_localclient_stability_unstable_reason{};
        bool m_visual_verify_debug_force_reapply{false};
        bool m_localclient_motion_reapply_enabled{true};
        bool m_visual_verify_session_ready{false};
        bool m_visual_verify_pass1_done{false};
        bool m_visual_verify_pass2_done{false};
        bool m_visual_verify_pass3_done{false};
        bool m_visual_verify_motion_logged{false};
        uint64_t m_restore_scan_cycle_counter{0};
        uint64_t m_visual_verify_pass1_scan_cycle{0};
        std::chrono::steady_clock::time_point m_visual_verify_ready_at{};
        RC::Unreal::FVector m_visual_verify_ready_pawn_loc{};
        bool m_visual_verify_ready_pawn_loc_valid{false};
        std::unordered_set<std::string> m_visual_verify_expected_keys{};
        std::unordered_map<std::string, bool> m_visual_verify_last_result{};
        std::unordered_map<std::string, std::string> m_visual_verify_last_tier{};
        std::unordered_map<std::string, int> m_visual_verify_recently_rendered_streak{};
        std::unordered_map<std::string, int> m_visual_verify_no_render_streak{};
        std::unordered_map<std::string, float> m_visual_verify_last_render_time_seen{};
        bool m_role_lock_restore_pass1_pending{false};
        bool m_role_lock_restore_pass1_done{false};
        bool m_role_lock_restore_pass2_pending{false};
        bool m_role_lock_restore_pass2_done{false};
        uint64_t m_role_lock_restore_epoch{0};
        std::chrono::steady_clock::time_point m_role_lock_restore_pass2_due{};
    };
}
