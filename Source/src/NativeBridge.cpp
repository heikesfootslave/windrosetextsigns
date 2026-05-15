#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <WindroseTextSigns/NativeBridge.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <utility>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    constexpr uintptr_t k_invalid_socket_value = static_cast<uintptr_t>(~0ull);

    auto is_valid_socket(const uintptr_t raw_socket) -> bool
    {
        return raw_socket != k_invalid_socket_value;
    }

    auto to_raw_socket(SOCKET socket_handle) -> uintptr_t
    {
        return static_cast<uintptr_t>(socket_handle);
    }

    auto to_socket(uintptr_t raw_socket) -> SOCKET
    {
        return static_cast<SOCKET>(raw_socket);
    }

    auto close_socket_if_valid(uintptr_t& raw_socket) -> void
    {
        if (!is_valid_socket(raw_socket))
        {
            return;
        }
        closesocket(to_socket(raw_socket));
        raw_socket = k_invalid_socket_value;
    }

    auto ip_octets_from_be(const uint32_t ip_be) -> std::array<uint8_t, 4>
    {
        const auto ip_host = ntohl(ip_be);
        return {
            static_cast<uint8_t>((ip_host >> 24) & 0xFF),
            static_cast<uint8_t>((ip_host >> 16) & 0xFF),
            static_cast<uint8_t>((ip_host >> 8) & 0xFF),
            static_cast<uint8_t>(ip_host & 0xFF)};
    }

    auto is_loopback_ip_be(const uint32_t ip_be) -> bool
    {
        const auto o = ip_octets_from_be(ip_be);
        return o[0] == 127;
    }

    auto is_private_ip_be(const uint32_t ip_be) -> bool
    {
        const auto o = ip_octets_from_be(ip_be);
        return o[0] == 10 ||
               (o[0] == 172 && o[1] >= 16 && o[1] <= 31) ||
               (o[0] == 192 && o[1] == 168) ||
               (o[0] == 169 && o[1] == 254);
    }
}

namespace WindroseTextSigns
{
    auto NativeBridge::instance() -> NativeBridge&
    {
        static NativeBridge bridge{};
        return bridge;
    }

    auto NativeBridge::set_role(const BridgeRole role) -> void
    {
        std::scoped_lock lock(m_mutex);
        const auto previous_role = m_role;
        m_role = role;
        (void)ensure_runtime_ready_locked();

        if (previous_role != role)
        {
            if (role == BridgeRole::DedicatedServer)
            {
                close_socket_if_valid(m_client_socket);
                m_rx_client.clear();
            }
            else if (role == BridgeRole::RemoteClient)
            {
                close_socket_if_valid(m_server_socket);
                m_server_bound_port = 0;
                m_server_last_bind_error = 0;
                m_known_clients.clear();
                m_rx_server.clear();
            }
            else if (role == BridgeRole::Unknown)
            {
                close_socket_if_valid(m_server_socket);
                close_socket_if_valid(m_client_socket);
                m_server_bound_port = 0;
                m_server_last_bind_error = 0;
                m_known_clients.clear();
                m_rx_server.clear();
                m_rx_client.clear();
            }
        }

        if (role == BridgeRole::DedicatedServer || role == BridgeRole::ListenHost)
        {
            (void)ensure_server_socket_locked();
        }
        if (role == BridgeRole::RemoteClient || role == BridgeRole::ListenHost)
        {
            (void)ensure_client_socket_locked();
        }
    }

    auto NativeBridge::set_remote_server(std::string host, const uint16_t port) -> void
    {
        std::scoped_lock lock(m_mutex);
        if (host.empty())
        {
            host = "127.0.0.1";
        }
        m_udp_server_port = port == 0 ? k_default_udp_server_port : port;
        m_remote_server_host = std::move(host);
        m_remote_server_port = port == 0 ? k_default_udp_server_port : port;
        m_remote_server_endpoint = Endpoint{0, 0};
        m_remote_server_resolved = false;
    }

    auto NativeBridge::role() const -> BridgeRole
    {
        std::scoped_lock lock(m_mutex);
        return m_role;
    }

