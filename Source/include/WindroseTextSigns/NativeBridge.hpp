#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace WindroseTextSigns
{
    enum class BridgeRole : uint32_t
    {
        Unknown = 0,
        DedicatedServer = 1,
        ListenHost = 2,
        RemoteClient = 3
    };

    struct BridgeCounters
    {
        uint64_t send_to_server_calls{0};
        uint64_t broadcast_to_clients_calls{0};
        uint64_t poll_calls{0};
        uint64_t dropped_queue_full{0};
        uint64_t dropped_payload_too_large{0};
    };

    struct KnownClientStats
    {
        size_t total{0};
        size_t loopback{0};
        size_t private_net{0};
        size_t public_net{0};
    };

    class NativeBridge
    {
      public:
        static auto instance() -> NativeBridge&;

        auto set_role(BridgeRole role) -> void;
        auto set_remote_server(std::string host, uint16_t port) -> void;
        [[nodiscard]] auto role() const -> BridgeRole;

        auto send_to_server(const std::string& payload) -> bool;
        auto broadcast_to_clients(const std::string& payload) -> bool;
        auto poll_incoming() -> std::vector<std::string>;
        [[nodiscard]] auto server_bound_port() const -> uint16_t;
        [[nodiscard]] auto server_socket_open() const -> bool;
        [[nodiscard]] auto server_last_bind_error() const -> int;

        [[nodiscard]] auto counters() const -> BridgeCounters;
        [[nodiscard]] auto known_client_count() const -> size_t;
        [[nodiscard]] auto known_client_stats() const -> KnownClientStats;
        [[nodiscard]] auto status_json() const -> std::string;

      private:
        NativeBridge() = default;

        struct Endpoint
        {
            uint32_t ip_be{0};
            uint16_t port_be{0};

            auto operator==(const Endpoint& rhs) const -> bool
            {
                return ip_be == rhs.ip_be && port_be == rhs.port_be;
            }
        };

        struct EndpointHasher
        {
            auto operator()(const Endpoint& value) const -> size_t
            {
                return (static_cast<size_t>(value.ip_be) << 16) ^ static_cast<size_t>(value.port_be);
            }
        };

        auto enqueue_locked(std::deque<std::string>& q, const std::string& payload) -> bool;
        auto ensure_runtime_ready_locked() -> bool;
        auto ensure_server_socket_locked() -> bool;
        auto ensure_client_socket_locked() -> bool;
        auto receive_server_packets_locked() -> void;
        auto receive_client_packets_locked() -> void;
        auto send_udp_locked(const std::string& payload, uint32_t ip_be, uint16_t port_be, bool prefer_server_socket) -> bool;
        auto resolve_remote_server_locked() -> bool;
        auto prune_stale_clients_locked() -> void;

        mutable std::mutex m_mutex{};
        BridgeRole m_role{BridgeRole::Unknown};
        std::deque<std::string> m_rx_server{};
        std::deque<std::string> m_rx_client{};
        static constexpr size_t k_max_queue_items = 2048;
        static constexpr size_t k_max_payload_bytes = 16384;
        static constexpr uint16_t k_default_udp_server_port = 45801;
        static constexpr int64_t k_client_expire_ms = 90000;

        bool m_runtime_ready{false};
        bool m_winsock_ready{false};
        uintptr_t m_server_socket{static_cast<uintptr_t>(~0ull)};
        uintptr_t m_client_socket{static_cast<uintptr_t>(~0ull)};
        uint16_t m_server_bound_port{0};
        int m_server_last_bind_error{0};
        std::string m_remote_server_host{"127.0.0.1"};
        uint16_t m_udp_server_port{k_default_udp_server_port};
        uint16_t m_remote_server_port{k_default_udp_server_port};
        Endpoint m_remote_server_endpoint{0, 0};
        bool m_remote_server_resolved{false};
        std::unordered_map<Endpoint, std::chrono::steady_clock::time_point, EndpointHasher> m_known_clients{};

        std::atomic<uint64_t> m_send_to_server_calls{0};
        std::atomic<uint64_t> m_broadcast_to_clients_calls{0};
        std::atomic<uint64_t> m_poll_calls{0};
        std::atomic<uint64_t> m_dropped_queue_full{0};
        std::atomic<uint64_t> m_dropped_payload_too_large{0};
    };
}