    auto NativeBridge::enqueue_locked(std::deque<std::string>& q, const std::string& payload) -> bool
    {
        if (payload.size() > k_max_payload_bytes)
        {
            m_dropped_payload_too_large.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (q.size() >= k_max_queue_items)
        {
            q.pop_front();
            m_dropped_queue_full.fetch_add(1, std::memory_order_relaxed);
        }
        q.push_back(payload);
        return true;
    }

    auto NativeBridge::ensure_runtime_ready_locked() -> bool
    {
        if (m_runtime_ready)
        {
            return true;
        }

        if (!m_winsock_ready)
        {
            WSADATA winsock_data{};
            if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0)
            {
                return false;
            }
            m_winsock_ready = true;
        }

        m_runtime_ready = true;
        return true;
    }

    auto NativeBridge::ensure_server_socket_locked() -> bool
    {
        if (!ensure_runtime_ready_locked())
        {
            return false;
        }
        if (is_valid_socket(m_server_socket))
        {
            return true;
        }
        m_server_last_bind_error = 0;

        const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET)
        {
            m_server_last_bind_error = WSAGetLastError();
            return false;
        }

        u_long non_blocking = 1;
        ioctlsocket(sock, FIONBIO, &non_blocking);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_udp_server_port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            m_server_last_bind_error = WSAGetLastError();
            closesocket(sock);
            return false;
        }

        sockaddr_in bound_addr{};
        int bound_len = sizeof(bound_addr);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len) == 0)
        {
            m_server_bound_port = ntohs(bound_addr.sin_port);
        }
        else
        {
            m_server_bound_port = m_udp_server_port;
        }
        m_server_socket = to_raw_socket(sock);
        return true;
    }

    auto NativeBridge::ensure_client_socket_locked() -> bool
    {
        if (!ensure_runtime_ready_locked())
        {
            return false;
        }
        if (is_valid_socket(m_client_socket))
        {
            return true;
        }

        const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET)
        {
            return false;
        }

        u_long non_blocking = 1;
        ioctlsocket(sock, FIONBIO, &non_blocking);

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(0);
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) == SOCKET_ERROR)
        {
            closesocket(sock);
            return false;
        }

        m_client_socket = to_raw_socket(sock);
        return true;
    }

    auto NativeBridge::send_udp_locked(
        const std::string& payload,
        const uint32_t ip_be,
        const uint16_t port_be,
        const bool prefer_server_socket) -> bool
    {
        SOCKET preferred_socket = INVALID_SOCKET;
        SOCKET fallback_socket = INVALID_SOCKET;
        if (prefer_server_socket)
        {
            if (is_valid_socket(m_server_socket))
            {
                preferred_socket = to_socket(m_server_socket);
            }
            if (is_valid_socket(m_client_socket))
            {
                fallback_socket = to_socket(m_client_socket);
            }
        }
        else
        {
            if (is_valid_socket(m_client_socket))
            {
                preferred_socket = to_socket(m_client_socket);
            }
            if (is_valid_socket(m_server_socket))
            {
                fallback_socket = to_socket(m_server_socket);
            }
        }

        const SOCKET send_socket = (preferred_socket != INVALID_SOCKET) ? preferred_socket : fallback_socket;
        if (send_socket == INVALID_SOCKET)
        {
            return false;
        }

        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = ip_be;
        dest.sin_port = port_be;

        const int sent = sendto(send_socket,
                                payload.data(),
                                static_cast<int>(payload.size()),
                                0,
                                reinterpret_cast<sockaddr*>(&dest),
                                sizeof(dest));
        return sent > 0;
    }

    auto NativeBridge::resolve_remote_server_locked() -> bool
    {
        if (m_remote_server_resolved && m_remote_server_endpoint.ip_be != 0 && m_remote_server_endpoint.port_be != 0)
        {
            return true;
        }

        if (!ensure_runtime_ready_locked())
        {
            return false;
        }

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo* result = nullptr;
        const auto port_text = std::to_string(m_remote_server_port == 0 ? k_default_udp_server_port : m_remote_server_port);
        const int rc = getaddrinfo(m_remote_server_host.c_str(), port_text.c_str(), &hints, &result);
        if (rc != 0 || !result)
        {
            return false;
        }

        for (auto* row = result; row; row = row->ai_next)
        {
            if (!row->ai_addr || row->ai_addrlen < static_cast<int>(sizeof(sockaddr_in)))
            {
                continue;
            }
            const auto* addr = reinterpret_cast<const sockaddr_in*>(row->ai_addr);
            m_remote_server_endpoint = Endpoint{addr->sin_addr.s_addr, addr->sin_port};
            m_remote_server_resolved = true;
            break;
        }
        freeaddrinfo(result);
        return m_remote_server_resolved;
    }

    auto NativeBridge::receive_server_packets_locked() -> void
    {
        if (!is_valid_socket(m_server_socket))
        {
            return;
        }

        const SOCKET recv_socket = to_socket(m_server_socket);
        std::array<char, k_max_payload_bytes + 1> buffer{};
        for (;;)
        {
            sockaddr_in from{};
            int from_size = sizeof(from);
            const int received = recvfrom(recv_socket,
                                          buffer.data(),
                                          static_cast<int>(k_max_payload_bytes),
                                          0,
                                          reinterpret_cast<sockaddr*>(&from),
                                          &from_size);

            if (received == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    break;
                }
                break;
            }
            if (received <= 0)
            {
                break;
            }

            (void)enqueue_locked(m_rx_server, std::string(buffer.data(), static_cast<size_t>(received)));
            m_known_clients[Endpoint{from.sin_addr.s_addr, from.sin_port}] = std::chrono::steady_clock::now();
        }
    }

    auto NativeBridge::receive_client_packets_locked() -> void
    {
        if (!is_valid_socket(m_client_socket))
        {
            return;
        }

        const SOCKET recv_socket = to_socket(m_client_socket);
        std::array<char, k_max_payload_bytes + 1> buffer{};
        for (;;)
        {
            sockaddr_in from{};
            int from_size = sizeof(from);
            const int received = recvfrom(recv_socket,
                                          buffer.data(),
                                          static_cast<int>(k_max_payload_bytes),
                                          0,
                                          reinterpret_cast<sockaddr*>(&from),
                                          &from_size);

            if (received == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    break;
                }
                break;
            }
            if (received <= 0)
            {
                break;
            }

            (void)enqueue_locked(m_rx_client, std::string(buffer.data(), static_cast<size_t>(received)));
        }
    }

    auto NativeBridge::prune_stale_clients_locked() -> void
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = m_known_clients.begin(); it != m_known_clients.end();)
        {
            const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
            if (age_ms > k_client_expire_ms)
            {
                it = m_known_clients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    auto NativeBridge::send_to_server(const std::string& payload) -> bool
    {
        m_send_to_server_calls.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(m_mutex);
        if (!ensure_runtime_ready_locked())
        {
            return false;
        }
        if (m_role == BridgeRole::ListenHost)
        {
            return enqueue_locked(m_rx_server, payload);
        }
        if (!ensure_client_socket_locked())
        {
            return false;
        }
        if (!resolve_remote_server_locked())
        {
            return false;
        }
        return send_udp_locked(payload, m_remote_server_endpoint.ip_be, m_remote_server_endpoint.port_be, false);
    }

    auto NativeBridge::broadcast_to_clients(const std::string& payload) -> bool
    {
        m_broadcast_to_clients_calls.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(m_mutex);
        if (!ensure_runtime_ready_locked())
        {
            return false;
        }
        if (!ensure_server_socket_locked())
        {
            return false;
        }

        bool sent_any = false;
        if (m_role == BridgeRole::ListenHost)
        {
            sent_any = enqueue_locked(m_rx_client, payload) || sent_any;
        }

        prune_stale_clients_locked();
        for (const auto& [endpoint, _] : m_known_clients)
        {
            sent_any = send_udp_locked(payload, endpoint.ip_be, endpoint.port_be, true) || sent_any;
        }
        return sent_any;
    }

    auto NativeBridge::poll_incoming() -> std::vector<std::string>
    {
        m_poll_calls.fetch_add(1, std::memory_order_relaxed);
        std::vector<std::string> out{};
        std::scoped_lock lock(m_mutex);

        if (m_role == BridgeRole::DedicatedServer || m_role == BridgeRole::ListenHost)
        {
            (void)ensure_server_socket_locked();
            receive_server_packets_locked();
        }
        if (m_role == BridgeRole::RemoteClient || m_role == BridgeRole::ListenHost || m_role == BridgeRole::Unknown)
        {
            (void)ensure_client_socket_locked();
            receive_client_packets_locked();
        }

        if (m_role == BridgeRole::DedicatedServer)
        {
            out.reserve(m_rx_server.size());
            while (!m_rx_server.empty())
            {
                out.emplace_back(std::move(m_rx_server.front()));
                m_rx_server.pop_front();
            }
            return out;
        }

        out.reserve(m_rx_server.size() + m_rx_client.size());
        while (!m_rx_server.empty())
        {
            out.emplace_back(std::move(m_rx_server.front()));
            m_rx_server.pop_front();
        }
        while (!m_rx_client.empty())
        {
            out.emplace_back(std::move(m_rx_client.front()));
            m_rx_client.pop_front();
        }
        return out;
    }

    auto NativeBridge::counters() const -> BridgeCounters
    {
        return {
            m_send_to_server_calls.load(std::memory_order_relaxed),
            m_broadcast_to_clients_calls.load(std::memory_order_relaxed),
            m_poll_calls.load(std::memory_order_relaxed),
            m_dropped_queue_full.load(std::memory_order_relaxed),
            m_dropped_payload_too_large.load(std::memory_order_relaxed),
        };
    }

    auto NativeBridge::known_client_count() const -> size_t
    {
        std::scoped_lock lock(m_mutex);
        return m_known_clients.size();
    }

    auto NativeBridge::known_client_stats() const -> KnownClientStats
    {
        KnownClientStats out{};
        std::scoped_lock lock(m_mutex);
        out.total = m_known_clients.size();
        for (const auto& [endpoint, _] : m_known_clients)
        {
            if (is_loopback_ip_be(endpoint.ip_be))
            {
                ++out.loopback;
            }
            else if (is_private_ip_be(endpoint.ip_be))
            {
                ++out.private_net;
            }
            else
            {
                ++out.public_net;
            }
        }
        return out;
    }

    auto NativeBridge::status_json() const -> std::string
    {
        const auto c = counters();
        std::ostringstream out{};
        out << "{";
        out << "\"mode\":\"wts_native_bridge_udp\"";
        out << ",\"role\":" << static_cast<uint32_t>(role());
        out << ",\"known_clients\":" << known_client_count();
        out << ",\"udp_server_port\":" << m_udp_server_port;
        out << ",\"remote_server_host\":\"" << m_remote_server_host << "\"";
        out << ",\"remote_server_port\":" << m_remote_server_port;
        out << ",\"server_bound_port\":" << m_server_bound_port;
        out << ",\"server_socket_open\":" << (is_valid_socket(m_server_socket) ? "true" : "false");
        out << ",\"server_last_bind_error\":" << m_server_last_bind_error;
        out << ",\"remote_server_resolved\":" << (m_remote_server_resolved ? "true" : "false");
        out << ",\"send_to_server_calls\":" << c.send_to_server_calls;
        out << ",\"broadcast_to_clients_calls\":" << c.broadcast_to_clients_calls;
        out << ",\"poll_calls\":" << c.poll_calls;
        out << ",\"dropped_queue_full\":" << c.dropped_queue_full;
        out << ",\"dropped_payload_too_large\":" << c.dropped_payload_too_large;
        out << "}";
        return out.str();
    }

    auto NativeBridge::server_bound_port() const -> uint16_t
    {
        std::scoped_lock lock(m_mutex);
        return m_server_bound_port;
    }

    auto NativeBridge::server_socket_open() const -> bool
    {
        std::scoped_lock lock(m_mutex);
        return is_valid_socket(m_server_socket);
    }

    auto NativeBridge::server_last_bind_error() const -> int
    {
        std::scoped_lock lock(m_mutex);
        return m_server_last_bind_error;
    }
}
