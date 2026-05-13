#include <WindroseTextSigns/SignTextMod.hpp>
#include <WindroseTextSigns/UpnpNat.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iomanip>
#include <ios>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <UE4SSProgram.hpp>

#include <imgui.h>

#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FClassProperty.hpp>
#include <Unreal/Property/FNameProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FStrProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>
#include <Unreal/Transform.hpp>
#include <Unreal/UActorComponent.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/UnrealCoreStructs.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/World.hpp>

namespace
{
    using namespace RC;
    using namespace RC::Unreal;
    using WtsHhook = void*;
    using WtsHwnd = void*;
    using WtsWparam = uintptr_t;
    using WtsLparam = intptr_t;
    using WtsLresult = intptr_t;
    using WtsHookproc = WtsLresult(__stdcall*)(int, WtsWparam, WtsLparam);
    struct WtsPoint
    {
        long x;
        long y;
    };
    struct WtsMsg
    {
        WtsHwnd hwnd;
        unsigned int message;
        WtsWparam wParam;
        WtsLparam lParam;
        unsigned long time;
        WtsPoint pt;
        unsigned long lPrivate;
    };
    struct WtsKbdllhookstruct
    {
        unsigned long vkCode;
        unsigned long scanCode;
        unsigned long flags;
        unsigned long time;
        uintptr_t dwExtraInfo;
    };
    struct WtsProcessentry32w
    {
        unsigned long dwSize;
        unsigned long cntUsage;
        unsigned long th32ProcessID;
        uintptr_t th32DefaultHeapID;
        unsigned long th32ModuleID;
        unsigned long cntThreads;
        unsigned long th32ParentProcessID;
        long pcPriClassBase;
        unsigned long dwFlags;
        wchar_t szExeFile[260];
    };
    extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
    extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* hModule, char* lpFilename, unsigned long nSize);
    extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
    extern "C" __declspec(dllimport) void* __stdcall CreateToolhelp32Snapshot(unsigned long dwFlags, unsigned long th32ProcessID);
    extern "C" __declspec(dllimport) int __stdcall Process32FirstW(void* hSnapshot, WtsProcessentry32w* lppe);
    extern "C" __declspec(dllimport) int __stdcall Process32NextW(void* hSnapshot, WtsProcessentry32w* lppe);
    extern "C" __declspec(dllimport) int __stdcall CloseHandle(void* hObject);
    extern "C" __declspec(dllimport) WtsHhook __stdcall SetWindowsHookExW(int idHook, WtsHookproc lpfn, void* hmod, unsigned long dwThreadId);
    extern "C" __declspec(dllimport) int __stdcall UnhookWindowsHookEx(WtsHhook hhk);
    extern "C" __declspec(dllimport) WtsLresult __stdcall CallNextHookEx(WtsHhook hhk, int nCode, WtsWparam wParam, WtsLparam lParam);
    extern "C" __declspec(dllimport) int __stdcall GetMessageW(WtsMsg* lpMsg, WtsHwnd hWnd, unsigned int wMsgFilterMin, unsigned int wMsgFilterMax);
    extern "C" __declspec(dllimport) int __stdcall TranslateMessage(const WtsMsg* lpMsg);
    extern "C" __declspec(dllimport) WtsLresult __stdcall DispatchMessageW(const WtsMsg* lpMsg);
    extern "C" __declspec(dllimport) int __stdcall PostThreadMessageW(unsigned long idThread, unsigned int msg, WtsWparam wParam, WtsLparam lParam);
    constexpr int k_wh_keyboard_ll = 13;
    constexpr unsigned int k_wm_keydown = 0x0100;
    constexpr unsigned int k_wm_keyup = 0x0101;
    constexpr unsigned int k_wm_syskeydown = 0x0104;
    constexpr unsigned int k_wm_syskeyup = 0x0105;
    constexpr unsigned int k_wm_quit = 0x0012;
    constexpr unsigned long k_th32cs_snapprocess = 0x00000002;
    constexpr int k_default_hotkey_vk = 0x77;
    constexpr int k_vk_return = 0x0D;
    constexpr int k_vk_escape = 0x1B;
    constexpr int k_vk_shift = 0x10;
    std::atomic<bool>* g_phase7_keyboard_capture_active{};
    std::atomic<bool>* g_phase7_enter_requested{};
    std::atomic<bool>* g_phase7_escape_requested{};

    struct Viewpoint
    {
        FVector location{};
        FRotator rotation{};
        bool valid{false};
    };

    struct AutoSizeResult
    {
        std::string wrapped_text{};
        float font_size{10.0f};
        int rows{1};
        int char_limit{12};
        bool truncated{false};
    };

    struct DataRootResolution
    {
        std::filesystem::path data_root{};
        std::filesystem::path profile_root{};
        std::string world_id{};
        std::string runtime_role{"Unknown"};
        std::string data_mode{"Unknown"};
        std::string authority_mode{"Unknown"};
        std::string sidecar_kind{"unknown"};
        bool authoritative{false};
    };

    extern "C" WtsLresult __stdcall phase7_keyboard_capture_proc(int code, WtsWparam w_param, WtsLparam l_param)
    {
        if (code >= 0 &&
            g_phase7_keyboard_capture_active &&
            g_phase7_keyboard_capture_active->load(std::memory_order_relaxed) &&
            l_param != 0)
        {
            const auto* key = reinterpret_cast<const WtsKbdllhookstruct*>(l_param);
            const bool key_down = (w_param == k_wm_keydown || w_param == k_wm_syskeydown);
            const bool key_up = (w_param == k_wm_keyup || w_param == k_wm_syskeyup);
            if ((key_down || key_up) && key)
            {
                if (key->vkCode == static_cast<unsigned long>(k_vk_escape))
                {
                    if (key_down && g_phase7_escape_requested)
                    {
                        g_phase7_escape_requested->store(true, std::memory_order_relaxed);
                    }
                    return 1;
                }

                if (key->vkCode == static_cast<unsigned long>(k_vk_return))
                {
                    const bool shift_down = ((GetAsyncKeyState(k_vk_shift) & 0x8000) != 0);
                    if (!shift_down)
                    {
                        if (key_down && g_phase7_enter_requested)
                        {
                            g_phase7_enter_requested->store(true, std::memory_order_relaxed);
                        }
                        return 1;
                    }
                }
            }
        }
        return CallNextHookEx(nullptr, code, w_param, l_param);
    }

    auto bridge_role_name(const WindroseTextSigns::BridgeRole role) -> std::string
    {
        switch (role)
        {
        case WindroseTextSigns::BridgeRole::DedicatedServer: return "DedicatedServer";
        case WindroseTextSigns::BridgeRole::ListenHost: return "ListenHost";
        case WindroseTextSigns::BridgeRole::RemoteClient: return "RemoteClient";
        default: return "Unknown";
        }
    }

    auto bridge_message_field(const std::string& payload, const std::string& name) -> std::optional<std::string>
    {
        const std::regex rx{"\\\"" + name + "\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"\\\\])*)\\\""};
        std::smatch match{};
        if (std::regex_search(payload, match, rx) && match.size() > 1)
        {
            return match[1].str();
        }
        return std::nullopt;
    }

    auto bridge_message_number(const std::string& payload, const std::string& name) -> std::optional<std::string>
    {
        const std::regex rx{"\\\"" + name + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)"};
        std::smatch match{};
        if (std::regex_search(payload, match, rx) && match.size() > 1)
        {
            return match[1].str();
        }
        return std::nullopt;
    }

    auto parse_bridge_message(const std::string& payload) -> std::unordered_map<std::string, std::string>
    {
        std::unordered_map<std::string, std::string> fields{};
        const std::array<std::string, 15> string_fields = {
            "mod", "type", "session", "key", "stableId", "worldId",
            "text", "asset", "kind", "backingAsset", "lastSeen", "reason", "schema", "writer",
            "snapshotId"};
        for (const auto& name : string_fields)
        {
            if (auto value = bridge_message_field(payload, name); value.has_value())
            {
                fields[name] = *value;
            }
        }
        const std::array<std::string, 12> number_fields = {
            "revision", "surfaceAxis", "surfaceSign", "depthOffset", "alignX",
            "alignY", "fontSize", "colorR", "colorG", "colorB", "colorA", "snapshotCount"};
        for (const auto& name : number_fields)
        {
            if (auto value = bridge_message_number(payload, name); value.has_value())
            {
                fields[name] = *value;
            }
        }
        return fields;
    }

    auto lower_ascii_path_token(std::string value) -> std::string
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    auto is_hex_world_id(const std::string& value) -> bool
    {
        if (value.size() != 32)
        {
            return false;
        }
        return std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        });
    }

    auto get_env_var(const char* name) -> std::string;
    auto read_text_file(const std::filesystem::path& path, std::string& out_content) -> bool;

    auto try_latest_connected_island_id_from_local_log() -> std::optional<std::string>
    {
        static auto last_check = std::chrono::steady_clock::time_point{};
        static std::optional<std::string> cached_result{};
        const auto now = std::chrono::steady_clock::now();
        if (last_check.time_since_epoch().count() != 0 && (now - last_check) < std::chrono::seconds(2))
        {
            return cached_result;
        }
        last_check = now;
        cached_result.reset();

        const auto local_app_data = get_env_var("LOCALAPPDATA");
        if (local_app_data.empty())
        {
            return std::nullopt;
        }

        std::string content{};
        if (!read_text_file(std::filesystem::path{local_app_data} / "R5" / "Saved" / "Logs" / "R5.log", content))
        {
            return std::nullopt;
        }

        static const std::regex connected_island_rx{
            R"(BL connected\. IslandId\s*'([A-Fa-f0-9]{32})')",
            std::regex::icase};
        std::optional<std::string> result{};
        for (auto it = std::sregex_iterator(content.begin(), content.end(), connected_island_rx);
             it != std::sregex_iterator{};
             ++it)
        {
            if (it->size() > 1)
            {
                auto value = (*it)[1].str();
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                    return static_cast<char>(std::toupper(c));
                });
                result = value;
            }
        }
        cached_result = result;
        return cached_result;
    }

    auto normalized_path_for_compare(const std::filesystem::path& path) -> std::string
    {
        std::error_code ec{};
        auto value = std::filesystem::weakly_canonical(path, ec);
        if (ec)
        {
            value = std::filesystem::absolute(path, ec);
        }
        auto out = value.string();
        std::replace(out.begin(), out.end(), '/', '\\');
        return lower_ascii_path_token(out);
    }

    auto append_unique_path(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) -> void
    {
        const auto candidate_key = normalized_path_for_compare(path);
        for (const auto& existing : paths)
        {
            if (normalized_path_for_compare(existing) == candidate_key)
            {
                return;
            }
        }
        paths.push_back(path);
    }

    struct BridgeRouteDiscovery
    {
        bool found{false};
        std::filesystem::path log_path{};
        std::string host{};
        std::string reason{};
        std::string remote_host_candidate{};
        std::string remote_public_candidate{};
        std::string local_host_summary{};
        bool same_machine_evidence{false};
        bool same_machine_process_evidence{false};
        std::vector<std::string> ordered_candidates{};
        std::vector<std::pair<std::string, std::string>> fallback_direct_candidates{};
    };

    auto parse_ipv4_octets(const std::string& ip) -> std::optional<std::array<int, 4>>
    {
        std::array<int, 4> out{};
        std::istringstream input{ip};
        std::string part{};
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (!std::getline(input, part, '.'))
            {
                return std::nullopt;
            }
            if (part.empty() || part.size() > 3 ||
                !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            {
                return std::nullopt;
            }
            const auto value = std::stoi(part);
            if (value < 0 || value > 255)
            {
                return std::nullopt;
            }
            out[i] = value;
        }
        if (std::getline(input, part, '.'))
        {
            return std::nullopt;
        }
        return out;
    }

    auto is_private_ipv4(const std::string& ip) -> bool
    {
        const auto parsed = parse_ipv4_octets(ip);
        if (!parsed)
        {
            return false;
        }
        const auto& o = *parsed;
        return o[0] == 10 ||
               (o[0] == 172 && o[1] >= 16 && o[1] <= 31) ||
               (o[0] == 192 && o[1] == 168) ||
               (o[0] == 169 && o[1] == 254);
    }

    auto is_public_ipv4(const std::string& ip) -> bool
    {
        const auto parsed = parse_ipv4_octets(ip);
        if (!parsed)
        {
            return false;
        }
        const auto& o = *parsed;
        if (o[0] == 0 || o[0] == 127 || o[0] >= 224)
        {
            return false;
        }
        return !is_private_ipv4(ip);
    }

    auto is_same_lan_hint(const std::string& a, const std::string& b) -> bool
    {
        const auto left = parse_ipv4_octets(a);
        const auto right = parse_ipv4_octets(b);
        if (!left || !right || !is_private_ipv4(a) || !is_private_ipv4(b))
        {
            return false;
        }
        return (*left)[0] == (*right)[0] && (*left)[1] == (*right)[1] && (*left)[2] == (*right)[2];
    }

    auto summarize_ips(const std::vector<std::string>& ips) -> std::string
    {
        if (ips.empty())
        {
            return "none";
        }
        std::string out{};
        for (const auto& ip : ips)
        {
            if (!out.empty())
            {
                out += ",";
            }
            out += ip;
        }
        return out;
    }

    auto parse_comma_separated_ips(const std::string& csv) -> std::vector<std::string>
    {
        std::vector<std::string> out{};
        std::istringstream input{csv};
        std::string token{};
        while (std::getline(input, token, ','))
        {
            token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char c) {
                return std::isspace(c) != 0;
            }), token.end());
            if (!token.empty() && parse_ipv4_octets(token).has_value() &&
                std::find(out.begin(), out.end(), token) == out.end())
            {
                out.push_back(token);
            }
        }
        return out;
    }

    auto is_private_candidate_on_local_subnet(
        const std::string& candidate_ip,
        const std::vector<std::string>& local_hosts) -> bool
    {
        if (!is_private_ipv4(candidate_ip))
        {
            return true;
        }
        for (const auto& local_host : local_hosts)
        {
            if (is_same_lan_hint(local_host, candidate_ip))
            {
                return true;
            }
        }
        return false;
    }

    auto append_unique_ip(std::vector<std::string>& ips, const std::string& ip) -> void
    {
        if (ip.empty())
        {
            return;
        }
        if (!parse_ipv4_octets(ip).has_value())
        {
            return;
        }
        if (std::find(ips.begin(), ips.end(), ip) != ips.end())
        {
            return;
        }
        ips.push_back(ip);
    }

    auto make_bridge_snapshot_id(const std::string& session, const uint64_t revision, const uint32_t count) -> std::string
    {
        static std::atomic<uint64_t> sequence{0};
        std::ostringstream out{};
        out << session << "-rev" << revision << "-count" << count << "-seq" << sequence.fetch_add(1, std::memory_order_relaxed);
        return out.str();
    }

    auto collect_r5_log_candidates(const std::filesystem::path& preferred) -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> candidates{};
        if (!preferred.empty())
        {
            append_unique_path(candidates, preferred);
        }

        const auto local_app_data = get_env_var("LOCALAPPDATA");
        if (!local_app_data.empty())
        {
            append_unique_path(candidates, std::filesystem::path{local_app_data} / "R5" / "Saved" / "Logs" / "R5.log");
        }

        const auto cwd = std::filesystem::current_path();
        append_unique_path(candidates, cwd / ".." / ".." / "Saved" / "Logs" / "R5.log");
        append_unique_path(candidates, cwd / "R5" / "Saved" / "Logs" / "R5.log");
        append_unique_path(candidates, cwd / "Saved" / "Logs" / "R5.log");
        return candidates;
    }

    auto wts_lower_ascii_from_wide(const wchar_t* text) -> std::string
    {
        if (!text)
        {
            return {};
        }

        std::string out{};
        out.reserve(260);
        for (size_t i = 0; text[i] != L'\0'; ++i)
        {
            const wchar_t wc = text[i];
            if (wc >= 0 && wc <= 0x7F)
            {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(wc))));
            }
        }
        return out;
    }

    auto has_local_dedicated_server_process_evidence() -> bool
    {
        static std::chrono::steady_clock::time_point last_probe{};
        static bool cached_result{false};

        const auto now = std::chrono::steady_clock::now();
        if (last_probe.time_since_epoch().count() != 0 &&
            (now - last_probe) < std::chrono::seconds(3))
        {
            return cached_result;
        }

        last_probe = now;
        cached_result = false;

        const auto snapshot = CreateToolhelp32Snapshot(k_th32cs_snapprocess, 0);
        if (!snapshot || snapshot == reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
        {
            return false;
        }

        WtsProcessentry32w entry{};
        entry.dwSize = sizeof(WtsProcessentry32w);
        if (!Process32FirstW(snapshot, &entry))
        {
            CloseHandle(snapshot);
            return false;
        }

        uint32_t r5_shipping_count = 0;
        do
        {
            const auto exe_name = wts_lower_ascii_from_wide(entry.szExeFile);
            if (exe_name.find("windroseserver") != std::string::npos)
            {
                cached_result = true;
                break;
            }
            if (exe_name == "r5-win64-shipping.exe")
            {
                ++r5_shipping_count;
                if (r5_shipping_count >= 2)
                {
                    cached_result = true;
                    break;
                }
            }
        } while (Process32NextW(snapshot, &entry));

        CloseHandle(snapshot);
        return cached_result;
    }

    auto discover_bridge_route_from_r5_log(const std::filesystem::path& preferred) -> BridgeRouteDiscovery
    {
        static const std::regex candidate_rx{
            R"(\b(?:UDP|TCP)\s+([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)\s+[A-Fa-f0-9]+\s+\d+\s+(host|srflx)\b)",
            std::regex::icase};
        static const std::regex direct_server_address_rx{
            R"(Start direct connection to server\.\s*ServerAddress\s+([0-9]{1,3}(?:\.[0-9]{1,3}){3}))",
            std::regex::icase};
        static const std::regex browse_endpoint_rx{
            R"(\bBrowse:\s*([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)\b)",
            std::regex::icase};
        static const std::regex loadmap_endpoint_rx{
            R"(\bLoadMap:\s*([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)\b)",
            std::regex::icase};
        static const std::regex remoteaddr_endpoint_rx{
            R"(\bRemoteAddr:\s*([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)\b)",
            std::regex::icase};

        for (const auto& candidate_path : collect_r5_log_candidates(preferred))
        {
            std::string content{};
            if (!read_text_file(candidate_path, content))
            {
                continue;
            }

            std::vector<std::string> local_hosts{};
            std::vector<std::string> remote_hosts{};
            std::vector<std::string> remote_publics{};
            std::vector<std::pair<std::string, std::string>> fallback_direct_candidates{};
            auto lower_ascii_local = [](std::string value) {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return value;
            };
            auto append_fallback_candidate = [&](const std::string& ip, const std::string& source) {
                if (!parse_ipv4_octets(ip).has_value())
                {
                    return;
                }
                for (auto& existing : fallback_direct_candidates)
                {
                    if (existing.first == ip)
                    {
                        const auto existing_source = lower_ascii_local(existing.second);
                        const auto incoming_source = lower_ascii_local(source);
                        if (existing_source == "start_direct_connection" &&
                            incoming_source != "start_direct_connection")
                        {
                            existing.second = source;
                        }
                        return;
                    }
                }
                fallback_direct_candidates.emplace_back(ip, source);
            };
            std::istringstream lines{content};
            std::string line{};
            while (std::getline(lines, line))
            {
                const bool local_line = line.find("Added Local Ice Candidates") != std::string::npos;
                const bool remote_line =
                    line.find("SetRemoteIceData") != std::string::npos ||
                    line.find("Added remote candidates") != std::string::npos;
                if (local_line || remote_line)
                {
                    for (auto it = std::sregex_iterator(line.begin(), line.end(), candidate_rx);
                         it != std::sregex_iterator{};
                         ++it)
                    {
                        if (it->size() < 4)
                        {
                            continue;
                        }
                        const auto ip = (*it)[1].str();
                        auto type = (*it)[3].str();
                        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) {
                            return static_cast<char>(std::tolower(c));
                        });

                        if (local_line && type == "host")
                        {
                            append_unique_ip(local_hosts, ip);
                        }
                        else if (remote_line && type == "host")
                        {
                            append_unique_ip(remote_hosts, ip);
                        }
                        else if (remote_line && type == "srflx" && is_public_ipv4(ip))
                        {
                            append_unique_ip(remote_publics, ip);
                        }
                    }
                }

                std::smatch direct_match{};
                if (std::regex_search(line, direct_match, direct_server_address_rx) && direct_match.size() >= 2)
                {
                    append_fallback_candidate(direct_match[1].str(), "start_direct_connection");
                }
                if (std::regex_search(line, direct_match, browse_endpoint_rx) && direct_match.size() >= 2)
                {
                    append_fallback_candidate(direct_match[1].str(), "browse_endpoint");
                }
                if (std::regex_search(line, direct_match, loadmap_endpoint_rx) && direct_match.size() >= 2)
                {
                    append_fallback_candidate(direct_match[1].str(), "loadmap_endpoint");
                }
                if (std::regex_search(line, direct_match, remoteaddr_endpoint_rx) && direct_match.size() >= 2)
                {
                    append_fallback_candidate(direct_match[1].str(), "remoteaddr_endpoint");
                }
            }

            BridgeRouteDiscovery result{};
            result.log_path = candidate_path;
            result.remote_host_candidate = remote_hosts.empty() ? std::string{} : remote_hosts.front();
            result.remote_public_candidate = remote_publics.empty() ? std::string{} : remote_publics.front();
            result.local_host_summary = summarize_ips(local_hosts);
            result.fallback_direct_candidates = fallback_direct_candidates;
            const bool process_same_machine_evidence = has_local_dedicated_server_process_evidence();
            result.same_machine_process_evidence = process_same_machine_evidence;

            bool same_machine = false;
            for (const auto& remote_host : remote_hosts)
            {
                if (std::find(local_hosts.begin(), local_hosts.end(), remote_host) != local_hosts.end())
                {
                    same_machine = true;
                    break;
                }
            }

            if (same_machine)
            {
                result.same_machine_evidence = true;
                append_unique_ip(result.ordered_candidates, "127.0.0.1");
                for (const auto& remote_host : remote_hosts)
                {
                    if (std::find(local_hosts.begin(), local_hosts.end(), remote_host) != local_hosts.end())
                    {
                        append_unique_ip(result.ordered_candidates, remote_host);
                    }
                }
                for (const auto& local_host : local_hosts)
                {
                    append_unique_ip(result.ordered_candidates, local_host);
                }
                for (const auto& remote_public : remote_publics)
                {
                    append_unique_ip(result.ordered_candidates, remote_public);
                }
                if (!result.ordered_candidates.empty())
                {
                    result.found = true;
                    result.host = result.ordered_candidates.front();
                    result.reason = "same_machine_host_candidate";
                    return result;
                }
            }

            if (!same_machine && process_same_machine_evidence)
            {
                const bool loopback_local_observed =
                    std::find(local_hosts.begin(), local_hosts.end(), "127.0.0.1") != local_hosts.end();
                bool loopback_direct_observed = false;
                for (const auto& direct_candidate : fallback_direct_candidates)
                {
                    if (direct_candidate.first == "127.0.0.1")
                    {
                        loopback_direct_observed = true;
                        break;
                    }
                }

                if (loopback_local_observed || loopback_direct_observed)
                {
                    result.same_machine_evidence = true;
                    append_unique_ip(result.ordered_candidates, "127.0.0.1");
                    for (const auto& local_host : local_hosts)
                    {
                        append_unique_ip(result.ordered_candidates, local_host);
                    }
                    for (const auto& remote_public : remote_publics)
                    {
                        append_unique_ip(result.ordered_candidates, remote_public);
                    }
                    for (const auto& remote_host : remote_hosts)
                    {
                        append_unique_ip(result.ordered_candidates, remote_host);
                    }
                    if (!result.ordered_candidates.empty())
                    {
                        result.found = true;
                        result.host = result.ordered_candidates.front();
                        result.reason = "same_machine_process_candidate";
                        return result;
                    }
                }
            }

            std::vector<std::string> same_lan_hosts{};
            for (const auto& remote_host : remote_hosts)
            {
                for (const auto& local_host : local_hosts)
                {
                    if (is_same_lan_hint(local_host, remote_host))
                    {
                        append_unique_ip(same_lan_hosts, remote_host);
                    }
                }
            }
            if (!same_lan_hosts.empty())
            {
                for (const auto& same_lan : same_lan_hosts)
                {
                    append_unique_ip(result.ordered_candidates, same_lan);
                }
                for (const auto& remote_public : remote_publics)
                {
                    append_unique_ip(result.ordered_candidates, remote_public);
                }
                for (const auto& remote_host : remote_hosts)
                {
                    append_unique_ip(result.ordered_candidates, remote_host);
                }
                result.found = true;
                result.host = result.ordered_candidates.front();
                result.reason = "same_lan_host_candidate";
                return result;
            }

            for (const auto& remote_public : remote_publics)
            {
                append_unique_ip(result.ordered_candidates, remote_public);
            }
            for (const auto& remote_host : remote_hosts)
            {
                append_unique_ip(result.ordered_candidates, remote_host);
            }
            if (!result.ordered_candidates.empty())
            {
                result.found = true;
                result.host = result.ordered_candidates.front();
                result.reason = is_public_ipv4(result.host) ? "public_srflx_candidate" : "remote_host_fallback";
                return result;
            }

            if (!fallback_direct_candidates.empty())
            {
                std::vector<std::string> same_machine_hosts{};
                std::vector<std::string> same_lan_hosts_from_direct{};
                std::vector<std::string> public_hosts_from_direct{};
                std::vector<std::string> other_hosts_from_direct{};
                bool explicit_loopback_direct_evidence = false;
                for (const auto& entry : fallback_direct_candidates)
                {
                    const auto& ip = entry.first;
                    if (ip == "127.0.0.1")
                    {
                        const auto source_lower = lower_ascii_local(entry.second);
                        if (source_lower == "remoteaddr_endpoint" ||
                            source_lower == "browse_endpoint" ||
                            source_lower == "loadmap_endpoint")
                        {
                            explicit_loopback_direct_evidence = true;
                        }
                        continue;
                    }
                    const bool same_machine_direct =
                        std::find(local_hosts.begin(), local_hosts.end(), ip) != local_hosts.end();
                    if (same_machine_direct)
                    {
                        append_unique_ip(same_machine_hosts, ip);
                        continue;
                    }
                    if (is_private_candidate_on_local_subnet(ip, local_hosts))
                    {
                        if (is_private_ipv4(ip))
                        {
                            append_unique_ip(same_lan_hosts_from_direct, ip);
                        }
                        else if (is_public_ipv4(ip))
                        {
                            append_unique_ip(public_hosts_from_direct, ip);
                        }
                        else
                        {
                            append_unique_ip(other_hosts_from_direct, ip);
                        }
                        continue;
                    }
                    if (is_public_ipv4(ip))
                    {
                        append_unique_ip(public_hosts_from_direct, ip);
                    }
                    else
                    {
                        append_unique_ip(other_hosts_from_direct, ip);
                    }
                }

                if (explicit_loopback_direct_evidence && process_same_machine_evidence)
                {
                    append_unique_ip(same_machine_hosts, "127.0.0.1");
                }

                if (!same_machine_hosts.empty())
                {
                    result.same_machine_evidence = true;
                    for (const auto& ip : same_machine_hosts)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    for (const auto& ip : same_lan_hosts_from_direct)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    for (const auto& ip : public_hosts_from_direct)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    for (const auto& ip : other_hosts_from_direct)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    result.found = !result.ordered_candidates.empty();
                    if (result.found)
                    {
                        result.host = result.ordered_candidates.front();
                        result.reason = "same_machine_direct_connect_candidate";
                        if (result.remote_host_candidate.empty())
                        {
                            result.remote_host_candidate = result.host;
                        }
                        return result;
                    }
                }

                if (!same_lan_hosts_from_direct.empty())
                {
                    for (const auto& ip : same_lan_hosts_from_direct)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    for (const auto& ip : public_hosts_from_direct)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    for (const auto& ip : other_hosts_from_direct)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    result.found = !result.ordered_candidates.empty();
                    if (result.found)
                    {
                        result.host = result.ordered_candidates.front();
                        result.reason = "same_lan_direct_connect_candidate";
                        if (result.remote_host_candidate.empty())
                        {
                            result.remote_host_candidate = result.host;
                        }
                        return result;
                    }
                }

                if (!public_hosts_from_direct.empty())
                {
                    for (const auto& ip : public_hosts_from_direct)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    for (const auto& ip : other_hosts_from_direct)
                    {
                        append_unique_ip(result.ordered_candidates, ip);
                    }
                    result.found = !result.ordered_candidates.empty();
                    if (result.found)
                    {
                        result.host = result.ordered_candidates.front();
                        result.reason = "public_direct_connect_candidate";
                        if (result.remote_host_candidate.empty())
                        {
                            result.remote_host_candidate = result.host;
                        }
                        return result;
                    }
                }

                for (const auto& ip : other_hosts_from_direct)
                {
                    append_unique_ip(result.ordered_candidates, ip);
                }
                if (!result.ordered_candidates.empty())
                {
                    result.found = true;
                    result.host = result.ordered_candidates.front();
                    result.reason = "direct_connect_fallback";
                    if (result.remote_host_candidate.empty())
                    {
                        result.remote_host_candidate = result.host;
                    }
                    return result;
                }
            }
        }

        return {};
    }

    auto get_env_var(const char* name) -> std::string
    {
        if (!name || !*name)
        {
            return {};
        }
#if defined(_WIN32)
        char* value = nullptr;
        size_t value_size = 0;
        if (_dupenv_s(&value, &value_size, name) != 0 || !value)
        {
            return {};
        }
        std::string out{value};
        std::free(value);
        return out;
#else
        if (const char* value = std::getenv(name); value && *value)
        {
            return value;
        }
        return {};
#endif
    }

    auto current_executable_path() -> std::filesystem::path
    {
        std::array<char, 32768> buffer{};
        const auto len = GetModuleFileNameA(nullptr, buffer.data(), static_cast<unsigned long>(buffer.size()));
        if (len == 0 || len >= buffer.size())
        {
            return {};
        }
        return std::filesystem::path(std::string{buffer.data(), len});
    }

    auto is_dedicated_server_process(const std::filesystem::path& cwd, const std::filesystem::path& mod_root) -> bool
    {
        auto combined = current_executable_path().string() + "|" + cwd.string() + "|" + mod_root.string();
        std::replace(combined.begin(), combined.end(), '/', '\\');
        combined = lower_ascii_path_token(combined);
        return combined.find("windroseserver") != std::string::npos ||
               combined.find("\\windowsserver\\") != std::string::npos ||
               combined.find("\\serverfiles\\") != std::string::npos;
    }

    auto find_r5_root_from_path(std::filesystem::path start) -> std::optional<std::filesystem::path>
    {
        if (start.empty())
        {
            return std::nullopt;
        }
        std::error_code ec{};
        if (!std::filesystem::is_directory(start, ec))
        {
            start = start.parent_path();
        }

        for (auto current = start; !current.empty(); current = current.parent_path())
        {
            if (std::filesystem::exists(current / "Saved" / "SaveProfiles"))
            {
                return current;
            }
            if (current == current.parent_path())
            {
                break;
            }
        }
        return std::nullopt;
    }

    auto collect_dedicated_save_profile_roots(const std::filesystem::path& cwd, const std::filesystem::path& mod_root) -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> roots{};
        if (auto r5_root = find_r5_root_from_path(cwd); r5_root.has_value())
        {
            append_unique_path(roots, *r5_root / "Saved" / "SaveProfiles" / "Default");
        }
        if (auto r5_root = find_r5_root_from_path(mod_root); r5_root.has_value())
        {
            append_unique_path(roots, *r5_root / "Saved" / "SaveProfiles" / "Default");
        }
        roots.erase(
            std::remove_if(roots.begin(), roots.end(), [](const auto& path) { return !std::filesystem::exists(path); }),
            roots.end());
        return roots;
    }

    auto collect_local_client_save_profile_roots() -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> roots{};
        const auto local_app_data = get_env_var("LOCALAPPDATA");
        if (local_app_data.empty())
        {
            return roots;
        }

        const auto appdata_profiles = std::filesystem::path(local_app_data) / "R5" / "Saved" / "SaveProfiles";
        if (!std::filesystem::exists(appdata_profiles))
        {
            return roots;
        }

        std::vector<std::filesystem::directory_entry> profile_dirs{};
        std::error_code iter_ec{};
        for (const auto& entry : std::filesystem::directory_iterator(appdata_profiles, iter_ec))
        {
            if (!iter_ec && entry.is_directory())
            {
                profile_dirs.push_back(entry);
            }
        }

        std::sort(profile_dirs.begin(), profile_dirs.end(), [](const auto& lhs, const auto& rhs) {
            std::error_code lhs_ec{};
            std::error_code rhs_ec{};
            return std::filesystem::last_write_time(lhs.path(), lhs_ec) > std::filesystem::last_write_time(rhs.path(), rhs_ec);
        });

        for (const auto& entry : profile_dirs)
        {
            append_unique_path(roots, entry.path());
        }
        return roots;
    }

    auto choose_world_id_from_worlds_root(const std::filesystem::path& worlds_root) -> std::optional<std::string>
    {
        if (!std::filesystem::exists(worlds_root))
        {
            return std::nullopt;
        }

        std::vector<std::filesystem::directory_entry> world_dirs{};
        std::error_code iter_ec{};
        for (const auto& entry : std::filesystem::directory_iterator(worlds_root, iter_ec))
        {
            if (iter_ec || !entry.is_directory())
            {
                continue;
            }
            const auto name = entry.path().filename().string();
            if (is_hex_world_id(name))
            {
                world_dirs.push_back(entry);
            }
        }

        if (world_dirs.empty())
        {
            return std::nullopt;
        }

        std::sort(world_dirs.begin(), world_dirs.end(), [](const auto& lhs, const auto& rhs) {
            std::error_code lhs_ec{};
            std::error_code rhs_ec{};
            return std::filesystem::last_write_time(lhs.path(), lhs_ec) > std::filesystem::last_write_time(rhs.path(), rhs_ec);
        });
        return world_dirs.front().path().filename().string();
    }

    auto choose_world_id_for_profile(const std::filesystem::path& profile_root) -> std::optional<std::string>
    {
        const std::vector<std::filesystem::path> worlds_roots = {
            profile_root / "RocksDB_v2" / "0.10.0" / "Worlds",
            profile_root / "RocksDB_v2_Backups" / "Worlds",
            profile_root / "RocksDB" / "0.10.0" / "Worlds"};

        for (const auto& worlds_root : worlds_roots)
        {
            if (auto world_id = choose_world_id_from_worlds_root(worlds_root); world_id.has_value())
            {
                return world_id;
            }
        }
        return std::nullopt;
    }

    auto resolve_data_root_for_role(const std::filesystem::path& cwd, const std::filesystem::path& mod_root) -> DataRootResolution
    {
        DataRootResolution out{};
        const bool dedicated_server = is_dedicated_server_process(cwd, mod_root);
        out.runtime_role = dedicated_server ? "DedicatedServer" : "LocalClientPending";
        out.authority_mode = dedicated_server ? "DedicatedServerAuthoritative" : "WorldAuthorityPending";
        out.data_mode = dedicated_server ? "ServerAuthoritative" : "LocalClientStartupCache";
        out.sidecar_kind = dedicated_server ? "authoritative" : "cache";
        out.authoritative = dedicated_server;

        const auto profile_roots = dedicated_server
            ? collect_dedicated_save_profile_roots(cwd, mod_root)
            : collect_local_client_save_profile_roots();

        for (const auto& profile_root : profile_roots)
        {
            if (auto world_id = choose_world_id_for_profile(profile_root); world_id.has_value())
            {
                out.profile_root = profile_root;
                out.world_id = *world_id;
                out.data_root = dedicated_server
                    ? profile_root / "WindroseTextSigns" / *world_id
                    : profile_root / "WindroseTextSigns" / "StartupCache" / *world_id;
                return out;
            }
        }

        if (!profile_roots.empty())
        {
            out.profile_root = profile_roots.front();
            out.world_id = "unknown-world";
            out.data_root = dedicated_server
                ? out.profile_root / "WindroseTextSigns" / out.world_id
                : out.profile_root / "WindroseTextSigns" / "StartupCache" / out.world_id;
            out.data_mode = dedicated_server ? "ServerAuthoritativePendingWorld" : "LocalClientStartupCachePendingWorld";
            out.sidecar_kind = dedicated_server ? "authoritative-pending-world" : "cache-pending-world";
            out.authoritative = dedicated_server;
            return out;
        }

        out.profile_root = mod_root;
        out.world_id = "unknown-world";
        out.data_root = mod_root / "Cache";
        out.data_mode = dedicated_server ? "ServerAuthoritativeFallbackModRoot" : "LocalClientStartupCacheFallbackModRoot";
        out.sidecar_kind = dedicated_server ? "authoritative-fallback-modroot" : "cache-fallback-modroot";
        out.authoritative = dedicated_server;
        out.data_root /= dedicated_server ? "DedicatedServer" : "StartupCache";
        out.data_root /= out.world_id;
        return out;
    }

    auto normalize_spaces(std::string_view text) -> std::string
    {
        std::string out{};
        out.reserve(text.size());
        bool in_space = false;
        for (char ch : text)
        {
            const bool is_space = (ch == ' ' || ch == '\t');
            if (is_space)
            {
                if (!in_space)
                {
                    out.push_back(' ');
                    in_space = true;
                }
            }
            else
            {
                out.push_back(ch);
                in_space = false;
            }
        }
        while (!out.empty() && out.front() == ' ')
        {
            out.erase(out.begin());
        }
        while (!out.empty() && out.back() == ' ')
        {
            out.pop_back();
        }
        return out;
    }

    auto split_lines_preserve_breaks(std::string_view text) -> std::vector<std::string>
    {
        std::vector<std::string> lines{};
        std::string current{};
        current.reserve(text.size());
        for (char ch : text)
        {
            if (ch == '\r')
            {
                continue;
            }
            if (ch == '\n')
            {
                lines.push_back(normalize_spaces(current));
                current.clear();
                continue;
            }
            current.push_back(ch);
        }
        lines.push_back(normalize_spaces(current));
        if (lines.empty())
        {
            lines.push_back("");
        }
        return lines;
    }

    auto split_words(std::string_view text) -> std::vector<std::string>
    {
        std::vector<std::string> words{};
        std::string current{};
        for (char ch : text)
        {
            if (ch == ' ')
            {
                if (!current.empty())
                {
                    words.push_back(current);
                    current.clear();
                }
            }
            else
            {
                current.push_back(ch);
            }
        }
        if (!current.empty())
        {
            words.push_back(current);
        }
        return words;
    }

    auto wrap_words_with_limit(const std::vector<std::string>& words, int char_limit, int max_rows) -> AutoSizeResult
    {
        AutoSizeResult out{};
        out.char_limit = std::max(1, char_limit);
        out.rows = 0;
        out.truncated = false;

        std::vector<std::string> rows{};
        std::string line{};
        size_t word_index = 0;
        while (word_index < words.size())
        {
            std::string word = words[word_index];
            if (word.empty())
            {
                ++word_index;
                continue;
            }

            if (static_cast<int>(word.size()) > out.char_limit)
            {
                if (!line.empty())
                {
                    rows.push_back(line);
                    line.clear();
                    if (static_cast<int>(rows.size()) >= max_rows)
                    {
                        out.truncated = true;
                        break;
                    }
                }

                size_t consumed = 0;
                while (consumed < word.size())
                {
                    const size_t take = static_cast<size_t>(out.char_limit);
                    std::string chunk = word.substr(consumed, take);
                    consumed += take;
                    if (consumed < word.size())
                    {
                        rows.push_back(chunk);
                        if (static_cast<int>(rows.size()) >= max_rows)
                        {
                            out.truncated = true;
                            break;
                        }
                    }
                    else
                    {
                        line = chunk;
                    }
                }
                if (out.truncated)
                {
                    break;
                }
                ++word_index;
                continue;
            }

            if (line.empty())
            {
                line = word;
                ++word_index;
                continue;
            }

            const int candidate_len = static_cast<int>(line.size() + 1 + word.size());
            if (candidate_len <= out.char_limit)
            {
                line += " ";
                line += word;
                ++word_index;
                continue;
            }

            rows.push_back(line);
            line.clear();
            if (static_cast<int>(rows.size()) >= max_rows)
            {
                out.truncated = true;
                break;
            }
        }

        if (!out.truncated && !line.empty())
        {
            rows.push_back(line);
        }

        if (rows.empty())
        {
            rows.push_back("");
        }

        out.rows = static_cast<int>(rows.size());
        out.wrapped_text.clear();
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (i > 0)
            {
                out.wrapped_text.push_back('\n');
            }
            out.wrapped_text += rows[i];
        }

        return out;
    }

    auto split_rows(std::string_view wrapped_text) -> std::vector<std::string>
    {
        std::vector<std::string> rows{};
        std::string current{};
        for (char ch : wrapped_text)
        {
            if (ch == '\n')
            {
                rows.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(ch);
            }
        }
        rows.push_back(current);
        return rows;
    }

    auto fit_text_for_plaque(std::string_view input_text) -> AutoSizeResult
    {
        constexpr float k_font_min = 10.0f;
        constexpr float k_font_max = 20.0f;
        constexpr int k_rows_min = 1;
        constexpr int k_rows_max = 4;
        constexpr float k_chars_at_font_min = 12.0f;
        constexpr float k_chars_at_font_max = 0.0f;
        constexpr float k_line_step_factor = 0.60f;
        constexpr float k_vertical_budget = 24.0f;

        AutoSizeResult best{};
        best.font_size = k_font_min;
        best.rows = 1;
        best.char_limit = 12;
        best.truncated = false;

        const auto input_lines = split_lines_preserve_breaks(input_text);
        const bool has_explicit_line_breaks = (input_lines.size() > 1);
        const int explicit_rows = static_cast<int>(input_lines.size());
        bool found_valid = false;
        int best_rows = 999;
        float best_font = k_font_min;
        int best_added_rows = 999;

        if (input_lines.empty())
        {
            best.wrapped_text.clear();
            return best;
        }

        const float span = std::max(0.001f, (k_font_max - k_font_min));
        for (float font = k_font_max; font >= k_font_min; font -= 0.5f)
        {
            const float t = (font - k_font_min) / span;
            const float chars_f = k_chars_at_font_min + ((k_chars_at_font_max - k_chars_at_font_min) * t);
            const int char_limit = std::max(1, static_cast<int>(std::floor(chars_f)));
            AutoSizeResult candidate{};
            candidate.font_size = font;
            candidate.char_limit = char_limit;
            candidate.truncated = false;

            std::vector<std::string> rows{};
            int remaining_rows = k_rows_max;
            for (const auto& raw_line : input_lines)
            {
                if (remaining_rows <= 0)
                {
                    candidate.truncated = true;
                    break;
                }

                if (raw_line.empty())
                {
                    rows.push_back("");
                    --remaining_rows;
                    continue;
                }

                auto line_words = split_words(raw_line);
                auto line_wrap = wrap_words_with_limit(line_words, char_limit, remaining_rows);
                if (line_wrap.truncated)
                {
                    candidate.truncated = true;
                }

                auto line_rows = split_rows(line_wrap.wrapped_text);
                for (const auto& row : line_rows)
                {
                    rows.push_back(row);
                    --remaining_rows;
                    if (remaining_rows <= 0 && (&row != &line_rows.back()))
                    {
                        candidate.truncated = true;
                    }
                }

                if (candidate.truncated)
                {
                    break;
                }
            }

            if (rows.empty())
            {
                rows.push_back("");
            }

            candidate.rows = static_cast<int>(rows.size());
            candidate.wrapped_text.clear();
            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (i > 0)
                {
                    candidate.wrapped_text.push_back('\n');
                }
                candidate.wrapped_text += rows[i];
            }

            const float vertical_usage = static_cast<float>(candidate.rows) * (font * k_line_step_factor);
            if (candidate.truncated || candidate.rows < k_rows_min || candidate.rows > k_rows_max || vertical_usage > k_vertical_budget)
            {
                best = candidate;
                continue;
            }

            const int added_rows = std::max(0, candidate.rows - explicit_rows);
            bool better = false;
            if (!found_valid)
            {
                better = true;
            }
            else if (!has_explicit_line_breaks)
            {
                if (candidate.rows < best_rows)
                {
                    better = true;
                }
                else if (candidate.rows == best_rows && font > best_font)
                {
                    better = true;
                }
            }
            else
            {
                if (added_rows < best_added_rows)
                {
                    better = true;
                }
                else if (added_rows == best_added_rows && font > best_font)
                {
                    better = true;
                }
            }

            if (better)
            {
                found_valid = true;
                best = candidate;
                best_rows = candidate.rows;
                best_font = font;
                best_added_rows = added_rows;
            }
        }

        if (!found_valid)
        {
            best.font_size = k_font_min;
        }
        return best;
    }

    auto count_wrapped_rows(std::string_view text_value) -> int
    {
        if (text_value.empty())
        {
            return 1;
        }
        int rows = 1;
        for (char ch : text_value)
        {
            if (ch == '\n')
            {
                ++rows;
            }
        }
        return std::max(1, rows);
    }

    auto for_each_property_in_chain_compat(UStruct* owner, const std::function<void(FProperty*)>& visitor) -> void
    {
        if (!owner || !visitor)
        {
            return;
        }

        auto* cursor = owner;
        uint32_t safety_hops = 0;
        while (cursor && safety_hops++ < 256)
        {
            for (auto* prop = cursor->GetFirstProperty(); prop; prop = prop->GetNextFieldAsProperty())
            {
                visitor(prop);
            }
            cursor = cursor->GetSuperStruct();
        }
    }

    auto lower_copy_ascii(std::string value) -> std::string
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    auto trim_copy_ascii(std::string value) -> std::string
    {
        auto is_not_space = [](unsigned char ch) {
            return std::isspace(ch) == 0;
        };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
        return value;
    }

    auto count_line_breaks(std::string_view value) -> size_t
    {
        return static_cast<size_t>(std::count(value.begin(), value.end(), '\n'));
    }

    auto strip_one_terminal_line_break(std::string value) -> std::string
    {
        if (!value.empty() && value.back() == '\n')
        {
            value.pop_back();
            if (!value.empty() && value.back() == '\r')
            {
                value.pop_back();
            }
        }
        return value;
    }

    auto strip_terminal_line_breaks(std::string value) -> std::string
    {
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
        {
            value.pop_back();
        }
        return value;
    }

    auto sanitize_path_segment(std::string value) -> std::string
    {
        if (value.empty())
        {
            return "unknown";
        }
        for (auto& ch : value)
        {
            const auto uch = static_cast<unsigned char>(ch);
            if (std::isalnum(uch) == 0 && ch != '-' && ch != '_' && ch != '.')
            {
                ch = '_';
            }
        }
        return value.empty() ? std::string{"unknown"} : value;
    }

    auto contains_any_token(const std::string& haystack, std::initializer_list<const char*> tokens) -> bool
    {
        for (const auto* token : tokens)
        {
            if (!token)
            {
                continue;
            }
            if (haystack.find(token) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    auto is_confirmed_label_text_kind(const std::string& kind) -> bool
    {
        const auto lowered = lower_copy_ascii(kind);
        return lowered == "labeltext" || lowered == "labeltextplacement";
    }

    auto infer_new_record_kind_from_asset(const std::string& asset) -> std::string
    {
        const auto lowered = lower_copy_ascii(asset);
        if (lowered.find("lables_wooden_text") != std::string::npos ||
            lowered.find("label_text") != std::string::npos)
        {
            return "LabelText";
        }
        return "UnverifiedWoodenLabel";
    }

    auto infer_backing_asset_from_kind(const std::string& kind, const std::string& asset) -> std::string
    {
        if (!is_confirmed_label_text_kind(kind))
        {
            return {};
        }
        if (!asset.empty() && asset != "unknown")
        {
            return asset;
        }
        return "DA_BI_Utilities_Lables_Wooden_Ship";
    }

    auto try_extract_property_log_value(FProperty* prop, void* container) -> std::optional<std::string>
    {
        if (!prop || !container)
        {
            return std::nullopt;
        }

        const auto prop_name_lower = lower_copy_ascii(RC::to_string(prop->GetName()));
        if (!contains_any_token(prop_name_lower, {
                "icon",
                "texture",
                "sprite",
                "label",
                "title",
                "name",
                "recipe",
                "build",
                "construct",
                "class",
                "actor",
                "mesh",
                "material",
                "plaque",
                "wallplaque",
                "utility",
                "lables",
                "wooden"}))
        {
            return std::nullopt;
        }

        const auto prop_hash = prop->GetClass().HashObject();
        if (prop_hash == FNameProperty::StaticClass().HashObject())
        {
            if (auto* value = prop->ContainerPtrToValuePtr<FName>(container))
            {
                return "name=" + RC::to_string(value->ToString());
            }
            return std::nullopt;
        }
        if (prop_hash == FTextProperty::StaticClass().HashObject())
        {
            if (auto* value = prop->ContainerPtrToValuePtr<FText>(container))
            {
                return "text=" + RC::to_string(value->ToString());
            }
            return std::nullopt;
        }
        if (prop_hash == FObjectProperty::StaticClass().HashObject())
        {
            if (auto* obj_ptr = prop->ContainerPtrToValuePtr<UObject*>(container); obj_ptr && *obj_ptr)
            {
                return "obj=" + RC::to_string((*obj_ptr)->GetFullName());
            }
            return std::nullopt;
        }
        if (prop_hash == FClassProperty::StaticClass().HashObject())
        {
            if (auto* class_ptr = prop->ContainerPtrToValuePtr<UClass*>(container); class_ptr && *class_ptr)
            {
                return "class=" + RC::to_string((*class_ptr)->GetFullName());
            }
            return std::nullopt;
        }
        if (prop_hash == FBoolProperty::StaticClass().HashObject())
        {
            if (auto* bool_prop = static_cast<FBoolProperty*>(prop))
            {
                if (auto* storage = prop->ContainerPtrToValuePtr<void>(container))
                {
                    return std::string{"bool="} + (bool_prop->GetPropertyValue(storage) ? "true" : "false");
                }
            }
            return std::nullopt;
        }

        return std::nullopt;
    }

    auto try_extract_phase5_selection_property_value(FProperty* prop, void* container) -> std::optional<std::string>
    {
        if (!prop || !container)
        {
            return std::nullopt;
        }

        const auto prop_name = RC::to_string(prop->GetName());
        const auto prop_name_lower = lower_copy_ascii(prop_name);
        const bool relevant_name = contains_any_token(prop_name_lower, {
            "selected",
            "hover",
            "focus",
            "active",
            "current",
            "entry",
            "item",
            "asset",
            "recipe",
            "build",
            "construct",
            "label",
            "lable",
            "plaque",
            "category",
            "group",
            "index",
            "slot",
            "data"});

        const auto prop_hash = prop->GetClass().HashObject();
        if (prop_hash == FNameProperty::StaticClass().HashObject())
        {
            if (auto* value = prop->ContainerPtrToValuePtr<FName>(container))
            {
                auto result = "name=" + RC::to_string(value->ToString());
                if (relevant_name || contains_any_token(lower_copy_ascii(result), {"building_lable_", "da_bi_utilities_lables_wooden_", "wallplaque", "plaque"}))
                {
                    return result;
                }
            }
            return std::nullopt;
        }
        if (prop_hash == FTextProperty::StaticClass().HashObject())
        {
            if (auto* value = prop->ContainerPtrToValuePtr<FText>(container))
            {
                auto result = "text=" + RC::to_string(value->ToString());
                if (relevant_name || contains_any_token(lower_copy_ascii(result), {"building_lable_", "label:", "plaque"}))
                {
                    return result;
                }
            }
            return std::nullopt;
        }
        if (prop_hash == FObjectProperty::StaticClass().HashObject())
        {
            if (auto* obj_ptr = prop->ContainerPtrToValuePtr<UObject*>(container); obj_ptr && *obj_ptr)
            {
                auto result = "obj=" + RC::to_string((*obj_ptr)->GetFullName());
                if (relevant_name || contains_any_token(lower_copy_ascii(result), {"da_bi_utilities_lables_wooden_", "building_lable_", "wallplaque", "plaque"}))
                {
                    return result;
                }
            }
            return std::nullopt;
        }
        if (prop_hash == FClassProperty::StaticClass().HashObject())
        {
            if (auto* class_ptr = prop->ContainerPtrToValuePtr<UClass*>(container); class_ptr && *class_ptr)
            {
                auto result = "class=" + RC::to_string((*class_ptr)->GetFullName());
                if (relevant_name || contains_any_token(lower_copy_ascii(result), {"building", "plaque", "label", "lable"}))
                {
                    return result;
                }
            }
            return std::nullopt;
        }
        if (prop_hash == FBoolProperty::StaticClass().HashObject())
        {
            if (!relevant_name)
            {
                return std::nullopt;
            }
            if (auto* bool_prop = static_cast<FBoolProperty*>(prop))
            {
                if (auto* storage = prop->ContainerPtrToValuePtr<void>(container))
                {
                    return std::string{"bool="} + (bool_prop->GetPropertyValue(storage) ? "true" : "false");
                }
            }
            return std::nullopt;
        }

        const auto prop_class_name = lower_copy_ascii(RC::to_string(prop->GetClass().GetName()));
        if (relevant_name &&
            contains_any_token(prop_class_name, {"intproperty", "uint32property"}) &&
            contains_any_token(prop_name_lower, {"index", "slot", "count", "id"}))
        {
            if (auto* value = prop->ContainerPtrToValuePtr<int32_t>(container))
            {
                return "int=" + std::to_string(*value);
            }
        }

        return std::nullopt;
    }

    auto append_flag_name(std::string& out, const char* name) -> void
    {
        if (!name || !*name)
        {
            return;
        }
        if (!out.empty())
        {
            out += "|";
        }
        out += name;
    }

    auto function_flag_summary(UFunction* function) -> std::string
    {
        if (!function)
        {
            return "none";
        }

        std::string out{};
        try
        {
            const auto flags = static_cast<uint32_t>(function->GetFunctionFlags());
            const auto has = [&](EFunctionFlags flag) {
                return (flags & static_cast<uint32_t>(flag)) != 0U;
            };

            if (has(FUNC_Net)) { append_flag_name(out, "Net"); }
            if (has(FUNC_NetReliable)) { append_flag_name(out, "Reliable"); }
            if (has(FUNC_NetServer)) { append_flag_name(out, "Server"); }
            if (has(FUNC_NetClient)) { append_flag_name(out, "Client"); }
            if (has(FUNC_NetMulticast)) { append_flag_name(out, "Multicast"); }
            if (has(FUNC_NetValidate)) { append_flag_name(out, "Validate"); }
            if (has(FUNC_BlueprintAuthorityOnly)) { append_flag_name(out, "AuthorityOnly"); }
            if (has(FUNC_Native)) { append_flag_name(out, "Native"); }
            if (has(FUNC_Event)) { append_flag_name(out, "Event"); }
            if (has(FUNC_BlueprintCallable)) { append_flag_name(out, "BlueprintCallable"); }
            if (has(FUNC_BlueprintEvent)) { append_flag_name(out, "BlueprintEvent"); }

            std::ostringstream hex{};
            hex << "0x" << std::uppercase << std::hex << flags;
            if (!out.empty())
            {
                out += ",";
            }
            out += "raw=" + hex.str();
        }
        catch (...)
        {
            out = "unreadable";
        }

        return out.empty() ? std::string{"none"} : out;
    }

    auto property_flag_summary(FProperty* prop) -> std::string
    {
        if (!prop)
        {
            return "none";
        }

        std::string out{};
        try
        {
            const auto flags = static_cast<uint64_t>(prop->GetPropertyFlags());
            const auto has = [&](EPropertyFlags flag) {
                return (flags & static_cast<uint64_t>(flag)) != 0ULL;
            };

            if (has(CPF_Parm)) { append_flag_name(out, "Parm"); }
            if (has(CPF_OutParm)) { append_flag_name(out, "Out"); }
            if (has(CPF_ReturnParm)) { append_flag_name(out, "Return"); }
            if (has(CPF_ConstParm)) { append_flag_name(out, "Const"); }
            if (has(CPF_ReferenceParm)) { append_flag_name(out, "Ref"); }
            if (has(CPF_Net)) { append_flag_name(out, "Net"); }
            if (has(CPF_RepNotify)) { append_flag_name(out, "RepNotify"); }
            if (has(CPF_RepSkip)) { append_flag_name(out, "RepSkip"); }
            if (has(CPF_SaveGame)) { append_flag_name(out, "SaveGame"); }
            if (has(CPF_Transient)) { append_flag_name(out, "Transient"); }
            if (has(CPF_BlueprintVisible)) { append_flag_name(out, "BlueprintVisible"); }
            if (has(CPF_Edit)) { append_flag_name(out, "Edit"); }

            std::ostringstream hex{};
            hex << "0x" << std::uppercase << std::hex << flags;
            if (!out.empty())
            {
                out += ",";
            }
            out += "raw=" + hex.str();
        }
        catch (...)
        {
            out = "unreadable";
        }

        return out.empty() ? std::string{"none"} : out;
    }

    auto score_native_transport_candidate(const std::string& full_name_lower, UFunction* function) -> int
    {
        int score = 0;
        const auto bump = [&](std::initializer_list<const char*> tokens, int amount) {
            for (const auto* token : tokens)
            {
                if (token && full_name_lower.find(token) != std::string::npos)
                {
                    score += amount;
                }
            }
        };

        bump({"usermarker", "mapmarker", "marker", "ping", "chat", "message"}, 7);
        bump({"playerinworld", "player", "session", "account", "island", "world"}, 4);
        bump({"r5bl", "businessrule", "rule", "request", "response"}, 4);
        bump({"server", "client", "multicast", "replicate", "replicated", "rpc", "net"}, 5);
        bump({"add", "update", "remove", "set", "send", "broadcast", "notify"}, 2);

        try
        {
            const auto flags = static_cast<uint32_t>(function->GetFunctionFlags());
            const auto has = [&](EFunctionFlags flag) {
                return (flags & static_cast<uint32_t>(flag)) != 0U;
            };
            if (has(FUNC_Net)) { score += 12; }
            if (has(FUNC_NetServer)) { score += 10; }
            if (has(FUNC_NetClient)) { score += 10; }
            if (has(FUNC_NetMulticast)) { score += 10; }
            if (has(FUNC_NetReliable)) { score += 3; }
        }
        catch (...)
        {
        }

        return score;
    }

    auto to_hex_guid(const FGuid& guid) -> std::string;

    auto is_player_marker_probe_object_candidate(
        const std::string& full_name_lower,
        const std::string& class_name_lower,
        const std::string& outer_name_lower) -> bool
    {
        const auto haystack = full_name_lower + " " + class_name_lower + " " + outer_name_lower;
        if (haystack.find("default__") != std::string::npos)
        {
            return false;
        }

        if (contains_any_token(haystack, {
                "playermappablekeysettings",
                "enhancedplayermappablekeyprofile",
                "inputmappingcontext",
                "inputaction ",
                "/game/gameplay/game/input/",
                "/script/enhancedinput.",
                "imc_",
                "ia_"}))
        {
            return false;
        }

        // Component templates on generated blueprint classes carry lots of inherited
        // replicated SceneComponent fields, but they are not live per-player state.
        if (contains_any_token(haystack, {"blueprintgeneratedclass /game/"}) &&
            contains_any_token(haystack, {"_gen_variable", "defaultsceneroot"}))
        {
            return false;
        }

        const bool live_context = contains_any_token(haystack, {
            "persistentlevel",
            "/engine/transient.gameengine",
            "/engine/transient.r5gameinstance",
            "playercontroller_c_",
            "playerstate_c_",
            "bp_r5character_c_",
            "gamestate",
            "game_state",
            "netdriver",
            "replicator"});

        if (contains_any_token(haystack, {
                "r5markermodelpawn",
                "r5markermodelship",
                "r5markermodeluser",
                "r5markercomponent",
                "r5playerstate",
                "r5playercontroller",
                "r5markersreplicator",
                "mapcontroller",
                "playercontroller",
                "playerstate"}))
        {
            return true;
        }

        return live_context &&
            contains_any_token(haystack, {"marker", "map"}) &&
            contains_any_token(haystack, {"player", "pawn", "ship", "party", "account", "owner"});
    }

    auto is_player_marker_focused_probe_object(
        const std::string& full_name_lower,
        const std::string& class_name_lower,
        const std::string& outer_name_lower) -> bool
    {
        const auto haystack = full_name_lower + " " + class_name_lower + " " + outer_name_lower;
        if (haystack.find("default__") != std::string::npos)
        {
            return false;
        }

        if (contains_any_token(haystack, {"blueprintgeneratedclass /game/"}) &&
            contains_any_token(haystack, {"_gen_variable", "defaultsceneroot"}))
        {
            return false;
        }

        return contains_any_token(haystack, {
            "r5markersreplicator",
            "r5markersreplicationcomponent",
            "r5markermodelbase",
            "r5markermodelsimple",
            "r5markermodeluser",
            "bp_markermodel_simple"});
    }

    auto is_player_marker_probe_live_context(
        const std::string& full_name_lower,
        const std::string& class_name_lower,
        const std::string& outer_name_lower) -> bool
    {
        const auto haystack = full_name_lower + " " + class_name_lower + " " + outer_name_lower;
        return contains_any_token(haystack, {
            "persistentlevel",
            "/engine/transient.gameengine",
            "/engine/transient.r5gameinstance",
            "playercontroller_c_",
            "playerstate_c_",
            "bp_r5character_c_",
            "gamestate",
            "game_state",
            "netdriver",
            "replicator",
            "r5markercomponent",
            "markermodel"});
    }

    auto is_player_marker_probe_property_candidate(FProperty* prop) -> bool
    {
        if (!prop)
        {
            return false;
        }

        const auto prop_name_lower = lower_copy_ascii(RC::to_string(prop->GetName()));
        const auto prop_class_lower = lower_copy_ascii(RC::to_string(prop->GetClass().GetName()));
        const auto flags = static_cast<uint64_t>(prop->GetPropertyFlags());
        const bool replicatedish =
            (flags & static_cast<uint64_t>(CPF_Net)) != 0ULL ||
            (flags & static_cast<uint64_t>(CPF_RepNotify)) != 0ULL;
        const bool stringish = contains_any_token(prop_class_lower, {
            "strproperty",
            "textproperty",
            "nameproperty"});
        const bool objectish = contains_any_token(prop_class_lower, {
            "objectproperty",
            "classproperty"});
        const bool numericish = contains_any_token(prop_class_lower, {
            "boolproperty",
            "floatproperty",
            "doubleproperty",
            "intproperty",
            "uint32property",
            "uint64property",
            "byteproperty",
            "enumproperty"});
        const bool vectorish =
            prop->GetSize() >= FVector::StaticSize() &&
            contains_any_token(prop_name_lower + " " + prop_class_lower, {
                "location",
                "position",
                "vector",
                "transform",
                "rotation",
                "rotator"});
        const bool name_interesting = contains_any_token(prop_name_lower, {
            "player",
            "pawn",
            "controller",
            "marker",
            "map",
            "location",
            "position",
            "rotation",
            "transform",
            "velocity",
            "ship",
            "island",
            "world",
            "party",
            "team",
            "owner",
            "account",
            "name",
            "display",
            "icon",
            "visible",
            "selected",
            "target",
            "id",
            "guid",
            "state"});

        return replicatedish || ((stringish || objectish || numericish || vectorish) && name_interesting);
    }

    auto is_player_marker_focused_probe_property_candidate(FProperty* prop) -> bool
    {
        if (!prop)
        {
            return false;
        }

        const auto prop_name_lower = lower_copy_ascii(RC::to_string(prop->GetName()));
        const auto prop_class_lower = lower_copy_ascii(RC::to_string(prop->GetClass().GetName()));
        const auto flags = static_cast<uint64_t>(prop->GetPropertyFlags());
        const bool replicatedish =
            (flags & static_cast<uint64_t>(CPF_Net)) != 0ULL ||
            (flags & static_cast<uint64_t>(CPF_RepNotify)) != 0ULL;

        if (replicatedish)
        {
            return true;
        }

        return contains_any_token(prop_name_lower + " " + prop_class_lower, {
            "shownname",
            "displayname",
            "markername",
            "markerguid",
            "markerid",
            "markertype",
            "selectedicon",
            "icon",
            "location",
            "position",
            "rotationangle",
            "owner",
            "worldcache",
            "snapping"});
    }

    auto truncate_probe_value(std::string value, const size_t max_len = 220) -> std::string
    {
        std::replace(value.begin(), value.end(), '\n', ' ');
        std::replace(value.begin(), value.end(), '\r', ' ');
        if (value.size() > max_len)
        {
            value.resize(max_len);
            value += "...";
        }
        return value;
    }

    auto try_extract_player_marker_probe_property_value(FProperty* prop, void* container) -> std::optional<std::string>
    {
        if (!prop || !container)
        {
            return std::nullopt;
        }

        try
        {
            const auto prop_hash = prop->GetClass().HashObject();
            const auto prop_name_lower = lower_copy_ascii(RC::to_string(prop->GetName()));
            const auto prop_class_lower = lower_copy_ascii(RC::to_string(prop->GetClass().GetName()));

            if (prop_hash == FNameProperty::StaticClass().HashObject())
            {
                if (auto* value = prop->ContainerPtrToValuePtr<FName>(container))
                {
                    return truncate_probe_value("name=" + RC::to_string(value->ToString()));
                }
                return std::nullopt;
            }
            if (prop_hash == FTextProperty::StaticClass().HashObject())
            {
                if (auto* value = prop->ContainerPtrToValuePtr<FText>(container))
                {
                    return truncate_probe_value("text=" + RC::to_string(value->ToString()));
                }
                return std::nullopt;
            }
            if (prop_hash == FStrProperty::StaticClass().HashObject())
            {
                if (auto* value = prop->ContainerPtrToValuePtr<FString>(container))
                {
                    return truncate_probe_value("str=" + RC::to_string(**value));
                }
                return std::nullopt;
            }
            if (prop_hash == FObjectProperty::StaticClass().HashObject())
            {
                if (auto* obj_ptr = prop->ContainerPtrToValuePtr<UObject*>(container); obj_ptr && *obj_ptr)
                {
                    return truncate_probe_value("obj=" + RC::to_string((*obj_ptr)->GetFullName()));
                }
                return std::string{"obj=null"};
            }
            if (prop_hash == FClassProperty::StaticClass().HashObject())
            {
                if (auto* class_ptr = prop->ContainerPtrToValuePtr<UClass*>(container); class_ptr && *class_ptr)
                {
                    return truncate_probe_value("class=" + RC::to_string((*class_ptr)->GetFullName()));
                }
                return std::string{"class=null"};
            }
            if (prop_hash == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* bool_prop = static_cast<FBoolProperty*>(prop))
                {
                    if (auto* storage = prop->ContainerPtrToValuePtr<void>(container))
                    {
                        return std::string{"bool="} + (bool_prop->GetPropertyValue(storage) ? "true" : "false");
                    }
                }
                return std::nullopt;
            }
            if (prop_hash == FStructProperty::StaticClass().HashObject() &&
                prop->GetSize() >= FVector::StaticSize() &&
                contains_any_token(prop_name_lower + " " + prop_class_lower, {"location", "position", "vector"}))
            {
                if (auto* value = prop->ContainerPtrToValuePtr<FVector>(container))
                {
                    std::ostringstream out{};
                    out << std::fixed << std::setprecision(1)
                        << "vec=(" << value->GetX() << "," << value->GetY() << "," << value->GetZ() << ")";
                    return out.str();
                }
                return std::nullopt;
            }
            if (prop_hash == FStructProperty::StaticClass().HashObject() &&
                prop->GetSize() >= static_cast<int32_t>(sizeof(FRotator)) &&
                contains_any_token(prop_name_lower + " " + prop_class_lower, {"rotation", "rotator"}))
            {
                if (auto* value = prop->ContainerPtrToValuePtr<FRotator>(container))
                {
                    std::ostringstream out{};
                    out << std::fixed << std::setprecision(1)
                        << "rot=(" << value->GetPitch() << "," << value->GetYaw() << "," << value->GetRoll() << ")";
                    return out.str();
                }
                return std::nullopt;
            }
            if (prop_hash == FStructProperty::StaticClass().HashObject() &&
                prop->GetSize() >= static_cast<int32_t>(sizeof(FGuid)) &&
                contains_any_token(prop_name_lower + " " + prop_class_lower, {"guid", "id"}))
            {
                if (auto* value = prop->ContainerPtrToValuePtr<FGuid>(container))
                {
                    return "guid=" + to_hex_guid(*value);
                }
                return std::nullopt;
            }

            if (contains_any_token(prop_class_lower, {"floatproperty"}) && prop->GetSize() == static_cast<int32_t>(sizeof(float)))
            {
                if (auto* value = prop->ContainerPtrToValuePtr<float>(container))
                {
                    std::ostringstream out{};
                    out << std::fixed << std::setprecision(3) << "float=" << *value;
                    return out.str();
                }
            }
            if (contains_any_token(prop_class_lower, {"doubleproperty"}) && prop->GetSize() == static_cast<int32_t>(sizeof(double)))
            {
                if (auto* value = prop->ContainerPtrToValuePtr<double>(container))
                {
                    std::ostringstream out{};
                    out << std::fixed << std::setprecision(3) << "double=" << *value;
                    return out.str();
                }
            }
            if (contains_any_token(prop_class_lower, {"intproperty"}) && prop->GetSize() == static_cast<int32_t>(sizeof(int32_t)))
            {
                if (auto* value = prop->ContainerPtrToValuePtr<int32_t>(container))
                {
                    return "int=" + std::to_string(*value);
                }
            }
            if (contains_any_token(prop_class_lower, {"uint32property"}) && prop->GetSize() == static_cast<int32_t>(sizeof(uint32_t)))
            {
                if (auto* value = prop->ContainerPtrToValuePtr<uint32_t>(container))
                {
                    return "uint32=" + std::to_string(*value);
                }
            }
            if (contains_any_token(prop_class_lower, {"byteproperty", "enumproperty"}) && prop->GetSize() == static_cast<int32_t>(sizeof(uint8_t)))
            {
                if (auto* value = prop->ContainerPtrToValuePtr<uint8_t>(container))
                {
                    return "byte=" + std::to_string(static_cast<uint32_t>(*value));
                }
            }
        }
        catch (...)
        {
            return std::string{"unreadable"};
        }

        return std::nullopt;
    }

    auto to_hex_guid(const FGuid& guid) -> std::string
    {
        std::ostringstream out{};
        out << std::uppercase << std::hex << std::setfill('0')
            << std::setw(8) << guid.A
            << std::setw(8) << guid.B
            << std::setw(8) << guid.C
            << std::setw(8) << guid.D;
        return out.str();
    }

    auto rotation_to_forward(const FRotator& rot) -> FVector
    {
        constexpr double k_deg_to_rad = 3.14159265358979323846 / 180.0;
        const double pitch = rot.GetPitch() * k_deg_to_rad;
        const double yaw = rot.GetYaw() * k_deg_to_rad;

        const double cp = std::cos(pitch);
        const double sp = std::sin(pitch);
        const double cy = std::cos(yaw);
        const double sy = std::sin(yaw);

        return FVector(cp * cy, cp * sy, sp);
    }

    auto vec_sub(FVector a, FVector b) -> FVector
    {
        return FVector(a.GetX() - b.GetX(), a.GetY() - b.GetY(), a.GetZ() - b.GetZ());
    }

    auto vec_dot(FVector a, FVector b) -> double
    {
        return (a.GetX() * b.GetX()) + (a.GetY() * b.GetY()) + (a.GetZ() * b.GetZ());
    }

    auto vec_cross(FVector a, FVector b) -> FVector
    {
        return FVector(
            (a.GetY() * b.GetZ()) - (a.GetZ() * b.GetY()),
            (a.GetZ() * b.GetX()) - (a.GetX() * b.GetZ()),
            (a.GetX() * b.GetY()) - (a.GetY() * b.GetX()));
    }

    auto vec_len(FVector v) -> double
    {
        return std::sqrt(vec_dot(v, v));
    }

    auto vec_normalize(FVector v) -> FVector
    {
        const double len = vec_len(v);
        if (len <= 0.0001)
        {
            return FVector(0.0, 0.0, 0.0);
        }
        return FVector(v.GetX() / len, v.GetY() / len, v.GetZ() / len);
    }

    auto get_player_viewpoint_reflective(UObject* player_controller) -> Viewpoint
    {
        Viewpoint out{};
        if (!player_controller)
        {
            return out;
        }

        auto* fn = player_controller->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
        if (!fn)
        {
            fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.PlayerController:GetPlayerViewPoint"));
        }
        if (!fn)
        {
            return out;
        }

        const int32_t param_bytes = std::max<int32_t>(fn->GetStructureSize(), 256);
        std::vector<uint8_t> params(static_cast<size_t>(param_bytes), 0);
        player_controller->ProcessEvent(fn, params.data());

        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || !prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }

            std::string name = RC::to_string(prop->GetName());
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (name.find("location") != std::string::npos && prop->GetSize() >= FVector::StaticSize())
            {
                if (auto* loc = prop->ContainerPtrToValuePtr<FVector>(params.data()))
                {
                    out.location = *loc;
                    out.valid = true;
                }
            }
            else if (name.find("rotation") != std::string::npos && prop->GetSize() >= static_cast<int32_t>(sizeof(FRotator)))
            {
                if (auto* rot = prop->ContainerPtrToValuePtr<FRotator>(params.data()))
                {
                    out.rotation = *rot;
                    out.valid = true;
                }
            }
        });

        return out;
    }

    auto try_extract_building_block_instance_token_from_name(const std::string& full_name) -> std::optional<std::string>
    {
        // Prefer full instance token to keep per-sign uniqueness:
        // BuildingBlock|<GUID32>|<index>
        static const std::regex token_rx(R"((BuildingBlock)\|([A-Fa-f0-9]{32})\|([0-9]+))", std::regex::icase);
        std::smatch match{};
        if (!std::regex_search(full_name, match, token_rx) || match.size() < 4)
        {
            return std::nullopt;
        }

        std::string prefix = match[1].str();
        std::string guid = match[2].str();
        std::string index = match[3].str();
        std::transform(guid.begin(), guid.end(), guid.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return prefix + "|" + guid + "|" + index;
    }

    auto try_extract_building_slot_from_stable_id(const std::string& stable_id) -> std::optional<std::pair<std::string, std::string>>
    {
        static const std::regex token_rx(R"((BuildingBlock)\|([A-Fa-f0-9]{16,32})\|([0-9]+))", std::regex::icase);
        std::smatch match{};
        if (!std::regex_search(stable_id, match, token_rx) || match.size() < 4)
        {
            return std::nullopt;
        }

        auto guid = match[2].str();
        auto index = match[3].str();
        std::transform(guid.begin(), guid.end(), guid.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return std::make_pair(guid, index);
    }

    auto make_building_slot_key(const std::string& guid, const std::string& index) -> std::string
    {
        return guid + "|" + index;
    }

    auto is_uobject_reflection_safe(UObject* candidate) -> bool
    {
        if (!candidate)
        {
            return false;
        }
        bool found = false;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (found)
            {
                return LoopAction::Break;
            }
            if (object == candidate)
            {
                found = true;
                return LoopAction::Break;
            }
            return LoopAction::Continue;
        });
        if (!found)
        {
            return false;
        }
        auto* class_private = candidate->GetClassPrivate();
        if (!class_private)
        {
            return false;
        }
        bool class_found = false;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (class_found)
            {
                return LoopAction::Break;
            }
            if (object == class_private)
            {
                class_found = true;
                return LoopAction::Break;
            }
            return LoopAction::Continue;
        });
        return class_found;
    }

    auto find_function_by_chain_or_path(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name) -> UFunction*
    {
        if (context && in_chain_name && is_uobject_reflection_safe(context))
        {
            if (auto* fn = context->GetFunctionByNameInChain(in_chain_name))
            {
                return fn;
            }
        }
        if (path_name)
        {
            return UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path_name);
        }
        return nullptr;
    }

    auto invoke_no_param(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn || !is_uobject_reflection_safe(context))
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 1)), 0);
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_no_param_cached(UObject* context, UFunction* fn) -> bool
    {
        if (!context || !fn || !is_uobject_reflection_safe(context))
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 1)), 0);
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_bool_return_no_param(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, bool& out_value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn || !is_uobject_reflection_safe(context))
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 8)), 0);
        context->ProcessEvent(fn, params.data());

        bool found_return = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || !prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    out_value = *value_ptr;
                    found_return = true;
                }
            }
        });
        return found_return;
    }

    auto invoke_object_return_no_param(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, UObject*& out_value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn || !is_uobject_reflection_safe(context))
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 32)), 0);
        context->ProcessEvent(fn, params.data());

        bool found_return = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || !prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(params.data()); value_ptr && *value_ptr)
                {
                    out_value = *value_ptr;
                    found_return = true;
                }
            }
        });
        return found_return;
    }

    auto invoke_vector_return_no_param(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, FVector& out_value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        context->ProcessEvent(fn, params.data());

        bool found_value = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop)
            {
                return;
            }
            if (!prop->HasAnyPropertyFlags(CPF_ReturnParm) && !prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FStructProperty::StaticClass().HashObject())
            {
                return;
            }
            if (prop->GetSize() < FVector::StaticSize())
            {
                return;
            }
            if (auto* value_ptr = prop->ContainerPtrToValuePtr<FVector>(params.data()))
            {
                out_value = *value_ptr;
                found_value = true;
            }
        });

        return found_value;
    }

    auto invoke_with_int_param(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, int32 value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 16)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }

            const auto prop_name = RC::to_string(prop->GetName());
            std::string prop_name_lower = prop_name;
            std::transform(prop_name_lower.begin(), prop_name_lower.end(), prop_name_lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (prop->GetSize() == static_cast<int32_t>(sizeof(int32)) &&
                (prop_name_lower == "zorder" || prop_name_lower == "z_order" || prop_name_lower == "value"))
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<int32>(params.data()))
                {
                    *value_ptr = value;
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_with_int_param_cached(UObject* context, UFunction* fn, int32 value) -> bool
    {
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 16)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }

            const auto prop_name = RC::to_string(prop->GetName());
            std::string prop_name_lower = prop_name;
            std::transform(prop_name_lower.begin(), prop_name_lower.end(), prop_name_lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (prop->GetSize() == static_cast<int32_t>(sizeof(int32)) &&
                (prop_name_lower == "zorder" || prop_name_lower == "z_order" || prop_name_lower == "value"))
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<int32>(params.data()))
                {
                    *value_ptr = value;
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_with_byte_or_int_param_cached(UObject* context, UFunction* fn, uint8_t value) -> bool
    {
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 16)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            const auto prop_hash = prop->GetClass().HashObject();
            if (prop_hash == FByteProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<uint8_t>(params.data()))
                {
                    *value_ptr = value;
                    assigned = true;
                }
                return;
            }
            if (!assigned && prop->GetSize() == static_cast<int32_t>(sizeof(int32)))
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<int32>(params.data()))
                {
                    *value_ptr = static_cast<int32>(value);
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_with_bool_param(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, bool value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 16)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* bool_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    *bool_ptr = value;
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_add_to_viewport(UObject* widget, int32 z_order) -> bool
    {
        if (!widget)
        {
            return false;
        }
        if (invoke_with_int_param(widget, STR("AddToViewport"), STR("/Script/UMG.UserWidget:AddToViewport"), z_order))
        {
            return true;
        }
        return invoke_no_param(widget, STR("AddToViewport"), STR("/Script/UMG.UserWidget:AddToViewport"));
    }

    auto find_uclass_by_path(const TCHAR* path) -> UClass*
    {
        if (!path)
        {
            return nullptr;
        }
        auto* cls = Cast<UClass>(UObjectGlobals::StaticFindObject<UObject*>(UClass::StaticClass(), nullptr, path));
        if (!cls)
        {
            cls = UObjectGlobals::FindObject<UClass>(nullptr, path);
        }
        return cls;
    }

    auto find_loaded_object_by_path_or_name(UClass* required_class, const std::vector<const TCHAR*>& exact_paths, const std::string& name_token) -> UObject*
    {
        if (!required_class)
        {
            return nullptr;
        }

        for (const auto* path : exact_paths)
        {
            if (!path)
            {
                continue;
            }
            if (auto* object = UObjectGlobals::StaticFindObject<UObject*>(required_class, nullptr, path))
            {
                return object;
            }
            if (auto* object = UObjectGlobals::FindObject<UObject>(nullptr, path); object && object->IsA(required_class))
            {
                return object;
            }
        }

        UObject* found = nullptr;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (found || !object || !object->IsA(required_class))
            {
                return LoopAction::Continue;
            }

            auto full_name = RC::to_string(object->GetFullName());
            std::transform(full_name.begin(), full_name.end(), full_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (full_name.find(name_token) != std::string::npos)
            {
                found = object;
            }
            return LoopAction::Continue;
        });
        return found;
    }

    auto set_object_property_if_present(UObject* object, const std::string& property_name, UObject* value) -> bool
    {
        const auto is_uobject_reflection_safe = [](UObject* candidate) -> bool {
            if (!candidate)
            {
                return false;
            }
            bool found = false;
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (found)
                {
                    return LoopAction::Break;
                }
                if (object == candidate)
                {
                    found = true;
                    return LoopAction::Break;
                }
                return LoopAction::Continue;
            });
            if (!found)
            {
                return false;
            }
            auto* class_private = candidate->GetClassPrivate();
            if (!class_private)
            {
                return false;
            }
            bool class_found = false;
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (class_found)
                {
                    return LoopAction::Break;
                }
                if (object == class_private)
                {
                    class_found = true;
                    return LoopAction::Break;
                }
                return LoopAction::Continue;
            });
            return class_found;
        };
        if (!object || property_name.empty() || !is_uobject_reflection_safe(object))
        {
            return false;
        }
        const auto target = lower_copy_ascii(property_name);
        bool set = false;
        for_each_property_in_chain_compat(object->GetClassPrivate(), [&](FProperty* prop) {
            if (set || !prop)
            {
                return;
            }
            if (prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
            {
                return;
            }
            auto prop_name = lower_copy_ascii(RC::to_string(prop->GetName()));
            if (prop_name != target)
            {
                return;
            }
            if (auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(object))
            {
                *value_ptr = value;
                set = true;
            }
        });
        return set;
    }

    auto get_object_property_if_present(UObject* object, const std::string& property_name) -> UObject*
    {
        const auto is_uobject_reflection_safe = [](UObject* candidate) -> bool {
            if (!candidate)
            {
                return false;
            }
            bool found = false;
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (found)
                {
                    return LoopAction::Break;
                }
                if (object == candidate)
                {
                    found = true;
                    return LoopAction::Break;
                }
                return LoopAction::Continue;
            });
            if (!found)
            {
                return false;
            }
            auto* class_private = candidate->GetClassPrivate();
            if (!class_private)
            {
                return false;
            }
            bool class_found = false;
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (class_found)
                {
                    return LoopAction::Break;
                }
                if (object == class_private)
                {
                    class_found = true;
                    return LoopAction::Break;
                }
                return LoopAction::Continue;
            });
            return class_found;
        };
        if (!object || property_name.empty() || !is_uobject_reflection_safe(object))
        {
            return nullptr;
        }
        const auto target = lower_copy_ascii(property_name);
        UObject* found = nullptr;
        for_each_property_in_chain_compat(object->GetClassPrivate(), [&](FProperty* prop) {
            if (found || !prop)
            {
                return;
            }
            if (prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
            {
                return;
            }
            auto prop_name = lower_copy_ascii(RC::to_string(prop->GetName()));
            if (prop_name != target)
            {
                return;
            }
            if (auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(object); value_ptr && *value_ptr)
            {
                found = *value_ptr;
            }
        });
        return found;
    }

    auto invoke_set_object_value(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, UObject* value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn || !value)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 32)), 0);
        FObjectProperty* fallback_property = nullptr;
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (assigned || !prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
            {
                return;
            }

            auto* object_prop = static_cast<FObjectProperty*>(prop);
            if (!fallback_property)
            {
                fallback_property = object_prop;
            }
            auto prop_name = lower_copy_ascii(RC::to_string(prop->GetName()));
            if (prop_name.find("font") == std::string::npos)
            {
                return;
            }
            if (auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(params.data()))
            {
                *value_ptr = value;
                assigned = true;
            }
        });

        if (!assigned && fallback_property)
        {
            if (auto* value_ptr = fallback_property->ContainerPtrToValuePtr<UObject*>(params.data()))
            {
                *value_ptr = value;
                assigned = true;
            }
        }
        if (!assigned)
        {
            return false;
        }

        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_widget_tree_construct_widget(UObject* widget_tree, UClass* widget_class, const std::string& widget_name) -> UObject*
    {
        auto* fn = find_function_by_chain_or_path(
            widget_tree,
            STR("ConstructWidget"),
            STR("/Script/UMG.WidgetTree:ConstructWidget"));
        if (!widget_tree || !fn || !widget_class)
        {
            return nullptr;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        bool assigned_class = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop)
            {
                return;
            }
            if (prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            const auto prop_hash = prop->GetClass().HashObject();
            const auto prop_name = lower_copy_ascii(RC::to_string(prop->GetName()));
            if (prop_hash == FClassProperty::StaticClass().HashObject())
            {
                if (auto* class_ptr = prop->ContainerPtrToValuePtr<UClass*>(params.data()))
                {
                    *class_ptr = widget_class;
                    assigned_class = true;
                }
            }
            else if (prop_hash == FNameProperty::StaticClass().HashObject() && prop_name.find("name") != std::string::npos)
            {
                if (auto* name_ptr = prop->ContainerPtrToValuePtr<FName>(params.data()))
                {
                    *name_ptr = FName(RC::to_wstring(widget_name).c_str(), FNAME_Add);
                }
            }
        });
        if (!assigned_class)
        {
            return nullptr;
        }

        widget_tree->ProcessEvent(fn, params.data());

        UObject* out_widget = nullptr;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (out_widget || !prop || !prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
            {
                if (auto* obj_ptr = prop->ContainerPtrToValuePtr<UObject*>(params.data()); obj_ptr && *obj_ptr)
                {
                    out_widget = *obj_ptr;
                }
            }
        });
        return out_widget;
    }

    auto create_umg_widget_object(UObject* widget_tree, UClass* widget_class, const std::string& widget_name) -> UObject*
    {
        if (!widget_tree || !widget_class)
        {
            return nullptr;
        }
        if (auto* constructed = invoke_widget_tree_construct_widget(widget_tree, widget_class, widget_name))
        {
            return constructed;
        }
        return UObjectGlobals::NewObject<UObject>(
            widget_tree,
            widget_class,
            FName(RC::to_wstring(widget_name).c_str(), FNAME_Add),
            RF_Transient);
    }

    auto invoke_add_child(UObject* panel, UObject* child) -> UObject*
    {
        auto* fn = find_function_by_chain_or_path(
            panel,
            STR("AddChild"),
            STR("/Script/UMG.PanelWidget:AddChild"));
        if (!panel || !child || !fn)
        {
            return nullptr;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        bool assigned_child = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
            {
                if (auto* obj_ptr = prop->ContainerPtrToValuePtr<UObject*>(params.data()))
                {
                    *obj_ptr = child;
                    assigned_child = true;
                }
            }
        });
        if (!assigned_child)
        {
            return nullptr;
        }

        panel->ProcessEvent(fn, params.data());

        UObject* out_slot = nullptr;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (out_slot || !prop || !prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
            {
                if (auto* obj_ptr = prop->ContainerPtrToValuePtr<UObject*>(params.data()); obj_ptr && *obj_ptr)
                {
                    out_slot = *obj_ptr;
                }
            }
        });
        return out_slot;
    }

    auto invoke_set_content(UObject* content_widget, UObject* child) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            content_widget,
            STR("SetContent"),
            STR("/Script/UMG.ContentWidget:SetContent"));
        if (!content_widget || !child || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        bool assigned_child = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
            {
                if (auto* obj_ptr = prop->ContainerPtrToValuePtr<UObject*>(params.data()))
                {
                    *obj_ptr = child;
                    assigned_child = true;
                }
            }
        });
        if (!assigned_child)
        {
            return false;
        }

        content_widget->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_umg_set_text(UObject* context, const std::string& text_value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, STR("SetText"), nullptr);
        if (!context || !fn)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FTextProperty::StaticClass().HashObject())
            {
                if (auto* text_ptr = prop->ContainerPtrToValuePtr<FText>(params.data()))
                {
                    *text_ptr = FText(RC::to_wstring(text_value).c_str());
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto set_bool_property_if_present(UObject* object, const std::string& property_name, bool value) -> bool
    {
        if (!object || property_name.empty())
        {
            return false;
        }
        const auto target = property_name;
        bool set = false;
        for_each_property_in_chain_compat(object->GetClassPrivate(), [&](FProperty* prop) {
            if (set || !prop)
            {
                return;
            }
            if (prop->GetClass().HashObject() != FBoolProperty::StaticClass().HashObject())
            {
                return;
            }
            auto prop_name = RC::to_string(prop->GetName());
            std::transform(prop_name.begin(), prop_name.end(), prop_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (prop_name != target)
            {
                return;
            }
            if (auto* bool_ptr = prop->ContainerPtrToValuePtr<bool>(object))
            {
                *bool_ptr = value;
                set = true;
            }
        });
        return set;
    }

    auto get_bool_property_if_present(UObject* object, const std::string& property_name, bool& out_value) -> bool
    {
        if (!object || property_name.empty())
        {
            return false;
        }
        const auto target = property_name;
        bool found = false;
        for_each_property_in_chain_compat(object->GetClassPrivate(), [&](FProperty* prop) {
            if (found || !prop)
            {
                return;
            }
            if (prop->GetClass().HashObject() != FBoolProperty::StaticClass().HashObject())
            {
                return;
            }
            auto prop_name = RC::to_string(prop->GetName());
            std::transform(prop_name.begin(), prop_name.end(), prop_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (prop_name != target)
            {
                return;
            }
            if (auto* bool_ptr = prop->ContainerPtrToValuePtr<bool>(object))
            {
                out_value = *bool_ptr;
                found = true;
            }
        });
        return found;
    }

    auto invoke_set_text(UObject* context, const std::string& text_value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            context,
            STR("K2_SetText"),
            STR("/Script/Engine.TextRenderComponent:K2_SetText"));
        if (!fn)
        {
            fn = find_function_by_chain_or_path(
                context,
                STR("SetText"),
                STR("/Script/Engine.TextRenderComponent:SetText"));
        }
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FTextProperty::StaticClass().HashObject())
            {
                auto* text_ptr = prop->ContainerPtrToValuePtr<FText>(params.data());
                if (text_ptr)
                {
                    *text_ptr = FText(RC::to_wstring(text_value).c_str());
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_float_value(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, float value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 32)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            const auto prop_name = RC::to_string(prop->GetName());
            std::string prop_name_lower = prop_name;
            std::transform(prop_name_lower.begin(), prop_name_lower.end(), prop_name_lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if ((prop_name_lower == "value" ||
                 prop_name_lower.find("size") != std::string::npos ||
                 prop_name_lower.find("scale") != std::string::npos ||
                 prop_name_lower.find("width") != std::string::npos ||
                 prop_name_lower.find("height") != std::string::npos ||
                 prop_name_lower.find("opacity") != std::string::npos)
                && prop->GetSize() == static_cast<int32_t>(sizeof(float)))
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<float>(params.data()))
                {
                    *value_ptr = value;
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_vector2d_value(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, float x, float y) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 32)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || assigned || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FStructProperty::StaticClass().HashObject())
            {
                return;
            }
            auto* storage = prop->ContainerPtrToValuePtr<void>(params.data());
            if (!storage)
            {
                return;
            }
            if (prop->GetSize() == 8)
            {
                auto* values = static_cast<float*>(storage);
                values[0] = x;
                values[1] = y;
                assigned = true;
            }
            else if (prop->GetSize() == 16)
            {
                auto* values = static_cast<double*>(storage);
                values[0] = static_cast<double>(x);
                values[1] = static_cast<double>(y);
                assigned = true;
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_rgba_value(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, float r, float g, float b, float a) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || assigned || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FStructProperty::StaticClass().HashObject())
            {
                return;
            }
            auto* storage = prop->ContainerPtrToValuePtr<void>(params.data());
            if (!storage)
            {
                return;
            }
            if (prop->GetSize() >= 16)
            {
                auto* values = static_cast<float*>(storage);
                values[0] = r;
                values[1] = g;
                values[2] = b;
                values[3] = a;
                assigned = true;
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_margin_value(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, float left, float top, float right, float bottom) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || assigned || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FStructProperty::StaticClass().HashObject())
            {
                return;
            }
            auto* storage = prop->ContainerPtrToValuePtr<void>(params.data());
            if (!storage)
            {
                return;
            }
            if (prop->GetSize() == 16)
            {
                auto* values = static_cast<float*>(storage);
                values[0] = left;
                values[1] = top;
                values[2] = right;
                values[3] = bottom;
                assigned = true;
            }
            else if (prop->GetSize() == 32)
            {
                auto* values = static_cast<double*>(storage);
                values[0] = static_cast<double>(left);
                values[1] = static_cast<double>(top);
                values[2] = static_cast<double>(right);
                values[3] = static_cast<double>(bottom);
                assigned = true;
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_byte_value(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name, uint8_t value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 16)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            const auto prop_name = RC::to_string(prop->GetName());
            std::string prop_name_lower = prop_name;
            std::transform(prop_name_lower.begin(), prop_name_lower.end(), prop_name_lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if ((prop_name_lower == "value" || prop_name_lower.find("alignment") != std::string::npos) && prop->GetSize() == 1)
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<uint8_t>(params.data()))
                {
                    *value_ptr = value;
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_relative_location(UObject* context, const FVector& location) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            context,
            STR("SetRelativeLocation"),
            STR("/Script/Engine.SceneComponent:SetRelativeLocation"));
        if (!context || !fn)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 128)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            const auto prop_name = RC::to_string(prop->GetName());
            std::string prop_name_lower = prop_name;
            std::transform(prop_name_lower.begin(), prop_name_lower.end(), prop_name_lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject())
            {
                if ((prop_name_lower.find("location") != std::string::npos || prop_name_lower.find("newlocation") != std::string::npos) &&
                    prop->GetSize() >= FVector::StaticSize())
                {
                    if (auto* location_ptr = prop->ContainerPtrToValuePtr<FVector>(params.data()))
                    {
                        *location_ptr = location;
                        assigned = true;
                    }
                }
            }
            else if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* bool_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    *bool_ptr = false;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_text_render_color(UObject* context, float color_r, float color_g, float color_b, float color_a) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            context,
            STR("SetTextRenderColor"),
            STR("/Script/Engine.TextRenderComponent:SetTextRenderColor"));
        if (!context || !fn)
        {
            return false;
        }

        color_r = std::clamp(color_r, 0.0f, 1.0f);
        color_g = std::clamp(color_g, 0.0f, 1.0f);
        color_b = std::clamp(color_b, 0.0f, 1.0f);
        color_a = std::clamp(color_a, 0.0f, 1.0f);

        const uint8_t byte_r = static_cast<uint8_t>(std::round(color_r * 255.0f));
        const uint8_t byte_g = static_cast<uint8_t>(std::round(color_g * 255.0f));
        const uint8_t byte_b = static_cast<uint8_t>(std::round(color_b * 255.0f));
        const uint8_t byte_a = static_cast<uint8_t>(std::round(color_a * 255.0f));

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 32)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject() && prop->GetSize() >= 4)
            {
                auto* bytes = prop->ContainerPtrToValuePtr<uint8_t>(params.data());
                if (bytes)
                {
                    // Unreal FColor memory order is B,G,R,A on little-endian platforms.
                    bytes[0] = byte_b;
                    bytes[1] = byte_g;
                    bytes[2] = byte_r;
                    bytes[3] = byte_a;
                    assigned = true;
                }
            }
        });

        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_visibility(UObject* context, bool is_visible) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            context,
            STR("SetVisibility"),
            STR("/Script/Engine.SceneComponent:SetVisibility"));
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 16)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* bool_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    const auto prop_name = RC::to_string(prop->GetName());
                    std::string prop_name_lower = prop_name;
                    std::transform(prop_name_lower.begin(), prop_name_lower.end(), prop_name_lower.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (prop_name_lower.find("visible") != std::string::npos)
                    {
                        *bool_ptr = is_visible;
                        assigned = true;
                    }
                    else
                    {
                        *bool_ptr = false;
                    }
                }
            }
        });

        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto invoke_set_hidden_in_game(UObject* context, bool is_hidden) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            context,
            STR("SetHiddenInGame"),
            STR("/Script/Engine.SceneComponent:SetHiddenInGame"));
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 16)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* bool_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    const auto prop_name = RC::to_string(prop->GetName());
                    std::string prop_name_lower = prop_name;
                    std::transform(prop_name_lower.begin(), prop_name_lower.end(), prop_name_lower.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (prop_name_lower.find("hidden") != std::string::npos)
                    {
                        *bool_ptr = is_hidden;
                        assigned = true;
                    }
                    else
                    {
                        *bool_ptr = false;
                    }
                }
            }
        });

        if (!assigned)
        {
            return false;
        }
        context->ProcessEvent(fn, params.data());
        return true;
    }

    auto read_text_file(const std::filesystem::path& path, std::string& out_content) -> bool
    {
        out_content.clear();
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (!in.is_open())
        {
            return false;
        }
        out_content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        in.close();
        return true;
    }

    auto safe_stof(const std::string& s, float fallback) -> float
    {
        try
        {
            return std::stof(s);
        }
        catch (...)
        {
            return fallback;
        }
    }

    auto safe_stoi(const std::string& s, int fallback) -> int
    {
        try
        {
            return std::stoi(s);
        }
        catch (...)
        {
            return fallback;
        }
    }

    auto normalize_hotkey_value(std::string value) -> std::string
    {
        value = trim_copy_ascii(value);
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\'')))
        {
            value = value.substr(1, value.size() - 2);
        }
        value = trim_copy_ascii(value);
        std::string out{};
        out.reserve(value.size());
        for (const auto ch : value)
        {
            if (ch == ' ' || ch == '_' || ch == '-')
            {
                continue;
            }
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        if (out.rfind("VK", 0) == 0)
        {
            out = out.substr(2);
        }
        return out;
    }

    auto display_name_for_vk(int vk) -> std::string
    {
        if (vk >= 0x70 && vk <= 0x87)
        {
            return "F" + std::to_string(vk - 0x6F);
        }
        if (vk >= 0x41 && vk <= 0x5A)
        {
            return std::string{static_cast<char>('A' + (vk - 0x41))};
        }
        if (vk >= 0x30 && vk <= 0x39)
        {
            return std::string{static_cast<char>('0' + (vk - 0x30))};
        }
        if (vk >= 0x60 && vk <= 0x69)
        {
            return "NUM" + std::to_string(vk - 0x60);
        }
        switch (vk)
        {
        case 0x08: return "BACKSPACE";
        case 0x09: return "TAB";
        case 0x0D: return "ENTER";
        case 0x1B: return "ESCAPE";
        case 0x20: return "SPACE";
        case 0x21: return "PAGEUP";
        case 0x22: return "PAGEDOWN";
        case 0x23: return "END";
        case 0x24: return "HOME";
        case 0x25: return "LEFT";
        case 0x26: return "UP";
        case 0x27: return "RIGHT";
        case 0x28: return "DOWN";
        case 0x2D: return "INSERT";
        case 0x2E: return "DELETE";
        default: return "VK" + std::to_string(vk);
        }
    }

    auto hotkey_vk_from_config(const std::string& raw_value, int fallback) -> int
    {
        const auto value = normalize_hotkey_value(raw_value);
        if (value.empty())
        {
            return fallback;
        }
        if (value.size() == 1)
        {
            const auto ch = value.front();
            if (ch >= 'A' && ch <= 'Z')
            {
                return static_cast<int>(ch);
            }
            if (ch >= '0' && ch <= '9')
            {
                return static_cast<int>(ch);
            }
        }
        if (value.size() >= 2 && value.front() == 'F')
        {
            const auto index = safe_stoi(value.substr(1), -1);
            if (index >= 1 && index <= 24)
            {
                return 0x6F + index;
            }
        }
        if (value.rfind("NUMPAD", 0) == 0 || value.rfind("NUM", 0) == 0)
        {
            const auto digits = value.rfind("NUMPAD", 0) == 0 ? value.substr(6) : value.substr(3);
            const auto index = safe_stoi(digits, -1);
            if (index >= 0 && index <= 9)
            {
                return 0x60 + index;
            }
        }
        if (value == "RETURN" || value == "ENTER") return 0x0D;
        if (value == "ESC" || value == "ESCAPE") return 0x1B;
        if (value == "DEL" || value == "DELETE") return 0x2E;
        if (value == "INS" || value == "INSERT") return 0x2D;
        if (value == "SPACE" || value == "SPACEBAR") return 0x20;
        if (value == "TAB") return 0x09;
        if (value == "BACKSPACE") return 0x08;
        if (value == "PAGEUP" || value == "PGUP") return 0x21;
        if (value == "PAGEDOWN" || value == "PGDN") return 0x22;
        if (value == "HOME") return 0x24;
        if (value == "END") return 0x23;
        if (value == "LEFT" || value == "LEFTARROW") return 0x25;
        if (value == "UP" || value == "UPARROW") return 0x26;
        if (value == "RIGHT" || value == "RIGHTARROW") return 0x27;
        if (value == "DOWN" || value == "DOWNARROW") return 0x28;
        if (value.rfind("0X", 0) == 0)
        {
            try
            {
                const auto parsed = std::stoi(value, nullptr, 16);
                if (parsed > 0 && parsed <= 0xFF)
                {
                    return parsed;
                }
            }
            catch (...)
            {
            }
        }
        return fallback;
    }
}

namespace WindroseTextSigns
{
    auto parse_retry_delay_ms_config(const std::string& raw_value) -> std::array<uint32_t, 3>;
    auto invoke_get_text_value(UObject* context, std::string& out_text) -> bool;
    auto read_text_property_value_no_process_event(UObject* context, std::string& out_text) -> bool;

    SignTextMod::SignTextMod()
    {
        ModName = STR("WindroseTextSigns");
        ModVersion = STR("0.1.2-prototype");
        ModDescription = STR("Wooden Label custom text prototype");
        ModAuthors = STR("Windrose modding prototype");

        register_tab(STR("WindroseTextSigns"), [](CppUserModBase* mod) {
            UE4SS_ENABLE_IMGUI();
            static_cast<SignTextMod*>(mod)->render_ui();
        });
    }

    SignTextMod::~SignTextMod()
    {
        uninstall_phase7_keyboard_capture_hook();
        if (m_log.is_open())
        {
            flush_log_repeat_summary();
            m_log.flush();
            m_log.close();
        }
    }

    auto SignTextMod::narrow_ascii(const RC::StringType& value) const -> std::string
    {
        std::string out{};
        out.reserve(value.size());
        for (const auto ch : value)
        {
            if constexpr (sizeof(ch) == 1)
            {
                out.push_back(static_cast<char>(ch));
            }
            else
            {
                out.push_back(static_cast<char>((ch >= 0 && ch <= 127) ? ch : '?'));
            }
        }
        return out;
    }

    auto SignTextMod::lower_ascii(std::string value) const -> std::string
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    auto SignTextMod::config_bool_value(std::string_view key, bool fallback) const -> bool
    {
        if (key.empty())
        {
            return fallback;
        }

        std::string content{};
        if (!read_text_file(m_mod_root / "Config" / "WindroseTextSigns.ini", content))
        {
            return fallback;
        }

        const auto key_lower = lower_copy_ascii(std::string{key});
        std::istringstream rows{content};
        std::string row{};
        while (std::getline(rows, row))
        {
            auto trimmed = trim_copy_ascii(row);
            if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';' || trimmed.front() == '[')
            {
                continue;
            }
            const auto eq = trimmed.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            auto found_key = lower_copy_ascii(trim_copy_ascii(trimmed.substr(0, eq)));
            if (found_key != key_lower)
            {
                continue;
            }
            auto value = lower_copy_ascii(trim_copy_ascii(trimmed.substr(eq + 1)));
            return value == "1" || value == "true" || value == "yes" || value == "on";
        }

        return fallback;
    }

    auto SignTextMod::config_string_value(std::string_view key, std::string fallback) const -> std::string
    {
        if (key.empty())
        {
            return fallback;
        }

        std::string content{};
        if (!read_text_file(m_mod_root / "Config" / "WindroseTextSigns.ini", content))
        {
            return fallback;
        }

        const auto key_lower = lower_copy_ascii(std::string{key});
        std::istringstream rows{content};
        std::string row{};
        while (std::getline(rows, row))
        {
            auto trimmed = trim_copy_ascii(row);
            if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';' || trimmed.front() == '[')
            {
                continue;
            }
            const auto eq = trimmed.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            auto found_key = lower_copy_ascii(trim_copy_ascii(trimmed.substr(0, eq)));
            if (found_key != key_lower)
            {
                continue;
            }
            return trim_copy_ascii(trimmed.substr(eq + 1));
        }

        return fallback;
    }

    auto SignTextMod::is_hide_native_label_icon_enabled() const -> bool
    {
        return config_bool_value("WTS_HIDE_NATIVE_LABEL_ICON", true);
    }

    auto SignTextMod::is_label_text_visual_diagnostics_enabled() const -> bool
    {
        return config_bool_value("WTS_LABEL_TEXT_VISUAL_DIAGNOSTICS", false);
    }

    auto SignTextMod::is_native_transport_inventory_probe_enabled() const -> bool
    {
        return config_bool_value("WTS_NATIVE_TRANSPORT_INVENTORY_PROBE", false);
    }

    auto SignTextMod::now_utc() const -> std::string
    {
        const auto now = std::time(nullptr);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &now);
#else
        gmtime_r(&now, &tm_utc);
#endif
        std::ostringstream out{};
        out << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    }

    auto SignTextMod::resolve_mod_root() -> std::filesystem::path
    {
        const auto cwd = std::filesystem::current_path();
        const std::vector<std::filesystem::path> candidates = {
            cwd / "ue4ss" / "Mods" / "WindroseTextSigns",
            cwd / "Mods" / "WindroseTextSigns",
            cwd / "WindroseTextSigns"};

        for (const auto& path : candidates)
        {
            if (std::filesystem::exists(path))
            {
                return path;
            }
        }

        std::error_code ec{};
        std::filesystem::create_directories(candidates.back(), ec);
        std::filesystem::create_directories(candidates.back() / "Config", ec);
        std::filesystem::create_directories(candidates.back() / "dlls", ec);
        return candidates.back();
    }

    auto SignTextMod::configure_data_root() -> void
    {
        const auto cwd = std::filesystem::current_path();
        const auto resolution = resolve_data_root_for_role(cwd, m_mod_root);
        m_data_root = resolution.data_root;
        m_runtime_role = resolution.runtime_role;
        m_data_mode = resolution.data_mode;
        m_authority_mode = resolution.authority_mode;
        m_sidecar_kind = resolution.sidecar_kind;
        m_sidecar_authoritative = resolution.authoritative;
        m_save_profile_root = resolution.profile_root.string();
        m_world_folder_id = resolution.world_id;
    }

    auto SignTextMod::open_log() -> void
    {
        m_log_path = m_mod_root / "WindroseTextSigns.log";
        m_log.open(m_log_path, std::ios::out | std::ios::app | std::ios::binary);
        std::error_code ec{};
        const auto existing_size = std::filesystem::exists(m_log_path, ec) ? std::filesystem::file_size(m_log_path, ec) : 0;
        m_log_bytes_written = ec ? 0 : static_cast<size_t>(existing_size);
        m_log_size_cap_hit = false;
        m_bootstrap_started = std::chrono::steady_clock::now();
        m_last_log_payload.clear();
        m_last_log_repeat_count = 0;
        m_bootstrap_begin_logged = true;
        m_bootstrap_end_logged = false;
        m_bootstrap_prune_phase_observed = false;
        m_role_lock_acquired = false;
        m_role_lock_runtime_role.clear();
        m_role_lock_bridge_role.clear();
        m_role_lock_world_id.clear();
        m_bridge_route_lock_acquired = false;
        m_bridge_route_locked_host.clear();
        m_bridge_route_loopback_same_machine_ok = false;
        m_bridge_route_rejected_candidates_logged.clear();
        m_bridge_route_fallback_candidates_logged.clear();
        m_bridge_route_bootstrap_pause_logged = false;
        log_line("[session] bootstrap_begin stage=startup");
        log_line("[startup] WindroseTextSigns initialized");
    }

    auto SignTextMod::compact_log_line(std::string line) const -> std::string
    {
        if (line.rfind("[phase4] apply_success key=", 0) == 0)
        {
            const auto cut = line.find(" component=");
            if (cut != std::string::npos)
            {
                line.resize(cut);
            }
        }
        return line;
    }

    auto SignTextMod::write_log_row(const std::string& row) -> void
    {
        constexpr size_t k_max_log_bytes = 2 * 1024 * 1024;
        constexpr size_t k_bootstrap_target_bytes = 256 * 1024;
        constexpr size_t k_bootstrap_hard_max_bytes = 307 * 1024;
        const size_t row_bytes = row.size() + 1;
        if (m_log.is_open() && m_log_bytes_written + row_bytes <= k_max_log_bytes)
        {
            m_log << row << "\n";
            m_log.flush();
            m_log_bytes_written += row_bytes;
            return;
        }

        // Rolling cap with bootstrap budget:
        // - preserve newest complete bootstrap blocks up to target budget
        // - preserve newest non-bootstrap tail with remaining bytes
        // - if still oversized, reduce oldest bootstrap blocks until <= hard cap and file cap
        if (m_log.is_open())
        {
            m_log.flush();
            m_log.close();
        }

        std::string content{};
        if (!read_text_file(m_log_path, content))
        {
            content.clear();
        }
        content += row;
        content.push_back('\n');

        if (content.size() > k_max_log_bytes)
        {
            struct LineSpan
            {
                size_t start{0};
                size_t end{0};
            };
            struct Block
            {
                size_t begin_line{0};
                size_t end_line{0};
                size_t bytes{0};
            };

            std::vector<LineSpan> lines{};
            lines.reserve(content.size() / 64);
            size_t cursor = 0;
            while (cursor < content.size())
            {
                const auto next_nl = content.find('\n', cursor);
                if (next_nl == std::string::npos)
                {
                    lines.push_back(LineSpan{cursor, content.size()});
                    break;
                }
                lines.push_back(LineSpan{cursor, next_nl + 1});
                cursor = next_nl + 1;
            }

            std::vector<Block> blocks{};
            std::vector<uint8_t> line_in_block(lines.size(), 0);
            for (size_t i = 0; i < lines.size(); ++i)
            {
                const std::string_view line_view{content.data() + lines[i].start, lines[i].end - lines[i].start};
                if (line_view.find("[session] bootstrap_begin") == std::string_view::npos)
                {
                    continue;
                }
                size_t j = i;
                while (j < lines.size())
                {
                    const std::string_view end_line{content.data() + lines[j].start, lines[j].end - lines[j].start};
                    if (end_line.find("[session] bootstrap_end") != std::string_view::npos)
                    {
                        break;
                    }
                    ++j;
                }
                if (j >= lines.size())
                {
                    continue;
                }

                size_t block_bytes = 0;
                for (size_t k = i; k <= j; ++k)
                {
                    line_in_block[k] = 1;
                    block_bytes += (lines[k].end - lines[k].start);
                }
                blocks.push_back(Block{i, j, block_bytes});
                i = j;
            }

            std::vector<uint8_t> keep_block(blocks.size(), 0);
            size_t kept_bootstrap_bytes = 0;
            for (size_t bi = blocks.size(); bi > 0; --bi)
            {
                const size_t idx = bi - 1;
                const auto block_bytes = blocks[idx].bytes;
                if (block_bytes > k_bootstrap_hard_max_bytes)
                {
                    continue;
                }

                if (kept_bootstrap_bytes + block_bytes <= k_bootstrap_target_bytes ||
                    (kept_bootstrap_bytes == 0 && block_bytes <= k_bootstrap_hard_max_bytes))
                {
                    keep_block[idx] = 1;
                    kept_bootstrap_bytes += block_bytes;
                }
            }

            std::string bootstrap_kept{};
            bootstrap_kept.reserve(kept_bootstrap_bytes);
            for (size_t bi = 0; bi < blocks.size(); ++bi)
            {
                if (!keep_block[bi])
                {
                    continue;
                }
                const auto& block = blocks[bi];
                for (size_t li = block.begin_line; li <= block.end_line; ++li)
                {
                    bootstrap_kept.append(content, lines[li].start, lines[li].end - lines[li].start);
                }
            }

            std::vector<uint8_t> kept_bootstrap_lines(lines.size(), 0);
            for (size_t bi = 0; bi < blocks.size(); ++bi)
            {
                if (!keep_block[bi])
                {
                    continue;
                }
                for (size_t li = blocks[bi].begin_line; li <= blocks[bi].end_line; ++li)
                {
                    kept_bootstrap_lines[li] = 1;
                }
            }

            std::string non_bootstrap{};
            non_bootstrap.reserve(content.size() - bootstrap_kept.size());
            for (size_t li = 0; li < lines.size(); ++li)
            {
                if (line_in_block[li] && kept_bootstrap_lines[li])
                {
                    continue;
                }
                if (line_in_block[li] && !kept_bootstrap_lines[li])
                {
                    continue;
                }
                non_bootstrap.append(content, lines[li].start, lines[li].end - lines[li].start);
            }

            const size_t non_bootstrap_budget = (bootstrap_kept.size() >= k_max_log_bytes)
                ? 0
                : (k_max_log_bytes - bootstrap_kept.size());
            size_t tail_start = 0;
            if (non_bootstrap.size() > non_bootstrap_budget)
            {
                tail_start = non_bootstrap.size() - non_bootstrap_budget;
                if (const auto nl = non_bootstrap.find('\n', tail_start); nl != std::string::npos)
                {
                    tail_start = nl + 1;
                }
            }

            std::string trimmed_non_bootstrap = non_bootstrap.substr(tail_start);
            content = bootstrap_kept + trimmed_non_bootstrap;
            if (content.size() > k_max_log_bytes)
            {
                const auto overflow = content.size() - k_max_log_bytes;
                if (overflow < trimmed_non_bootstrap.size())
                {
                    trimmed_non_bootstrap.erase(0, overflow);
                    if (const auto nl = trimmed_non_bootstrap.find('\n'); nl != std::string::npos)
                    {
                        trimmed_non_bootstrap.erase(0, nl + 1);
                    }
                    content = bootstrap_kept + trimmed_non_bootstrap;
                }
                else
                {
                    content = content.substr(content.size() - k_max_log_bytes);
                    if (const auto nl = content.find('\n'); nl != std::string::npos)
                    {
                        content.erase(0, nl + 1);
                    }
                }
            }
        }

        m_log.open(m_log_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (m_log.is_open())
        {
            m_log.write(content.data(), static_cast<std::streamsize>(content.size()));
            m_log.flush();
            m_log.close();
        }
        m_log.open(m_log_path, std::ios::out | std::ios::app);
        m_log_bytes_written = content.size();
        m_log_size_cap_hit = false;

    }

    auto SignTextMod::flush_log_repeat_summary() -> void
    {
        if (m_last_log_repeat_count == 0)
        {
            return;
        }

        std::string preview = m_last_log_payload;
        constexpr size_t k_preview_limit = 160;
        if (preview.size() > k_preview_limit)
        {
            preview.resize(k_preview_limit);
            preview += "...";
        }
        const auto row = now_utc() +
            " [log] repeat_suppressed count=" + std::to_string(m_last_log_repeat_count) +
            " line=" + preview;
        write_log_row(row);
        m_last_log_repeat_count = 0;
    }

    auto SignTextMod::log_line(const std::string& line) -> void
    {
        const auto payload = compact_log_line(line);
        if (payload == m_last_log_payload)
        {
            ++m_last_log_repeat_count;
            return;
        }

        flush_log_repeat_summary();
        m_last_log_payload = payload;
        m_last_log_repeat_count = 0;

        const auto row = now_utc() + " " + payload;
        write_log_row(row);
    }

    auto SignTextMod::trace_behavior_sm(const std::string& event, const std::string& fields) -> void
    {
        if (!m_behavior_trace_enabled || event.empty())
        {
            return;
        }

        std::string row = "[trace-sm] event=" + event;
        if (!fields.empty())
        {
            row += " " + fields;
        }
        log_line(row);
    }

    auto SignTextMod::on_unreal_init() -> void
    {
        m_mod_root = resolve_mod_root();
        m_hide_native_label_icon_enabled = is_hide_native_label_icon_enabled();
        m_label_text_visual_diagnostics_enabled = is_label_text_visual_diagnostics_enabled();
        m_native_transport_inventory_probe_enabled = is_native_transport_inventory_probe_enabled();
        m_f8_latency_breakdown_enabled = config_bool_value("WTS_F8_LATENCY_BREAKDOWN_ENABLED", true);
        m_behavior_trace_enabled = config_bool_value("WTS_BEHAVIOR_TRACE_ENABLED", false);
        m_create_null_short_retry_enabled = config_bool_value("WTS_CREATE_NULL_SHORT_RETRY_ENABLED", true);
        m_create_null_retry_delays_ms = parse_retry_delay_ms_config(
            config_string_value("WTS_CREATE_NULL_RETRY_DELAYS_MS", "250,750,1500"));
        m_visual_verify_debug_force_reapply = config_bool_value("WTS_VISUAL_VERIFY_DEBUG_FORCE_REAPPLY", false);
        m_localclient_motion_reapply_enabled = config_bool_value("WTS_LOCALCLIENT_MOTION_REAPPLY_ENABLED", true);
        m_localclient_controller_probe_interval_sec = std::clamp(
            safe_stof(config_string_value("WTS_LOCALCLIENT_CONTROLLER_PROBE_INTERVAL_SEC", "0.2"), 0.2f),
            0.1f,
            1.0f);
        m_world_text_font_enabled = config_bool_value("WTS_WORLD_TEXT_FONT_ENABLED", false);
        const auto hotkey_config_value = config_string_value("WTS_HOTKEY", "F8");
        m_hotkey_vk = hotkey_vk_from_config(hotkey_config_value, k_default_hotkey_vk);
        m_hotkey_name = display_name_for_vk(m_hotkey_vk);
        m_bridge_remote_server_host_config = config_string_value("WTS_BRIDGE_SERVER_HOST", "auto");
        const auto bridge_host_config_lower = lower_copy_ascii(trim_copy_ascii(m_bridge_remote_server_host_config));
        m_bridge_route_auto_enabled =
            bridge_host_config_lower.empty() ||
            bridge_host_config_lower == "auto" ||
            bridge_host_config_lower == "discover";
        m_bridge_remote_server_host = m_bridge_route_auto_enabled ? std::string{} : m_bridge_remote_server_host_config;
        m_bridge_udp_port = std::clamp(
            safe_stoi(config_string_value("WTS_BRIDGE_UDP_PORT", "45801"), 45801),
            1,
            65535);
        m_destroy_confirm_ttl_sec = static_cast<uint32_t>(std::clamp(
            safe_stoi(config_string_value("WTS_DESTROY_CONFIRM_TTL_SEC", "10"), 10),
            2,
            30));
        const auto upnp_mode_raw = lower_copy_ascii(trim_copy_ascii(config_string_value("WTS_BRIDGE_UPNP_MODE", "")));
        if (upnp_mode_raw == "off" || upnp_mode_raw == "false" || upnp_mode_raw == "0" || upnp_mode_raw == "disabled")
        {
            m_bridge_upnp_mode = BridgeUpnpMode::Off;
        }
        else if (upnp_mode_raw == "on" || upnp_mode_raw == "true" || upnp_mode_raw == "1" || upnp_mode_raw == "enabled")
        {
            m_bridge_upnp_mode = BridgeUpnpMode::On;
        }
        else if (upnp_mode_raw == "auto")
        {
            m_bridge_upnp_mode = BridgeUpnpMode::Auto;
        }
        else
        {
            const bool upnp_enabled_legacy = config_bool_value("WTS_BRIDGE_UPNP_ENABLED", false);
            m_bridge_upnp_mode = upnp_enabled_legacy ? BridgeUpnpMode::Auto : BridgeUpnpMode::Off;
        }
        m_bridge_upnp_enabled = (m_bridge_upnp_mode != BridgeUpnpMode::Off);
        NativeBridge::instance().set_remote_server(m_bridge_remote_server_host, static_cast<uint16_t>(m_bridge_udp_port));
        m_session_id = now_utc() + "-" + sanitize_path_segment(current_executable_path().filename().string());
        configure_data_root();
        m_sidecar_path = m_data_root / "SignTexts.json";
        m_backup_root = m_data_root / "Backups";

        open_log();
        log_line(std::string{"[build] version=0.1.2-prototype compiled="} + __DATE__ + " " + __TIME__ + " flags=configurable-hotkey,phase2-role-aware-sidecar,remote-cache-routing,staticconstruct-gated,phase6-native-udp-bridge,bridge-auto-route,bridge-batched-snapshots,phase7-umg-no-llhook,dedicated-no-render-components,phase4-marker-guard,restore-scan-diag,label-text-native-icon-hide,probes-default-off");
        log_line("[role] runtimeRole=" + m_runtime_role +
                 " dataMode=" + m_data_mode +
                 " authorityMode=" + m_authority_mode +
                 " sidecarKind=" + m_sidecar_kind +
                 " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                 " profileRoot=" + (m_save_profile_root.empty() ? "none" : m_save_profile_root) +
                 " worldFolderId=" + m_world_folder_id);
        log_line("[bridge] config udpPort=" + std::to_string(m_bridge_udp_port) +
                 " remoteServerHost=" + (m_bridge_remote_server_host.empty() ? "none" : m_bridge_remote_server_host) +
                 " configuredHost=" + (m_bridge_remote_server_host_config.empty() ? "none" : m_bridge_remote_server_host_config) +
                 " autoRoute=" + std::string{m_bridge_route_auto_enabled ? "true" : "false"} +
                 " upnpEnabled=" + std::string{m_bridge_upnp_enabled ? "true" : "false"} +
                 " upnpMode=" + bridge_upnp_mode_name());
        log_line("[save] data_root=" + m_data_root.string() +
                 " sidecar=" + m_sidecar_path.string() +
                 " backups=" + m_backup_root.string());
        configure_bridge_role("startup");
        log_line("[visual] hideNativeLabelIcon=" +
                 std::string{m_hide_native_label_icon_enabled ? "true" : "false"} +
                 " diagnostics=" +
                 std::string{m_label_text_visual_diagnostics_enabled ? "true" : "false"});
        log_line("[phase4-font] config enabled=" + std::string{m_world_text_font_enabled ? "true" : "false"} +
                 " asset=" + config_string_value("WTS_WORLD_TEXT_FONT_ASSET", "none") +
                 " hint=" + config_string_value("WTS_WORLD_TEXT_FONT_NAME_HINT", "") +
                 " nativeFallback=" + std::string{config_bool_value("WTS_WORLD_TEXT_FONT_NATIVE_FALLBACK", false) ? "true" : "false"});
        log_line("[native-transport-probe] config enabled=" +
                 std::string{m_native_transport_inventory_probe_enabled ? "true" : "false"});
        log_line("[input] hotkey config=" + hotkey_config_value +
                 " resolved=" + m_hotkey_name +
                 " vk=" + std::to_string(m_hotkey_vk));
        log_line("[save] destroy_confirm_ttl_sec=" + std::to_string(m_destroy_confirm_ttl_sec));
        log_line("[save] localclient_controller_probe_interval_sec=" + std::to_string(m_localclient_controller_probe_interval_sec));
        log_line("[save] localclient_motion_reapply_enabled=" + std::string{m_localclient_motion_reapply_enabled ? "true" : "false"});
        if (m_behavior_trace_enabled)
        {
            log_line("[trace-sm] config enabled=true");
        }

        std::error_code mkdir_ec{};
        std::filesystem::create_directories(m_data_root, mkdir_ec);
        if (mkdir_ec)
        {
            log_line("[save] failed to create data root path=" + m_data_root.string() + " error=" + mkdir_ec.message());
        }
        std::error_code backup_mkdir_ec{};
        std::filesystem::create_directories(m_backup_root, backup_mkdir_ec);
        if (backup_mkdir_ec)
        {
            log_line("[save] failed to create backup root path=" + m_backup_root.string() + " error=" + backup_mkdir_ec.message());
        }

        load_sidecar_json();
        if (!is_dedicated_runtime_process())
        {
            register_input_hotkey();
            probe_phase7_native_ui_capabilities();
        }
        else
        {
            log_line("[input] Runtime input/UI registration skipped reason=dedicated_server");
        }

        m_unreal_ready = true;
        m_last_restore_scan = std::chrono::steady_clock::now();
        if (m_native_transport_inventory_probe_enabled)
        {
            m_native_transport_inventory_requested.store(true);
        }

        log_line("[phase] Phase 1 bootstrap active: hooks + hotkey + sidecar loaded");
    }

    auto SignTextMod::register_input_hotkey() -> void
    {
        if (is_dedicated_runtime_process())
        {
            log_line("[input] Hotkey registration skipped reason=dedicated_server");
            return;
        }
        register_keydown_event(static_cast<Input::Key>(m_hotkey_vk), [this]() {
            m_hotkey_requested.store(true);
        });
        register_keydown_event(Input::Key::RETURN, [this]() {
            if (m_phase7_umg_widget)
            {
                m_phase7_enter_requested.store(true);
            }
        });
        register_keydown_event(Input::Key::ESCAPE, [this]() {
            if (m_phase7_umg_widget)
            {
                m_phase7_escape_requested.store(true);
            }
        });
        log_line("[input] Registered hotkeys: " + m_hotkey_name + "=target/open_editor");
    }

    auto SignTextMod::install_phase7_keyboard_capture_hook() -> void
    {
        m_phase7_keyboard_capture_active.store(false);
        m_phase7_keyboard_hook_installed.store(false);
    }

    auto SignTextMod::uninstall_phase7_keyboard_capture_hook() -> void
    {
        m_phase7_keyboard_capture_active.store(false);
        m_phase7_keyboard_hook_stop.store(false);
        m_phase7_keyboard_hook_installed.store(false);
        m_phase7_keyboard_hook_thread_id.store(0);
    }

    auto SignTextMod::probe_phase7_native_ui_capabilities() -> void
    {
        if (m_phase7_native_probe_ran)
        {
            return;
        }
        m_phase7_native_probe_ran = true;

        auto has_class = [&](const TCHAR* path) -> bool {
            auto* cls = Cast<UClass>(UObjectGlobals::StaticFindObject<UObject*>(UClass::StaticClass(), nullptr, path));
            if (!cls)
            {
                cls = UObjectGlobals::FindObject<UClass>(nullptr, path);
            }
            return cls != nullptr;
        };

        auto has_function = [&](const TCHAR* path) -> bool {
            return UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path) != nullptr;
        };

        const bool has_user_widget_class = has_class(STR("/Script/UMG.UserWidget"));
        const bool has_widget_tree_class = has_class(STR("/Script/UMG.WidgetTree"));
        const bool has_editable_text_class = has_class(STR("/Script/UMG.EditableTextBox"));
        const bool has_button_class = has_class(STR("/Script/UMG.Button"));
        const bool has_panel_class = has_class(STR("/Script/UMG.VerticalBox")) || has_class(STR("/Script/UMG.CanvasPanel"));
        const bool has_add_to_viewport = has_function(STR("/Script/UMG.UserWidget:AddToViewport"));
        const bool has_remove_from_parent = has_function(STR("/Script/UMG.Widget:RemoveFromParent")) ||
            has_function(STR("/Script/UMG.UserWidget:RemoveFromParent"));
        const bool has_input_mode_game_ui = has_function(STR("/Script/Engine.PlayerController:SetInputModeGameAndUI"));
        const bool has_input_mode_game_only = has_function(STR("/Script/Engine.PlayerController:SetInputModeGameOnly"));

        m_phase7_native_supported =
            has_user_widget_class &&
            has_widget_tree_class &&
            has_editable_text_class &&
            has_button_class &&
            has_panel_class &&
            has_add_to_viewport &&
            has_remove_from_parent &&
            has_input_mode_game_ui &&
            has_input_mode_game_only;

        m_phase7_imgui_fallback_enabled = !m_phase7_native_supported;

        std::ostringstream summary{};
        summary << "userWidget=" << (has_user_widget_class ? "1" : "0")
                << " widgetTree=" << (has_widget_tree_class ? "1" : "0")
                << " editableText=" << (has_editable_text_class ? "1" : "0")
                << " button=" << (has_button_class ? "1" : "0")
                << " panel=" << (has_panel_class ? "1" : "0")
                << " addToViewport=" << (has_add_to_viewport ? "1" : "0")
                << " removeFromParent=" << (has_remove_from_parent ? "1" : "0")
                << " inputGameAndUI=" << (has_input_mode_game_ui ? "1" : "0")
                << " inputGameOnly=" << (has_input_mode_game_only ? "1" : "0");
        m_phase7_native_probe_summary = summary.str();

        log_line("[phase7] native_ui_probe supported=" + std::string{m_phase7_native_supported ? "true" : "false"} +
                 " " + m_phase7_native_probe_summary);
        if (!m_phase7_native_supported)
        {
            log_line("[phase7] native_ui_probe fallback=imgui reason=missing_required_umg_or_input_mode");
        }
    }

    auto SignTextMod::set_phase7_game_and_ui_input_mode(bool enable_ui_mode) -> bool
    {
        if (m_phase7_ui_input_mode_active == enable_ui_mode)
        {
            return true;
        }
        auto* controller = try_get_primary_player_controller();
        if (!controller)
        {
            return false;
        }

        bool input_mode_applied = false;
        std::string applied_mode_name = "none";
        if (enable_ui_mode)
        {
            // Prefer UIOnly so the game's Escape -> Main Menu binding is suppressed while the
            // editor is open. GameAndUI leaks game input through and lets Esc open the pause
            // menu underneath the editor, which is the user-visible bug.
            input_mode_applied = invoke_no_param(
                controller,
                STR("SetInputModeUIOnly"),
                STR("/Script/Engine.PlayerController:SetInputModeUIOnly"));
            if (input_mode_applied)
            {
                applied_mode_name = "UIOnly";
            }
            else
            {
                input_mode_applied = invoke_no_param(
                    controller,
                    STR("SetInputModeGameAndUI"),
                    STR("/Script/Engine.PlayerController:SetInputModeGameAndUI"));
                if (input_mode_applied)
                {
                    applied_mode_name = "GameAndUI";
                }
            }
        }
        else
        {
            input_mode_applied = invoke_no_param(
                controller,
                STR("SetInputModeGameOnly"),
                STR("/Script/Engine.PlayerController:SetInputModeGameOnly"));
            if (input_mode_applied)
            {
                applied_mode_name = "GameOnly";
            }
        }

        const bool cursor_set = set_bool_property_if_present(controller, "bshowmousecursor", enable_ui_mode);
        const bool look_ignored = invoke_with_bool_param(controller, STR("SetIgnoreLookInput"), STR("/Script/Engine.Controller:SetIgnoreLookInput"), enable_ui_mode);
        const bool move_ignored = invoke_with_bool_param(controller, STR("SetIgnoreMoveInput"), STR("/Script/Engine.Controller:SetIgnoreMoveInput"), enable_ui_mode);
        m_phase7_keyboard_capture_active.store(false);
        log_line("[phase7-umg] input_capture enable=" + std::string{enable_ui_mode ? "true" : "false"} +
                 " inputMode=" + std::string{input_mode_applied ? "true" : "false"} +
                 " appliedMode=" + applied_mode_name +
                 " cursor=" + std::string{cursor_set ? "true" : "false"} +
                 " ignoreLook=" + std::string{look_ignored ? "true" : "false"} +
                 " ignoreMove=" + std::string{move_ignored ? "true" : "false"} +
                 " llHook=disabled");
        const bool applied = input_mode_applied || cursor_set || look_ignored || move_ignored;
        if (applied)
        {
            m_phase7_ui_input_mode_active = enable_ui_mode;
        }
        return applied;
    }

    auto SignTextMod::open_phase7_native_editor_for_selection() -> bool
    {
        if (!m_selected.has_value())
        {
            return false;
        }
        if (!ensure_selected_actor_valid("open_phase7_native_editor_for_selection"))
        {
            return false;
        }
        probe_phase7_native_ui_capabilities();
        if (!m_phase7_native_supported)
        {
            return false;
        }
        if (m_phase7_native_editor_open)
        {
            return true;
        }

        auto* user_widget_class = Cast<UClass>(UObjectGlobals::StaticFindObject<UObject*>(
            UClass::StaticClass(),
            nullptr,
            STR("/Script/UMG.UserWidget")));
        auto* controller = try_get_primary_player_controller();
        if (!user_widget_class || !controller)
        {
            log_line("[phase7] open_native_editor failed reason=missing_widget_class_or_controller");
            return false;
        }

        // Investigation-first path:
        // instantiate the base UserWidget and verify AddToViewport + input-mode flow.
        // Runtime widget tree construction (text field/buttons) is a follow-up step in this phase.
        auto* widget = UObjectGlobals::NewObject<UObject>(controller, user_widget_class, NAME_None, RF_Transient);
        if (!widget)
        {
            log_line("[phase7] open_native_editor failed reason=NewObjectUserWidgetReturnedNull");
            return false;
        }

        const bool added = invoke_add_to_viewport(widget, 1000);
        const bool input_mode = set_phase7_game_and_ui_input_mode(true);
        if (!added)
        {
            log_line("[phase7] open_native_editor failed reason=AddToViewportReturnedFalse inputModeApplied=" +
                     std::string{input_mode ? "true" : "false"});
            return false;
        }

        m_phase7_native_widget = widget;
        m_phase7_native_editor_open = true;
        m_phase7_teardown_skip_logged = false;
        log_line("[phase7] open_native_editor success widget=" + narrow_ascii(widget->GetFullName()) +
                 " inputModeApplied=" + std::string{input_mode ? "true" : "false"});
        return true;
    }

    auto SignTextMod::close_phase7_native_editor(bool restore_game_input) -> void
    {
        m_phase7_keyboard_capture_active.store(false);
        if (m_phase7_native_widget)
        {
            const bool removed = invoke_no_param(
                m_phase7_native_widget,
                STR("RemoveFromParent"),
                STR("/Script/UMG.Widget:RemoveFromParent"));
            log_line("[phase7] close_native_editor removedWidget=" + std::string{removed ? "true" : "false"});
        }
        if (restore_game_input)
        {
            const bool restored = set_phase7_game_and_ui_input_mode(false);
            log_line("[phase7] close_native_editor restoreInput=" + std::string{restored ? "true" : "false"});
        }
        m_phase7_native_widget = nullptr;
        m_phase7_native_editor_open = false;
    }

    auto SignTextMod::cache_phase7_umg_class_pointers() -> bool
    {
        if (!m_phase7_class_user_widget)
        {
            m_phase7_class_user_widget = find_uclass_by_path(STR("/Script/UMG.UserWidget"));
        }
        if (!m_phase7_class_widget_tree)
        {
            m_phase7_class_widget_tree = find_uclass_by_path(STR("/Script/UMG.WidgetTree"));
        }
        if (!m_phase7_class_canvas_panel)
        {
            m_phase7_class_canvas_panel = find_uclass_by_path(STR("/Script/UMG.CanvasPanel"));
        }
        if (!m_phase7_class_border)
        {
            m_phase7_class_border = find_uclass_by_path(STR("/Script/UMG.Border"));
        }
        if (!m_phase7_class_size_box)
        {
            m_phase7_class_size_box = find_uclass_by_path(STR("/Script/UMG.SizeBox"));
        }
        if (!m_phase7_class_text_block)
        {
            m_phase7_class_text_block = find_uclass_by_path(STR("/Script/UMG.TextBlock"));
        }
        if (!m_phase7_class_text_box)
        {
            m_phase7_class_text_box = find_uclass_by_path(STR("/Script/UMG.MultiLineEditableText"));
        }
        if (!m_phase7_class_text_box)
        {
            m_phase7_class_text_box = find_uclass_by_path(STR("/Script/UMG.MultiLineEditableTextBox"));
        }
        if (!m_phase7_class_text_box)
        {
            m_phase7_class_text_box = find_uclass_by_path(STR("/Script/UMG.EditableText"));
        }
        if (!m_phase7_class_text_box)
        {
            m_phase7_class_text_box = find_uclass_by_path(STR("/Script/UMG.EditableTextBox"));
        }
        return m_phase7_class_user_widget &&
               m_phase7_class_widget_tree &&
               m_phase7_class_canvas_panel &&
               m_phase7_class_border &&
               m_phase7_class_size_box &&
               m_phase7_class_text_block &&
               m_phase7_class_text_box;
    }

    auto SignTextMod::cache_phase7_umg_function_pointers() -> void
    {
        if (m_phase7_umg_widget)
        {
            if (!m_phase7_fn_add_to_viewport)
            {
                m_phase7_fn_add_to_viewport = find_function_by_chain_or_path(
                    m_phase7_umg_widget,
                    STR("AddToViewport"),
                    STR("/Script/UMG.UserWidget:AddToViewport"));
            }
            if (!m_phase7_fn_remove_from_parent)
            {
                m_phase7_fn_remove_from_parent = find_function_by_chain_or_path(
                    m_phase7_umg_widget,
                    STR("RemoveFromParent"),
                    STR("/Script/UMG.Widget:RemoveFromParent"));
            }
            if (!m_phase7_fn_set_visibility)
            {
                m_phase7_fn_set_visibility = find_function_by_chain_or_path(
                    m_phase7_umg_widget,
                    STR("SetVisibility"),
                    STR("/Script/UMG.Widget:SetVisibility"));
            }
        }
        if (m_phase7_umg_text_box)
        {
            if (!m_phase7_fn_set_keyboard_focus)
            {
                m_phase7_fn_set_keyboard_focus = find_function_by_chain_or_path(
                    m_phase7_umg_text_box,
                    STR("SetKeyboardFocus"),
                    STR("/Script/UMG.Widget:SetKeyboardFocus"));
            }
            if (!m_phase7_fn_set_focus)
            {
                m_phase7_fn_set_focus = find_function_by_chain_or_path(
                    m_phase7_umg_text_box,
                    STR("SetFocus"),
                    STR("/Script/UMG.Widget:SetFocus"));
            }
        }
    }

    auto parse_retry_delay_ms_config(const std::string& raw_value) -> std::array<uint32_t, 3>
    {
        std::array<uint32_t, 3> parsed{250, 750, 1500};
        std::stringstream ss(raw_value);
        std::string token{};
        size_t idx = 0;
        while (std::getline(ss, token, ',') && idx < parsed.size())
        {
            const auto trimmed = trim_copy_ascii(token);
            if (trimmed.empty())
            {
                ++idx;
                continue;
            }
            const auto value = safe_stoi(trimmed, static_cast<int>(parsed[idx]));
            parsed[idx] = static_cast<uint32_t>(std::clamp(value, 50, 10000));
            ++idx;
        }

        for (size_t i = 1; i < parsed.size(); ++i)
        {
            if (parsed[i] <= parsed[i - 1])
            {
                parsed[i] = parsed[i - 1] + 100;
            }
        }
        return parsed;
    }

    auto SignTextMod::invalidate_phase7_umg_widget_cache(const std::string& reason) -> void
    {
        m_phase7_umg_widget = nullptr;
        m_phase7_umg_text_box = nullptr;
        m_phase7_umg_title = nullptr;
        m_phase7_umg_hint = nullptr;
        m_phase7_umg_apply_button = nullptr;
        m_phase7_umg_clear_button = nullptr;
        m_phase7_umg_cancel_button = nullptr;
        m_phase7_fn_add_to_viewport = nullptr;
        m_phase7_fn_remove_from_parent = nullptr;
        m_phase7_fn_set_keyboard_focus = nullptr;
        m_phase7_fn_set_focus = nullptr;
        m_phase7_fn_set_visibility = nullptr;
        m_phase7_umg_in_viewport = false;
        m_phase7_ui_input_mode_active = false;
        m_phase7_active_epoch = 0;
        m_phase7_teardown_pending = false;
        m_phase7_teardown_pending_reason.clear();
        m_phase7_watchdog_logged = false;
        m_phase7_last_close_removed = false;
        m_phase7_guard_fail_started = {};
        m_phase7_guard_fail_reason.clear();
        m_phase7_guard_hysteresis_logged = false;
        m_phase7_stale_epoch_last_log = {};
        m_phase7_stale_epoch_last_detail.clear();
        if (!reason.empty())
        {
            log_line("[phase7-umg] widget_cache_invalidated reason=" + reason);
        }
    }

    auto SignTextMod::ensure_phase7_umg_widget_built() -> bool
    {
        if (m_phase7_umg_widget && m_phase7_umg_text_box &&
            is_uobject_reflection_safe(m_phase7_umg_widget) &&
            is_uobject_reflection_safe(m_phase7_umg_text_box))
        {
            cache_phase7_umg_function_pointers();
            return true;
        }
        if (m_phase7_umg_widget || m_phase7_umg_text_box)
        {
            invalidate_phase7_umg_widget_cache("stale_widget_pointer");
        }

        auto* controller = try_get_primary_player_controller();
        const bool classes_ok = cache_phase7_umg_class_pointers();
        if (!controller || !classes_ok)
        {
            log_line("[phase7-umg] open_failed reason=missing_class controller=" + std::string{controller ? "1" : "0"} +
                     " userWidget=" + std::string{m_phase7_class_user_widget ? "1" : "0"} +
                     " widgetTree=" + std::string{m_phase7_class_widget_tree ? "1" : "0"} +
                     " canvas=" + std::string{m_phase7_class_canvas_panel ? "1" : "0"} +
                     " border=" + std::string{m_phase7_class_border ? "1" : "0"} +
                     " sizeBox=" + std::string{m_phase7_class_size_box ? "1" : "0"} +
                     " textBlock=" + std::string{m_phase7_class_text_block ? "1" : "0"} +
                     " textBox=" + std::string{m_phase7_class_text_box ? "1" : "0"});
            return false;
        }

        auto* widget = UObjectGlobals::NewObject<UObject>(controller, m_phase7_class_user_widget, NAME_None, RF_Transient);
        auto* tree = widget ? get_object_property_if_present(widget, "WidgetTree") : nullptr;
        if (widget && !tree)
        {
            tree = UObjectGlobals::NewObject<UObject>(widget, m_phase7_class_widget_tree, FName(STR("WidgetTree"), FNAME_Add), RF_Transient);
            if (tree)
            {
                (void)set_object_property_if_present(widget, "WidgetTree", tree);
            }
        }
        if (!widget || !tree)
        {
            log_line("[phase7-umg] open_failed reason=create_widget_or_tree widget=" + std::string{widget ? "1" : "0"} +
                     " tree=" + std::string{tree ? "1" : "0"});
            return false;
        }

        auto* root = create_umg_widget_object(tree, m_phase7_class_canvas_panel, "WTS_RootCanvas");
        auto* frame = create_umg_widget_object(tree, m_phase7_class_border, "WTS_Frame");
        auto* background = create_umg_widget_object(tree, m_phase7_class_border, "WTS_Background");
        auto* panel = create_umg_widget_object(tree, m_phase7_class_canvas_panel, "WTS_InnerCanvas");
        auto* title = create_umg_widget_object(tree, m_phase7_class_text_block, "WTS_Title");
        auto* divider = create_umg_widget_object(tree, m_phase7_class_border, "WTS_Divider");
        auto* input_frame = create_umg_widget_object(tree, m_phase7_class_border, "WTS_InputFrame");
        auto* input_background = create_umg_widget_object(tree, m_phase7_class_border, "WTS_InputBackground");
        auto* editor = create_umg_widget_object(tree, m_phase7_class_size_box, "WTS_EditorSize");
        auto* text_box = create_umg_widget_object(tree, m_phase7_class_text_box, "WTS_TextBox");
        auto* hint = create_umg_widget_object(tree, m_phase7_class_text_block, "WTS_KeyHints");

        if (!root || !frame || !background || !panel || !title || !divider || !input_frame || !input_background || !editor || !text_box || !hint)
        {
            log_line("[phase7-umg] open_failed reason=construct_children root=" + std::string{root ? "1" : "0"} +
                     " frame=" + std::string{frame ? "1" : "0"} +
                     " background=" + std::string{background ? "1" : "0"} +
                     " panel=" + std::string{panel ? "1" : "0"} +
                     " title=" + std::string{title ? "1" : "0"} +
                     " divider=" + std::string{divider ? "1" : "0"} +
                     " inputFrame=" + std::string{input_frame ? "1" : "0"} +
                     " inputBackground=" + std::string{input_background ? "1" : "0"} +
                     " editor=" + std::string{editor ? "1" : "0"} +
                     " textBox=" + std::string{text_box ? "1" : "0"} +
                     " hint=" + std::string{hint ? "1" : "0"});
            return false;
        }
        log_line("[phase7-umg] construct_children_ok rootClass=" +
                 narrow_ascii(root->GetClassPrivate() ? root->GetClassPrivate()->GetFullName() : RC::StringType{}) +
                 " textBoxClass=" +
                 narrow_ascii(text_box->GetClassPrivate() ? text_box->GetClassPrivate()->GetFullName() : RC::StringType{}));
        if (m_f8_latency_breakdown_enabled && m_f8_latency_trace.active && !m_f8_latency_trace.construct_seen)
        {
            m_f8_latency_trace.construct = std::chrono::steady_clock::now();
            m_f8_latency_trace.construct_seen = true;
        }

        const bool root_set = set_object_property_if_present(tree, "RootWidget", root);
        const bool title_text = invoke_umg_set_text(title, "Sign Text");
        const bool hint_text = invoke_umg_set_text(hint, "Enter  Apply\nShift+Enter  New line\nEsc  Cancel");
        const bool input_text = invoke_umg_set_text(text_box, "");
        const bool title_color =
            invoke_set_rgba_value(title, STR("SetColorAndOpacity"), nullptr, 0.91f, 0.88f, 0.81f, 1.0f) ||
            invoke_set_rgba_value(title, STR("SetForegroundColor"), nullptr, 0.91f, 0.88f, 0.81f, 1.0f);
        const bool hint_color =
            invoke_set_rgba_value(hint, STR("SetColorAndOpacity"), nullptr, 0.66f, 0.64f, 0.60f, 1.0f) ||
            invoke_set_rgba_value(hint, STR("SetForegroundColor"), nullptr, 0.66f, 0.64f, 0.60f, 1.0f);
        const bool input_color =
            invoke_set_rgba_value(text_box, STR("SetColorAndOpacity"), nullptr, 1.0f, 1.0f, 1.0f, 1.0f) ||
            invoke_set_rgba_value(text_box, STR("SetForegroundColor"), nullptr, 1.0f, 1.0f, 1.0f, 1.0f);
        const bool frame_color = invoke_set_rgba_value(frame, STR("SetBrushColor"), STR("/Script/UMG.Border:SetBrushColor"), 0.17f, 0.17f, 0.16f, 0.94f);
        const bool background_color = invoke_set_rgba_value(background, STR("SetBrushColor"), STR("/Script/UMG.Border:SetBrushColor"), 0.045f, 0.050f, 0.055f, 0.90f);
        const bool divider_color = invoke_set_rgba_value(divider, STR("SetBrushColor"), STR("/Script/UMG.Border:SetBrushColor"), 0.72f, 0.68f, 0.58f, 0.76f);
        const bool input_frame_color = invoke_set_rgba_value(input_frame, STR("SetBrushColor"), STR("/Script/UMG.Border:SetBrushColor"), 0.43f, 0.40f, 0.34f, 0.95f);
        const bool input_background_color = invoke_set_rgba_value(input_background, STR("SetBrushColor"), STR("/Script/UMG.Border:SetBrushColor"), 0.025f, 0.028f, 0.030f, 0.84f);
        const bool frame_padding = invoke_set_margin_value(frame, STR("SetPadding"), STR("/Script/UMG.Border:SetPadding"), 2.0f, 2.0f, 2.0f, 2.0f);
        const bool background_padding = invoke_set_margin_value(background, STR("SetPadding"), STR("/Script/UMG.Border:SetPadding"), 8.0f, 8.0f, 8.0f, 8.0f);
        const bool input_frame_padding = invoke_set_margin_value(input_frame, STR("SetPadding"), STR("/Script/UMG.Border:SetPadding"), 1.5f, 1.5f, 1.5f, 1.5f);
        const bool input_background_padding = invoke_set_margin_value(input_background, STR("SetPadding"), STR("/Script/UMG.Border:SetPadding"), 7.0f, 7.0f, 7.0f, 7.0f);
        const bool editor_width = invoke_set_float_value(editor, STR("SetWidthOverride"), STR("/Script/UMG.SizeBox:SetWidthOverride"), 312.0f);
        const bool editor_height = invoke_set_float_value(editor, STR("SetHeightOverride"), STR("/Script/UMG.SizeBox:SetHeightOverride"), 154.0f);
        const bool root_opacity = invoke_set_float_value(root, STR("SetRenderOpacity"), STR("/Script/UMG.Widget:SetRenderOpacity"), 1.0f);
        const bool frame_opacity = invoke_set_float_value(frame, STR("SetRenderOpacity"), STR("/Script/UMG.Widget:SetRenderOpacity"), 1.0f);
        const bool background_opacity = invoke_set_float_value(background, STR("SetRenderOpacity"), STR("/Script/UMG.Widget:SetRenderOpacity"), 1.0f);
        const bool editor_opacity = invoke_set_float_value(editor, STR("SetRenderOpacity"), STR("/Script/UMG.Widget:SetRenderOpacity"), 1.0f);
        const bool input_opacity = invoke_set_float_value(text_box, STR("SetRenderOpacity"), STR("/Script/UMG.Widget:SetRenderOpacity"), 1.0f);
        const bool frame_scale = invoke_set_vector2d_value(frame, STR("SetRenderScale"), STR("/Script/UMG.Widget:SetRenderScale"), 1.0f, 1.0f);
        const bool title_scale = invoke_set_vector2d_value(title, STR("SetRenderScale"), STR("/Script/UMG.Widget:SetRenderScale"), 0.78f, 0.78f);
        const bool hint_scale = invoke_set_vector2d_value(hint, STR("SetRenderScale"), STR("/Script/UMG.Widget:SetRenderScale"), 0.42f, 0.42f);
        const bool input_scale = invoke_set_vector2d_value(text_box, STR("SetRenderScale"), STR("/Script/UMG.Widget:SetRenderScale"), 1.0f, 1.0f);

        auto* frame_slot = invoke_add_child(root, frame);
        const bool slot_pos = invoke_set_vector2d_value(frame_slot, STR("SetPosition"), nullptr, 960.0f, 540.0f);
        const bool slot_size = invoke_set_vector2d_value(frame_slot, STR("SetSize"), nullptr, 430.0f, 430.0f);
        const bool slot_align = invoke_set_vector2d_value(frame_slot, STR("SetAlignment"), nullptr, 0.5f, 0.5f);
        const bool frame_content = invoke_set_content(frame, background);
        const bool background_content = invoke_set_content(background, panel);

        auto* title_slot = invoke_add_child(panel, title);
        const bool title_pos = invoke_set_vector2d_value(title_slot, STR("SetPosition"), nullptr, 146.0f, 24.0f);
        const bool title_size = invoke_set_vector2d_value(title_slot, STR("SetSize"), nullptr, 220.0f, 50.0f);
        auto* divider_slot = invoke_add_child(panel, divider);
        const bool divider_pos = invoke_set_vector2d_value(divider_slot, STR("SetPosition"), nullptr, 18.0f, 82.0f);
        const bool divider_size = invoke_set_vector2d_value(divider_slot, STR("SetSize"), nullptr, 378.0f, 2.0f);
        auto* input_slot = invoke_add_child(panel, input_frame);
        const bool input_pos = invoke_set_vector2d_value(input_slot, STR("SetPosition"), nullptr, 52.0f, 112.0f);
        const bool input_size = invoke_set_vector2d_value(input_slot, STR("SetSize"), nullptr, 320.0f, 170.0f);
        auto* hint_slot = invoke_add_child(panel, hint);
        const bool hint_pos = invoke_set_vector2d_value(hint_slot, STR("SetPosition"), nullptr, 68.0f, 315.0f);
        const bool hint_size = invoke_set_vector2d_value(hint_slot, STR("SetSize"), nullptr, 660.0f, 180.0f);

        const bool input_frame_content = invoke_set_content(input_frame, input_background);
        const bool input_background_content = invoke_set_content(input_background, editor);
        const bool set_content = invoke_set_content(editor, text_box);
        const bool layout_ok =
            frame_slot && slot_pos && slot_size && slot_align &&
            frame_content && background_content &&
            title_slot && title_pos && title_size &&
            divider_slot && divider_pos && divider_size &&
            input_slot && input_pos && input_size &&
            hint_slot && hint_pos && hint_size &&
            input_frame_content && input_background_content && set_content;
        if (!layout_ok)
        {
            log_line("[phase7-umg] open_failed reason=layout_guard rootSet=" + std::string{root_set ? "true" : "false"} +
                     " slot=" + std::string{(frame_slot && slot_pos && slot_size && slot_align) ? "true" : "false"} +
                     " content=" + std::string{(frame_content && background_content && input_frame_content && input_background_content && set_content) ? "true" : "false"} +
                     " title=" + std::string{(title_slot && title_pos && title_size) ? "true" : "false"} +
                     " divider=" + std::string{(divider_slot && divider_pos && divider_size) ? "true" : "false"} +
                     " input=" + std::string{(input_slot && input_pos && input_size) ? "true" : "false"} +
                     " hint=" + std::string{(hint_slot && hint_pos && hint_size) ? "true" : "false"});
            return false;
        }

        m_phase7_umg_widget = widget;
        m_phase7_umg_text_box = text_box;
        m_phase7_umg_title = title;
        m_phase7_umg_hint = hint;
        cache_phase7_umg_function_pointers();

        // Prewarm should build the widget tree only; do not add/show during loading/bootstrap.
        m_phase7_umg_in_viewport = false;
        log_line(std::string{"[phase7-umg] prewarm_result built=true added=false collapsed=true focus=false"} +
                 " style=" + std::string{(title_text && hint_text && input_text && title_color && hint_color && input_color &&
                                          frame_color && background_color && divider_color && input_frame_color && input_background_color &&
                                          frame_padding && background_padding && input_frame_padding && input_background_padding &&
                                          editor_width && editor_height && root_opacity && frame_opacity && background_opacity &&
                                          editor_opacity && input_opacity && frame_scale && title_scale && hint_scale && input_scale) ? "true" : "false"});
        return true;
    }

    auto SignTextMod::maybe_prewarm_phase7_umg_editor() -> void
    {
        if (is_dedicated_runtime_process() || !m_session_ready_latched)
        {
            return;
        }
        if (m_phase7_umg_prewarm_succeeded)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_phase7_umg_prewarm_attempted &&
            m_phase7_umg_prewarm_next_try.time_since_epoch().count() != 0 &&
            now < m_phase7_umg_prewarm_next_try)
        {
            return;
        }
        m_phase7_umg_prewarm_attempted = true;
        const bool built = ensure_phase7_umg_widget_built();
        m_phase7_umg_prewarm_succeeded = built;
        m_phase7_umg_prewarm_next_try = now + std::chrono::seconds(built ? 60 : 3);
        log_line("[phase7-umg] prewarm_try success=" + std::string{built ? "true" : "false"} +
                 " sessionReady=" + std::string{m_session_ready_latched ? "true" : "false"});
    }

    auto SignTextMod::open_phase7_umg_editor_for_selection() -> bool
    {
        std::string ready_reason{};
        const bool inprocess_ready = is_session_ready_for_role_resolution(&ready_reason);
        if (!inprocess_ready)
        {
            log_line("[phase7-umg] open_suppressed reason=loading_or_transition detail=" + ready_reason);
            return false;
        }
        auto runtime_role_lower = lower_ascii(m_runtime_role);
        if (!m_role_lock_acquired && runtime_role_lower == "localclientpending")
        {
            // Try once to finalize role when runtime is already in gameplay-ready state.
            tick_localclient_role_resolution();
            runtime_role_lower = lower_ascii(m_runtime_role);
        }
        const bool allow_pending_localclient_open =
            !m_role_lock_acquired &&
            runtime_role_lower == "localclientpending";
        if (!m_role_lock_acquired && !allow_pending_localclient_open)
        {
            log_line("[phase7-umg] open_rejected reason=role_not_locked_or_unstable detail=role_lock_pending");
            return false;
        }
        if (runtime_role_lower != "localclient" &&
            runtime_role_lower != "remoteclient" &&
            !allow_pending_localclient_open)
        {
            log_line("[phase7-umg] open_rejected reason=role_not_locked_or_unstable detail=runtime_role_not_client role=" + m_runtime_role);
            return false;
        }
        if (!m_selected.has_value())
        {
            return false;
        }
        if (!ensure_selected_actor_valid("open_phase7_umg_editor_for_selection"))
        {
            return false;
        }
        if (!ensure_phase7_umg_widget_built())
        {
            return false;
        }
        if (!m_phase7_umg_widget || !m_phase7_umg_text_box || !is_uobject_reflection_safe(m_phase7_umg_widget) || !is_uobject_reflection_safe(m_phase7_umg_text_box))
        {
            invalidate_phase7_umg_widget_cache("open_invalid_widget");
            return false;
        }

        const auto actor_world_id = m_selected->world_id.empty() ? build_world_id_for_actor(m_selected->actor) : m_selected->world_id;
        configure_sidecar_for_actor(m_selected->actor, actor_world_id);
        const auto world_id = active_storage_world_id(actor_world_id);
        const auto key = build_storage_key(world_id, m_selected->stable_id);
        if (m_text_buffer_bound_key != key)
        {
            if (const auto found = m_labels.find(key); found != m_labels.end())
            {
                std::snprintf(m_text_buffer.data(), m_text_buffer.size(), "%s", found->second.text.c_str());
            }
            else
            {
                m_text_buffer.fill('\0');
            }
            m_text_buffer_bound_key = key;
        }
        m_phase7_umg_last_text = std::string{m_text_buffer.data()};
        const bool input_text = invoke_umg_set_text(m_phase7_umg_text_box, std::string{m_text_buffer.data()});
        if (m_f8_latency_breakdown_enabled && m_f8_latency_trace.active && !m_f8_latency_trace.construct_seen)
        {
            m_f8_latency_trace.construct = std::chrono::steady_clock::now();
            m_f8_latency_trace.construct_seen = true;
        }

        bool added = m_phase7_umg_in_viewport;
        if (!m_phase7_umg_in_viewport)
        {
            added = invoke_with_int_param_cached(m_phase7_umg_widget, m_phase7_fn_add_to_viewport, 1000) ||
                    invoke_add_to_viewport(m_phase7_umg_widget, 1000);
            m_phase7_umg_in_viewport = added;
        }
        const bool collapsed = invoke_with_byte_or_int_param_cached(m_phase7_umg_widget, m_phase7_fn_set_visibility, 1);
        const bool visible = invoke_with_byte_or_int_param_cached(m_phase7_umg_widget, m_phase7_fn_set_visibility, 0);
        const bool input_mode = set_phase7_game_and_ui_input_mode(true);
        const bool focus_keyboard = invoke_no_param_cached(m_phase7_umg_text_box, m_phase7_fn_set_keyboard_focus) ||
                                    invoke_no_param(m_phase7_umg_text_box, STR("SetKeyboardFocus"), STR("/Script/UMG.Widget:SetKeyboardFocus"));
        const bool focus_widget = invoke_no_param_cached(m_phase7_umg_text_box, m_phase7_fn_set_focus) ||
                                  invoke_no_param(m_phase7_umg_text_box, STR("SetFocus"), STR("/Script/UMG.Widget:SetFocus"));

        log_line("[phase7-umg] open_result added=" + std::string{added ? "true" : "false"} +
                 " inputModeApplied=" + std::string{input_mode ? "true" : "false"} +
                 " visible=" + std::string{visible ? "true" : "false"} +
                 " collapsedFirst=" + std::string{collapsed ? "true" : "false"} +
                 " inputText=" + std::string{input_text ? "true" : "false"} +
                 " focusKeyboard=" + std::string{focus_keyboard ? "true" : "false"} +
                 " focusWidget=" + std::string{focus_widget ? "true" : "false"} +
                 " key=" + key);
        if (m_f8_latency_breakdown_enabled && m_f8_latency_trace.active)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto ms_from = [&](const std::chrono::steady_clock::time_point& start,
                                     const std::chrono::steady_clock::time_point& end,
                                     bool valid) -> long long
            {
                if (!valid)
                {
                    return -1;
                }
                return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            };

            const bool target_valid = m_f8_latency_trace.target_seen;
            const bool construct_valid = m_f8_latency_trace.construct_seen;
            const long long edge_to_target_ms = ms_from(m_f8_latency_trace.edge, m_f8_latency_trace.target, target_valid);
            const long long target_to_construct_ms =
                ms_from(m_f8_latency_trace.target, m_f8_latency_trace.construct, target_valid && construct_valid);
            const long long construct_to_open_ms = ms_from(m_f8_latency_trace.construct, now, construct_valid);
            const long long edge_to_open_ms = ms_from(m_f8_latency_trace.edge, now, true);
            log_line("[input-latency] f8_open pressId=" + std::to_string(m_f8_latency_trace.press_id) +
                     " edge_to_target_ms=" + std::to_string(edge_to_target_ms) +
                     " target_to_construct_children_ok_ms=" + std::to_string(target_to_construct_ms) +
                     " construct_children_ok_to_open_result_ms=" + std::to_string(construct_to_open_ms) +
                     " edge_to_open_result_ms=" + std::to_string(edge_to_open_ms) +
                     " umgAdded=" + std::string{added ? "true" : "false"});
            m_f8_latency_trace.active = false;
        }

        if (!added)
        {
            return false;
        }
        m_phase7_teardown_skip_logged = false;
        m_phase7_enter_was_down = false;
        m_phase7_escape_was_down = false;
        m_phase7_enter_requested.store(false);
        m_phase7_escape_requested.store(false);
        ++m_phase7_open_epoch;
        m_phase7_active_epoch = m_phase7_open_epoch;
        m_phase7_opened_at = std::chrono::steady_clock::now();
        m_phase7_last_interaction_at = m_phase7_opened_at;
        m_phase7_watchdog_logged = false;
        m_phase7_teardown_pending = false;
        m_phase7_teardown_pending_reason.clear();
        return true;
    }

    auto SignTextMod::close_phase7_umg_editor(bool restore_game_input) -> void
    {
        m_phase7_keyboard_capture_active.store(false);
        m_phase7_last_close_removed = false;
        if (m_phase7_umg_widget)
        {
            const bool hidden = invoke_with_byte_or_int_param_cached(m_phase7_umg_widget, m_phase7_fn_set_visibility, 1);
            const bool removed = invoke_no_param_cached(m_phase7_umg_widget, m_phase7_fn_remove_from_parent) ||
                invoke_no_param(
                    m_phase7_umg_widget,
                    STR("RemoveFromParent"),
                    STR("/Script/UMG.Widget:RemoveFromParent"));
            m_phase7_last_close_removed = removed;
            m_phase7_umg_in_viewport = !removed;
            log_line("[phase7-umg] close hidden=" + std::string{hidden ? "true" : "false"} +
                     " removedWidget=" + std::string{removed ? "true" : "false"});
        }
        if (restore_game_input)
        {
            const bool restored = set_phase7_game_and_ui_input_mode(false);
            log_line("[phase7-umg] close restoreInput=" + std::string{restored ? "true" : "false"});
        }
        m_phase7_enter_was_down = false;
        m_phase7_escape_was_down = false;
        m_phase7_enter_requested.store(false);
        m_phase7_escape_requested.store(false);
        m_phase7_umg_last_text.clear();
        m_phase7_active_epoch = 0;
        m_phase7_watchdog_logged = false;
    }

    auto SignTextMod::reset_phase7_runtime_state() -> void
    {
        m_phase7_keyboard_capture_active.store(false);
        m_phase7_enter_requested.store(false);
        m_phase7_escape_requested.store(false);
        m_phase7_enter_was_down = false;
        m_phase7_escape_was_down = false;
        m_phase7_ui_input_mode_active = false;
        m_phase7_umg_in_viewport = false;
        m_phase7_umg_last_text.clear();
        m_phase7_native_editor_open = false;
        m_phase7_native_widget = nullptr;
        m_phase7_umg_widget = nullptr;
        m_phase7_umg_text_box = nullptr;
        m_phase7_umg_title = nullptr;
        m_phase7_umg_hint = nullptr;
        m_phase7_umg_apply_button = nullptr;
        m_phase7_umg_clear_button = nullptr;
        m_phase7_umg_cancel_button = nullptr;
        m_phase7_fn_add_to_viewport = nullptr;
        m_phase7_fn_remove_from_parent = nullptr;
        m_phase7_fn_set_keyboard_focus = nullptr;
        m_phase7_fn_set_focus = nullptr;
        m_phase7_fn_set_visibility = nullptr;
        m_phase7_active_epoch = 0;
        m_phase7_teardown_pending = false;
        m_phase7_teardown_pending_reason.clear();
        m_phase7_definitive_teardown_armed = false;
        m_phase7_definitive_teardown_reason.clear();
        m_phase7_watchdog_logged = false;
        m_phase7_guard_fail_started = {};
        m_phase7_guard_fail_reason.clear();
        m_phase7_guard_hysteresis_logged = false;
        m_phase7_stale_epoch_last_log = {};
        m_phase7_stale_epoch_last_detail.clear();
    }

    auto SignTextMod::arm_phase7_definitive_teardown(const std::string& reason) -> void
    {
        const bool had_phase7_state =
            m_phase7_native_widget ||
            m_phase7_umg_widget ||
            m_phase7_native_editor_open ||
            m_phase7_umg_in_viewport ||
            m_phase7_ui_input_mode_active;
        const bool had_active_session_state =
            had_phase7_state ||
            m_session_ready_latched ||
            m_role_lock_acquired;
        if (!had_active_session_state)
        {
            return;
        }
        if (m_phase7_definitive_teardown_armed &&
            m_phase7_definitive_teardown_reason == reason)
        {
            return;
        }
        m_phase7_definitive_teardown_armed = true;
        m_phase7_definitive_teardown_reason = reason;
        log_line("[phase7] teardown_armed reason=" + reason);
    }

    auto SignTextMod::maybe_run_phase7_bootstrap_sanitize() -> void
    {
        if (m_phase7_bootstrap_sanitize_epoch == m_session_epoch)
        {
            return;
        }
        m_phase7_bootstrap_sanitize_epoch = m_session_epoch;
        if (m_phase7_umg_widget && is_uobject_reflection_safe(m_phase7_umg_widget))
        {
            close_phase7_umg_editor(true);
        }
        reset_phase7_runtime_state();
        invalidate_phase7_umg_widget_cache("bootstrap_sanitize");
        m_phase7_teardown_skip_logged = false;
        log_line("[phase7] bootstrap_sanitize applied=true epoch=" + std::to_string(m_session_epoch));
    }

    auto SignTextMod::force_close_phase7_for_teardown(const std::string& reason) -> void
    {
        const bool had_phase7_state =
            m_phase7_native_widget ||
            m_phase7_umg_widget ||
            m_phase7_native_editor_open ||
            m_phase7_umg_in_viewport ||
            m_phase7_ui_input_mode_active;

        if (had_phase7_state && !m_phase7_teardown_skip_logged)
        {
            log_line("[phase7-umg] skipped during teardown reason=" + reason);
            m_phase7_teardown_skip_logged = true;
        }
        if (m_phase7_umg_widget && !m_phase7_teardown_pending)
        {
            m_phase7_teardown_pending = true;
            m_phase7_teardown_pending_reason = reason;
            m_phase7_keyboard_capture_active.store(false);
            m_phase7_enter_requested.store(false);
            m_phase7_escape_requested.store(false);
            m_phase7_enter_was_down = false;
            m_phase7_escape_was_down = false;
            return;
        }
        if (!m_phase7_umg_widget)
        {
            reset_phase7_runtime_state();
            log_line("[phase7] teardown_committed removed=false reason=" + reason);
        }
    }

    auto SignTextMod::is_phase7_runtime_interaction_safe(std::string* out_reason) -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };

        if (is_dedicated_runtime_process())
        {
            set_reason("dedicated_runtime");
            return false;
        }
        if (!m_session_ready_latched)
        {
            set_reason("session_not_ready");
            return false;
        }
        if (!m_role_lock_acquired)
        {
            set_reason("role_lock_pending");
            return false;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        if (runtime_role_lower != "localclient" && runtime_role_lower != "remoteclient")
        {
            set_reason("runtime_role_not_client");
            return false;
        }

        std::string ready_reason{};
        if (!is_session_ready_for_role_resolution(&ready_reason))
        {
            set_reason("session_not_ready:" + ready_reason);
            return false;
        }

        if (runtime_role_lower == "localclient")
        {
            std::string stability_reason{};
            if (!is_localclient_runtime_stable_for_post_ready(&stability_reason))
            {
                set_reason("localclient_unstable:" + stability_reason);
                return false;
            }
        }

        auto* controller = try_get_primary_player_controller();
        if (!controller || !is_uobject_reflection_safe(controller))
        {
            set_reason("no_valid_player_controller");
            return false;
        }
        if (!controller->IsA(AActor::StaticClass()))
        {
            set_reason("controller_not_actor");
            return false;
        }
        auto* controller_actor = Cast<AActor>(controller);
        if (!controller_actor)
        {
            set_reason("controller_cast_failed");
            return false;
        }
        auto* world = controller_actor->GetWorld();
        if (!world)
        {
            set_reason("controller_world_null");
            return false;
        }
        const auto world_name = lower_ascii(narrow_ascii(world->GetName()));
        if (world_name.empty() ||
            world_name == "none" ||
            world_name.find("transition") != std::string::npos ||
            world_name.find("lobby") != std::string::npos ||
            world_name.find("entrance") != std::string::npos)
        {
            set_reason("controller_world_not_gameplay");
            return false;
        }

        set_reason("ok");
        return true;
    }

    auto SignTextMod::tick_phase7_umg_editor() -> void
    {
        if (!m_phase7_umg_widget)
        {
            return;
        }
        const uint64_t open_epoch = m_phase7_active_epoch;
        const auto now = std::chrono::steady_clock::now();
        const auto log_stale_epoch = [&](const std::string& detail) {
            const bool should_log =
                m_phase7_stale_epoch_last_detail != detail ||
                m_phase7_stale_epoch_last_log.time_since_epoch().count() == 0 ||
                (now - m_phase7_stale_epoch_last_log) >= std::chrono::seconds(5);
            if (should_log)
            {
                log_line("[phase7-umg] event_ignored reason=stale_epoch detail=" + detail);
                m_phase7_stale_epoch_last_detail = detail;
                m_phase7_stale_epoch_last_log = now;
            }
        };
        if (open_epoch == 0)
        {
            log_stale_epoch("no_active_epoch");
            return;
        }

        const short enter_state = GetAsyncKeyState(k_vk_return);
        const short escape_state = GetAsyncKeyState(k_vk_escape);
        const bool enter_down = ((enter_state & 0x8000) != 0);
        const bool escape_down = ((escape_state & 0x8000) != 0);
        const bool enter_pressed_since_poll = ((enter_state & 0x0001) != 0);
        const bool escape_pressed_since_poll = ((escape_state & 0x0001) != 0);
        const bool shift_down = ((GetAsyncKeyState(k_vk_shift) & 0x8000) != 0);
        const bool enter_requested = m_phase7_enter_requested.exchange(false);
        const bool escape_requested = m_phase7_escape_requested.exchange(false);
        const bool enter_edge = (enter_down && !m_phase7_enter_was_down) || enter_pressed_since_poll;
        const bool escape_edge = (escape_down && !m_phase7_escape_was_down) || escape_pressed_since_poll;
        m_phase7_enter_was_down = enter_down;
        m_phase7_escape_was_down = escape_down;

        std::string live_text{};
        const bool live_read = invoke_get_text_value(m_phase7_umg_text_box, live_text) ||
            read_text_property_value_no_process_event(m_phase7_umg_text_box, live_text);
        const bool unshifted_newline_added =
            live_read &&
            !shift_down &&
            count_line_breaks(live_text) > count_line_breaks(m_phase7_umg_last_text);
        const bool explicit_enter_intent = enter_requested || enter_edge || enter_pressed_since_poll;
        const bool apply_pressed = explicit_enter_intent && !shift_down;
        const bool cancel_pressed = escape_requested || escape_edge;
        if (enter_requested || escape_requested || enter_edge || escape_edge || enter_pressed_since_poll || escape_pressed_since_poll)
        {
            m_phase7_last_interaction_at = now;
        }

        if (!apply_pressed && !cancel_pressed)
        {
            if (unshifted_newline_added && !explicit_enter_intent && !shift_down)
            {
                log_line("[phase7-umg] apply_blocked reason=no_explicit_enter enterRequested=" +
                         std::string{enter_requested ? "true" : "false"} +
                         " enterEdge=" + std::string{enter_edge ? "true" : "false"} +
                         " enterAsync=" + std::string{enter_pressed_since_poll ? "true" : "false"} +
                         " newlineApply=" + std::string{unshifted_newline_added ? "true" : "false"});
            }
            if (live_read)
            {
                m_phase7_umg_last_text = live_text;
            }
            return;
        }
        if (!m_session_ready_latched)
        {
            log_line("[phase7-umg] apply_blocked reason=session_teardown readyLatched=false");
            close_phase7_umg_editor(true);
            return;
        }
        {
            std::string ready_reason{};
            if (!is_session_ready_for_role_resolution(&ready_reason))
            {
                log_line("[phase7-umg] apply_blocked reason=session_teardown detail=" + ready_reason);
                close_phase7_umg_editor(true);
                return;
            }
        }
        if (!m_selected.has_value() || !ensure_selected_actor_valid("tick_phase7_umg_editor"))
        {
            close_phase7_umg_editor(true);
            log_line("[phase7-umg] action_ignored reason=no_valid_selection");
            return;
        }

        const auto actor_world_id = m_selected->world_id.empty() ? build_world_id_for_actor(m_selected->actor) : m_selected->world_id;
        configure_sidecar_for_actor(m_selected->actor, actor_world_id);
        const auto world_id = active_storage_world_id(actor_world_id);
        const auto key = build_storage_key(world_id, m_selected->stable_id);

        if (apply_pressed)
        {
            if (open_epoch != m_phase7_active_epoch)
            {
                log_stale_epoch("apply");
                return;
            }
            std::string text = live_text;
            bool read = live_read;
            if (!read)
            {
                read = invoke_get_text_value(m_phase7_umg_text_box, text) ||
                    read_text_property_value_no_process_event(m_phase7_umg_text_box, text);
            }
            if (unshifted_newline_added)
            {
                text = strip_one_terminal_line_break(text);
            }
            const auto trimmed_for_clear = trim_copy_ascii(text);
            log_line("[phase7-umg] enter/apply read=" + std::string{read ? "true" : "false"} +
                     " key=" + key +
                     " chars=" + std::to_string(text.size()) +
                     " enterRequested=" + std::string{enter_requested ? "true" : "false"} +
                     " enterEdge=" + std::string{enter_edge ? "true" : "false"} +
                     " enterAsync=" + std::string{enter_pressed_since_poll ? "true" : "false"} +
                     " newlineApply=" + std::string{unshifted_newline_added ? "true" : "false"} +
                     " emptyMeansBlankMarker=" + std::string{trimmed_for_clear.empty() ? "true" : "false"});
            if (read)
            {
                if (text.size() > 48)
                {
                    text.resize(48);
                }
                std::snprintf(m_text_buffer.data(), m_text_buffer.size(), "%s", text.c_str());
                m_text_buffer_bound_key = key;
                if (trimmed_for_clear.empty())
                {
                    m_text_buffer.fill('\0');
                }
                apply_text_to_selected_label(text);
            }
            close_phase7_umg_editor(true);
            return;
        }
        if (cancel_pressed)
        {
            if (open_epoch != m_phase7_active_epoch)
            {
                log_stale_epoch("cancel");
                return;
            }
            log_line("[phase7-umg] escape cancel key=" + key +
                     " escapeRequested=" + std::string{escape_requested ? "true" : "false"} +
                     " escapeEdge=" + std::string{escape_edge ? "true" : "false"} +
                     " escapeAsync=" + std::string{escape_pressed_since_poll ? "true" : "false"});
            close_phase7_umg_editor(true);
            return;
        }
    }

    auto SignTextMod::try_get_primary_player_controller() -> UObject*
    {
        const auto now = std::chrono::steady_clock::now();
        const auto probe_interval = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<float>(std::clamp(m_localclient_controller_probe_interval_sec, 0.1f, 1.0f)));
        if (m_localclient_controller_probe_cache_valid &&
            m_localclient_controller_probe_last.time_since_epoch().count() != 0 &&
            (now - m_localclient_controller_probe_last) < probe_interval)
        {
            if (m_localclient_controller_probe_cached &&
                is_uobject_reflection_safe(m_localclient_controller_probe_cached) &&
                m_localclient_controller_probe_cached->IsA(AActor::StaticClass()))
            {
                auto* cached_actor = Cast<AActor>(m_localclient_controller_probe_cached);
                if (cached_actor && cached_actor->GetWorld())
                {
                    return m_localclient_controller_probe_cached;
                }
            }
            return nullptr;
        }

        m_localclient_controller_probe_last = now;
        std::vector<UObject*> player_controllers{};
        UObjectGlobals::FindAllOf(STR("PlayerController"), player_controllers);
        UObject* first_non_default = nullptr;
        UObject* best = nullptr;
        int best_score = -999999;
        for (auto* object : player_controllers)
        {
            if (!object)
            {
                continue;
            }
            if (!object->IsA(AActor::StaticClass()))
            {
                continue;
            }
            const auto full_name = lower_ascii(narrow_ascii(object->GetFullName()));
            if (full_name.find("default__") != std::string::npos)
            {
                continue;
            }
            if (!first_non_default)
            {
                first_non_default = object;
            }

            auto* controller_actor = object->IsA(AActor::StaticClass()) ? Cast<AActor>(object) : nullptr;
            auto* controller_world = controller_actor ? controller_actor->GetWorld() : nullptr;
            if (!controller_world)
            {
                continue;
            }

            int score = 0;
            bool is_local_controller = false;
            if (invoke_bool_return_no_param(
                    object,
                    STR("IsLocalController"),
                    STR("/Script/Engine.Controller:IsLocalController"),
                    is_local_controller) &&
                is_local_controller)
            {
                score += 200;
            }

            const auto view = get_player_viewpoint_reflective(object);
            if (view.valid)
            {
                score += 100;
            }

            UObject* controlled_pawn = nullptr;
            const bool got_pawn_from_fn = invoke_object_return_no_param(
                object,
                STR("GetPawn"),
                STR("/Script/Engine.Controller:GetPawn"),
                controlled_pawn);
            if (controlled_pawn)
            {
                score += 120;
            }
            else if (!got_pawn_from_fn)
            {
                // Avoid property-chain reflection fallback during world transition churn.
                // We will rescore on the next throttled probe.
                score -= 30;
            }

            const auto world_name = lower_ascii(narrow_ascii(controller_world->GetName()));
            if (world_name.find("lobby") != std::string::npos ||
                world_name.find("transition") != std::string::npos ||
                world_name.find("entrance") != std::string::npos)
            {
                score -= 120;
            }
            else
            {
                score += 60;
            }

            if (full_name.find("bp_r5playercontroller_c") != std::string::npos)
            {
                score += 25;
            }
            if (full_name.find("transitionmap") != std::string::npos ||
                full_name.find("clientlobby") != std::string::npos ||
                full_name.find("entrancehall") != std::string::npos)
            {
                score -= 80;
            }

            if (score > best_score)
            {
                best_score = score;
                best = object;
            }
        }

        auto* chosen = best ? best : first_non_default;
        m_localclient_controller_probe_cached = chosen;
        m_localclient_controller_probe_cache_valid = true;
        return chosen;
    }

    auto invoke_get_text_value(UObject* context, std::string& out_text) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            context,
            STR("GetText"),
            STR("/Script/UMG.EditableText:GetText"));
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        context->ProcessEvent(fn, params.data());

        bool found_return = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (found_return || !prop || !prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FTextProperty::StaticClass().HashObject())
            {
                return;
            }
            auto* text_ptr = prop->ContainerPtrToValuePtr<FText>(params.data());
            if (!text_ptr)
            {
                return;
            }
            out_text = RC::to_string(text_ptr->ToString());
            found_return = true;
        });
        return found_return;
    }

    auto invoke_get_text_render_value(UObject* context, std::string& out_text) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            context,
            STR("GetText"),
            STR("/Script/Engine.TextRenderComponent:GetText"));
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        context->ProcessEvent(fn, params.data());

        bool found_return = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (found_return || !prop || !prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FTextProperty::StaticClass().HashObject())
            {
                return;
            }
            auto* text_ptr = prop->ContainerPtrToValuePtr<FText>(params.data());
            if (!text_ptr)
            {
                return;
            }
            out_text = RC::to_string(text_ptr->ToString());
            found_return = true;
        });
        return found_return;
    }

    auto invoke_bool_return_with_float_param(
        UObject* context,
        const TCHAR* in_chain_name,
        const TCHAR* path_name,
        float value,
        bool& out_value) -> bool
    {
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 64)), 0);
        bool assigned = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FFloatProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<float>(params.data()))
                {
                    *value_ptr = value;
                    assigned = true;
                }
            }
        });
        if (!assigned)
        {
            return false;
        }

        context->ProcessEvent(fn, params.data());
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || !prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    out_value = *value_ptr;
                }
            }
        });
        return true;
    }

    auto invoke_project_world_to_screen(
        UObject* player_controller,
        const FVector& world_location,
        float& out_x,
        float& out_y,
        bool& out_projected) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            player_controller,
            STR("ProjectWorldLocationToScreen"),
            STR("/Script/Engine.PlayerController:ProjectWorldLocationToScreen"));
        if (!player_controller || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 128)), 0);
        bool assigned_world_loc = false;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop)
            {
                return;
            }
            if (prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }

            const auto prop_name = lower_copy_ascii(RC::to_string(prop->GetName()));
            if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject())
            {
                if ((prop_name.find("worldlocation") != std::string::npos || prop_name == "worldlocation") &&
                    prop->GetSize() >= FVector::StaticSize())
                {
                    if (auto* loc_ptr = prop->ContainerPtrToValuePtr<FVector>(params.data()))
                    {
                        *loc_ptr = world_location;
                        assigned_world_loc = true;
                    }
                    return;
                }
                if (prop->HasAnyPropertyFlags(CPF_OutParm))
                {
                    auto* storage = prop->ContainerPtrToValuePtr<void>(params.data());
                    if (!storage)
                    {
                        return;
                    }
                    if (prop->GetSize() == 8)
                    {
                        auto* values = static_cast<float*>(storage);
                        values[0] = 0.0f;
                        values[1] = 0.0f;
                    }
                    else if (prop->GetSize() == 16)
                    {
                        auto* values = static_cast<double*>(storage);
                        values[0] = 0.0;
                        values[1] = 0.0;
                    }
                }
            }
            else if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject() &&
                     !prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                // bPlayerViewportRelative=false for absolute viewport coordinates.
                if (auto* bool_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    *bool_ptr = false;
                }
            }
        });
        if (!assigned_world_loc)
        {
            return false;
        }

        player_controller->ProcessEvent(fn, params.data());
        out_projected = false;
        out_x = 0.0f;
        out_y = 0.0f;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop)
            {
                return;
            }
            if (prop->HasAnyPropertyFlags(CPF_ReturnParm) &&
                prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    out_projected = *value_ptr;
                }
                return;
            }
            if (!prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FStructProperty::StaticClass().HashObject())
            {
                return;
            }

            auto* storage = prop->ContainerPtrToValuePtr<void>(params.data());
            if (!storage)
            {
                return;
            }
            if (prop->GetSize() == 8)
            {
                auto* values = static_cast<float*>(storage);
                out_x = values[0];
                out_y = values[1];
            }
            else if (prop->GetSize() == 16)
            {
                auto* values = static_cast<double*>(storage);
                out_x = static_cast<float>(values[0]);
                out_y = static_cast<float>(values[1]);
            }
        });
        return true;
    }

    auto invoke_get_viewport_size(UObject* player_controller, int32_t& out_x, int32_t& out_y) -> bool
    {
        auto* fn = find_function_by_chain_or_path(
            player_controller,
            STR("GetViewportSize"),
            STR("/Script/Engine.PlayerController:GetViewportSize"));
        if (!player_controller || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 32)), 0);
        player_controller->ProcessEvent(fn, params.data());

        int32_t captured[2] = {0, 0};
        int32_t captured_count = 0;
        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || !prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() != FIntProperty::StaticClass().HashObject())
            {
                return;
            }
            if (auto* value_ptr = prop->ContainerPtrToValuePtr<int32_t>(params.data()))
            {
                if (captured_count < 2)
                {
                    captured[captured_count++] = *value_ptr;
                }
            }
        });
        if (captured_count < 2)
        {
            return false;
        }
        out_x = captured[0];
        out_y = captured[1];
        return true;
    }

    auto read_text_property_value_no_process_event(UObject* context, std::string& out_text) -> bool
    {
        if (!context || !context->GetClassPrivate())
        {
            return false;
        }

        bool found_text = false;
        for_each_property_in_chain_compat(context->GetClassPrivate(), [&](FProperty* prop) {
            if (found_text || !prop)
            {
                return;
            }
            if (prop->GetClass().HashObject() != FTextProperty::StaticClass().HashObject())
            {
                return;
            }
            auto prop_name = lower_copy_ascii(RC::to_string(prop->GetName()));
            if (prop_name.find("text") == std::string::npos &&
                prop_name.find("name") == std::string::npos)
            {
                return;
            }
            auto* text_ptr = prop->ContainerPtrToValuePtr<FText>(context);
            if (!text_ptr)
            {
                return;
            }
            out_text = RC::to_string(text_ptr->ToString());
            found_text = true;
        });
        return found_text;
    }

    auto SignTextMod::is_probable_label_actor(AActor* actor) const -> bool
    {
        if (!actor)
        {
            return false;
        }
        const auto full_name = lower_ascii(narrow_ascii(actor->GetFullName()));
        const auto class_name = lower_ascii(narrow_ascii(actor->GetClassPrivate()->GetFullName()));
        return full_name.find("wallplaque") != std::string::npos ||
            class_name.find("wallplaque") != std::string::npos ||
            full_name.find("lables_wooden") != std::string::npos ||
            class_name.find("lables_wooden") != std::string::npos;
    }

    auto SignTextMod::detect_label_asset(AActor* actor) const -> std::string
    {
        if (!actor)
        {
            return {};
        }
        const auto full_name = narrow_ascii(actor->GetFullName());
        const std::regex rx(R"((DA_BI_Utilities_Lables_Wooden_[A-Za-z0-9_]+))", std::regex::icase);
        std::smatch match{};
        if (std::regex_search(full_name, match, rx) && match.size() > 1)
        {
            return match[1].str();
        }
        return "unknown";
    }

    auto SignTextMod::is_dedicated_runtime_process() const -> bool
    {
        return is_dedicated_server_process(std::filesystem::current_path(), m_mod_root) ||
            lower_ascii(m_runtime_role).find("dedicatedserver") != std::string::npos;
    }

    auto SignTextMod::is_world_authoritative(UObject* world_object) const -> bool
    {
        if (!world_object)
        {
            return false;
        }

        bool saw_authority_game_mode = false;
        bool has_authority_game_mode = false;
        for_each_property_in_chain_compat(world_object->GetClassPrivate(), [&](FProperty* prop) {
            if (!prop || has_authority_game_mode)
            {
                return;
            }
            const auto prop_name = lower_ascii(RC::to_string(prop->GetName()));
            if (prop_name != "authoritygamemode")
            {
                return;
            }
            saw_authority_game_mode = true;
            if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(world_object); value_ptr && *value_ptr)
                {
                    has_authority_game_mode = true;
                }
            }
        });

        return saw_authority_game_mode && has_authority_game_mode;
    }

    auto SignTextMod::is_local_hosted_runtime() const -> bool
    {
        static auto last_check = std::chrono::steady_clock::time_point{};
        static bool cached_result = false;
        const auto now = std::chrono::steady_clock::now();
        if (last_check.time_since_epoch().count() != 0 && (now - last_check) < std::chrono::seconds(5))
        {
            return cached_result;
        }
        last_check = now;
        cached_result = false;

        const auto local_app_data = get_env_var("LOCALAPPDATA");
        if (local_app_data.empty())
        {
            return false;
        }

        std::string content{};
        if (!read_text_file(std::filesystem::path{local_app_data} / "R5" / "Saved" / "Logs" / "R5.log", content))
        {
            return false;
        }

        const auto last_host_start = content.rfind("Start Coop Host server");
        const auto last_find_favorite = content.rfind("FindFavoriteGs");
        const auto last_dedicated_hosting = content.rfind("\"Hosting\": \"Dedicated\"");
        size_t last_remote_signal = std::string::npos;
        if (last_find_favorite != std::string::npos)
        {
            last_remote_signal = last_find_favorite;
        }
        if (last_dedicated_hosting != std::string::npos &&
            (last_remote_signal == std::string::npos || last_dedicated_hosting > last_remote_signal))
        {
            last_remote_signal = last_dedicated_hosting;
        }
        if (last_host_start != std::string::npos &&
            (last_remote_signal == std::string::npos || last_host_start > last_remote_signal))
        {
            cached_result = true;
            return cached_result;
        }

        static const std::regex hosting_rx{"\"Hosting\"\\s*:\\s*\"([^\"]+)\"", std::regex::icase};
        std::string last_hosting{};
        size_t last_hosting_pos = std::string::npos;
        for (auto it = std::sregex_iterator(content.begin(), content.end(), hosting_rx);
             it != std::sregex_iterator{};
             ++it)
        {
            if (it->size() > 1)
            {
                last_hosting = lower_ascii((*it)[1].str());
                last_hosting_pos = static_cast<size_t>(it->position());
            }
        }

        cached_result =
            (last_hosting == "clienthosted" || last_hosting == "hosted" || last_hosting == "listen") &&
            (last_remote_signal == std::string::npos || last_hosting_pos > last_remote_signal);
        return cached_result;
    }

    auto SignTextMod::set_sidecar_route(
        const std::filesystem::path& data_root,
        const std::string& runtime_role,
        const std::string& data_mode,
        const std::string& authority_mode,
        const std::string& sidecar_kind,
        bool authoritative,
        const std::filesystem::path& profile_root,
        const std::string& world_folder_id,
        const std::string& reason) -> void
    {
        if (data_root.empty())
        {
            return;
        }

        const bool localclient_authoritative_target =
            !is_dedicated_runtime_process() &&
            authoritative &&
            lower_ascii(runtime_role) == "localclient";
        if (localclient_authoritative_target &&
            m_worldid_bind_phase == WorldIdBindPhase::StableIdLatched &&
            is_hex_world_id(m_worldid_latched_id) &&
            is_hex_world_id(world_folder_id) &&
            lower_ascii(world_folder_id) != lower_ascii(m_worldid_latched_id))
        {
            log_line("[worldid] switch_blocked reason=existing_world_protection current=" +
                     m_worldid_latched_id + " incoming=" + world_folder_id);
            return;
        }

        const bool world_changed =
            !m_world_folder_id.empty() &&
            !world_folder_id.empty() &&
            m_world_folder_id != world_folder_id;
        if (world_changed)
        {
            reset_role_route_locks("world_change");
        }

        const auto next_sidecar_path = data_root / "SignTexts.json";
        const bool route_changed =
            m_sidecar_path.empty() ||
            normalized_path_for_compare(next_sidecar_path) != normalized_path_for_compare(m_sidecar_path);
        const bool mode_changed =
            m_runtime_role != runtime_role ||
            m_data_mode != data_mode ||
            m_authority_mode != authority_mode ||
            m_sidecar_kind != sidecar_kind ||
            m_sidecar_authoritative != authoritative ||
            m_save_profile_root != profile_root.string() ||
            m_world_folder_id != world_folder_id;

        if (m_role_lock_acquired && !world_changed)
        {
            const bool role_or_authority_change =
                m_runtime_role != runtime_role ||
                m_data_mode != data_mode ||
                m_authority_mode != authority_mode ||
                m_sidecar_kind != sidecar_kind ||
                m_sidecar_authoritative != authoritative;
            if (role_or_authority_change)
            {
                log_line("[role] lock_held skip_sidecar_route reason=" + reason +
                         " lockedRuntimeRole=" + m_runtime_role +
                         " incomingRuntimeRole=" + runtime_role +
                         " lockedAuthorityMode=" + m_authority_mode +
                         " incomingAuthorityMode=" + authority_mode);
                return;
            }
        }

        if (!route_changed && !mode_changed)
        {
            return;
        }

        const auto old_sidecar = m_sidecar_path.string();
        m_data_root = data_root;
        m_sidecar_path = next_sidecar_path;
        m_backup_root = m_data_root / "Backups";
        m_runtime_role = runtime_role;
        m_data_mode = data_mode;
        m_authority_mode = authority_mode;
        m_sidecar_kind = sidecar_kind;
        m_sidecar_authoritative = authoritative;
        m_save_profile_root = profile_root.string();
        m_world_folder_id = world_folder_id;

        std::error_code mkdir_ec{};
        std::filesystem::create_directories(m_data_root, mkdir_ec);
        std::error_code backup_mkdir_ec{};
        std::filesystem::create_directories(m_backup_root, backup_mkdir_ec);

        log_line("[role] sidecar_route reason=" + reason +
                 " runtimeRole=" + m_runtime_role +
                 " dataMode=" + m_data_mode +
                 " authorityMode=" + m_authority_mode +
                 " sidecarKind=" + m_sidecar_kind +
                 " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                 " profileRoot=" + (m_save_profile_root.empty() ? "none" : m_save_profile_root) +
                 " worldFolderId=" + m_world_folder_id +
                 " oldSidecar=" + (old_sidecar.empty() ? "none" : old_sidecar) +
                 " newSidecar=" + m_sidecar_path.string());

        if (mkdir_ec)
        {
            log_line("[save] failed to create routed data root path=" + m_data_root.string() + " error=" + mkdir_ec.message());
        }
        if (backup_mkdir_ec)
        {
            log_line("[save] failed to create routed backup root path=" + m_backup_root.string() + " error=" + backup_mkdir_ec.message());
        }

        if (route_changed)
        {
            m_live_label_actor_ptrs.clear();
            m_missing_label_scan_counts.clear();
            m_seen_live_label_keys.clear();
            m_rendered_text_cache.clear();
            m_component_name_cache.clear();
            m_phase4_next_retry.clear();
            m_restore_scan_has_seen_live_labels = false;
            m_dedicated_restore_active_since = {};
            m_dedicated_restore_stable_since = {};
            m_dedicated_last_probable_label_count = 0;
            m_prune_deferred_logged = false;
            m_recent_destroy_guid_signals.clear();
            m_recent_destroy_slot_confirmations.clear();
            m_destroy_signal_log_offset = 0;
            m_destroy_signal_log_initialized = false;
            m_destroy_signal_last_poll = {};
            m_hosted_ready_world_client_seen = false;
            m_hosted_ready_player_ready_seen = false;
            m_hosted_ready_datakeeper_seen = false;
            m_hosted_ready_hide_loading_seen = false;
            m_hosted_ready_sequence_complete = false;
            m_hosted_post_ready_reconcile_done = false;
            m_pending_world_inactive_ignored_logged = false;
            m_locked_world_inactive_ignored_logged = false;
            m_world_inactive_since = {};
            m_ready_baseline_live_keys.clear();
            m_ready_baseline_capture_remaining_scans = 0;
            m_restore_scan_cycle_counter = 0;
            reset_localclient_role_lock_restore_pass_state();
            reset_visual_verify_debug_state();
            reset_bridge_snapshot_state("sidecar_route_change");
            m_bridge_next_snapshot_request = std::chrono::steady_clock::now();
            load_sidecar_json();
        }
        configure_bridge_role("sidecar_route");
    }

    auto SignTextMod::configure_sidecar_for_actor(AActor* actor, const std::string& world_id) -> void
    {
        if (!actor)
        {
            return;
        }

        if (is_dedicated_server_process(std::filesystem::current_path(), m_mod_root))
        {
            return;
        }
        if (!m_session_ready_latched)
        {
            return;
        }

        auto* world = actor->GetWorld();
        if (!world)
        {
            return;
        }

        std::filesystem::path profile_root{};
        if (!m_save_profile_root.empty() && std::filesystem::exists(m_save_profile_root))
        {
            profile_root = std::filesystem::path{m_save_profile_root};
        }
        else
        {
            const auto profile_roots = collect_local_client_save_profile_roots();
            if (!profile_roots.empty())
            {
                profile_root = profile_roots.front();
            }
        }

        const bool local_authority = is_world_authoritative(world) || is_local_hosted_runtime();
        const auto safe_world_id = sanitize_path_segment(world_id.empty() ? std::string{"unknown-world"} : world_id);
        if (local_authority)
        {
            auto world_folder_id = m_worldid_latched_id;
            if (!is_hex_world_id(world_folder_id))
            {
                world_folder_id = m_world_folder_id;
            }
            if (!is_hex_world_id(world_folder_id))
            {
                if (!profile_root.empty())
                {
                    if (auto chosen = choose_world_id_for_profile(profile_root); chosen.has_value())
                    {
                        world_folder_id = *chosen;
                    }
                }
                if (!is_hex_world_id(world_folder_id))
                {
                    world_folder_id = safe_world_id;
                }
            }

            const auto data_root = !profile_root.empty()
                ? profile_root / "WindroseTextSigns" / world_folder_id
                : m_mod_root / "Cache" / "LocalAuthoritative" / world_folder_id;
            set_sidecar_route(
                data_root,
                "LocalClient",
                profile_root.empty() ? "LocalProfileAuthoritativeFallbackModRoot" : "LocalProfileAuthoritative",
                "LocalClientSoloOrHostedAuthoritative",
                profile_root.empty() ? "authoritative-fallback" : "authoritative",
                true,
                profile_root,
                world_folder_id,
                "world_authority_detected");
            return;
        }

        std::filesystem::path cache_base{};
        if (!profile_root.empty())
        {
            cache_base = profile_root / "WindroseTextSigns";
        }
        else
        {
            cache_base = m_mod_root / "Cache";
        }

        auto remote_world_id = safe_world_id;
        if (auto connected_island_id = try_latest_connected_island_id_from_local_log();
            connected_island_id.has_value() && is_hex_world_id(*connected_island_id))
        {
            remote_world_id = *connected_island_id;
        }

        set_sidecar_route(
            cache_base / "RemoteCache" / remote_world_id,
            "RemoteClient",
            "RemoteClientCache",
            "ServerAuthoritativePendingBridge",
            "cache",
            false,
            profile_root,
            remote_world_id,
            "world_authority_absent_remote_cache");
    }

    auto SignTextMod::active_storage_world_id(const std::string& actor_world_id) const -> std::string
    {
        if (!m_world_folder_id.empty() && m_world_folder_id != "unknown-world")
        {
            return m_world_folder_id;
        }
        return actor_world_id.empty() ? std::string{"unknown-world"} : actor_world_id;
    }

    auto SignTextMod::try_extract_guid_from_object(UObject* object) -> std::optional<std::string>
    {
        if (!object)
        {
            return std::nullopt;
        }

        std::optional<std::string> result{};
        for_each_property_in_chain_compat(object->GetClassPrivate(), [&](FProperty* prop) {
            if (result.has_value() || !prop)
            {
                return;
            }

            const auto prop_name = lower_ascii(RC::to_string(prop->GetName()));
            if (prop_name.find("guid") == std::string::npos && prop_name.find("id") == std::string::npos)
            {
                return;
            }

            if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject() && prop->GetSize() >= static_cast<int32_t>(sizeof(FGuid)))
            {
                if (auto* guid = prop->ContainerPtrToValuePtr<FGuid>(object); guid && guid->is_valid())
                {
                    result = to_hex_guid(*guid);
                }
            }
        });

        return result;
    }

    auto SignTextMod::try_extract_guid_from_params(UFunction* function, void* params) -> std::optional<std::string>
    {
        if (!function || !params)
        {
            return std::nullopt;
        }

        std::optional<std::string> result{};
        for_each_property_in_chain_compat(function, [&](FProperty* prop) {
            if (result.has_value() || !prop)
            {
                return;
            }

            const auto prop_name = lower_ascii(RC::to_string(prop->GetName()));
            if (prop_name.find("guid") == std::string::npos && prop_name.find("id") == std::string::npos)
            {
                return;
            }

            if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject() && prop->GetSize() >= static_cast<int32_t>(sizeof(FGuid)))
            {
                if (auto* guid = prop->ContainerPtrToValuePtr<FGuid>(params); guid && guid->is_valid())
                {
                    result = to_hex_guid(*guid);
                }
            }
        });
        return result;
    }

    auto SignTextMod::extract_stable_id(UObject* object) -> std::string
    {
        if (!object)
        {
            return "invalid-object";
        }

        const auto full_name = narrow_ascii(object->GetFullName());
        if (auto instance_token = try_extract_building_block_instance_token_from_name(full_name); instance_token.has_value())
        {
            return *instance_token;
        }

        if (auto guid = try_extract_guid_from_object(object); guid.has_value())
        {
            return *guid;
        }

        static const std::regex guid_rx(R"(([A-Fa-f0-9]{32}))");
        std::smatch match{};
        if (std::regex_search(full_name, match, guid_rx) && match.size() > 1)
        {
            auto value = match[1].str();
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return value;
        }

        const auto hash = std::hash<std::string>{}(full_name);
        std::ostringstream fallback{};
        fallback << "fallback-" << std::hex << hash;
        return fallback.str();
    }

    auto SignTextMod::build_world_id_for_actor(AActor* actor) const -> std::string
    {
        if (!actor)
        {
            return "unknown-world";
        }
        if (auto* world = actor->GetWorld())
        {
            if (!is_local_hosted_runtime())
            {
                if (auto connected_island_id = try_latest_connected_island_id_from_local_log();
                    connected_island_id.has_value() && is_hex_world_id(*connected_island_id))
                {
                    return *connected_island_id;
                }
            }
            const auto world_name = lower_ascii(narrow_ascii(world->GetName()));
            if (!world_name.empty())
            {
                return world_name;
            }
        }
        return "unknown-world";
    }

    auto SignTextMod::build_storage_key(const std::string& world_id, const std::string& stable_id) const -> std::string
    {
        return world_id + "/" + stable_id;
    }

    auto SignTextMod::try_select_label_from_camera() -> std::optional<SelectionCandidate>
    {
        auto* controller = try_get_primary_player_controller();
        if (!controller)
        {
            log_line("[target] No non-default PlayerController found");
            return std::nullopt;
        }

        const auto view = get_player_viewpoint_reflective(controller);
        if (!view.valid)
        {
            log_line("[target] GetPlayerViewPoint failed");
            return std::nullopt;
        }
        const auto controller_name = narrow_ascii(controller->GetFullName());

        const auto forward = vec_normalize(rotation_to_forward(view.rotation));
        AActor* controller_actor = Cast<AActor>(controller);
        UWorld* controller_world = controller_actor ? controller_actor->GetWorld() : nullptr;
        const auto controller_world_id = build_world_id_for_actor(controller_actor);
        SelectionCandidate best{};
        double best_dot = -1.0;
        double best_perp = 999999.0;
        double best_angle_deg = 999.0;
        size_t candidate_count = 0;
        size_t strict_candidate_count = 0;
        size_t loose_candidate_count = 0;
        size_t best_anchor_sample_count = 0;

        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->IsA(AActor::StaticClass()))
            {
                return LoopAction::Continue;
            }

            auto* actor = Cast<AActor>(object);
            if (!actor || !is_probable_label_actor(actor))
            {
                return LoopAction::Continue;
            }

            const auto actor_full_name = lower_ascii(narrow_ascii(actor->GetFullName()));
            if (actor_full_name.find("default__") != std::string::npos)
            {
                return LoopAction::Continue;
            }

            if (controller_world && actor->GetWorld() != controller_world)
            {
                return LoopAction::Continue;
            }

            const auto stable_id = extract_stable_id(actor);
            const auto actor_loc = actor->K2_GetActorLocation();

            const auto to_actor = vec_sub(actor_loc, view.location);
            const auto dist = vec_len(to_actor);
            if (dist <= 1.0 || dist > 8000.0)
            {
                return LoopAction::Continue;
            }
            const auto dir = vec_normalize(to_actor);
            const auto dot = vec_dot(forward, dir);
            const double forward_dist = dot * dist;
            if (forward_dist <= 0.0)
            {
                return LoopAction::Continue;
            }
            double perp_sq = (dist * dist) - (forward_dist * forward_dist);
            if (perp_sq < 0.0)
            {
                perp_sq = 0.0;
            }
            const double perp = std::sqrt(perp_sq);
            const double angle_rad = std::atan2(perp, forward_dist);
            const double angle_deg = angle_rad * (180.0 / 3.14159265358979323846);

            // Crosshair-first gating:
            // strict pass approximates center-dot hit; loose pass is a small recovery band.
            constexpr double k_strict_angle_deg = 1.5;
            constexpr double k_loose_angle_deg = 3.0;
            const bool strict_match = (angle_deg <= k_strict_angle_deg);
            const bool loose_close_match = (angle_deg <= k_loose_angle_deg) && (dist <= 700.0);
            if (!strict_match && !loose_close_match)
            {
                return LoopAction::Continue;
            }

            ++candidate_count;
            if (strict_match)
            {
                ++strict_candidate_count;
            }
            else
            {
                ++loose_candidate_count;
            }
            // Score by center-dot alignment first (smallest angle), then distance.
            const double score =
                (strict_match ? 2000000.0 : 0.0) -
                (angle_deg * 100000.0) -
                (dist * 6.0) -
                (perp * 25.0) +
                ((m_selected.has_value() && m_selected->stable_id == stable_id) ? 3000.0 : 0.0);
            if (!best.actor || score > best.score)
            {
                best.actor = actor;
                best.score = score;
                best.distance = dist;
                best.stable_id = stable_id;
                best.world_id = build_world_id_for_actor(actor);
                best.asset = detect_label_asset(actor);
                best_dot = dot;
                best_perp = perp;
                best_angle_deg = angle_deg;
                best_anchor_sample_count = 1;
            }

            return LoopAction::Continue;
        });

        if (!best.actor)
        {
            log_line("[target] hotkey selection found no Wooden Label candidate candidateCount=0 worldId=" + controller_world_id +
                     " controller=" + controller_name);
            return std::nullopt;
        }

        std::string component_summary{"none"};
        auto components = best.actor->GetComponentsByClass(UActorComponent::StaticClass());
        if (components.Num() > 0 && components[0])
        {
            component_summary = narrow_ascii(components[0]->GetFullName());
        }

        std::string outermost_summary{"none"};
        if (best.actor->GetOutermost())
        {
            outermost_summary = narrow_ascii(best.actor->GetOutermost()->GetFullName());
        }

        log_line("[target] selected actor=" + narrow_ascii(best.actor->GetFullName()) +
                 " class=" + narrow_ascii(best.actor->GetClassPrivate()->GetFullName()) +
                 " stableId=" + best.stable_id +
                 " worldId=" + best.world_id +
                 " mode=ranked" +
                 " dot=" + std::to_string(best_dot) +
                 " perp=" + std::to_string(best_perp) +
                 " angleDeg=" + std::to_string(best_angle_deg) +
                 " anchors=" + std::to_string(best_anchor_sample_count) +
                 " candidateCount=" + std::to_string(candidate_count) +
                 " strictCandidateCount=" + std::to_string(strict_candidate_count) +
                 " looseCandidateCount=" + std::to_string(loose_candidate_count) +
                 " controller=" + controller_name +
                 " firstComponent=" + component_summary +
                 " outermost=" + outermost_summary);
        return best;
    }

    auto SignTextMod::is_actor_pointer_live(AActor* actor) const -> bool
    {
        if (!actor)
        {
            return false;
        }

        bool found = false;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (object == actor)
            {
                found = true;
                return LoopAction::Break;
            }
            return LoopAction::Continue;
        });
        return found;
    }

    auto SignTextMod::is_actor_ready_for_restore_retry(AActor* actor, std::string* out_reason) -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };

        if (!actor || !is_actor_pointer_live(actor))
        {
            set_reason("actor_not_live");
            return false;
        }

        bool pending_kill = false;
        if (invoke_bool_return_no_param(
                actor,
                STR("IsPendingKill"),
                STR("/Script/CoreUObject.Object:IsPendingKill"),
                pending_kill) &&
            pending_kill)
        {
            set_reason("actor_pending_kill");
            return false;
        }
        bool pending_prop = false;
        if (get_bool_property_if_present(actor, "bpendingkill", pending_prop) && pending_prop)
        {
            set_reason("actor_pending_kill_prop");
            return false;
        }
        bool destroyed_prop = false;
        if (get_bool_property_if_present(actor, "bbeingdestroyed", destroyed_prop) && destroyed_prop)
        {
            set_reason("actor_being_destroyed");
            return false;
        }

        auto* world = actor->GetWorld();
        if (!world)
        {
            set_reason("actor_world_missing");
            return false;
        }
        const auto world_name = lower_ascii(narrow_ascii(world->GetName()));
        if (world_name.empty() ||
            world_name == "none" ||
            world_name.find("transition") != std::string::npos ||
            world_name.find("lobby") != std::string::npos ||
            world_name.find("entrance") != std::string::npos)
        {
            set_reason("actor_world_transitional");
            return false;
        }

        auto* root = get_object_property_if_present(actor, "RootComponent");
        if (!root)
        {
            set_reason("actor_root_missing");
            return false;
        }

        set_reason("ready");
        return true;
    }

    auto SignTextMod::ensure_selected_actor_valid(const std::string& reason) -> bool
    {
        if (!m_selected.has_value())
        {
            return false;
        }
        if (is_actor_pointer_live(m_selected->actor))
        {
            return true;
        }

        AActor* rebound_actor = nullptr;
        std::string rebound_world_id{};
        const auto target_stable_id = m_selected->stable_id;
        const auto selected_world_id = m_selected->world_id;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->IsA(AActor::StaticClass()))
            {
                return LoopAction::Continue;
            }
            auto* actor = Cast<AActor>(object);
            if (!actor || !is_probable_label_actor(actor))
            {
                return LoopAction::Continue;
            }
            const auto stable_id = extract_stable_id(actor);
            if (stable_id != target_stable_id)
            {
                return LoopAction::Continue;
            }
            const auto world_id = build_world_id_for_actor(actor);
            if (!selected_world_id.empty() && selected_world_id != "unknown-world" && world_id != selected_world_id)
            {
                return LoopAction::Continue;
            }
            rebound_actor = actor;
            rebound_world_id = world_id;
            return LoopAction::Break;
        });

        if (rebound_actor)
        {
            m_selected->actor = rebound_actor;
            m_selected->world_id = rebound_world_id;
            m_selected->asset = detect_label_asset(rebound_actor);
            if (auto* controller = try_get_primary_player_controller())
            {
                const auto view = get_player_viewpoint_reflective(controller);
                if (view.valid)
                {
                    const auto actor_loc = rebound_actor->K2_GetActorLocation();
                    m_selected->distance = vec_len(vec_sub(actor_loc, view.location));
                }
            }
            log_line("[target] selected_actor_rebound reason=" + reason +
                     " stableId=" + m_selected->stable_id +
                     " worldId=" + m_selected->world_id +
                     " actor=" + narrow_ascii(rebound_actor->GetFullName()));
            return true;
        }

        log_line("[target] selected_actor_invalid_cleared reason=" + reason +
                 " stableId=" + m_selected->stable_id +
                 " worldId=" + m_selected->world_id);
        m_selected.reset();
        m_ui_open = false;
        return false;
    }

    auto SignTextMod::tick_pending_hotkey() -> void
    {
        const bool new_request = m_hotkey_requested.exchange(false);
        const auto now = std::chrono::steady_clock::now();
        if (new_request)
        {
            // One keypress should survive transient viewpoint/controller hiccups.
            // 25 attempts at 60ms gives a ~1.5s window. The previous 8-attempt budget
            // (~480ms) was too tight on machines with slow F8 latency: users reported
            // having to press F8 multiple times because the controller/camera resolve
            // didn't finish inside the retry window.
            m_hotkey_retry_remaining = 25;
            m_hotkey_retry_next = now;
            if (m_f8_latency_breakdown_enabled && !m_f8_latency_trace.active)
            {
                m_f8_latency_trace.active = true;
                m_f8_latency_trace.target_seen = false;
                m_f8_latency_trace.construct_seen = false;
                m_f8_latency_trace.edge = now;
                m_f8_latency_trace.target = {};
                m_f8_latency_trace.construct = {};
                ++m_f8_latency_trace.press_id;
            }
        }

        if (m_hotkey_retry_remaining == 0)
        {
            return;
        }
        if (now < m_hotkey_retry_next)
        {
            return;
        }
        m_hotkey_retry_next = now + std::chrono::milliseconds(60);
        if (m_hotkey_retry_remaining > 0)
        {
            --m_hotkey_retry_remaining;
        }

        auto selected = try_select_label_from_camera();
        if (!selected.has_value())
        {
            if (m_hotkey_retry_remaining == 0)
            {
                log_line("[target] hotkey selection retries_exhausted");
                if (m_f8_latency_breakdown_enabled && m_f8_latency_trace.active)
                {
                    const auto now_fail = std::chrono::steady_clock::now();
                    const long long edge_to_fail_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(now_fail - m_f8_latency_trace.edge).count();
                    log_line("[input-latency] f8_open_failed pressId=" + std::to_string(m_f8_latency_trace.press_id) +
                             " stage=target edge_to_failure_ms=" + std::to_string(edge_to_fail_ms));
                    m_f8_latency_trace.active = false;
                }
                m_ui_open = m_phase7_imgui_fallback_enabled;
            }
            return;
        }

        m_hotkey_retry_remaining = 0;
        if (m_f8_latency_breakdown_enabled && m_f8_latency_trace.active && !m_f8_latency_trace.target_seen)
        {
            m_f8_latency_trace.target = now;
            m_f8_latency_trace.target_seen = true;
        }
        m_selected = selected;
        m_selected->world_id = selected->world_id;
        const auto actor_world_id = m_selected->world_id.empty() ? build_world_id_for_actor(m_selected->actor) : m_selected->world_id;
        configure_sidecar_for_actor(m_selected->actor, actor_world_id);
        const auto world_id = active_storage_world_id(actor_world_id);
        const auto key = build_storage_key(world_id, selected->stable_id);
        if (const auto found = m_labels.find(key); found != m_labels.end())
        {
            std::snprintf(m_text_buffer.data(), m_text_buffer.size(), "%s", found->second.text.c_str());
        }
        else
        {
            m_text_buffer.fill('\0');
        }
        m_text_buffer_bound_key = key;

        const bool umg_opened = open_phase7_umg_editor_for_selection();
        if (umg_opened)
        {
            m_ui_open = false;
            return;
        }

        const bool native_opened = open_phase7_native_editor_for_selection();
        if (native_opened)
        {
            m_ui_open = false;
            return;
        }

        m_ui_open = m_phase7_imgui_fallback_enabled;
        log_line("[phase7] hotkey fallback_to_imgui=" + std::string{m_phase7_imgui_fallback_enabled ? "true" : "false"} +
                 " nativeSupported=" + std::string{m_phase7_native_supported ? "true" : "false"} +
                 " umgOpened=" + std::string{umg_opened ? "true" : "false"} +
                 " win32Default=false");
        if (m_f8_latency_breakdown_enabled && m_f8_latency_trace.active)
        {
            const auto now_fail = std::chrono::steady_clock::now();
            const long long edge_to_fail_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now_fail - m_f8_latency_trace.edge).count();
            log_line("[input-latency] f8_open_failed pressId=" + std::to_string(m_f8_latency_trace.press_id) +
                     " stage=editor_open edge_to_failure_ms=" + std::to_string(edge_to_fail_ms));
            m_f8_latency_trace.active = false;
        }
    }

    auto SignTextMod::ensure_selected_label_for_action(const std::string& action_name) -> bool
    {
        if (m_selected.has_value())
        {
            if (ensure_selected_actor_valid(action_name))
            {
                return true;
            }
        }

        auto selected = try_select_label_from_camera();
        if (!selected.has_value())
        {
            log_line("[input] " + action_name + " failed: no selected label and camera selection failed");
            return false;
        }

        m_selected = selected;
        m_selected->world_id = selected->world_id;
        {
            const auto actor_world_id = m_selected->world_id.empty() ? build_world_id_for_actor(m_selected->actor) : m_selected->world_id;
            configure_sidecar_for_actor(m_selected->actor, actor_world_id);
            const auto key = build_storage_key(active_storage_world_id(actor_world_id), m_selected->stable_id);
            if (const auto found = m_labels.find(key); found != m_labels.end())
            {
                std::snprintf(m_text_buffer.data(), m_text_buffer.size(), "%s", found->second.text.c_str());
            }
            else
            {
                m_text_buffer.fill('\0');
            }
            m_text_buffer_bound_key = key;
        }
        log_line("[input] " + action_name + " auto-selected stableId=" + m_selected->stable_id +
                 " actor=" + narrow_ascii(m_selected->actor->GetFullName()));
        return true;
    }

    auto SignTextMod::tick_pending_fallback_hotkeys() -> void
    {
        if (m_native_transport_inventory_requested.exchange(false))
        {
            run_native_transport_inventory_probe("request");
        }
    }

    auto SignTextMod::run_native_transport_inventory_probe(const std::string& reason) -> void
    {
        m_native_transport_inventory_probe_ran = true;

        uint32_t max_logged = 120;
        uint32_t max_specific_logged = 200;
        try
        {
            const auto configured = std::stoul(config_string_value("WTS_NATIVE_TRANSPORT_INVENTORY_MAX", "120"));
            max_logged = static_cast<uint32_t>(std::clamp<unsigned long>(configured, 10UL, 500UL));
        }
        catch (...)
        {
            max_logged = 120;
        }
        try
        {
            const auto configured = std::stoul(config_string_value("WTS_NATIVE_TRANSPORT_MARKER_INVENTORY_MAX", "200"));
            max_specific_logged = static_cast<uint32_t>(std::clamp<unsigned long>(configured, 10UL, 1000UL));
        }
        catch (...)
        {
            max_specific_logged = 200;
        }

        struct CandidateRow
        {
            UFunction* function{};
            std::string full_name{};
            std::string class_name{};
            std::string outer_name{};
            std::string flags{};
            int score{0};
            uint32_t param_count{0};
        };
        struct ObjectCandidateRow
        {
            std::string full_name{};
            std::string class_name{};
            std::string outer_name{};
        };

        std::vector<CandidateRow> rows{};
        rows.reserve(max_logged + 64);
        std::vector<CandidateRow> specific_rows{};
        specific_rows.reserve(max_specific_logged + 64);
        std::vector<ObjectCandidateRow> specific_objects{};
        specific_objects.reserve(max_specific_logged + 64);
        uint32_t scanned_functions = 0;
        uint32_t net_functions = 0;
        uint32_t specific_functions_seen = 0;
        uint32_t specific_objects_seen = 0;

        log_line("[native-transport-probe] start reason=" + reason +
                 " maxLogged=" + std::to_string(max_logged) +
                 " maxSpecificLogged=" + std::to_string(max_specific_logged) +
                 " passive=true");

        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->IsA(UFunction::StaticClass()))
            {
                return LoopAction::Continue;
            }

            auto* function = Cast<UFunction>(object);
            if (!function)
            {
                return LoopAction::Continue;
            }

            ++scanned_functions;
            const auto full_name = narrow_ascii(function->GetFullName());
            const auto lower_full_name = lower_ascii(full_name);
            const auto flags = function_flag_summary(function);
            const bool is_net_function = flags.find("Net") != std::string::npos ||
                flags.find("Server") != std::string::npos ||
                flags.find("Client") != std::string::npos ||
                flags.find("Multicast") != std::string::npos;
            if (is_net_function)
            {
                ++net_functions;
            }

            const auto score = score_native_transport_candidate(lower_full_name, function);
            const bool name_interesting = contains_any_token(lower_full_name, {
                "usermarker",
                "mapmarker",
                "marker",
                "ping",
                "chat",
                "message",
                "playerinworld",
                "player",
                "session",
                "account",
                "r5bl",
                "businessrule",
                "server",
                "client",
                "multicast",
                "replicate",
                "rpc",
                "r5p2p",
                "p2pgate",
                "coturn",
                "stun",
                "iceprotocol",
                "r5socket",
                "socketsubsystem",
                "netdriver",
                "netconnection",
                "ipconnection",
                "remotaddr",
                "remoteaddr",
                "sendto",
                "recvfrom",
                "datagram"});
            const bool marker_or_rule_specific = contains_any_token(lower_full_name, {
                "r5blplayerinworld",
                "playerinworld",
                "usermarker",
                "user_marker",
                "mapmarker",
                "map_marker",
                "mapcontroller",
                "markermodel",
                "r5netbl",
                "rulerequest",
                "rule_request",
                "businessrule",
                "r5p2p",
                "p2pgate",
                "coturn",
                "stun",
                "iceprotocol",
                "r5socket",
                "socketsubsystem",
                "netdriver",
                "netconnection",
                "ipconnection",
                "remoteaddr"});

            if (marker_or_rule_specific)
            {
                ++specific_functions_seen;
            }

            if (score < 10 && !is_net_function && !name_interesting)
            {
                return LoopAction::Continue;
            }

            uint32_t param_count = 0;
            for_each_property_in_chain_compat(function, [&](FProperty* prop) {
                if (prop && prop->HasAnyPropertyFlags(CPF_Parm))
                {
                    ++param_count;
                }
            });

            const auto class_name = function->GetClassPrivate()
                ? narrow_ascii(function->GetClassPrivate()->GetFullName())
                : std::string{"unknown"};
            const auto outer_name = function->GetOuterPrivate()
                ? narrow_ascii(function->GetOuterPrivate()->GetFullName())
                : std::string{"none"};

            rows.push_back(CandidateRow{
                function,
                full_name,
                class_name,
                outer_name,
                flags,
                score,
                param_count});
            if (marker_or_rule_specific)
            {
                specific_rows.push_back(CandidateRow{
                    function,
                    full_name,
                    class_name,
                    outer_name,
                    flags,
                    score,
                    param_count});
            }

            return LoopAction::Continue;
        });

        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || object->IsA(UFunction::StaticClass()))
            {
                return LoopAction::Continue;
            }

            const auto full_name = narrow_ascii(object->GetFullName());
            const auto class_name = object->GetClassPrivate()
                ? narrow_ascii(object->GetClassPrivate()->GetFullName())
                : std::string{"unknown"};
            const auto outer_name = object->GetOuterPrivate()
                ? narrow_ascii(object->GetOuterPrivate()->GetFullName())
                : std::string{"none"};
            const auto haystack = lower_ascii(full_name + " " + class_name + " " + outer_name);
            if (!contains_any_token(haystack, {
                    "r5blplayerinworld",
                    "playerinworld",
                    "usermarker",
                    "user_marker",
                    "mapmarker",
                    "map_marker",
                    "mapcontroller",
                    "markermodel",
                    "r5netbl",
                    "rulerequest",
                    "rule_request",
                    "businessrule",
                    "r5p2p",
                    "p2pgate",
                    "coturn",
                    "stun",
                    "iceprotocol",
                    "r5socket",
                    "socketsubsystem",
                    "netdriver",
                    "netconnection",
                    "ipconnection",
                    "remoteaddr"}))
            {
                return LoopAction::Continue;
            }

            ++specific_objects_seen;
            if (specific_objects.size() < max_specific_logged)
            {
                specific_objects.push_back(ObjectCandidateRow{
                    full_name,
                    class_name,
                    outer_name});
            }
            return LoopAction::Continue;
        });

        std::sort(rows.begin(), rows.end(), [](const CandidateRow& a, const CandidateRow& b) {
            if (a.score != b.score)
            {
                return a.score > b.score;
            }
            return a.full_name < b.full_name;
        });
        std::sort(specific_rows.begin(), specific_rows.end(), [](const CandidateRow& a, const CandidateRow& b) {
            if (a.score != b.score)
            {
                return a.score > b.score;
            }
            return a.full_name < b.full_name;
        });
        std::sort(specific_objects.begin(), specific_objects.end(), [](const ObjectCandidateRow& a, const ObjectCandidateRow& b) {
            return a.full_name < b.full_name;
        });

        log_line("[native-transport-probe] summary scannedFunctions=" + std::to_string(scanned_functions) +
                 " netFunctions=" + std::to_string(net_functions) +
                 " matched=" + std::to_string(rows.size()) +
                 " logging=" + std::to_string(std::min<uint32_t>(max_logged, static_cast<uint32_t>(rows.size()))));
        log_line("[native-transport-probe-specific] summary functionsSeen=" + std::to_string(specific_functions_seen) +
                 " functionsMatched=" + std::to_string(specific_rows.size()) +
                 " objectsSeen=" + std::to_string(specific_objects_seen) +
                 " functionLogging=" +
                 std::to_string(std::min<uint32_t>(max_specific_logged, static_cast<uint32_t>(specific_rows.size()))) +
                 " objectLogging=" +
                 std::to_string(std::min<uint32_t>(max_specific_logged, static_cast<uint32_t>(specific_objects.size()))));

        uint32_t logged = 0;
        for (const auto& row : rows)
        {
            if (!row.function || logged >= max_logged)
            {
                break;
            }
            ++logged;

            log_line("[native-transport-probe] function index=" + std::to_string(logged) +
                     " score=" + std::to_string(row.score) +
                     " params=" + std::to_string(row.param_count) +
                     " flags=" + row.flags +
                     " path=" + row.full_name +
                     " class=" + row.class_name +
                     " outer=" + row.outer_name);

            uint32_t param_index = 0;
            for_each_property_in_chain_compat(row.function, [&](FProperty* prop) {
                if (!prop || !prop->HasAnyPropertyFlags(CPF_Parm))
                {
                    return;
                }
                ++param_index;
                const auto prop_name = RC::to_string(prop->GetName());
                log_line("[native-transport-probe] param functionIndex=" + std::to_string(logged) +
                         " paramIndex=" + std::to_string(param_index) +
                         " name=" + prop_name +
                         " class=" + RC::to_string(prop->GetClass().GetName()) +
                         " size=" + std::to_string(prop->GetSize()) +
                         " flags=" + property_flag_summary(prop));
            });
        }

        uint32_t specific_logged = 0;
        for (const auto& row : specific_rows)
        {
            if (!row.function || specific_logged >= max_specific_logged)
            {
                break;
            }
            ++specific_logged;
            log_line("[native-transport-probe-specific] function index=" + std::to_string(specific_logged) +
                     " score=" + std::to_string(row.score) +
                     " params=" + std::to_string(row.param_count) +
                     " flags=" + row.flags +
                     " path=" + row.full_name +
                     " class=" + row.class_name +
                     " outer=" + row.outer_name);

            uint32_t param_index = 0;
            for_each_property_in_chain_compat(row.function, [&](FProperty* prop) {
                if (!prop || !prop->HasAnyPropertyFlags(CPF_Parm))
                {
                    return;
                }
                ++param_index;
                log_line("[native-transport-probe-specific] param functionIndex=" + std::to_string(specific_logged) +
                         " paramIndex=" + std::to_string(param_index) +
                         " name=" + RC::to_string(prop->GetName()) +
                         " class=" + RC::to_string(prop->GetClass().GetName()) +
                         " size=" + std::to_string(prop->GetSize()) +
                         " flags=" + property_flag_summary(prop));
            });
        }

        uint32_t specific_object_logged = 0;
        for (const auto& row : specific_objects)
        {
            if (specific_object_logged >= max_specific_logged)
            {
                break;
            }
            ++specific_object_logged;
            log_line("[native-transport-probe-specific] object index=" + std::to_string(specific_object_logged) +
                     " path=" + row.full_name +
                     " class=" + row.class_name +
                     " outer=" + row.outer_name);
        }

        log_line("[native-transport-probe] complete logged=" + std::to_string(logged));
    }
    auto SignTextMod::tick_file_triggers() -> void
    {
        const auto native_transport_trigger_path = m_mod_root / "Config" / "run_native_transport_inventory.flag";
        if (std::filesystem::exists(native_transport_trigger_path))
        {
            log_line("[native-transport-probe] trigger file detected path=" + native_transport_trigger_path.string());
            m_native_transport_inventory_requested.store(true);

            std::error_code remove_ec{};
            std::filesystem::remove(native_transport_trigger_path, remove_ec);
            if (remove_ec)
            {
                log_line("[native-transport-probe] trigger file remove failed path=" +
                         native_transport_trigger_path.string() + " error=" + remove_ec.message());
            }
        }
    }

    auto SignTextMod::escape_json(std::string_view s) const -> std::string
    {
        std::string out{};
        out.reserve(s.size() + 16);
        for (const auto c : s)
        {
            switch (c)
            {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
            }
        }
        return out;
    }

    auto SignTextMod::unescape_json(std::string_view s) const -> std::string
    {
        std::string out{};
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && (i + 1) < s.size())
            {
                const char next = s[i + 1];
                if (next == '\\' || next == '"') { out.push_back(next); ++i; continue; }
                if (next == 'n') { out.push_back('\n'); ++i; continue; }
                if (next == 'r') { out.push_back('\r'); ++i; continue; }
                if (next == 't') { out.push_back('\t'); ++i; continue; }
            }
            out.push_back(s[i]);
        }
        return out;
    }

    auto SignTextMod::load_sidecar_json() -> void
    {
        log_line("[save] load_start path=" + m_sidecar_path.string());
        const auto backup_path = m_sidecar_path.string() + ".bak";
        const auto temp_path = m_sidecar_path.string() + ".tmp";

        std::vector<std::filesystem::path> candidates = {
            m_sidecar_path,
            std::filesystem::path(backup_path),
            std::filesystem::path(temp_path)};
        if (std::filesystem::exists(m_backup_root))
        {
            std::vector<std::filesystem::directory_entry> snapshots{};
            std::error_code iter_ec{};
            for (const auto& entry : std::filesystem::directory_iterator(m_backup_root, iter_ec))
            {
                if (iter_ec || !entry.is_regular_file())
                {
                    continue;
                }
                const auto name = lower_ascii(entry.path().filename().string());
                if (name.size() >= 5 &&
                    name.find("signtexts.backup_") == 0 &&
                    name.rfind(".json") == (name.size() - 5))
                {
                    snapshots.push_back(entry);
                }
            }
            std::sort(snapshots.begin(), snapshots.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.last_write_time() > rhs.last_write_time();
            });
            for (size_t i = 0; i < snapshots.size() && i < 5; ++i)
            {
                candidates.push_back(snapshots[i].path());
            }
        }

        const std::regex row_rx(
            R"__RX__("([^"]+)"\s*:\s*\{\s*"text"\s*:\s*"((?:\\.|[^"\\])*)"\s*,\s*"asset"\s*:\s*"((?:\\.|[^"\\])*)"(?:\s*,\s*"kind"\s*:\s*"((?:\\.|[^"\\])*)")?(?:\s*,\s*"backingAsset"\s*:\s*"((?:\\.|[^"\\])*)")?(?:\s*,\s*"surfaceAxis"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"surfaceSign"\s*:\s*(-?1))?(?:\s*,\s*"depthOffset"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"alignX"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"alignY"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"fontSize"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"colorR"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"colorG"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"colorB"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"colorA"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"lastSeen"\s*:\s*"((?:\\.|[^"\\])*)")?\s*\})__RX__");

        struct ParsedCandidate
        {
            std::filesystem::path path{};
            std::unordered_map<std::string, LabelRecord> labels{};
            size_t parsed_rows{0};
            uint64_t revision{0};
        };

        std::vector<ParsedCandidate> parsed_candidates{};
        std::string content{};
        for (const auto& path : candidates)
        {
            if (!std::filesystem::exists(path))
            {
                continue;
            }
            if (!read_text_file(path, content))
            {
                continue;
            }

            const bool has_labels_node = (content.find("\"labels\"") != std::string::npos);
            if (!has_labels_node)
            {
                log_line("[save] load_candidate_rejected invalid_json_shape path=" + path.string());
                continue;
            }

            ParsedCandidate parsed{};
            parsed.path = path;
            if (std::smatch revision_match{}; std::regex_search(content, revision_match, std::regex{"\\\"revision\\\"\\s*:\\s*([0-9]+)"}) && revision_match.size() > 1)
            {
                try
                {
                    parsed.revision = static_cast<uint64_t>(std::stoull(revision_match[1].str()));
                }
                catch (...)
                {
                    parsed.revision = 0;
                }
            }
            size_t parsed_rows = 0;
            for (std::sregex_iterator it(content.begin(), content.end(), row_rx), end; it != end; ++it)
            {
                try
                {
                    const auto key = (*it)[1].str();
                    auto slash = key.find('/');

                    LabelRecord rec{};
                    rec.world_id = (slash == std::string::npos) ? "unknown-world" : key.substr(0, slash);
                    rec.stable_id = (slash == std::string::npos) ? key : key.substr(slash + 1);
                    rec.text = unescape_json((*it)[2].str());
                    rec.asset = unescape_json((*it)[3].str());
                    rec.kind = ((*it)[4].matched) ? unescape_json((*it)[4].str()) : std::string{"LabelText"};
                    rec.backing_asset = ((*it)[5].matched) ? unescape_json((*it)[5].str()) : std::string{"DA_BI_Utilities_Lables_Wooden_Ship"};
                    rec.surface_axis = ((*it)[6].matched) ? std::clamp(safe_stof((*it)[6].str(), 0.0f), 0.0f, 1.0f) : 0.0f;
                    rec.surface_sign = ((*it)[7].matched && safe_stoi((*it)[7].str(), 1) < 0) ? -1 : 1;
                    rec.depth_offset = ((*it)[8].matched) ? safe_stof((*it)[8].str(), 12.0f) : 12.0f;
                    rec.align_x = ((*it)[9].matched) ? safe_stof((*it)[9].str(), 0.0f) : 0.0f;
                    rec.align_y = ((*it)[10].matched) ? safe_stof((*it)[10].str(), 1.5f) : 1.5f;
                    rec.font_size = ((*it)[11].matched) ? std::max(1.0f, safe_stof((*it)[11].str(), 18.0f)) : 18.0f;
                    rec.color_r = ((*it)[12].matched) ? std::clamp(safe_stof((*it)[12].str(), 0.393822f), 0.0f, 1.0f) : 0.393822f;
                    rec.color_g = ((*it)[13].matched) ? std::clamp(safe_stof((*it)[13].str(), 0.393822f), 0.0f, 1.0f) : 0.393822f;
                    rec.color_b = ((*it)[14].matched) ? std::clamp(safe_stof((*it)[14].str(), 0.393822f), 0.0f, 1.0f) : 0.393822f;
                    rec.color_a = ((*it)[15].matched) ? std::clamp(safe_stof((*it)[15].str(), 1.0f), 0.0f, 1.0f) : 1.0f;
                    rec.last_seen_utc = ((*it)[16].matched) ? unescape_json((*it)[16].str()) : std::string{};
                    if (m_sidecar_authoritative &&
                        is_hex_world_id(m_world_folder_id) &&
                        rec.world_id != m_world_folder_id)
                    {
                        log_line("[save] load_record_skipped key=" + key +
                                 " reason=world_mismatch expectedWorld=" + m_world_folder_id +
                                 " recordWorld=" + rec.world_id +
                                 " path=" + path.string());
                        continue;
                    }
                    parsed.labels.emplace(key, std::move(rec));
                    ++parsed_rows;
                }
                catch (...)
                {
                    // Skip malformed row, continue parsing others.
                }
            }
            parsed.parsed_rows = parsed_rows;
            parsed_candidates.push_back(std::move(parsed));
        }

        if (parsed_candidates.empty())
        {
            log_line("[save] sidecar missing/unreadable across primary+backup+snapshots path=" + m_sidecar_path.string() + ", starting empty");
            m_labels.clear();
            return;
        }

        ParsedCandidate* primary_candidate = nullptr;
        for (auto& candidate : parsed_candidates)
        {
            if (candidate.path == m_sidecar_path)
            {
                primary_candidate = &candidate;
                break;
            }
        }

        ParsedCandidate* chosen = nullptr;
        bool restored_from_backup = false;

        if (primary_candidate && !primary_candidate->labels.empty())
        {
            chosen = primary_candidate;
        }
        else if (primary_candidate && primary_candidate->labels.empty())
        {
            for (auto& candidate : parsed_candidates)
            {
                if (candidate.path == m_sidecar_path)
                {
                    continue;
                }
                if (!candidate.labels.empty())
                {
                    chosen = &candidate;
                    restored_from_backup = true;
                    break;
                }
            }
            if (!chosen)
            {
                chosen = primary_candidate;
            }
        }
        else
        {
            for (auto& candidate : parsed_candidates)
            {
                if (!candidate.labels.empty())
                {
                    chosen = &candidate;
                    break;
                }
            }
            if (!chosen)
            {
                chosen = &parsed_candidates.front();
            }
        }

        if (!chosen)
        {
            log_line("[save] load_failed no_candidate_chosen path=" + m_sidecar_path.string());
            m_labels.clear();
            return;
        }

        m_labels = std::move(chosen->labels);
        m_revision = chosen->revision;
        for (const auto& [k, rec] : m_labels)
        {
            log_line("[save] load_record key=" + k + " stableId=" + rec.stable_id +
                     " worldId=" + rec.world_id + " path=" + chosen->path.string());
        }
        log_line("[save] load_done records=" + std::to_string(m_labels.size()) +
                 " parsedRows=" + std::to_string(chosen->parsed_rows) +
                 " revision=" + std::to_string(m_revision) +
                 " path=" + chosen->path.string());

        if (restored_from_backup)
        {
            log_line("[save] auto_restore_from_backup source=" + chosen->path.string() +
                     " target=" + m_sidecar_path.string() +
                     " restoredRecords=" + std::to_string(m_labels.size()));
            save_sidecar_json("auto_restore_from_backup", "auto-restore", "auto-restore", "auto-restore");
        }
    }

    auto SignTextMod::sidecar_record_count_on_disk() const -> std::optional<size_t>
    {
        std::string content{};
        if (!std::filesystem::exists(m_sidecar_path) || !read_text_file(m_sidecar_path, content))
        {
            return std::nullopt;
        }
        if (std::count(content.begin(), content.end(), '{') <= 0)
        {
            return 0;
        }
        const std::regex key_rx{"\"[^\"\\r\\n]+/BuildingBlock\\|"};
        return static_cast<size_t>(std::distance(std::sregex_iterator(content.begin(), content.end(), key_rx), std::sregex_iterator{}));
    }

    auto SignTextMod::is_authoritative_write_allowed() const -> bool
    {
        if (!m_sidecar_authoritative)
        {
            return false;
        }
        const auto role = lower_ascii(m_runtime_role);
        if (role.find("remote") != std::string::npos || role.find("pending") != std::string::npos || role.find("unknown") != std::string::npos)
        {
            return false;
        }
        const auto mode = lower_ascii(m_authority_mode);
        if (mode.find("pending") != std::string::npos)
        {
            return false;
        }
        const auto path = normalized_path_for_compare(m_sidecar_path);
        return path.find("\\saveprofiles\\") != std::string::npos &&
            path.find("\\remotecache\\") == std::string::npos &&
            path.find("\\startupcache\\") == std::string::npos;
    }

    auto SignTextMod::is_cache_path_allowed() const -> bool
    {
        const auto path = normalized_path_for_compare(m_sidecar_path);
        return path.find("\\remotecache\\") != std::string::npos ||
            path.find("\\startupcache\\") != std::string::npos ||
            path.find("\\moddata\\") != std::string::npos ||
            path.find("\\cache\\") != std::string::npos;
    }

    auto SignTextMod::save_sidecar_json(const std::string& reason, const std::string& key, const std::string& stable_id, const std::string& world_id) -> void
    {
        if (m_sidecar_authoritative && !is_authoritative_write_allowed())
        {
            log_line("[guardrail] write_refused reason=" + reason +
                     " key=" + key +
                     " cause=authoritative_role_or_path_not_confirmed runtimeRole=" + m_runtime_role +
                     " authorityMode=" + m_authority_mode +
                     " path=" + m_sidecar_path.string());
            return;
        }
        if (!m_sidecar_authoritative && !is_cache_path_allowed())
        {
            log_line("[guardrail] write_refused reason=" + reason +
                     " key=" + key +
                     " cause=cache_path_not_confirmed runtimeRole=" + m_runtime_role +
                     " sidecarKind=" + m_sidecar_kind +
                     " path=" + m_sidecar_path.string());
            return;
        }
        const bool explicit_empty_allowed =
            reason == "clear" ||
            reason.find("prune_") == 0 ||
            reason == "bridge_snapshot_reconcile";
        if (const auto disk_count = sidecar_record_count_on_disk(); disk_count.has_value() && *disk_count > 0 && m_labels.empty() && !explicit_empty_allowed)
        {
            log_line("[guardrail] write_refused reason=" + reason +
                     " key=" + key +
                     " cause=nonempty_file_to_empty_memory diskRecords=" + std::to_string(*disk_count) +
                     " path=" + m_sidecar_path.string());
            return;
        }

        if (m_sidecar_authoritative)
        {
            ++m_revision;
        }

        std::error_code ec{};
        std::filesystem::create_directories(m_sidecar_path.parent_path(), ec);
        if (ec)
        {
            log_line("[save] failed to ensure sidecar dir reason=" + reason +
                     " key=" + key + " stableId=" + stable_id + " worldId=" + world_id +
                     " path=" + m_sidecar_path.string() + " error=" + ec.message());
            return;
        }

        std::ostringstream payload{};
        payload << "{\n";
        payload << "  \"version\": 2,\n";
        payload << "  \"schema\": \"WindroseTextSigns.SignTexts.v2\",\n";
        payload << "  \"runtimeRole\": \"" << escape_json(m_runtime_role) << "\",\n";
        payload << "  \"dataMode\": \"" << escape_json(m_data_mode) << "\",\n";
        payload << "  \"authorityMode\": \"" << escape_json(m_authority_mode) << "\",\n";
        payload << "  \"authority\": \"" << escape_json(m_sidecar_authoritative ? "authoritative" : "cache") << "\",\n";
        payload << "  \"sidecarKind\": \"" << escape_json(m_sidecar_kind) << "\",\n";
        payload << "  \"authoritative\": " << (m_sidecar_authoritative ? "true" : "false") << ",\n";
        payload << "  \"revision\": " << m_revision << ",\n";
        payload << "  \"profileRoot\": \"" << escape_json(m_save_profile_root) << "\",\n";
        payload << "  \"worldIslandId\": \"" << escape_json(m_world_folder_id) << "\",\n";
        payload << "  \"worldFolderId\": \"" << escape_json(m_world_folder_id) << "\",\n";
        payload << "  \"writerSessionId\": \"" << escape_json(m_session_id) << "\",\n";
        payload << "  \"writerProcessKind\": \"" << escape_json(m_runtime_role) << "\",\n";
        payload << "  \"lastWriteUtc\": \"" << escape_json(now_utc()) << "\",\n";
        payload << "  \"labels\": {\n";
        bool first = true;
        for (const auto& [key, rec] : m_labels)
        {
            if (!first) { payload << ",\n"; }
            first = false;
            std::ostringstream axis_value{};
            axis_value << std::fixed << std::setprecision(2) << std::clamp(rec.surface_axis, 0.0f, 1.0f);
            const auto record_kind = rec.kind.empty() ? infer_new_record_kind_from_asset(rec.asset) : rec.kind;
            const auto record_backing_asset = rec.backing_asset.empty()
                ? infer_backing_asset_from_kind(record_kind, rec.asset)
                : rec.backing_asset;

            payload << "    \"" << escape_json(key) << "\": { \"text\": \"" << escape_json(rec.text)
                << "\", \"asset\": \"" << escape_json(rec.asset)
                << "\", \"kind\": \"" << escape_json(record_kind)
                << "\", \"backingAsset\": \"" << escape_json(record_backing_asset)
                << "\", \"surfaceAxis\": " << axis_value.str()
                << ", \"surfaceSign\": " << rec.surface_sign
                << ", \"depthOffset\": " << rec.depth_offset
                << ", \"alignX\": " << rec.align_x
                << ", \"alignY\": " << rec.align_y
                << ", \"fontSize\": " << rec.font_size
                << ", \"colorR\": " << rec.color_r
                << ", \"colorG\": " << rec.color_g
                << ", \"colorB\": " << rec.color_b
                << ", \"colorA\": " << rec.color_a
                << ", \"lastSeen\": \"" << escape_json(rec.last_seen_utc) << "\" }";
        }
        payload << "\n  }\n";
        payload << "}\n";

        const auto sidecar_tmp_path = std::filesystem::path(m_sidecar_path.string() + ".tmp");
        const auto sidecar_bak_path = std::filesystem::path(m_sidecar_path.string() + ".bak");

        const auto payload_str = payload.str();

        std::ofstream out(sidecar_tmp_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!out.is_open())
        {
            log_line("[save] failed to open sidecar tmp for write reason=" + reason +
                     " key=" + key + " stableId=" + stable_id + " worldId=" + world_id +
                     " path=" + sidecar_tmp_path.string());
            return;
        }
        out << payload_str;
        out.flush();
        out.close();

        if (std::filesystem::exists(m_sidecar_path))
        {
            std::error_code copy_ec{};
            std::filesystem::copy_file(m_sidecar_path, sidecar_bak_path, std::filesystem::copy_options::overwrite_existing, copy_ec);
            if (copy_ec)
            {
                log_line("[save] warning backup_copy_failed path=" + sidecar_bak_path.string() + " error=" + copy_ec.message());
            }
        }

        std::error_code rename_ec{};
        std::filesystem::rename(sidecar_tmp_path, m_sidecar_path, rename_ec);
        if (rename_ec)
        {
            std::error_code remove_ec{};
            std::filesystem::remove(m_sidecar_path, remove_ec);
            rename_ec.clear();
            std::filesystem::rename(sidecar_tmp_path, m_sidecar_path, rename_ec);
        }
        if (rename_ec)
        {
            log_line("[save] failed atomic_replace reason=" + reason +
                     " key=" + key + " stableId=" + stable_id + " worldId=" + world_id +
                     " path=" + m_sidecar_path.string() + " error=" + rename_ec.message());
            return;
        }

        log_line("[save] write_done reason=" + reason +
                 " key=" + key + " stableId=" + stable_id + " worldId=" + world_id +
                 " records=" + std::to_string(m_labels.size()) +
                 " runtimeRole=" + m_runtime_role +
                 " authorityMode=" + m_authority_mode +
                 " sidecarKind=" + m_sidecar_kind +
                 " revision=" + std::to_string(m_revision) +
                 " path=" + m_sidecar_path.string() +
                 " backup=" + sidecar_bak_path.string());
        maybe_write_backup_snapshot(reason, payload_str);
    }

    auto SignTextMod::sanitize_backup_reason(std::string reason) const -> std::string
    {
        if (reason.empty())
        {
            return "unknown";
        }
        for (auto& ch : reason)
        {
            const bool allowed = std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_';
            if (!allowed)
            {
                ch = '_';
            }
        }
        if (reason.size() > 24)
        {
            reason.resize(24);
        }
        return reason;
    }

    auto SignTextMod::maybe_write_backup_snapshot(const std::string& reason, const std::string& payload) -> void
    {
        if (m_backup_root.empty())
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto signature = std::to_string(payload.size()) + ":" + std::to_string(std::hash<std::string>{}(payload));
        const bool changed = (signature != m_last_backup_signature);
        const bool due_by_time =
            (m_last_backup_snapshot.time_since_epoch().count() == 0) ||
            ((now - m_last_backup_snapshot) >= std::chrono::seconds(90));
        const bool important_reason =
            reason == "apply" ||
            reason == "clear" ||
            reason == "prune_destroyed_label_batch";

        if (!changed || (!due_by_time && !important_reason))
        {
            return;
        }

        std::error_code mkdir_ec{};
        std::filesystem::create_directories(m_backup_root, mkdir_ec);
        if (mkdir_ec)
        {
            log_line("[save] snapshot_skip mkdir_failed path=" + m_backup_root.string() + " error=" + mkdir_ec.message());
            return;
        }

        const auto wall_now = std::time(nullptr);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &wall_now);
#else
        gmtime_r(&wall_now, &tm_utc);
#endif
        std::ostringstream stamp{};
        stamp << std::put_time(&tm_utc, "%Y%m%d_%H%M%S");

        const auto filename =
            "SignTexts.backup_" + stamp.str() + "_" + sanitize_backup_reason(reason) + ".json";
        const auto snapshot_path = m_backup_root / filename;

        std::ofstream out(snapshot_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!out.is_open())
        {
            log_line("[save] snapshot_skip open_failed path=" + snapshot_path.string());
            return;
        }
        out << payload;
        out.flush();
        out.close();

        std::vector<std::filesystem::directory_entry> snapshots{};
        std::error_code iter_ec{};
        for (const auto& entry : std::filesystem::directory_iterator(m_backup_root, iter_ec))
        {
            if (iter_ec || !entry.is_regular_file())
            {
                continue;
            }
            const auto lower_name = lower_ascii(entry.path().filename().string());
            if (lower_name.size() >= 5 &&
                lower_name.find("signtexts.backup_") == 0 &&
                lower_name.rfind(".json") == (lower_name.size() - 5))
            {
                snapshots.push_back(entry);
            }
        }
        std::sort(snapshots.begin(), snapshots.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.last_write_time() > rhs.last_write_time();
        });

        for (size_t i = 5; i < snapshots.size(); ++i)
        {
            std::error_code remove_ec{};
            std::filesystem::remove(snapshots[i].path(), remove_ec);
            if (remove_ec)
            {
                log_line("[save] snapshot_prune_failed path=" + snapshots[i].path().string() + " error=" + remove_ec.message());
            }
        }

        m_last_backup_signature = signature;
        m_last_backup_snapshot = now;
        log_line("[save] snapshot_write path=" + snapshot_path.string() + " reason=" + reason);
    }
    auto SignTextMod::send_bridge_snapshot_request(const std::string& reason) -> void
    {
        if (m_bridge_role != BridgeRole::RemoteClient)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!m_bridge_snapshot_received)
        {
            if (m_bridge_sync_wait_started.time_since_epoch().count() == 0)
            {
                m_bridge_sync_wait_started = now;
            }
            ++m_bridge_snapshot_retry_attempts;
        }
        m_bridge_last_snapshot_request = now;
        std::ostringstream payload{};
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"snapshot_request\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"worldId\":\"" << escape_json(m_world_folder_id) << "\""
                << ",\"reason\":\"" << escape_json(reason) << "\""
                << "}";
        m_bridge_snapshot_world_id.clear();
        m_bridge_snapshot_active = false;
        m_bridge_snapshot_end_seen = false;
        m_bridge_snapshot_expected_count = -1;
        m_bridge_snapshot_id.clear();
        m_bridge_snapshot_seen_keys.clear();
        const bool sent = NativeBridge::instance().send_to_server(payload.str());
        log_line("[bridge] snapshot_request reason=" + reason +
                 " sent=" + std::string{sent ? "true" : "false"} +
                 " role=" + bridge_role_name(m_bridge_role) +
                 " worldId=" + m_world_folder_id +
                 " serverHost=" + m_bridge_remote_server_host +
                 " pending=" + std::to_string(m_bridge_pending_request_keys.size()) +
                 " retryAttempt=" + std::to_string(m_bridge_snapshot_retry_attempts));
    }

    auto SignTextMod::send_bridge_record_request(const std::string& request_type, const LabelRecord& rec) -> bool
    {
        if (m_bridge_role != BridgeRole::RemoteClient)
        {
            return false;
        }

        std::ostringstream axis_value{};
        axis_value << std::fixed << std::setprecision(2) << std::clamp(rec.surface_axis, 0.0f, 1.0f);
        const auto record_kind = rec.kind.empty() ? infer_new_record_kind_from_asset(rec.asset) : rec.kind;
        const auto record_backing_asset = rec.backing_asset.empty()
            ? infer_backing_asset_from_kind(record_kind, rec.asset)
            : rec.backing_asset;
        std::ostringstream payload{};
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"" << escape_json(request_type) << "\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"stableId\":\"" << escape_json(rec.stable_id) << "\""
                << ",\"worldId\":\"" << escape_json(rec.world_id) << "\""
                << ",\"text\":\"" << escape_json(rec.text) << "\""
                << ",\"asset\":\"" << escape_json(rec.asset) << "\""
                << ",\"kind\":\"" << escape_json(record_kind) << "\""
                << ",\"backingAsset\":\"" << escape_json(record_backing_asset) << "\""
                << ",\"surfaceAxis\":" << axis_value.str()
                << ",\"surfaceSign\":" << rec.surface_sign
                << ",\"depthOffset\":" << rec.depth_offset
                << ",\"alignX\":" << rec.align_x
                << ",\"alignY\":" << rec.align_y
                << ",\"fontSize\":" << rec.font_size
                << ",\"colorR\":" << rec.color_r
                << ",\"colorG\":" << rec.color_g
                << ",\"colorB\":" << rec.color_b
                << ",\"colorA\":" << rec.color_a
                << ",\"lastSeen\":\"" << escape_json(rec.last_seen_utc) << "\""
                << "}";
        const bool sent = NativeBridge::instance().send_to_server(payload.str());
        const auto pending_key = build_storage_key(rec.world_id, rec.stable_id);
        if (sent)
        {
            m_bridge_pending_request_keys[pending_key] = std::chrono::steady_clock::now();
        }
        log_line("[bridge] client_request type=" + request_type +
                 " stableId=" + rec.stable_id +
                 " worldId=" + rec.world_id +
                 " sent=" + std::string{sent ? "true" : "false"} +
                 " textChars=" + std::to_string(rec.text.size()) +
                 " pendingKey=" + pending_key +
                 " pending=" + std::to_string(m_bridge_pending_request_keys.size()) +
                 " serverHost=" + m_bridge_remote_server_host);
        return sent;
    }

    auto SignTextMod::broadcast_bridge_record(
        const LabelRecord& rec,
        const std::string& reason,
        const std::string& snapshot_id,
        const int snapshot_count) -> void
    {
        if (m_bridge_role != BridgeRole::DedicatedServer && m_bridge_role != BridgeRole::ListenHost)
        {
            return;
        }
        std::ostringstream axis_value{};
        axis_value << std::fixed << std::setprecision(2) << std::clamp(rec.surface_axis, 0.0f, 1.0f);
        const auto record_kind = rec.kind.empty() ? infer_new_record_kind_from_asset(rec.asset) : rec.kind;
        const auto record_backing_asset = rec.backing_asset.empty()
            ? infer_backing_asset_from_kind(record_kind, rec.asset)
            : rec.backing_asset;
        std::string snapshot_fields{};
        if (!snapshot_id.empty())
        {
            snapshot_fields += ",\"snapshotId\":\"" + escape_json(snapshot_id) + "\"";
        }
        if (snapshot_count >= 0)
        {
            snapshot_fields += ",\"snapshotCount\":" + std::to_string(snapshot_count);
        }
        std::ostringstream payload{};
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"upsert\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"revision\":" << m_revision
                << snapshot_fields
                << ",\"stableId\":\"" << escape_json(rec.stable_id) << "\""
                << ",\"worldId\":\"" << escape_json(rec.world_id) << "\""
                << ",\"text\":\"" << escape_json(rec.text) << "\""
                << ",\"asset\":\"" << escape_json(rec.asset) << "\""
                << ",\"kind\":\"" << escape_json(record_kind) << "\""
                << ",\"backingAsset\":\"" << escape_json(record_backing_asset) << "\""
                << ",\"surfaceAxis\":" << axis_value.str()
                << ",\"surfaceSign\":" << rec.surface_sign
                << ",\"depthOffset\":" << rec.depth_offset
                << ",\"alignX\":" << rec.align_x
                << ",\"alignY\":" << rec.align_y
                << ",\"fontSize\":" << rec.font_size
                << ",\"colorR\":" << rec.color_r
                << ",\"colorG\":" << rec.color_g
                << ",\"colorB\":" << rec.color_b
                << ",\"colorA\":" << rec.color_a
                << ",\"lastSeen\":\"" << escape_json(rec.last_seen_utc) << "\""
                << ",\"reason\":\"" << escape_json(reason) << "\""
                << "}";
        const bool sent = NativeBridge::instance().broadcast_to_clients(payload.str());
        log_line("[bridge] broadcast_upsert reason=" + reason +
                 " stableId=" + rec.stable_id +
                 " worldId=" + rec.world_id +
                 " revision=" + std::to_string(m_revision) +
                 " sent=" + std::string{sent ? "true" : "false"} +
                 " knownClients=" + std::to_string(NativeBridge::instance().known_client_count()));
    }

    auto SignTextMod::broadcast_bridge_clear(const std::string& stable_id, const std::string& world_id, const std::string& reason) -> void
    {
        if (m_bridge_role != BridgeRole::DedicatedServer && m_bridge_role != BridgeRole::ListenHost)
        {
            return;
        }
        std::ostringstream payload{};
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"clear\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"revision\":" << m_revision
                << ",\"stableId\":\"" << escape_json(stable_id) << "\""
                << ",\"worldId\":\"" << escape_json(world_id) << "\""
                << ",\"reason\":\"" << escape_json(reason) << "\""
                << "}";
        const bool sent = NativeBridge::instance().broadcast_to_clients(payload.str());
        log_line("[bridge] broadcast_clear reason=" + reason +
                 " stableId=" + stable_id +
                 " worldId=" + world_id +
                 " revision=" + std::to_string(m_revision) +
                 " sent=" + std::string{sent ? "true" : "false"} +
                 " knownClients=" + std::to_string(NativeBridge::instance().known_client_count()));
    }

    auto SignTextMod::broadcast_bridge_snapshot(const std::string& reason) -> void
    {
        if (m_bridge_role != BridgeRole::DedicatedServer && m_bridge_role != BridgeRole::ListenHost)
        {
            return;
        }
        const auto snapshot_count = static_cast<uint32_t>(m_labels.size());
        const auto snapshot_id = make_bridge_snapshot_id(m_session_id, m_revision, snapshot_count);
        uint32_t count = 0;
        for (const auto& [_, rec] : m_labels)
        {
            broadcast_bridge_record(rec, reason, snapshot_id, static_cast<int>(snapshot_count));
            ++count;
        }
        std::ostringstream payload{};
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"snapshot_end\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"revision\":" << m_revision
                << ",\"snapshotId\":\"" << escape_json(snapshot_id) << "\""
                << ",\"snapshotCount\":" << snapshot_count
                << ",\"worldId\":\"" << escape_json(m_world_folder_id) << "\""
                << ",\"reason\":\"" << escape_json(reason) << "\""
                << "}";
        const bool sent = NativeBridge::instance().broadcast_to_clients(payload.str());
        log_line("[bridge] broadcast_snapshot reason=" + reason +
                 " records=" + std::to_string(count) +
                 " revision=" + std::to_string(m_revision) +
                 " sentEnd=" + std::string{sent ? "true" : "false"});
    }

    auto SignTextMod::server_has_label_stable_id(const std::string& stable_id) -> bool
    {
        if (stable_id.empty())
        {
            return false;
        }
        bool found = false;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (found || !object || !object->IsA(AActor::StaticClass()))
            {
                return LoopAction::Continue;
            }
            auto* actor = Cast<AActor>(object);
            if (!actor || !is_probable_label_actor(actor))
            {
                return LoopAction::Continue;
            }
            if (extract_stable_id(actor) == stable_id)
            {
                found = true;
            }
            return LoopAction::Continue;
        });
        return found;
    }

    auto get_float_property_if_present(UObject* object, const std::string& property_name, float& out_value) -> bool
    {
        if (!object || property_name.empty())
        {
            return false;
        }
        const auto target = property_name;
        bool found = false;
        for_each_property_in_chain_compat(object->GetClassPrivate(), [&](FProperty* prop) {
            if (found || !prop)
            {
                return;
            }
            const auto prop_hash = prop->GetClass().HashObject();
            if (prop_hash != FFloatProperty::StaticClass().HashObject() &&
                prop_hash != FDoubleProperty::StaticClass().HashObject())
            {
                return;
            }

            auto prop_name = RC::to_string(prop->GetName());
            std::transform(prop_name.begin(), prop_name.end(), prop_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (prop_name != target)
            {
                return;
            }

            if (prop_hash == FFloatProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<float>(object))
                {
                    out_value = *value_ptr;
                    found = true;
                }
                return;
            }

            if (auto* value_ptr = prop->ContainerPtrToValuePtr<double>(object))
            {
                out_value = static_cast<float>(*value_ptr);
                found = true;
            }
        });
        return found;
    }

    auto SignTextMod::handle_bridge_server_set(const std::unordered_map<std::string, std::string>& fields) -> void
    {
        if (!m_sidecar_authoritative)
        {
            log_line("[bridge] server_set_rejected reason=not_authoritative runtimeRole=" + m_runtime_role);
            return;
        }
        const auto stable_it = fields.find("stableId");
        if (stable_it == fields.end() || stable_it->second.empty())
        {
            log_line("[bridge] server_set_rejected reason=missing_stable_id");
            return;
        }
        const auto stable_id = unescape_json(stable_it->second);
        if (!server_has_label_stable_id(stable_id))
        {
            log_line("[bridge] server_set_rejected reason=stable_id_not_live stableId=" + stable_id);
            return;
        }

        LabelRecord rec{};
        const auto world_id = is_hex_world_id(m_world_folder_id)
            ? m_world_folder_id
            : unescape_json(fields.count("worldId") ? fields.at("worldId") : "unknown-world");
        const auto key = build_storage_key(world_id, stable_id);
        bool has_existing_record = false;
        bool existing_is_confirmed_label_text = false;
        if (const auto existing = m_labels.find(key); existing != m_labels.end())
        {
            rec = existing->second;
            has_existing_record = true;
            existing_is_confirmed_label_text = is_confirmed_label_text_kind(rec.kind);
        }
        const auto requested_asset = unescape_json(fields.count("asset") ? fields.at("asset") : "unknown");
        const auto requested_kind = unescape_json(fields.count("kind") ? fields.at("kind") : infer_new_record_kind_from_asset(requested_asset));
        if (!existing_is_confirmed_label_text && !is_confirmed_label_text_kind(requested_kind))
        {
            log_line("[bridge] server_set_rejected reason=unverified_label_text_marker_missing key=" + key +
                     " stableId=" + stable_id +
                     " asset=" + requested_asset +
                     " kind=" + requested_kind +
                     " existing=" + std::string{has_existing_record ? "true" : "false"});
            return;
        }
        rec.stable_id = stable_id;
        rec.world_id = world_id;
        rec.text = unescape_json(fields.count("text") ? fields.at("text") : "");
        rec.asset = requested_asset;
        rec.kind = existing_is_confirmed_label_text ? (rec.kind.empty() ? "LabelText" : rec.kind) : requested_kind;
        rec.backing_asset = unescape_json(fields.count("backingAsset") ? fields.at("backingAsset") : infer_backing_asset_from_kind(rec.kind, rec.asset));
        rec.surface_axis = fields.count("surfaceAxis") ? std::clamp(safe_stof(fields.at("surfaceAxis"), 0.0f), 0.0f, 1.0f) : rec.surface_axis;
        rec.surface_sign = (fields.count("surfaceSign") && safe_stoi(fields.at("surfaceSign"), 1) < 0) ? -1 : 1;
        rec.depth_offset = fields.count("depthOffset") ? safe_stof(fields.at("depthOffset"), 12.0f) : rec.depth_offset;
        rec.align_x = fields.count("alignX") ? safe_stof(fields.at("alignX"), 0.0f) : rec.align_x;
        rec.align_y = fields.count("alignY") ? safe_stof(fields.at("alignY"), 1.5f) : rec.align_y;
        rec.font_size = fields.count("fontSize") ? std::max(1.0f, safe_stof(fields.at("fontSize"), 18.0f)) : rec.font_size;
        rec.color_r = fields.count("colorR") ? std::clamp(safe_stof(fields.at("colorR"), 0.393822f), 0.0f, 1.0f) : rec.color_r;
        rec.color_g = fields.count("colorG") ? std::clamp(safe_stof(fields.at("colorG"), 0.393822f), 0.0f, 1.0f) : rec.color_g;
        rec.color_b = fields.count("colorB") ? std::clamp(safe_stof(fields.at("colorB"), 0.393822f), 0.0f, 1.0f) : rec.color_b;
        rec.color_a = fields.count("colorA") ? std::clamp(safe_stof(fields.at("colorA"), 1.0f), 0.0f, 1.0f) : rec.color_a;
        rec.last_seen_utc = now_utc();
        m_labels[key] = rec;
        save_sidecar_json("bridge_set", key, stable_id, world_id);
        broadcast_bridge_record(rec, "bridge_set");
        log_line("[bridge] server_set_accepted key=" + key +
                 " stableId=" + stable_id +
                 " worldId=" + world_id +
                 " textChars=" + std::to_string(rec.text.size()));
    }

    auto SignTextMod::handle_bridge_server_clear(const std::unordered_map<std::string, std::string>& fields) -> void
    {
        if (!m_sidecar_authoritative)
        {
            log_line("[bridge] server_clear_rejected reason=not_authoritative runtimeRole=" + m_runtime_role);
            return;
        }
        const auto stable_it = fields.find("stableId");
        if (stable_it == fields.end() || stable_it->second.empty())
        {
            log_line("[bridge] server_clear_rejected reason=missing_stable_id");
            return;
        }
        const auto stable_id = unescape_json(stable_it->second);
        const auto world_id = is_hex_world_id(m_world_folder_id) ? m_world_folder_id : unescape_json(fields.count("worldId") ? fields.at("worldId") : "unknown-world");
        const auto key = build_storage_key(world_id, stable_id);
        m_labels.erase(key);
        m_rendered_text_cache.erase(key);
        m_component_name_cache.erase(key);
        m_phase4_next_retry.erase(key);
        m_create_null_retry_states.erase(key);
        m_phase4_last_failure_reason.erase(key);
        save_sidecar_json("bridge_clear", key, stable_id, world_id);
        broadcast_bridge_clear(stable_id, world_id, "bridge_clear");
        log_line("[bridge] server_clear_accepted key=" + key +
                 " stableId=" + stable_id +
                 " worldId=" + world_id);
    }

    auto SignTextMod::handle_bridge_client_upsert(const std::unordered_map<std::string, std::string>& fields) -> void
    {
        if (m_sidecar_authoritative)
        {
            log_line("[bridge] client_upsert_ignored reason=local_authoritative");
            return;
        }
        const auto incoming_world_id = unescape_json(fields.count("worldId") ? fields.at("worldId") : "");
        if (!incoming_world_id.empty() &&
            is_hex_world_id(m_world_folder_id) &&
            is_hex_world_id(incoming_world_id) &&
            incoming_world_id != m_world_folder_id)
        {
            log_line("[bridge] client_upsert_ignored reason=world_mismatch incomingWorldId=" + incoming_world_id +
                     " localWorldId=" + m_world_folder_id);
            return;
        }
        const auto stable_it = fields.find("stableId");
        if (stable_it == fields.end() || stable_it->second.empty())
        {
            log_line("[bridge] client_upsert_rejected reason=missing_stable_id");
            return;
        }
        const auto stable_id = unescape_json(stable_it->second);
        const auto local_world_id = (!m_world_folder_id.empty() && m_world_folder_id != "unknown-world")
            ? m_world_folder_id
            : unescape_json(fields.count("worldId") ? fields.at("worldId") : "unknown-world");
        const auto key = build_storage_key(local_world_id, stable_id);
        const bool acked_pending = m_bridge_pending_request_keys.erase(key) > 0;
        const auto snapshot_id = unescape_json(fields.count("snapshotId") ? fields.at("snapshotId") : "");
        const auto snapshot_count = fields.count("snapshotCount") ? safe_stoi(fields.at("snapshotCount"), -1) : -1;
        if (!snapshot_id.empty())
        {
            const bool deferred_end_for_same_snapshot =
                !m_bridge_snapshot_active &&
                m_bridge_snapshot_end_seen &&
                !m_bridge_snapshot_id.empty() &&
                snapshot_id == m_bridge_snapshot_id;
            if (!m_bridge_snapshot_active || snapshot_id != m_bridge_snapshot_id)
            {
                m_bridge_snapshot_active = true;
                m_bridge_snapshot_end_seen = deferred_end_for_same_snapshot;
                m_bridge_snapshot_id = snapshot_id;
                if (snapshot_count >= 0)
                {
                    m_bridge_snapshot_expected_count = snapshot_count;
                }
                m_bridge_snapshot_seen_keys.clear();
                log_line("[bridge] snapshot_begin id=" + snapshot_id +
                         " expected=" + std::to_string(snapshot_count) +
                         " reason=" + std::string{deferred_end_for_same_snapshot ? "upsert_after_end" : "upsert"});
            }
            else if (snapshot_count >= 0)
            {
                m_bridge_snapshot_expected_count = snapshot_count;
            }
            m_bridge_snapshot_seen_keys.insert(key);
        }
        else
        {
            // Protect live server broadcasts from being pruned by an older snapshot
            // that may already be in flight over UDP.
            m_bridge_pending_request_keys[key] = std::chrono::steady_clock::now();
        }

        LabelRecord rec{};
        const auto existing = m_labels.find(key);
        if (existing != m_labels.end())
        {
            rec = existing->second;
        }
        rec.stable_id = stable_id;
        rec.world_id = local_world_id;
        rec.text = unescape_json(fields.count("text") ? fields.at("text") : "");
        rec.asset = unescape_json(fields.count("asset") ? fields.at("asset") : "unknown");
        rec.kind = unescape_json(fields.count("kind") ? fields.at("kind") : infer_new_record_kind_from_asset(rec.asset));
        rec.backing_asset = unescape_json(fields.count("backingAsset") ? fields.at("backingAsset") : infer_backing_asset_from_kind(rec.kind, rec.asset));
        rec.surface_axis = fields.count("surfaceAxis") ? std::clamp(safe_stof(fields.at("surfaceAxis"), 0.0f), 0.0f, 1.0f) : rec.surface_axis;
        rec.surface_sign = (fields.count("surfaceSign") && safe_stoi(fields.at("surfaceSign"), 1) < 0) ? -1 : 1;
        rec.depth_offset = fields.count("depthOffset") ? safe_stof(fields.at("depthOffset"), 12.0f) : rec.depth_offset;
        rec.align_x = fields.count("alignX") ? safe_stof(fields.at("alignX"), 0.0f) : rec.align_x;
        rec.align_y = fields.count("alignY") ? safe_stof(fields.at("alignY"), 1.5f) : rec.align_y;
        rec.font_size = fields.count("fontSize") ? std::max(1.0f, safe_stof(fields.at("fontSize"), 18.0f)) : rec.font_size;
        rec.color_r = fields.count("colorR") ? std::clamp(safe_stof(fields.at("colorR"), 0.393822f), 0.0f, 1.0f) : rec.color_r;
        rec.color_g = fields.count("colorG") ? std::clamp(safe_stof(fields.at("colorG"), 0.393822f), 0.0f, 1.0f) : rec.color_g;
        rec.color_b = fields.count("colorB") ? std::clamp(safe_stof(fields.at("colorB"), 0.393822f), 0.0f, 1.0f) : rec.color_b;
        rec.color_a = fields.count("colorA") ? std::clamp(safe_stof(fields.at("colorA"), 1.0f), 0.0f, 1.0f) : rec.color_a;
        rec.last_seen_utc = unescape_json(fields.count("lastSeen") ? fields.at("lastSeen") : now_utc());

        const auto same_float = [](const float lhs, const float rhs) {
            return std::abs(lhs - rhs) < 0.0001f;
        };
        const bool changed =
            existing == m_labels.end() ||
            existing->second.stable_id != rec.stable_id ||
            existing->second.world_id != rec.world_id ||
            existing->second.text != rec.text ||
            existing->second.asset != rec.asset ||
            existing->second.kind != rec.kind ||
            existing->second.backing_asset != rec.backing_asset ||
            !same_float(existing->second.surface_axis, rec.surface_axis) ||
            existing->second.surface_sign != rec.surface_sign ||
            !same_float(existing->second.depth_offset, rec.depth_offset) ||
            !same_float(existing->second.align_x, rec.align_x) ||
            !same_float(existing->second.align_y, rec.align_y) ||
            !same_float(existing->second.font_size, rec.font_size) ||
            !same_float(existing->second.color_r, rec.color_r) ||
            !same_float(existing->second.color_g, rec.color_g) ||
            !same_float(existing->second.color_b, rec.color_b) ||
            !same_float(existing->second.color_a, rec.color_a);

        m_labels[key] = rec;
        if (fields.count("revision"))
        {
            m_revision = std::max<uint64_t>(m_revision, static_cast<uint64_t>(safe_stoi(fields.at("revision"), static_cast<int>(m_revision))));
        }
        if (changed)
        {
            save_sidecar_json("bridge_cache_upsert", key, stable_id, local_world_id);
        }
        bool render_suppressed_by_rebuild_guard = false;
        if (const auto retry = m_phase4_next_retry.find(key); retry != m_phase4_next_retry.end())
        {
            render_suppressed_by_rebuild_guard = std::chrono::steady_clock::now() < retry->second;
        }
        if (is_confirmed_label_text_kind(rec.kind) && !render_suppressed_by_rebuild_guard)
        {
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (!object || !object->IsA(AActor::StaticClass()))
                {
                    return LoopAction::Continue;
                }
                auto* actor = Cast<AActor>(object);
                if (actor && is_probable_label_actor(actor) && extract_stable_id(actor) == stable_id)
                {
                    (void)apply_text_to_actor_component(actor, rec.text);
                }
                return LoopAction::Continue;
            });
        }
        m_bridge_snapshot_received = true;
        m_bridge_snapshot_world_id = local_world_id;
        mark_bridge_healthy("client_upsert_applied");
        log_line("[bridge] client_upsert_applied key=" + key +
                 " stableId=" + stable_id +
                 " localWorldId=" + local_world_id +
                 " changed=" + std::string{changed ? "true" : "false"} +
                 " ackedPending=" + std::string{acked_pending ? "true" : "false"} +
                 " renderSuppressed=" + std::string{render_suppressed_by_rebuild_guard ? "true" : "false"} +
                 " pending=" + std::to_string(m_bridge_pending_request_keys.size()) +
                 " textChars=" + std::to_string(rec.text.size()));

        if (!snapshot_id.empty() &&
            m_bridge_snapshot_active &&
            m_bridge_snapshot_end_seen &&
            m_bridge_snapshot_expected_count >= 0 &&
            static_cast<int>(m_bridge_snapshot_seen_keys.size()) >= m_bridge_snapshot_expected_count)
        {
            log_line("[bridge] snapshot_complete id=" + snapshot_id +
                     " seen=" + std::to_string(m_bridge_snapshot_seen_keys.size()) +
                     " expected=" + std::to_string(m_bridge_snapshot_expected_count) +
                     " completion=after_upsert");
            reconcile_bridge_snapshot("snapshot_complete_after_upsert");
        }
    }

    auto SignTextMod::handle_bridge_client_clear(const std::unordered_map<std::string, std::string>& fields) -> void
    {
        if (m_sidecar_authoritative)
        {
            log_line("[bridge] client_clear_ignored reason=local_authoritative");
            return;
        }
        const auto incoming_world_id = unescape_json(fields.count("worldId") ? fields.at("worldId") : "");
        if (!incoming_world_id.empty() &&
            is_hex_world_id(m_world_folder_id) &&
            is_hex_world_id(incoming_world_id) &&
            incoming_world_id != m_world_folder_id)
        {
            log_line("[bridge] client_clear_ignored reason=world_mismatch incomingWorldId=" + incoming_world_id +
                     " localWorldId=" + m_world_folder_id);
            return;
        }
        const auto stable_it = fields.find("stableId");
        if (stable_it == fields.end() || stable_it->second.empty())
        {
            log_line("[bridge] client_clear_rejected reason=missing_stable_id");
            return;
        }
        const auto stable_id = unescape_json(stable_it->second);
        const auto local_world_id = (!m_world_folder_id.empty() && m_world_folder_id != "unknown-world")
            ? m_world_folder_id
            : unescape_json(fields.count("worldId") ? fields.at("worldId") : "unknown-world");
        const auto key = build_storage_key(local_world_id, stable_id);
        const bool acked_pending = m_bridge_pending_request_keys.erase(key) > 0;
        m_labels.erase(key);
        m_rendered_text_cache.erase(key);
        m_component_name_cache.erase(key);
        m_phase4_next_retry.erase(key);
        m_create_null_retry_states.erase(key);
        m_phase4_last_failure_reason.erase(key);
        save_sidecar_json("bridge_cache_clear", key, stable_id, local_world_id);
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->IsA(AActor::StaticClass()))
            {
                return LoopAction::Continue;
            }
            auto* actor = Cast<AActor>(object);
            if (actor && is_probable_label_actor(actor) && extract_stable_id(actor) == stable_id)
            {
                (void)destroy_managed_text_component(actor, key);
            }
            return LoopAction::Continue;
        });
        m_bridge_snapshot_received = true;
        m_bridge_snapshot_world_id = local_world_id;
        mark_bridge_healthy("client_clear_applied");
        log_line("[bridge] client_clear_applied key=" + key +
                 " stableId=" + stable_id +
                 " localWorldId=" + local_world_id +
                 " ackedPending=" + std::string{acked_pending ? "true" : "false"} +
                 " pending=" + std::to_string(m_bridge_pending_request_keys.size()));
    }

    auto SignTextMod::write_recovery_candidate(
        const std::string& reason,
        const std::unordered_map<std::string, LabelRecord>& records) -> void
    {
        if (records.empty())
        {
            return;
        }

        std::filesystem::path recovery_root{};
        if (!m_save_profile_root.empty())
        {
            recovery_root = std::filesystem::path{m_save_profile_root} / "WindroseTextSigns" / "RecoveryCandidates";
        }
        else if (!m_data_root.empty())
        {
            recovery_root = m_data_root.parent_path() / "RecoveryCandidates";
        }
        else
        {
            recovery_root = m_mod_root / "RecoveryCandidates";
        }

        std::error_code mkdir_ec{};
        std::filesystem::create_directories(recovery_root, mkdir_ec);
        if (mkdir_ec)
        {
            log_line("[bridge] recovery_write_failed reason=mkdir path=" + recovery_root.string() +
                     " error=" + mkdir_ec.message());
            return;
        }

        const auto wall_now = std::time(nullptr);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &wall_now);
#else
        gmtime_r(&wall_now, &tm_utc);
#endif
        std::ostringstream stamp{};
        stamp << std::put_time(&tm_utc, "%Y%m%d_%H%M%S");
        const auto recovery_path = recovery_root / ("ClientCache_" + stamp.str() + "_" + sanitize_backup_reason(reason) + ".json");

        std::ofstream out(recovery_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!out.is_open())
        {
            log_line("[bridge] recovery_write_failed reason=open path=" + recovery_path.string());
            return;
        }

        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"schema\": \"WindroseTextSigns.RecoveryCandidates.v1\",\n";
        out << "  \"source\": \"client-cache\",\n";
        out << "  \"detectedUtc\": \"" << escape_json(now_utc()) << "\",\n";
        out << "  \"reason\": \"" << escape_json(reason) << "\",\n";
        out << "  \"serverRevision\": " << m_revision << ",\n";
        out << "  \"clientWorldId\": \"" << escape_json(m_world_folder_id) << "\",\n";
        out << "  \"records\": {\n";
        bool first = true;
        for (const auto& [key, rec] : records)
        {
            if (!first) { out << ",\n"; }
            first = false;
            std::ostringstream axis_value{};
            axis_value << std::fixed << std::setprecision(2) << std::clamp(rec.surface_axis, 0.0f, 1.0f);
            const auto record_kind = rec.kind.empty() ? infer_new_record_kind_from_asset(rec.asset) : rec.kind;
            const auto record_backing_asset = rec.backing_asset.empty()
                ? infer_backing_asset_from_kind(record_kind, rec.asset)
                : rec.backing_asset;
            out << "    \"" << escape_json(key) << "\": { \"text\": \"" << escape_json(rec.text)
                << "\", \"asset\": \"" << escape_json(rec.asset)
                << "\", \"kind\": \"" << escape_json(record_kind)
                << "\", \"backingAsset\": \"" << escape_json(record_backing_asset)
                << "\", \"stableId\": \"" << escape_json(rec.stable_id)
                << "\", \"worldId\": \"" << escape_json(rec.world_id)
                << "\", \"surfaceAxis\": " << axis_value.str()
                << ", \"surfaceSign\": " << rec.surface_sign
                << ", \"depthOffset\": " << rec.depth_offset
                << ", \"alignX\": " << rec.align_x
                << ", \"alignY\": " << rec.align_y
                << ", \"fontSize\": " << rec.font_size
                << ", \"colorR\": " << rec.color_r
                << ", \"colorG\": " << rec.color_g
                << ", \"colorB\": " << rec.color_b
                << ", \"colorA\": " << rec.color_a
                << ", \"lastSeen\": \"" << escape_json(rec.last_seen_utc) << "\" }";
        }
        out << "\n  }\n";
        out << "}\n";
        out.flush();
        out.close();

        log_line("[bridge] recovery_candidate_written path=" + recovery_path.string() +
                 " records=" + std::to_string(records.size()) +
                 " reason=" + reason);
    }

    auto SignTextMod::reconcile_bridge_snapshot(const std::string& reason) -> void
    {
        if (m_sidecar_authoritative || m_bridge_role != BridgeRole::RemoteClient || !m_bridge_snapshot_active)
        {
            return;
        }

        std::unordered_map<std::string, LabelRecord> removed{};
        uint32_t pending_skipped = 0;
        const auto now = std::chrono::steady_clock::now();
        for (auto it = m_labels.begin(); it != m_labels.end();)
        {
            const auto& key = it->first;
            const auto& rec = it->second;
            if (rec.world_id == m_world_folder_id && m_bridge_snapshot_seen_keys.find(key) == m_bridge_snapshot_seen_keys.end())
            {
                const auto pending = m_bridge_pending_request_keys.find(key);
                if (pending != m_bridge_pending_request_keys.end())
                {
                    if ((now - pending->second) < std::chrono::seconds(120))
                    {
                        ++pending_skipped;
                        ++it;
                        continue;
                    }
                    m_bridge_pending_request_keys.erase(pending);
                }
                removed.emplace(key, rec);
                m_seen_live_label_keys.erase(key);
                m_live_label_actor_ptrs.erase(key);
                m_missing_label_scan_counts.erase(key);
                if (m_text_buffer_bound_key == key)
                {
                    m_text_buffer.fill('\0');
                    m_text_buffer_bound_key.clear();
                }
                it = m_labels.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (!removed.empty())
        {
            uint32_t removed_components = 0;
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (!object || !object->IsA(AActor::StaticClass()))
                {
                    return LoopAction::Continue;
                }
                auto* actor = Cast<AActor>(object);
                if (!actor || !is_probable_label_actor(actor))
                {
                    return LoopAction::Continue;
                }
                const auto stable_id = extract_stable_id(actor);
                for (const auto& [key, rec] : removed)
                {
                    if (rec.stable_id == stable_id)
                    {
                        if (destroy_managed_text_component(actor, key))
                        {
                            ++removed_components;
                        }
                    }
                }
                return LoopAction::Continue;
            });

            for (const auto& [key, rec] : removed)
            {
                (void)rec;
                m_rendered_text_cache.erase(key);
                m_component_name_cache.erase(key);
                m_phase4_next_retry.erase(key);
                m_create_null_retry_states.erase(key);
                m_phase4_last_failure_reason.erase(key);
            }

            write_recovery_candidate(reason, removed);
            save_sidecar_json("bridge_snapshot_reconcile", "batch:" + std::to_string(removed.size()), "batch", m_world_folder_id);
            log_line("[bridge] snapshot_reconcile_components removedRecords=" + std::to_string(removed.size()) +
                     " removedComponents=" + std::to_string(removed_components));
        }

        log_line("[bridge] snapshot_reconcile reason=" + reason +
                 " seen=" + std::to_string(m_bridge_snapshot_seen_keys.size()) +
                 " removed=" + std::to_string(removed.size()) +
                 " pendingSkipped=" + std::to_string(pending_skipped) +
                 " remaining=" + std::to_string(m_labels.size()) +
                 " worldId=" + m_world_folder_id);
        m_bridge_snapshot_received = true;
        m_bridge_snapshot_world_id = m_world_folder_id;
        mark_bridge_healthy(reason);
        m_bridge_snapshot_active = false;
        m_bridge_snapshot_end_seen = false;
        m_bridge_snapshot_expected_count = -1;
        m_bridge_snapshot_id.clear();
        m_bridge_snapshot_seen_keys.clear();
    }

    auto SignTextMod::handle_bridge_payload(const std::string& payload) -> void
    {
        if (payload.find("\"mod\":\"WindroseTextSigns\"") == std::string::npos)
        {
            return;
        }
        const auto fields = parse_bridge_message(payload);
        const auto type_it = fields.find("type");
        if (type_it == fields.end())
        {
            log_line("[bridge] payload_rejected reason=missing_type bytes=" + std::to_string(payload.size()));
            return;
        }
        const auto type = unescape_json(type_it->second);
        const auto incoming_session = unescape_json(fields.count("session") ? fields.at("session") : "");
        if (m_bridge_role == BridgeRole::ListenHost &&
            !incoming_session.empty() &&
            !m_session_id.empty() &&
            incoming_session == m_session_id)
        {
            log_line("[bridge] payload_ignored type=" + type +
                     " role=ListenHost reason=self_originated session=" + incoming_session);
            return;
        }
        if (type == "snapshot_request")
        {
            log_line("[bridge] snapshot_request_received fromSession=" + unescape_json(fields.count("session") ? fields.at("session") : ""));
            broadcast_bridge_snapshot("snapshot_request");
            return;
        }
        if (type == "set")
        {
            handle_bridge_server_set(fields);
            return;
        }
        if (type == "clear_request")
        {
            handle_bridge_server_clear(fields);
            return;
        }
        if (type == "upsert")
        {
            handle_bridge_client_upsert(fields);
            return;
        }
        if (type == "clear")
        {
            handle_bridge_client_clear(fields);
            return;
        }
        if (type == "snapshot_end")
        {
            const auto incoming_world_id = unescape_json(fields.count("worldId") ? fields.at("worldId") : "");
            if (!incoming_world_id.empty() &&
                is_hex_world_id(m_world_folder_id) &&
                is_hex_world_id(incoming_world_id) &&
                incoming_world_id != m_world_folder_id)
            {
                log_line("[bridge] snapshot_end_ignored reason=world_mismatch incomingWorldId=" + incoming_world_id +
                         " localWorldId=" + m_world_folder_id);
                return;
            }
            if (fields.count("revision"))
            {
                m_revision = std::max<uint64_t>(m_revision, static_cast<uint64_t>(safe_stoi(fields.at("revision"), static_cast<int>(m_revision))));
            }
            const auto snapshot_id = unescape_json(fields.count("snapshotId") ? fields.at("snapshotId") : "");
            const auto snapshot_count = fields.count("snapshotCount") ? safe_stoi(fields.at("snapshotCount"), -1) : -1;
            if (!snapshot_id.empty())
            {
                if (!m_bridge_snapshot_active || snapshot_id != m_bridge_snapshot_id)
                {
                    m_bridge_snapshot_active = false;
                    m_bridge_snapshot_id = snapshot_id;
                    m_bridge_snapshot_seen_keys.clear();
                    if (snapshot_count >= 0)
                    {
                        m_bridge_snapshot_expected_count = snapshot_count;
                    }
                    m_bridge_snapshot_end_seen = true;
                    log_line("[bridge] snapshot_end_deferred id=" + snapshot_id +
                             " expected=" + std::to_string(snapshot_count) +
                             " reason=awaiting_first_record");
                    return;
                }
                if (snapshot_count >= 0)
                {
                    m_bridge_snapshot_expected_count = snapshot_count;
                }
                m_bridge_snapshot_end_seen = true;

                const bool has_any_snapshot_record = !m_bridge_snapshot_seen_keys.empty();
                const bool complete =
                    ((m_bridge_snapshot_expected_count == 0) || has_any_snapshot_record) &&
                    m_bridge_snapshot_expected_count >= 0 &&
                    static_cast<int>(m_bridge_snapshot_seen_keys.size()) >= m_bridge_snapshot_expected_count;
                log_line("[bridge] snapshot_end revision=" + std::to_string(m_revision) +
                         " role=" + bridge_role_name(m_bridge_role) +
                         " id=" + snapshot_id +
                         " seen=" + std::to_string(m_bridge_snapshot_seen_keys.size()) +
                         " expected=" + std::to_string(m_bridge_snapshot_expected_count) +
                         " hasRecord=" + std::string{has_any_snapshot_record ? "true" : "false"} +
                         " complete=" + std::string{complete ? "true" : "false"});
                if (complete)
                {
                    m_bridge_snapshot_received = true;
                    m_bridge_snapshot_world_id = m_world_folder_id;
                    mark_bridge_healthy("snapshot_complete");
                    reconcile_bridge_snapshot("snapshot_complete");
                }
                else
                {
                    log_line("[bridge] snapshot_reconcile_deferred reason=incomplete_snapshot id=" + snapshot_id);
                }
                return;
            }

            m_bridge_snapshot_received = true;
            m_bridge_snapshot_world_id = m_world_folder_id;
            mark_bridge_healthy("snapshot_end_legacy");
            reconcile_bridge_snapshot("snapshot_end_legacy");
            log_line("[bridge] snapshot_end revision=" + std::to_string(m_revision) +
                     " role=" + bridge_role_name(m_bridge_role) +
                     " legacy=true");
            return;
        }
        log_line("[bridge] payload_ignored type=" + type +
                 " role=" + bridge_role_name(m_bridge_role));
    }

    auto SignTextMod::tick_bridge_route_discovery() -> void
    {
        if (!m_bridge_route_auto_enabled || m_bridge_role != BridgeRole::RemoteClient)
        {
            return;
        }

        if (m_bridge_route_lock_acquired && !m_bridge_route_force_non_loopback)
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool snapshot_current_world =
            m_bridge_snapshot_received &&
            (!is_hex_world_id(m_world_folder_id) || m_bridge_snapshot_world_id == m_world_folder_id);
        const bool bootstrap_window_active = is_bootstrap_resolution_window_active(now);
        const bool need_recovery_pass =
            !snapshot_current_world || m_bridge_health_unhealthy || m_bridge_route_force_non_loopback;

        if (!bootstrap_window_active && !need_recovery_pass)
        {
            if (!m_bridge_route_bootstrap_pause_logged)
            {
                m_bridge_route_bootstrap_pause_logged = true;
                log_line("[bridge-route] discovery_paused reason=bootstrap_window_closed routeLocked=false");
            }
            return;
        }

        if (!bootstrap_window_active && need_recovery_pass)
        {
            if (!m_bridge_route_recovery_logged)
            {
                m_bridge_route_recovery_logged = true;
                log_line("[bridge-route] recovery_pass_start reason=degraded_unsynced routeLocked=" +
                         std::string{m_bridge_route_lock_acquired ? "true" : "false"} +
                         " routeHost=" + m_bridge_remote_server_host);
            }
            if (now < m_bridge_route_next_check)
            {
                return;
            }
            m_bridge_route_next_check = now + (m_bridge_health_unhealthy ? std::chrono::seconds(10) : std::chrono::seconds(15));
        }
        else
        {
            m_bridge_route_recovery_logged = false;
            m_bridge_route_bootstrap_pause_logged = false;
            if (now < m_bridge_route_next_check)
            {
                return;
            }
            m_bridge_route_next_check = now + std::chrono::seconds(5);
        }

        const auto discovered = discover_bridge_route_from_r5_log(m_bridge_route_log_path);
        if (!discovered.log_path.empty())
        {
            m_bridge_route_log_path = discovered.log_path;
        }

        for (const auto& fallback : discovered.fallback_direct_candidates)
        {
            const std::string key = fallback.first + "|" + fallback.second;
            if (m_bridge_route_fallback_candidates_logged.insert(key).second)
            {
                log_line("[bridge-route] fallback_direct_connect_candidate host=" + fallback.first +
                         " source=" + fallback.second);
            }
        }

        if (!discovered.found || discovered.host.empty())
        {
            const auto wait_reason = std::string{"no_candidate|"} +
                                     (m_bridge_route_log_path.empty() ? "unknown" : m_bridge_route_log_path.string());
            const bool should_log_wait =
                m_bridge_route_wait_last_reason != wait_reason ||
                m_bridge_route_wait_last_log.time_since_epoch().count() == 0 ||
                (now - m_bridge_route_wait_last_log) >= std::chrono::seconds(15);
            if (should_log_wait)
            {
                log_line("[bridge-route] waiting reason=no_candidate logPath=" +
                         (m_bridge_route_log_path.empty() ? "unknown" : m_bridge_route_log_path.string()));
                m_bridge_route_wait_last_reason = wait_reason;
                m_bridge_route_wait_last_log = now;
            }
            return;
        }

        auto ordered_candidates = discovered.ordered_candidates;
        if (ordered_candidates.empty() && !discovered.host.empty())
        {
            ordered_candidates.push_back(discovered.host);
        }
        if (ordered_candidates.empty())
        {
            return;
        }

        const auto local_hosts = parse_comma_separated_ips(discovered.local_host_summary);
        std::vector<std::string> viable_candidates{};
        viable_candidates.reserve(ordered_candidates.size());
        for (const auto& candidate : ordered_candidates)
        {
            if (candidate == "127.0.0.1" && !discovered.same_machine_evidence)
            {
                if (m_bridge_route_rejected_candidates_logged.insert("127.0.0.1:no_same_machine_evidence").second)
                {
                    log_line("[bridge-route] candidate_rejected host=127.0.0.1 reason=loopback_without_same_machine_evidence");
                }
                continue;
            }
            if (candidate == "127.0.0.1" && m_bridge_route_force_non_loopback)
            {
                if (m_bridge_route_rejected_candidates_logged.insert("127.0.0.1:force_non_loopback").second)
                {
                    log_line("[bridge-route] candidate_rejected host=127.0.0.1 reason=loopback_disallowed_unsynced_recovery");
                }
                continue;
            }
            if (candidate != "127.0.0.1" &&
                is_private_ipv4(candidate) &&
                !is_private_candidate_on_local_subnet(candidate, local_hosts))
            {
                if (m_bridge_route_rejected_candidates_logged.insert(candidate).second)
                {
                    log_line("[bridge-route] candidate_rejected host=" + candidate +
                             " reason=not_same_subnet_private localHosts=" +
                             (discovered.local_host_summary.empty() ? "none" : discovered.local_host_summary));
                }
                continue;
            }
            append_unique_ip(viable_candidates, candidate);
        }

        if (viable_candidates.empty())
        {
            const auto wait_reason = std::string{"no_viable_candidate|"} + summarize_ips(ordered_candidates) +
                                     "|" + (discovered.local_host_summary.empty() ? "none" : discovered.local_host_summary);
            const bool should_log_wait =
                m_bridge_route_wait_last_reason != wait_reason ||
                m_bridge_route_wait_last_log.time_since_epoch().count() == 0 ||
                (now - m_bridge_route_wait_last_log) >= std::chrono::seconds(15);
            if (should_log_wait)
            {
                log_line("[bridge-route] waiting reason=no_viable_candidate candidates=" + summarize_ips(ordered_candidates) +
                         " localHosts=" + (discovered.local_host_summary.empty() ? "none" : discovered.local_host_summary));
                m_bridge_route_wait_last_reason = wait_reason;
                m_bridge_route_wait_last_log = now;
            }
            return;
        }

        auto candidate_list_changed = false;
        if (viable_candidates.size() != m_bridge_route_last_candidates.size())
        {
            candidate_list_changed = true;
        }
        else
        {
            for (size_t i = 0; i < viable_candidates.size(); ++i)
            {
                if (viable_candidates[i] != m_bridge_route_last_candidates[i])
                {
                    candidate_list_changed = true;
                    break;
                }
            }
        }

        m_bridge_route_last_candidates = viable_candidates;
        const std::string selected_host = viable_candidates.front();
        if (!candidate_list_changed &&
            selected_host == m_bridge_route_last_discovered_host &&
            selected_host == m_bridge_remote_server_host)
        {
            // Keep low-noise route handling deterministic: lock to the first viable route.
            m_bridge_route_lock_acquired = true;
            m_bridge_route_locked_host = selected_host;
            m_bridge_route_loopback_same_machine_ok =
                (selected_host == "127.0.0.1" && discovered.same_machine_evidence);
            m_bridge_route_wait_last_reason.clear();
            m_bridge_route_wait_last_log = {};
            log_line("[bridge-route] lock_acquired host=" + m_bridge_route_locked_host +
                     " reason=first_viable_candidate");
            trace_behavior_sm("route_lock_acquired",
                              "host=" + m_bridge_route_locked_host + " reason=first_viable_candidate");
            return;
        }

        m_bridge_route_last_discovered_host = selected_host;
        if (selected_host != m_bridge_remote_server_host)
        {
            m_bridge_remote_server_host = selected_host;
            NativeBridge::instance().set_remote_server(
                m_bridge_remote_server_host,
                static_cast<uint16_t>(m_bridge_udp_port));
            m_bridge_next_snapshot_request = now;
            m_bridge_snapshot_received = false;
            m_bridge_snapshot_retry_attempts = 0;
            m_bridge_sync_wait_started = now;
            m_bridge_health_unhealthy = false;
            m_bridge_health_warning_logged = false;
        }

        m_bridge_route_lock_acquired = true;
        m_bridge_route_locked_host = selected_host;
        m_bridge_route_loopback_same_machine_ok =
            (selected_host == "127.0.0.1" && discovered.same_machine_evidence);
        m_bridge_route_wait_last_reason.clear();
        m_bridge_route_wait_last_log = {};
        log_line("[bridge-route] discovered host=" + m_bridge_remote_server_host +
                 " reason=" + discovered.reason +
                 " port=" + std::to_string(m_bridge_udp_port) +
                 " logPath=" + (discovered.log_path.empty() ? "unknown" : discovered.log_path.string()) +
                 " remoteHostCandidate=" + (discovered.remote_host_candidate.empty() ? "none" : discovered.remote_host_candidate) +
                 " remotePublicCandidate=" + (discovered.remote_public_candidate.empty() ? "none" : discovered.remote_public_candidate) +
                 " localHosts=" + discovered.local_host_summary +
                 " candidates=" + summarize_ips(viable_candidates));
        log_line("[bridge-route] lock_acquired host=" + m_bridge_route_locked_host +
                 " reason=first_viable_candidate");
        trace_behavior_sm("route_lock_acquired",
                          "host=" + m_bridge_route_locked_host + " reason=first_viable_candidate");
    }

    auto SignTextMod::mark_bridge_healthy(const std::string& reason) -> void
    {
        const bool was_unhealthy = m_bridge_health_unhealthy;
        m_bridge_health_unhealthy = false;
        m_bridge_health_warning_logged = false;
        m_bridge_route_force_non_loopback = false;
        m_bridge_route_recovery_logged = false;
        m_bridge_route_retry_consumed = false;
        m_bridge_snapshot_retry_attempts = 0;
        m_bridge_sync_wait_started = std::chrono::steady_clock::now();
        if (was_unhealthy)
        {
            log_line("[bridge-health] recovered reason=" + reason +
                     " worldId=" + m_world_folder_id +
                     " pending=" + std::to_string(m_bridge_pending_request_keys.size()));
        }
    }

    auto SignTextMod::reset_bridge_snapshot_state(const std::string& reason) -> void
    {
        (void)reason;
        m_bridge_snapshot_received = false;
        m_bridge_snapshot_world_id.clear();
        m_bridge_health_unhealthy = false;
        m_bridge_health_warning_logged = false;
        m_bridge_snapshot_retry_attempts = 0;
        m_bridge_sync_wait_started = std::chrono::steady_clock::now();
        m_bridge_last_snapshot_request = {};
        m_bridge_snapshot_active = false;
        m_bridge_snapshot_end_seen = false;
        m_bridge_snapshot_expected_count = -1;
        m_bridge_snapshot_id.clear();
        m_bridge_snapshot_seen_keys.clear();
        m_bridge_pending_request_keys.clear();
        m_bridge_route_recovery_logged = false;
        m_bridge_route_retry_consumed = false;
    }

    auto SignTextMod::configure_bridge_role(const std::string& reason) -> void
    {
        const auto now = std::chrono::steady_clock::now();
        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        BridgeRole desired = BridgeRole::Unknown;
        if (is_dedicated_server_process(std::filesystem::current_path(), m_mod_root))
        {
            desired = BridgeRole::DedicatedServer;
        }
        else if (m_sidecar_authoritative && runtime_role_lower == "localclient")
        {
            desired = BridgeRole::ListenHost;
        }
        else if (runtime_role_lower == "remoteclient" || runtime_role_lower.find("remote") != std::string::npos)
        {
            desired = BridgeRole::RemoteClient;
        }

        if (m_role_lock_acquired && desired != m_bridge_role)
        {
            log_line("[role] lock_held skip_bridge_role_change reason=" + reason +
                     " lockedBridgeRole=" + bridge_role_name(m_bridge_role) +
                     " requestedBridgeRole=" + bridge_role_name(desired));
            return;
        }

        if (desired == m_bridge_role)
        {
            maybe_acquire_role_lock(now, "role_stable_" + reason);
            return;
        }

        m_bridge_role = desired;
        NativeBridge::instance().set_role(desired);
        reset_bridge_snapshot_state("role_change_" + reason);
        m_bridge_route_last_candidates.clear();
        m_bridge_route_last_discovered_host.clear();
        m_bridge_route_lock_acquired = false;
        m_bridge_route_locked_host.clear();
        m_bridge_route_loopback_same_machine_ok = false;
        m_bridge_route_force_non_loopback = false;
        m_bridge_route_recovery_logged = false;
        m_bridge_route_rejected_candidates_logged.clear();
        m_bridge_route_fallback_candidates_logged.clear();
        m_bridge_route_bootstrap_pause_logged = false;
        m_bridge_route_wait_last_log = {};
        m_bridge_route_wait_last_reason.clear();
        m_bridge_upnp_timeout_logged = false;
        if (m_bridge_route_auto_enabled)
        {
            m_bridge_remote_server_host.clear();
            NativeBridge::instance().set_remote_server(
                m_bridge_remote_server_host,
                static_cast<uint16_t>(m_bridge_udp_port));
        }
        if (desired != BridgeRole::DedicatedServer && desired != BridgeRole::ListenHost)
        {
            m_bridge_upnp_last_policy.clear();
        }
        m_bridge_next_snapshot_request = std::chrono::steady_clock::now();
        log_line("[bridge] role_set reason=" + reason +
                 " role=" + bridge_role_name(m_bridge_role) +
                 " runtimeRole=" + m_runtime_role +
                 " authorityMode=" + m_authority_mode +
                 " sidecarKind=" + m_sidecar_kind +
                 " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"});
        maybe_acquire_role_lock(now, "role_set_" + reason);
    }

    auto SignTextMod::bridge_upnp_mode_name() const -> std::string
    {
        switch (m_bridge_upnp_mode)
        {
        case BridgeUpnpMode::Off:
            return "off";
        case BridgeUpnpMode::On:
            return "on";
        case BridgeUpnpMode::Auto:
            return "auto";
        default:
            return "unknown";
        }
    }

    auto SignTextMod::maybe_start_bridge_upnp_attempt(const std::string& reason) -> void
    {
        if (!m_bridge_upnp_enabled)
        {
            return;
        }
        if (m_bridge_role != BridgeRole::DedicatedServer && m_bridge_role != BridgeRole::ListenHost)
        {
            return;
        }
        if (m_bridge_upnp_mapped || m_bridge_upnp_job)
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (m_bridge_upnp_last_attempt.time_since_epoch().count() != 0 &&
            (now - m_bridge_upnp_last_attempt) < std::chrono::seconds(30))
        {
            return;
        }

        std::string policy = "disabled";
        bool should_attempt = false;
        const auto stats = NativeBridge::instance().known_client_stats();
        if (m_bridge_upnp_mode == BridgeUpnpMode::On)
        {
            policy = "forced_on";
            should_attempt = true;
        }
        else if (m_bridge_upnp_mode == BridgeUpnpMode::Auto)
        {
            if (stats.public_net > 0)
            {
                policy = "auto_public_client_detected";
                should_attempt = true;
            }
            else if (stats.total == 0)
            {
                policy = "auto_wait_no_bridge_clients";
            }
            else
            {
                policy = "auto_local_or_lan_clients_only";
            }
        }

        if (m_bridge_upnp_last_policy != policy)
        {
            log_line("[bridge-upnp] policy mode=" + bridge_upnp_mode_name() +
                     " decision=" + policy +
                     " role=" + bridge_role_name(m_bridge_role) +
                     " knownClients=" + std::to_string(stats.total) +
                     " loopbackClients=" + std::to_string(stats.loopback) +
                     " privateClients=" + std::to_string(stats.private_net) +
                     " publicClients=" + std::to_string(stats.public_net) +
                     " reason=" + reason);
            m_bridge_upnp_last_policy = policy;
        }

        if (!should_attempt)
        {
            return;
        }

        auto job = std::make_shared<BridgeUpnpJobState>();
        const auto udp_port = static_cast<uint16_t>(m_bridge_udp_port);
        m_bridge_upnp_job = job;
        m_bridge_upnp_attempted = true;
        m_bridge_upnp_attempt_count += 1;
        m_bridge_upnp_last_attempt = now;
        m_bridge_upnp_attempt_started = now;
        m_bridge_upnp_timeout_logged = false;
        std::thread([job, udp_port]() {
            const auto result = UpnpNat::map_udp_port(udp_port, udp_port, "WindroseTextSigns bridge");
            {
                std::scoped_lock lock(job->mutex);
                job->result = result;
            }
            job->done.store(true);
        }).detach();

        log_line("[bridge-upnp] attempt_start mode=" + bridge_upnp_mode_name() +
                 " trigger=" + reason +
                 " attempt=" + std::to_string(m_bridge_upnp_attempt_count) +
                 " role=" + bridge_role_name(m_bridge_role) +
                 " timeoutMs=90000");
    }

    auto SignTextMod::tick_bridge_upnp() -> void
    {
        if (m_bridge_role != BridgeRole::DedicatedServer && m_bridge_role != BridgeRole::ListenHost)
        {
            return;
        }

        if (m_bridge_upnp_job)
        {
            if (m_bridge_upnp_job->done.load())
            {
                UpnpNatResult upnp{};
                {
                    std::scoped_lock lock(m_bridge_upnp_job->mutex);
                    upnp = m_bridge_upnp_job->result;
                }
                m_bridge_upnp_job.reset();
                m_bridge_upnp_mapped = upnp.ok;
                const auto waited_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - m_bridge_upnp_attempt_started)
                                           .count();
                log_line("[bridge-upnp] attempt_complete status=finished mode=" + bridge_upnp_mode_name() +
                         " attempt=" + std::to_string(m_bridge_upnp_attempt_count) +
                         " waitedMs=" + std::to_string(waited_ms) +
                         " mapped=" + std::string{m_bridge_upnp_mapped ? "true" : "false"});
                log_line("[bridge-upnp] attempted=" + std::string{upnp.attempted ? "true" : "false"} +
                         " ok=" + std::string{upnp.ok ? "true" : "false"} +
                         " comAvailable=" + std::string{upnp.com_available ? "true" : "false"} +
                         " collectionAvailable=" + std::string{upnp.collection_available ? "true" : "false"} +
                         " localIp=" + (upnp.local_ip.empty() ? "none" : upnp.local_ip) +
                         " internalPort=" + std::to_string(upnp.internal_port) +
                         " externalPort=" + std::to_string(upnp.external_port) +
                         " protocol=" + upnp.protocol +
                         " message=" + upnp.message +
                         " mode=" + bridge_upnp_mode_name() +
                         " mapped=" + std::string{m_bridge_upnp_mapped ? "true" : "false"});
            }
            else if (!m_bridge_upnp_timeout_logged &&
                     m_bridge_upnp_attempt_started.time_since_epoch().count() != 0 &&
                     (std::chrono::steady_clock::now() - m_bridge_upnp_attempt_started) >= std::chrono::seconds(3))
            {
                m_bridge_upnp_timeout_logged = true;
                log_line("[bridge-upnp] attempt_inflight mode=" + bridge_upnp_mode_name() +
                         " waitedMs=" + std::to_string(
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - m_bridge_upnp_attempt_started)
                                 .count()) +
                         " note=non_blocking_wait_continues");
            }
            constexpr auto k_upnp_hard_timeout = std::chrono::seconds(90);
            if (m_bridge_upnp_attempt_started.time_since_epoch().count() != 0 &&
                (std::chrono::steady_clock::now() - m_bridge_upnp_attempt_started) >= k_upnp_hard_timeout)
            {
                const auto waited_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - m_bridge_upnp_attempt_started)
                                           .count();
                log_line("[bridge-upnp] attempt_complete status=timeout mode=" + bridge_upnp_mode_name() +
                         " attempt=" + std::to_string(m_bridge_upnp_attempt_count) +
                         " waitedMs=" + std::to_string(waited_ms) +
                         " action=abandon_inflight_result mapped=false");
                m_bridge_upnp_job.reset();
                m_bridge_upnp_mapped = false;
            }
            return;
        }

        maybe_start_bridge_upnp_attempt("tick");
    }

    auto SignTextMod::has_viable_remote_route_for_snapshot() const -> bool
    {
        if (m_bridge_role != BridgeRole::RemoteClient)
        {
            return false;
        }
        if (!m_bridge_route_auto_enabled)
        {
            return !m_bridge_remote_server_host.empty();
        }
        if (!m_bridge_route_lock_acquired || m_bridge_route_locked_host.empty())
        {
            return false;
        }
        if (m_bridge_route_locked_host != m_bridge_remote_server_host)
        {
            return false;
        }
        if (m_bridge_route_locked_host == "127.0.0.1" && !m_bridge_route_loopback_same_machine_ok)
        {
            return false;
        }
        return true;
    }

    auto SignTextMod::is_bootstrap_resolution_window_active(const std::chrono::steady_clock::time_point now) const -> bool
    {
        constexpr auto k_bootstrap_resolution_window = std::chrono::seconds(45);
        if (m_bootstrap_started.time_since_epoch().count() == 0)
        {
            return true;
        }
        if (!m_bootstrap_end_logged && (now - m_bootstrap_started) < k_bootstrap_resolution_window)
        {
            return true;
        }
        return false;
    }

    auto SignTextMod::reset_role_route_locks(const std::string& reason) -> void
    {
        const bool had_lock = m_role_lock_acquired || m_bridge_route_lock_acquired;
        m_role_lock_acquired = false;
        m_role_lock_runtime_role.clear();
        m_role_lock_bridge_role.clear();
        m_role_lock_world_id.clear();
        m_bridge_route_lock_acquired = false;
        m_bridge_route_locked_host.clear();
        m_bridge_route_last_discovered_host.clear();
        m_bridge_route_loopback_same_machine_ok = false;
        m_bridge_route_rejected_candidates_logged.clear();
        m_bridge_route_fallback_candidates_logged.clear();
        m_bridge_route_bootstrap_pause_logged = false;
        m_bridge_route_wait_last_log = {};
        m_bridge_route_wait_last_reason.clear();
        m_bridge_route_force_non_loopback = false;
        m_bridge_route_recovery_logged = false;
        m_bridge_route_retry_consumed = false;
        reset_localclient_role_lock_restore_pass_state();
        if (m_bridge_route_auto_enabled)
        {
            m_bridge_remote_server_host.clear();
            NativeBridge::instance().set_remote_server(
                m_bridge_remote_server_host,
                static_cast<uint16_t>(m_bridge_udp_port));
        }
        if (had_lock)
        {
            log_line("[role] lock_released reason=" + reason);
        }
    }

    auto SignTextMod::maybe_acquire_role_lock(
        const std::chrono::steady_clock::time_point now,
        const std::string& reason) -> void
    {
        if (m_role_lock_acquired)
        {
            return;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const bool runtime_stable =
            runtime_role_lower == "localclient" ||
            runtime_role_lower == "remoteclient" ||
            runtime_role_lower == "dedicatedserver";
        const bool bridge_stable =
            m_bridge_role == BridgeRole::DedicatedServer ||
            m_bridge_role == BridgeRole::ListenHost ||
            m_bridge_role == BridgeRole::RemoteClient;
        if (!runtime_stable || !bridge_stable)
        {
            return;
        }
        if (!is_dedicated_runtime_process() && !m_session_ready_latched)
        {
            return;
        }

        m_role_lock_acquired = true;
        m_role_lock_runtime_role = m_runtime_role;
        m_role_lock_bridge_role = bridge_role_name(m_bridge_role);
        m_role_lock_world_id = m_world_folder_id;
        log_line("[role] lock_acquired runtimeRole=" + m_role_lock_runtime_role +
                 " bridgeRole=" + m_role_lock_bridge_role +
                 " worldId=" + (m_role_lock_world_id.empty() ? "unknown" : m_role_lock_world_id) +
                 " reason=" + reason);
        trace_behavior_sm("role_lock_acquired",
                          "runtimeRole=" + m_role_lock_runtime_role +
                          " bridgeRole=" + m_role_lock_bridge_role +
                          " worldId=" + (m_role_lock_world_id.empty() ? "unknown" : m_role_lock_world_id) +
                          " reason=" + reason);
        schedule_localclient_role_lock_restore_passes("role_lock_acquired");
    }

    auto SignTextMod::update_bridge_health(const std::chrono::steady_clock::time_point now) -> void
    {
        if (m_bridge_role != BridgeRole::RemoteClient)
        {
            m_bridge_health_unhealthy = false;
            m_bridge_health_warning_logged = false;
            m_bridge_snapshot_retry_attempts = 0;
            m_bridge_sync_wait_started = now;
            return;
        }

        const bool snapshot_current_world =
            m_bridge_snapshot_received &&
            (!is_hex_world_id(m_world_folder_id) || m_bridge_snapshot_world_id == m_world_folder_id);
        if (m_bridge_snapshot_received && !snapshot_current_world)
        {
            m_bridge_snapshot_received = false;
            m_bridge_snapshot_world_id.clear();
        }

        if (!has_viable_remote_route_for_snapshot())
        {
            m_bridge_health_unhealthy = false;
            m_bridge_health_warning_logged = false;
            m_bridge_snapshot_retry_attempts = 0;
            m_bridge_sync_wait_started = {};
            return;
        }

        if (snapshot_current_world)
        {
            m_bridge_health_unhealthy = false;
            m_bridge_health_warning_logged = false;
            m_bridge_snapshot_retry_attempts = 0;
            m_bridge_sync_wait_started = now;
            return;
        }

        if (m_bridge_sync_wait_started.time_since_epoch().count() == 0)
        {
            m_bridge_sync_wait_started = now;
        }

        constexpr auto k_unhealthy_after = std::chrono::seconds(45);
        const auto wait_time = now - m_bridge_sync_wait_started;
        if (wait_time < k_unhealthy_after)
        {
            return;
        }

        const auto waited_sec = std::chrono::duration_cast<std::chrono::seconds>(wait_time).count();
        if (!m_bridge_route_retry_consumed)
        {
            m_bridge_route_retry_consumed = true;
            m_bridge_route_lock_acquired = false;
            m_bridge_route_locked_host.clear();
            m_bridge_route_last_discovered_host.clear();
            m_bridge_route_loopback_same_machine_ok = false;
            m_bridge_route_bootstrap_pause_logged = false;
            if (m_bridge_remote_server_host == "127.0.0.1" &&
                !m_bridge_route_loopback_same_machine_ok)
            {
                if (!m_bridge_route_force_non_loopback)
                {
                    log_line("[bridge-route] loopback_invalidated reason=no_snapshot_ack waitedSec=" + std::to_string(waited_sec));
                }
                m_bridge_route_force_non_loopback = true;
                m_bridge_route_recovery_logged = false;
            }
            if (m_bridge_route_auto_enabled)
            {
                m_bridge_remote_server_host.clear();
                NativeBridge::instance().set_remote_server(
                    m_bridge_remote_server_host,
                    static_cast<uint16_t>(m_bridge_udp_port));
            }
            m_bridge_route_next_check = now;
            m_bridge_next_snapshot_request = now;
            m_bridge_sync_wait_started = {};
            m_bridge_health_unhealthy = false;
            m_bridge_health_warning_logged = false;
            log_line("[bridge-route] retry_reselect reason=no_snapshot_ack waitedSec=" + std::to_string(waited_sec));
            return;
        }

        m_bridge_health_unhealthy = true;
        if (!m_bridge_health_warning_logged)
        {
            m_bridge_health_warning_logged = true;
            log_line("[bridge-health] degraded reason=no_snapshot_ack role=RemoteClient" +
                     std::string(" waitedSec=") + std::to_string(waited_sec) +
                     " pending=" + std::to_string(m_bridge_pending_request_keys.size()) +
                     " routeHost=" + m_bridge_remote_server_host +
                     " routeLocked=true" +
                     " upnpEnabled=" + std::string{m_bridge_upnp_enabled ? "true" : "false"} +
                     " mitigation=check_host_udp_45801_or_set_WTS_BRIDGE_SERVER_HOST_static");
        }
    }

    auto SignTextMod::next_snapshot_retry_delay() const -> std::chrono::seconds
    {
        const bool snapshot_current_world =
            m_bridge_snapshot_received &&
            (!is_hex_world_id(m_world_folder_id) || m_bridge_snapshot_world_id == m_world_folder_id);
        if (snapshot_current_world)
        {
            return std::chrono::seconds(30);
        }
        const auto now = std::chrono::steady_clock::now();
        if (!is_bootstrap_resolution_window_active(now))
        {
            return std::chrono::seconds(60);
        }
        if (m_bridge_snapshot_retry_attempts >= 16)
        {
            return std::chrono::seconds(30);
        }
        if (m_bridge_snapshot_retry_attempts >= 8)
        {
            return std::chrono::seconds(20);
        }
        if (m_bridge_snapshot_retry_attempts >= 4)
        {
            return std::chrono::seconds(10);
        }
        return std::chrono::seconds(5);
    }

    auto SignTextMod::tick_bridge() -> void
    {
        if (!is_dedicated_runtime_process() && !m_session_ready_latched)
        {
            if (m_bridge_role != BridgeRole::Unknown)
            {
                m_bridge_role = BridgeRole::Unknown;
                NativeBridge::instance().set_role(m_bridge_role);
            }
            return;
        }
        configure_bridge_role("tick");
        tick_bridge_upnp();
        tick_bridge_route_discovery();
        const auto payloads = NativeBridge::instance().poll_incoming();
        for (const auto& payload : payloads)
        {
            handle_bridge_payload(payload);
        }

        const auto now = std::chrono::steady_clock::now();
        update_bridge_health(now);
        if (m_bridge_role == BridgeRole::RemoteClient && now >= m_bridge_next_snapshot_request)
        {
            if (!has_viable_remote_route_for_snapshot())
            {
                m_bridge_next_snapshot_request = now + std::chrono::seconds(5);
            }
            else
            {
                const bool bootstrap_window_active = is_bootstrap_resolution_window_active(now);
                const std::string request_reason = m_bridge_snapshot_received
                    ? "periodic_sync"
                    : (bootstrap_window_active ? "initial_sync" : "post_bootstrap_sync");
                send_bridge_snapshot_request(request_reason);
                m_bridge_next_snapshot_request = now + next_snapshot_retry_delay();
            }
        }
        maybe_acquire_role_lock(now, "tick");

        if (now - m_bridge_last_status > std::chrono::seconds(20))
        {
            m_bridge_last_status = now;
            const bool snapshot_current_world =
                m_bridge_snapshot_received &&
                (!is_hex_world_id(m_world_folder_id) || m_bridge_snapshot_world_id == m_world_folder_id);
            log_line("[bridge] status role=" + bridge_role_name(m_bridge_role) +
                     " worldId=" + m_world_folder_id +
                     " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                     " routeHost=" + m_bridge_remote_server_host +
                     " autoRoute=" + std::string{m_bridge_route_auto_enabled ? "true" : "false"} +
                     " snapshotReceived=" + std::string{snapshot_current_world ? "true" : "false"} +
                     " snapshotWorldId=" + (m_bridge_snapshot_world_id.empty() ? "none" : m_bridge_snapshot_world_id) +
                     " health=" + std::string{m_bridge_health_unhealthy ? "degraded" : "ok"} +
                     " snapshotActive=" + std::string{m_bridge_snapshot_active ? "true" : "false"} +
                     " snapshotSeen=" + std::to_string(m_bridge_snapshot_seen_keys.size()) +
                     " snapshotExpected=" + std::to_string(m_bridge_snapshot_expected_count) +
                     " snapshotRetries=" + std::to_string(m_bridge_snapshot_retry_attempts) +
                     " pending=" + std::to_string(m_bridge_pending_request_keys.size()) +
                     " labels=" + std::to_string(m_labels.size()) +
                     " native=" + NativeBridge::instance().status_json());
        }
    }

    auto SignTextMod::make_managed_component_name(const std::string& storage_key) const -> std::string
    {
        const auto hash_value = std::hash<std::string>{}(storage_key);
        std::ostringstream out{};
        out << "WTS_TextRender_" << std::uppercase << std::hex << hash_value;
        return out.str();
    }

    auto SignTextMod::find_managed_text_component(AActor* actor, const std::string& storage_key) -> UObject*
    {
        if (!actor)
        {
            return nullptr;
        }

        std::string cached_full_name{};
        if (const auto found = m_component_name_cache.find(storage_key); found != m_component_name_cache.end())
        {
            cached_full_name = lower_ascii(found->second);
        }

        const auto expected_name = lower_ascii(make_managed_component_name(storage_key));
        auto components = actor->GetComponentsByClass(UActorComponent::StaticClass());
        for (int32_t i = 0; i < components.Num(); ++i)
        {
            auto* component = components[i];
            if (!component)
            {
                continue;
            }

            const auto component_class = lower_ascii(narrow_ascii(component->GetClassPrivate()->GetFullName()));
            if (component_class.find("textrendercomponent") == std::string::npos)
            {
                continue;
            }

            const auto component_name = lower_ascii(narrow_ascii(component->GetName()));
            const auto component_full_name = lower_ascii(narrow_ascii(component->GetFullName()));
            if (!cached_full_name.empty() && component_full_name == cached_full_name)
            {
                return component;
            }

            if (component_name == expected_name || component_full_name.find(expected_name) != std::string::npos)
            {
                m_component_name_cache[storage_key] = narrow_ascii(component->GetFullName());
                return component;
            }
        }

        // Fallback: text components created through AddComponentByClass do not keep
        // our requested name, and after a restart the in-memory cache is gone. For
        // wooden labels there should not be a native TextRenderComponent, so recover
        // the first one and blank any older duplicates left by previous prototype
        // builds instead of creating more overlapping text components.
        std::vector<UObject*> fallback_components_to_manage{};
        auto fallback_components = actor->GetComponentsByClass(UActorComponent::StaticClass());
        for (int32_t i = 0; i < fallback_components.Num(); ++i)
        {
            auto* component = fallback_components[i];
            if (!component)
            {
                continue;
            }
            const auto component_class = lower_ascii(narrow_ascii(component->GetClassPrivate()->GetFullName()));
            if (component_class.find("textrendercomponent") == std::string::npos)
            {
                continue;
            }
            fallback_components_to_manage.push_back(component);
        }
        if (!fallback_components_to_manage.empty())
        {
            auto* fallback = fallback_components_to_manage.front();
            for (size_t i = 1; i < fallback_components_to_manage.size(); ++i)
            {
                auto* duplicate = fallback_components_to_manage[i];
                (void)invoke_set_hidden_in_game(duplicate, true);
                (void)invoke_set_visibility(duplicate, false);
                (void)invoke_set_text(duplicate, "");
            }
            m_component_name_cache[storage_key] = narrow_ascii(fallback->GetFullName());
            if (fallback_components_to_manage.size() > 1)
            {
                log_line("[phase4] component_recovered key=" + storage_key +
                         " count=" + std::to_string(fallback_components_to_manage.size()) +
                         " keeping=" + narrow_ascii(fallback->GetFullName()) +
                         " action=blank_duplicate_components");
            }
            return fallback;
        }

        return nullptr;
    }

    auto SignTextMod::resolve_world_text_font_asset() -> UObject*
    {
        if (m_world_text_font_resolved)
        {
            return m_world_text_font_asset;
        }

        m_world_text_font_resolved = true;
        if (!m_world_text_font_enabled)
        {
            if (!m_world_text_font_missing_logged)
            {
                m_world_text_font_missing_logged = true;
                log_line("[phase4-font] disabled usingEngineDefault=true");
            }
            return nullptr;
        }

        auto* font_class = find_uclass_by_path(STR("/Script/Engine.Font"));
        if (!font_class)
        {
            log_line("[phase4-font] unresolved reason=FontClassMissing");
            return nullptr;
        }

        std::vector<RC::StringType> configured_candidates{};
        const auto configured_font_asset = trim_copy_ascii(config_string_value(
            "WTS_WORLD_TEXT_FONT_ASSET",
            "/Game/WindroseTextSigns/Fonts/PencilantScript.PencilantScript"));
        if (!configured_font_asset.empty() &&
            lower_copy_ascii(configured_font_asset) != "none" &&
            lower_copy_ascii(configured_font_asset) != "false")
        {
            configured_candidates.push_back(RC::to_wstring(configured_font_asset));
        }

        const std::vector<const TCHAR*> builtin_candidates = {
            STR("/Game/WindroseTextSigns/Fonts/PencilantScript_Font.PencilantScript_Font"),
            STR("/Game/WindroseTextSigns/Fonts/PencilantScript.PencilantScript"),
            STR("/Game/Fonts/PencilantScript_Font.PencilantScript_Font"),
            STR("/Game/Fonts/PencilantScript.PencilantScript"),
            STR("/Game/UI/Fonts/PencilantScript_Font.PencilantScript_Font"),
            STR("/Game/UI/Fonts/PencilantScript.PencilantScript")};
        std::vector<const TCHAR*> candidates{};
        for (const auto& candidate : configured_candidates)
        {
            candidates.push_back(candidate.c_str());
        }
        candidates.insert(candidates.end(), builtin_candidates.begin(), builtin_candidates.end());

        const auto name_hint = lower_copy_ascii(trim_copy_ascii(config_string_value("WTS_WORLD_TEXT_FONT_NAME_HINT", "pencilant")));
        m_world_text_font_asset = find_loaded_object_by_path_or_name(
            font_class,
            candidates,
            name_hint.empty() ? "pencilant" : name_hint);
        if (m_world_text_font_asset)
        {
            log_line("[phase4-font] resolved asset=" + narrow_ascii(m_world_text_font_asset->GetFullName()));
            return m_world_text_font_asset;
        }

        if (config_bool_value("WTS_WORLD_TEXT_FONT_NATIVE_FALLBACK", false))
        {
            const std::vector<const TCHAR*> native_candidates = {
                STR("/Game/UI/System/Fonts/F_CWindSerif.F_CWindSerif"),
                STR("/Game/UI/System/Fonts/F_PTSans.F_PTSans"),
                STR("/Engine/EngineFonts/Roboto.Roboto"),
                STR("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField")};
            m_world_text_font_asset = find_loaded_object_by_path_or_name(font_class, native_candidates, "font");
            if (m_world_text_font_asset)
            {
                log_line("[phase4-font] resolved nativeFallback=true asset=" + narrow_ascii(m_world_text_font_asset->GetFullName()));
                return m_world_text_font_asset;
            }
        }

        if (config_bool_value("WTS_WORLD_TEXT_FONT_INVENTORY_PROBE", false))
        {
            const auto max_fonts = static_cast<size_t>(std::clamp(
                safe_stoi(config_string_value("WTS_WORLD_TEXT_FONT_INVENTORY_MAX", "80"), 80),
                1,
                500));
            size_t seen = 0;
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (!object || !object->IsA(font_class) || seen >= max_fonts)
                {
                    return LoopAction::Continue;
                }
                ++seen;
                log_line("[phase4-font-inventory] font=" + narrow_ascii(object->GetFullName()));
                return LoopAction::Continue;
            });
            log_line("[phase4-font-inventory] complete countLogged=" + std::to_string(seen) +
                     " max=" + std::to_string(max_fonts));
        }

        if (!m_world_text_font_missing_logged)
        {
            m_world_text_font_missing_logged = true;
            const auto raw_font_path = m_mod_root / "assets" / "fonts" / "Pencilant Script.ttf";
            log_line("[phase4-font] unresolved reason=NoLoadedUFont rawFont=" + raw_font_path.string() +
                     " configuredAsset=" + (configured_font_asset.empty() ? "none" : configured_font_asset) +
                     " note=TextRenderComponent requires a packaged UFont asset; falling back to default font");
        }
        return nullptr;
    }

    auto SignTextMod::apply_world_text_font(UObject* text_component) -> bool
    {
        if (!text_component)
        {
            return false;
        }

        auto* font_asset = resolve_world_text_font_asset();
        if (!font_asset)
        {
            return false;
        }

        const bool property_set = set_object_property_if_present(text_component, "Font", font_asset);
        const bool function_set = invoke_set_object_value(
            text_component,
            STR("SetFont"),
            STR("/Script/Engine.TextRenderComponent:SetFont"),
            font_asset);
        return property_set || function_set;
    }

    auto SignTextMod::create_managed_text_component(AActor* actor, const std::string& storage_key, const FVector& relative_location) -> UObject*
    {
        if (!actor)
        {
            return nullptr;
        }

        auto* text_render_class = Cast<UClass>(UObjectGlobals::StaticFindObject<UObject*>(
            UClass::StaticClass(),
            nullptr,
            STR("/Script/Engine.TextRenderComponent")));
        if (!text_render_class)
        {
            text_render_class = Cast<UClass>(UObjectGlobals::StaticFindObject<UObject*>(
                UClass::StaticClass(),
                nullptr,
                STR("Class /Script/Engine.TextRenderComponent")));
        }
        if (!text_render_class)
        {
            text_render_class = UObjectGlobals::FindObject<UClass>(nullptr, STR("TextRenderComponent"));
        }
        if (!text_render_class)
        {
            log_line("[phase4] create_failed reason=TextRenderClassNotFound actor=" + narrow_ascii(actor->GetFullName()));
            return nullptr;
        }

        auto* add_component_fn = find_function_by_chain_or_path(
            actor,
            STR("AddComponentByClass"),
            STR("/Script/Engine.Actor:AddComponentByClass"));
        if (!add_component_fn)
        {
            log_line("[phase4] create_failed reason=AddComponentByClassMissing actor=" + narrow_ascii(actor->GetFullName()));
            return nullptr;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(add_component_fn->GetStructureSize(), 256)), 0);
        const auto relative_transform = FTransform(
            FQuat(0.0, 0.0, 0.0, 1.0),
            relative_location,
            FVector(1.0, 1.0, 1.0));
        bool assigned_component_class = false;

        for_each_property_in_chain_compat(add_component_fn, [&](FProperty* prop) {
            if (!prop)
            {
                return;
            }

            const auto prop_name = lower_ascii(RC::to_string(prop->GetName()));
            if (prop->GetClass().HashObject() == FClassProperty::StaticClass().HashObject())
            {
                if (auto* class_ptr = prop->ContainerPtrToValuePtr<UClass*>(params.data()))
                {
                    *class_ptr = text_render_class;
                    assigned_component_class = true;
                }
            }
            else if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
            {
                if (prop_name == "class" || prop_name.find("componentclass") != std::string::npos)
                {
                    if (auto* class_ptr = prop->ContainerPtrToValuePtr<UClass*>(params.data()))
                    {
                        *class_ptr = text_render_class;
                        assigned_component_class = true;
                    }
                }
            }
            else if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
            {
                if (auto* bool_ptr = prop->ContainerPtrToValuePtr<bool>(params.data()))
                {
                    if (prop_name == "bmanualattachment" || prop_name == "bdeferredfinish")
                    {
                        *bool_ptr = false;
                    }
                }
            }
            else if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject())
            {
                if (prop_name.find("relativetransform") != std::string::npos &&
                    prop->GetSize() >= static_cast<int32_t>(sizeof(FTransform)))
                {
                    if (auto* transform_ptr = prop->ContainerPtrToValuePtr<FTransform>(params.data()))
                    {
                        *transform_ptr = relative_transform;
                    }
                }
            }
        });

        if (!assigned_component_class)
        {
            log_line("[phase4] create_failed reason=ClassParamAssignmentMiss actor=" + narrow_ascii(actor->GetFullName()));
            return nullptr;
        }

        actor->ProcessEvent(add_component_fn, params.data());

        UObject* created_component = nullptr;
        for_each_property_in_chain_compat(add_component_fn, [&](FProperty* prop) {
            if (!prop || !prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return;
            }
            if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(params.data()))
                {
                    created_component = *value_ptr;
                }
            }
        });

        if (!created_component)
        {
            log_line("[phase4] create_failed reason=AddComponentByClassReturnedNull actor=" + narrow_ascii(actor->GetFullName()));
            return nullptr;
        }
        m_component_name_cache[storage_key] = narrow_ascii(created_component->GetFullName());
        const bool font_applied = apply_world_text_font(created_component);

        (void)invoke_set_float_value(
            created_component,
            STR("SetWorldSize"),
            STR("/Script/Engine.TextRenderComponent:SetWorldSize"),
            18.0f);
        (void)invoke_set_byte_value(
            created_component,
            STR("SetHorizontalAlignment"),
            STR("/Script/Engine.TextRenderComponent:SetHorizontalAlignment"),
            1);
        (void)invoke_set_byte_value(
            created_component,
            STR("SetVerticalAlignment"),
            STR("/Script/Engine.TextRenderComponent:SetVerticalAlignment"),
            1);
        (void)invoke_set_relative_location(created_component, relative_location);
        log_line("[phase4-font] create_component_font component=" + narrow_ascii(created_component->GetFullName()) +
                 " applied=" + std::string{font_applied ? "true" : "false"});
        return created_component;
    }

    auto SignTextMod::destroy_managed_text_component(AActor* actor, const std::string& storage_key) -> bool
    {
        if (!actor)
        {
            m_component_name_cache.erase(storage_key);
            return false;
        }

        bool any_removed = false;
        auto components = actor->GetComponentsByClass(UActorComponent::StaticClass());
        for (int32_t i = 0; i < components.Num(); ++i)
        {
            auto* component = components[i];
            if (!component)
            {
                continue;
            }
            const auto component_class = lower_ascii(narrow_ascii(component->GetClassPrivate()->GetFullName()));
            if (component_class.find("textrendercomponent") == std::string::npos)
            {
                continue;
            }

            const bool hidden_applied = invoke_set_hidden_in_game(component, true);
            const bool visibility_applied = invoke_set_visibility(component, false);
            const bool blanked = invoke_set_text(component, "");
            any_removed = any_removed || hidden_applied || visibility_applied || blanked;
        }

        m_component_name_cache.erase(storage_key);
        return any_removed;
    }

    auto SignTextMod::should_render_world_text_components() const -> bool
    {
        return !is_dedicated_runtime_process();
    }

    auto SignTextMod::diagnose_or_patch_label_visual(AActor* actor, const std::string& storage_key, const std::string& reason) -> bool
    {
        if (!actor || !should_render_world_text_components())
        {
            return false;
        }
        if (!m_label_text_visual_diagnostics_enabled && !m_hide_native_label_icon_enabled)
        {
            return false;
        }

        const bool first_log_for_key = m_label_text_visual_logged_keys.insert(storage_key).second;
        auto components = actor->GetComponentsByClass(UActorComponent::StaticClass());
        uint32_t hidden_candidates = 0;
        uint32_t candidate_count = 0;

        if (m_label_text_visual_diagnostics_enabled && first_log_for_key)
        {
            log_line("[visual] inspect_start key=" + storage_key +
                     " reason=" + reason +
                     " actor=" + narrow_ascii(actor->GetFullName()) +
                     " class=" + narrow_ascii(actor->GetClassPrivate() ? actor->GetClassPrivate()->GetFullName() : RC::StringType{}) +
                     " componentCount=" + std::to_string(components.Num()) +
                     " hideNativeLabelIcon=" + std::string{m_hide_native_label_icon_enabled ? "true" : "false"});
        }

        for (int32_t i = 0; i < components.Num(); ++i)
        {
            auto* component = components[i];
            if (!component)
            {
                continue;
            }

            const auto component_name = narrow_ascii(component->GetName());
            const auto component_full_name = narrow_ascii(component->GetFullName());
            const auto component_class = component->GetClassPrivate()
                ? narrow_ascii(component->GetClassPrivate()->GetFullName())
                : std::string{"unknown"};
            const auto haystack = lower_ascii(component_name + " " + component_full_name + " " + component_class);

            const bool is_text_component = haystack.find("textrendercomponent") != std::string::npos ||
                haystack.find("wts_textrender") != std::string::npos;
            const bool is_basic_plane_component =
                !is_text_component &&
                lower_ascii(component_name) == "plane" &&
                haystack.find("staticmeshcomponent") != std::string::npos;
            const bool looks_icon_component =
                !is_text_component &&
                (is_basic_plane_component ||
                 contains_any_token(haystack, {
                     "icon",
                     "symbol",
                     "decal",
                     "anchor",
                     "ship",
                     "food",
                     "ore",
                     "alchemy",
                     "weapon",
                     "treasure",
                     "trade",
                     "clothing"}));

            if (looks_icon_component)
            {
                ++candidate_count;
            }

            if (m_label_text_visual_diagnostics_enabled && first_log_for_key)
            {
                log_line("[visual] component index=" + std::to_string(i) +
                         " candidate=" + std::string{looks_icon_component ? "true" : "false"} +
                         " basicPlane=" + std::string{is_basic_plane_component ? "true" : "false"} +
                         " name=" + component_name +
                         " class=" + component_class +
                         " object=" + component_full_name);

                uint32_t logged_fields = 0;
                for_each_property_in_chain_compat(component->GetClassPrivate(), [&](FProperty* prop) {
                    if (!prop || logged_fields >= 12)
                    {
                        return;
                    }
                    auto value = try_extract_property_log_value(prop, component);
                    if (!value.has_value() || value->empty())
                    {
                        return;
                    }
                    log_line("[visual] component_field index=" + std::to_string(i) +
                             " prop=" + lower_ascii(RC::to_string(prop->GetName())) +
                             " value=" + *value);
                    ++logged_fields;
                });
            }

            if (m_hide_native_label_icon_enabled && looks_icon_component)
            {
                const bool hidden = invoke_set_hidden_in_game(component, true);
                const bool invisible = invoke_set_visibility(component, false);
                if (hidden || invisible)
                {
                    ++hidden_candidates;
                    if (first_log_for_key || m_label_text_visual_diagnostics_enabled)
                    {
                        log_line("[visual] native_icon_hidden key=" + storage_key +
                                 " component=" + component_name +
                                 " hidden=" + std::string{hidden ? "true" : "false"} +
                                 " invisible=" + std::string{invisible ? "true" : "false"});
                    }
                }
            }
        }

        if (m_label_text_visual_diagnostics_enabled && (first_log_for_key || hidden_candidates > 0))
        {
            log_line("[visual] inspect_complete key=" + storage_key +
                     " candidates=" + std::to_string(candidate_count) +
                     " hidden=" + std::to_string(hidden_candidates));
        }
        return hidden_candidates > 0;
    }

    auto SignTextMod::apply_text_to_actor_component(AActor* actor, const std::string& text_value) -> bool
    {
        if (!should_render_world_text_components())
        {
            return false;
        }
        if (!actor)
        {
            return false;
        }

        const auto stable_id = extract_stable_id(actor);
        const auto actor_world_id = build_world_id_for_actor(actor);
        configure_sidecar_for_actor(actor, actor_world_id);
        const auto world_id = active_storage_world_id(actor_world_id);
        const auto key = build_storage_key(world_id, stable_id);

        if (text_value.empty())
        {
            const bool removed = destroy_managed_text_component(actor, key);
            m_rendered_text_cache.erase(key);
            m_phase4_last_failure_reason.erase(key);
            m_phase4_next_retry.erase(key);
            m_create_null_retry_states.erase(key);
            log_line("[phase4] apply_empty_text_clear key=" + key + " removed=" + std::string{removed ? "true" : "false"});
            return removed;
        }

        (void)diagnose_or_patch_label_visual(actor, key, "apply_text");

        FVector relative_location(12.0, 0.0, 1.5);
        float desired_font_size = 18.0f;
        float desired_r = 0.393822f;
        float desired_g = 0.393822f;
        float desired_b = 0.393822f;
        float desired_a = 1.0f;
        if (const auto rec_it = m_labels.find(key); rec_it != m_labels.end())
        {
            const auto& rec = rec_it->second;
            const float axis_blend = std::clamp(rec.surface_axis, 0.0f, 1.0f);
            const float sign = (rec.surface_sign < 0) ? -1.0f : 1.0f;
            float normal_x = (1.0f - axis_blend) * sign;
            float normal_y = axis_blend * sign;
            const float normal_len = std::max(0.0001f, std::sqrt((normal_x * normal_x) + (normal_y * normal_y)));
            normal_x /= normal_len;
            normal_y /= normal_len;

            const float tangent_x = -normal_y;
            const float tangent_y = normal_x;

            const float depth = rec.depth_offset;
            const float on_surface_x = rec.align_x;
            // align_y is the center offset on the label surface.
            const float on_surface_y = rec.align_y;

            const float x = (normal_x * depth) + (tangent_x * on_surface_x);
            const float y = (normal_y * depth) + (tangent_y * on_surface_x);
            relative_location = FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(on_surface_y));

            desired_font_size = std::max(1.0f, rec.font_size);
            desired_r = std::clamp(rec.color_r, 0.0f, 1.0f);
            desired_g = std::clamp(rec.color_g, 0.0f, 1.0f);
            desired_b = std::clamp(rec.color_b, 0.0f, 1.0f);
            desired_a = std::clamp(rec.color_a, 0.0f, 1.0f);
        }

        auto* text_component = find_managed_text_component(actor, key);
        const bool reused_existing = text_component != nullptr;
        if (!text_component)
        {
            text_component = create_managed_text_component(actor, key, relative_location);
        }
        if (!text_component)
        {
            m_phase4_last_failure_reason[key] = "CreateTextComponent";
            if (m_create_null_short_retry_enabled)
            {
                auto& retry_state = m_create_null_retry_states[key];
                const bool fresh_retry_context =
                    retry_state.session_epoch != m_session_epoch ||
                    retry_state.world_id != world_id ||
                    retry_state.stable_id != stable_id ||
                    retry_state.actor_ptr != reinterpret_cast<uintptr_t>(actor) ||
                    retry_state.attempt_idx == 0 ||
                    retry_state.attempt_idx > m_create_null_retry_delays_ms.size();
                if (fresh_retry_context)
                {
                    retry_state.session_epoch = m_session_epoch;
                    retry_state.world_id = world_id;
                    retry_state.stable_id = stable_id;
                    retry_state.actor_ptr = reinterpret_cast<uintptr_t>(actor);
                    retry_state.attempt_idx = 1;
                    retry_state.next_due =
                        std::chrono::steady_clock::now() + std::chrono::milliseconds(m_create_null_retry_delays_ms[0]);
                    std::ostringstream delays{};
                    delays << m_create_null_retry_delays_ms[0]
                           << "," << m_create_null_retry_delays_ms[1]
                           << "," << m_create_null_retry_delays_ms[2];
                    log_line("[apply-retry] scheduled key=" + key +
                             " delays_ms=" + delays.str() +
                             " reason=create_null");
                }
            }
            else
            {
                m_phase4_next_retry[key] = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            }
            log_line("[phase4] apply_failed reason=CreateTextComponent actor=" + narrow_ascii(actor->GetFullName()) +
                     " key=" + key + " reusedExisting=" + std::string{reused_existing ? "true" : "false"});
            return false;
        }
        log_line("[phase4] component_created key=" + key +
                 " reusedExisting=" + std::string{reused_existing ? "true" : "false"} +
                 " component=" + narrow_ascii(text_component->GetFullName()));

        (void)invoke_set_hidden_in_game(text_component, false);
        (void)invoke_set_visibility(text_component, true);
        bool moved = invoke_set_relative_location(text_component, relative_location);
        bool sized = invoke_set_float_value(
            text_component,
            STR("SetWorldSize"),
            STR("/Script/Engine.TextRenderComponent:SetWorldSize"),
            desired_font_size);
        bool vcentered = invoke_set_byte_value(
            text_component,
            STR("SetVerticalAlignment"),
            STR("/Script/Engine.TextRenderComponent:SetVerticalAlignment"),
            1);
        bool fonted = apply_world_text_font(text_component);
        bool colored = invoke_set_text_render_color(text_component, desired_r, desired_g, desired_b, desired_a);

        bool text_applied = invoke_set_text(text_component, text_value);

        // Runtime fallback: if final properties/text fail, rebuild component once.
        if ((!sized || !vcentered || !colored || !text_applied) && text_component)
        {
            log_line("[phase4] update_partial_failure key=" + key +
                     " moved=" + std::string{moved ? "true" : "false"} +
                     " sized=" + std::string{sized ? "true" : "false"} +
                     " vcentered=" + std::string{vcentered ? "true" : "false"} +
                     " fonted=" + std::string{fonted ? "true" : "false"} +
                     " colored=" + std::string{colored ? "true" : "false"} +
                     " text=" + std::string{text_applied ? "true" : "false"} +
                     " action=rebuild_component");

            (void)destroy_managed_text_component(actor, key);
            text_component = create_managed_text_component(actor, key, relative_location);
            if (text_component)
            {
                moved = invoke_set_relative_location(text_component, relative_location);
                sized = invoke_set_float_value(
                    text_component,
                    STR("SetWorldSize"),
                    STR("/Script/Engine.TextRenderComponent:SetWorldSize"),
                    desired_font_size);
                vcentered = invoke_set_byte_value(
                    text_component,
                    STR("SetVerticalAlignment"),
                    STR("/Script/Engine.TextRenderComponent:SetVerticalAlignment"),
                    1);
                fonted = apply_world_text_font(text_component);
                colored = invoke_set_text_render_color(text_component, desired_r, desired_g, desired_b, desired_a);
                text_applied = invoke_set_text(text_component, text_value);
            }
        }
        if (!text_applied)
        {
            m_phase4_last_failure_reason[key] = "SetTextFailed";
            m_phase4_next_retry[key] = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            m_create_null_retry_states.erase(key);
            log_line("[phase4] apply_failed reason=SetTextFailed key=" + key +
                     " component=" + narrow_ascii(text_component->GetFullName()));
            return false;
        }

        m_phase4_last_failure_reason.erase(key);
        m_phase4_next_retry.erase(key);
        m_create_null_retry_states.erase(key);
        m_rendered_text_cache[key] = text_value;
        std::ostringstream loc{};
        loc << std::fixed << std::setprecision(2)
            << relative_location.GetX() << ","
            << relative_location.GetY() << ","
            << relative_location.GetZ();
        log_line("[phase4] apply_success key=" + key +
                 " component=" + narrow_ascii(text_component->GetFullName()) +
                 " moved=" + std::string{moved ? "true" : "false"} +
                 " sized=" + std::string{sized ? "true" : "false"} +
                 " vcentered=" + std::string{vcentered ? "true" : "false"} +
                 " fonted=" + std::string{fonted ? "true" : "false"} +
                 " colored=" + std::string{colored ? "true" : "false"} +
                 " relLoc=" + loc.str() +
                 " textChars=" + std::to_string(text_value.size()));
        log_line("[phase4] row_center key=" + key +
                 " rows=" + std::to_string(count_wrapped_rows(text_value)) +
                 " fontSize=" + std::to_string(desired_font_size) +
                 " alignYCenter=" + std::to_string((m_labels.find(key) != m_labels.end()) ? m_labels.at(key).align_y : 1.5f));
        return true;
    }

    auto SignTextMod::apply_text_to_selected_label(const std::string& text_value) -> void
    {
        if (!m_selected.has_value())
        {
            return;
        }
        if (!ensure_selected_actor_valid("apply_text_to_selected_label"))
        {
            return;
        }
        auto* actor = m_selected->actor;
        const auto actor_world_id = m_selected->world_id.empty() ? build_world_id_for_actor(actor) : m_selected->world_id;
        configure_sidecar_for_actor(actor, actor_world_id);
        const auto world_id = active_storage_world_id(actor_world_id);
        const auto key = build_storage_key(world_id, m_selected->stable_id);

        LabelRecord rec{};
        bool has_existing_record = false;
        if (const auto existing = m_labels.find(key); existing != m_labels.end())
        {
            rec = existing->second;
            has_existing_record = true;
        }
        rec.stable_id = m_selected->stable_id;
        rec.world_id = world_id;
        const auto normalized_text_value = strip_terminal_line_breaks(text_value);
        const auto fit = fit_text_for_plaque(normalized_text_value);
        rec.text = fit.wrapped_text;
        rec.font_size = std::clamp(fit.font_size, 10.0f, 20.0f);
        rec.asset = m_selected->asset.empty() ? detect_label_asset(actor) : m_selected->asset;
        if (has_existing_record && is_confirmed_label_text_kind(rec.kind))
        {
            rec.kind = rec.kind.empty() ? "LabelText" : rec.kind;
        }
        else
        {
            const auto inferred_kind = infer_new_record_kind_from_asset(rec.asset);
            rec.kind = is_confirmed_label_text_kind(inferred_kind) ? inferred_kind : "LabelText";
            if (!has_existing_record)
            {
                log_line("[phase5-convert] native_label_marked_as_text key=" + key +
                         " stableId=" + rec.stable_id +
                         " asset=" + rec.asset);
            }
        }
        rec.backing_asset = infer_backing_asset_from_kind(rec.kind, rec.asset);
        rec.last_seen_utc = now_utc();

        if (!is_confirmed_label_text_kind(rec.kind))
        {
            log_line("[phase4] apply_rejected_unverified_label key=" + key +
                     " stableId=" + rec.stable_id +
                     " asset=" + rec.asset +
                     " kind=" + rec.kind +
                     " reason=no_label_text_placement_marker");
            return;
        }

        auto* controller = try_get_primary_player_controller();
        const auto view = get_player_viewpoint_reflective(controller);
        if (!has_existing_record && view.valid)
        {
            const auto actor_loc = actor->K2_GetActorLocation();
            const auto to_camera = vec_normalize(vec_sub(view.location, actor_loc));
            const auto forward = vec_normalize(actor->GetActorForwardVector());
            const auto up = FVector(0.0, 0.0, 1.0);
            const auto right = vec_normalize(vec_cross(up, forward));

            const double d_forward = vec_dot(forward, to_camera);
            const double d_right = vec_dot(right, to_camera);

            if (std::abs(d_right) > std::abs(d_forward))
            {
                rec.surface_axis = 1.00f;
                rec.surface_sign = (d_right < 0.0) ? -1 : 1;
            }
            else
            {
                rec.surface_axis = 0.00f;
                rec.surface_sign = (d_forward < 0.0) ? -1 : 1;
            }
        }
        m_labels[key] = rec;
        std::ostringstream axis_value{};
        axis_value << std::fixed << std::setprecision(2) << rec.surface_axis;
        log_line("[phase4] surface_pick key=" + key +
                 " axis=" + axis_value.str() +
                 " sign=" + std::to_string(rec.surface_sign));

        log_line("[autosize] key=" + key +
                 " rows=" + std::to_string(fit.rows) +
                 " charLimit=" + std::to_string(fit.char_limit) +
                 " fontSize=" + std::to_string(rec.font_size) +
                 " truncated=" + std::string{fit.truncated ? "true" : "false"});
        log_line("[apply] request key=" + key + " stableId=" + rec.stable_id +
                 " worldId=" + rec.world_id + " textChars=" + std::to_string(rec.text.size()) +
                 " path=" + m_sidecar_path.string());
        if (!m_sidecar_authoritative)
        {
            configure_bridge_role("remote_apply");
            const bool sent = send_bridge_record_request("set", rec);
            const bool rendered = apply_text_to_actor_component(actor, rec.text);
            const bool unsynced_preview = m_bridge_health_unhealthy || !m_bridge_snapshot_received;
            if (unsynced_preview)
            {
                log_line("[bridge-health] unsynced_preview key=" + key +
                         " stableId=" + rec.stable_id +
                         " worldId=" + rec.world_id +
                         " reason=no_authoritative_ack_path");
            }
            log_line("[bridge] remote_apply_pending key=" + key +
                     " stableId=" + rec.stable_id +
                     " sent=" + std::string{sent ? "true" : "false"} +
                     " renderedOptimistic=" + std::string{rendered ? "true" : "false"} +
                     " unsyncedPreview=" + std::string{unsynced_preview ? "true" : "false"} +
                     " cacheWrite=deferred_until_server_ack");
            return;
        }
        save_sidecar_json("apply", key, rec.stable_id, rec.world_id);
        broadcast_bridge_record(rec, "apply");
        const bool rendered = apply_text_to_actor_component(actor, rec.text);
        log_line("[apply] done key=" + key + " stableId=" + rec.stable_id +
                 " worldId=" + rec.world_id + " rendered=" +
                 (rendered ? std::string{"true"} : std::string{"false"}) +
                 " path=" + m_sidecar_path.string());
    }

    auto SignTextMod::clear_text_on_selected_label() -> void
    {
        if (!m_selected.has_value())
        {
            return;
        }
        if (!ensure_selected_actor_valid("clear_text_on_selected_label"))
        {
            return;
        }
        const auto actor_world_id = m_selected->world_id.empty() ? build_world_id_for_actor(m_selected->actor) : m_selected->world_id;
        configure_sidecar_for_actor(m_selected->actor, actor_world_id);
        const auto world_id = active_storage_world_id(actor_world_id);
        const auto key = build_storage_key(world_id, m_selected->stable_id);
        log_line("[apply] clear_request key=" + key + " stableId=" + m_selected->stable_id +
                 " worldId=" + world_id + " path=" + m_sidecar_path.string());
        m_labels.erase(key);
        m_rendered_text_cache.erase(key);
        m_phase4_last_failure_reason.erase(key);
        m_component_name_cache.erase(key);
        m_phase4_next_retry.erase(key);
        m_create_null_retry_states.erase(key);
        m_seen_live_label_keys.erase(key);
        m_missing_label_scan_counts.erase(key);
        if (!m_sidecar_authoritative)
        {
            configure_bridge_role("remote_clear");
            LabelRecord rec{};
            rec.stable_id = m_selected->stable_id;
            rec.world_id = world_id;
            const bool sent = send_bridge_record_request("clear_request", rec);
            const bool removed = destroy_managed_text_component(m_selected->actor, key);
            const bool unsynced_preview = m_bridge_health_unhealthy || !m_bridge_snapshot_received;
            log_line("[bridge] remote_clear_pending key=" + key +
                     " stableId=" + m_selected->stable_id +
                     " sent=" + std::string{sent ? "true" : "false"} +
                     " removedOptimistic=" + std::string{removed ? "true" : "false"} +
                     " unsyncedPreview=" + std::string{unsynced_preview ? "true" : "false"} +
                     " cacheWrite=deferred_until_server_ack");
            return;
        }
        save_sidecar_json("clear", key, m_selected->stable_id, world_id);
        broadcast_bridge_clear(m_selected->stable_id, world_id, "clear");
        const bool removed = destroy_managed_text_component(m_selected->actor, key);
        log_line("[phase4] clear_component key=" + key + " removed=" + std::string{removed ? "true" : "false"});
        log_line("[apply] clear_done key=" + key + " stableId=" + m_selected->stable_id +
                 " worldId=" + world_id + " path=" + m_sidecar_path.string());
    }

    auto SignTextMod::restore_known_text_if_any(
        AActor* actor,
        const std::string& stable_id,
        bool force_bypass_retry_guard) -> void
    {
        if (!should_render_world_text_components())
        {
            return;
        }
        if (!actor || stable_id.empty())
        {
            return;
        }
        const auto actor_world_id = build_world_id_for_actor(actor);
        const auto key = build_storage_key(active_storage_world_id(actor_world_id), stable_id);
        const auto found = m_labels.find(key);
        if (found == m_labels.end() || found->second.text.empty())
        {
            return;
        }
        if (!is_confirmed_label_text_kind(found->second.kind))
        {
            return;
        }
        // Top-level destroy guard: if a destroy-construct correlation was observed for this
        // slot within the destroy-confirm TTL, do not restore the cached text onto the rebuilt
        // actor. Previously this check only ran inside the create-null retry branch, so a fresh
        // actor (no retry-state) could pick up stale m_labels[key] and the user would see the
        // previous sign's text reappear on a newly built sign within ~10 seconds (the workaround
        // documented in README's "Wait 5-10 seconds before rebuilding").
        // Pending create-null retry for this key is also canceled here, mirroring the existing
        // retry-side guard so we don't keep trying to attach a stale text after the slot was
        // destroyed.
        if (has_recent_destroy_confirmation(stable_id, actor_world_id))
        {
            if (auto create_retry = m_create_null_retry_states.find(key);
                create_retry != m_create_null_retry_states.end())
            {
                log_line("[apply-retry] blocked key=" + key + " reason=trusted_destroy_top_guard");
                m_create_null_retry_states.erase(create_retry);
            }
            log_line("[save] restore_blocked_recent_destroy key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + actor_world_id);
            return;
        }
        const auto log_restore_skip_guard = [&](const std::string& reason, const std::chrono::milliseconds remaining) {
            const auto remaining_ms = static_cast<int64_t>(std::max<int64_t>(0, remaining.count()));
            const uint32_t remaining_bucket_sec = static_cast<uint32_t>((remaining_ms + 999) / 1000);
            const std::string bucket_key = key + "|" + reason;
            const auto existing = m_restore_skip_guard_log_buckets.find(bucket_key);
            if (existing != m_restore_skip_guard_log_buckets.end() && existing->second == remaining_bucket_sec)
            {
                return;
            }
            m_restore_skip_guard_log_buckets[bucket_key] = remaining_bucket_sec;
            log_line("[save] restore_skip key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + actor_world_id +
                     " reason=" + reason +
                     " remainingGuardMs=" + std::to_string(remaining_ms));
        };

        std::optional<uint32_t> create_retry_attempt{};
        if (auto create_retry = m_create_null_retry_states.find(key); create_retry != m_create_null_retry_states.end())
        {
            auto& state = create_retry->second;
            const bool stale_context =
                state.session_epoch != m_session_epoch ||
                state.world_id != actor_world_id ||
                state.stable_id != stable_id ||
                state.actor_ptr != reinterpret_cast<uintptr_t>(actor);
            if (stale_context)
            {
                log_line("[apply-retry] canceled key=" + key + " reason=context_changed");
                m_create_null_retry_states.erase(create_retry);
            }
            else if (const auto suppress = m_suspect_rebuild_states.find(key); suppress != m_suspect_rebuild_states.end())
            {
                log_line("[apply-retry] blocked key=" + key + " reason=suspect_rebuild");
                m_create_null_retry_states.erase(create_retry);
                return;
            }
            else if (has_recent_destroy_confirmation(stable_id, actor_world_id))
            {
                log_line("[apply-retry] blocked key=" + key + " reason=trusted_destroy");
                m_create_null_retry_states.erase(create_retry);
                return;
            }
            else if (const auto retry_guard = m_phase4_next_retry.find(key);
                     retry_guard != m_phase4_next_retry.end() && std::chrono::steady_clock::now() < retry_guard->second)
            {
                log_line("[apply-retry] blocked key=" + key + " reason=suppressed");
                m_create_null_retry_states.erase(create_retry);
                return;
            }
            else
            {
                const auto now = std::chrono::steady_clock::now();
                if (now < state.next_due)
                {
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(state.next_due - now);
                    log_restore_skip_guard("create_null_retry_pending", remaining);
                    return;
                }

                std::string actor_ready_reason{};
                const bool actor_ready = is_actor_ready_for_restore_retry(actor, &actor_ready_reason);
                log_line("[apply-retry] attempt key=" + key +
                         " idx=" + std::to_string(state.attempt_idx) +
                         " actorReady=" + std::string{actor_ready ? "true" : "false"} +
                         " actorReadyReason=" + actor_ready_reason);
                if (!actor_ready)
                {
                    if (state.attempt_idx < m_create_null_retry_delays_ms.size())
                    {
                        ++state.attempt_idx;
                        state.next_due =
                            now + std::chrono::milliseconds(m_create_null_retry_delays_ms[state.attempt_idx - 1]);
                    }
                    else
                    {
                        log_line("[apply-retry] exhausted key=" + key);
                        m_create_null_retry_states.erase(create_retry);
                    }
                    return;
                }
                create_retry_attempt = state.attempt_idx;
            }
        }

        if (const auto suspect = m_suspect_rebuild_states.find(key); suspect != m_suspect_rebuild_states.end())
        {
            const auto now = std::chrono::steady_clock::now();
            if (now < suspect->second.suppress_until)
            {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(suspect->second.suppress_until - now);
                log_restore_skip_guard("suspect_rebuild_active", remaining);
                trace_behavior_sm("restore_suppressed",
                                  "key=" + key + " reason=suspect_rebuild_active");
                return;
            }
        }
        if (!force_bypass_retry_guard)
        {
            if (const auto retry = m_phase4_next_retry.find(key); retry != m_phase4_next_retry.end())
            {
                const auto now = std::chrono::steady_clock::now();
                if (now < retry->second)
                {
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(retry->second - now);
                    log_restore_skip_guard("retry_guard_active", remaining);
                    trace_behavior_sm("restore_suppressed",
                                      "key=" + key + " reason=retry_guard_active");
                    return;
                }
            }
        }
        const auto rendered = m_rendered_text_cache.find(key);
        if (rendered != m_rendered_text_cache.end() && rendered->second == found->second.text)
        {
            if (find_managed_text_component(actor, key))
            {
                trace_behavior_sm("restore_suppressed",
                                  "key=" + key + " reason=already_rendered_component_present");
                return;
            }
        }
        trace_behavior_sm("restore_attempt",
                          "key=" + key +
                          " stableId=" + stable_id +
                          " bypassRetryGuard=" + std::string{force_bypass_retry_guard ? "true" : "false"});
        const bool applied = apply_text_to_actor_component(actor, found->second.text);
        if (create_retry_attempt.has_value())
        {
            if (applied)
            {
                log_line("[apply-retry] success key=" + key +
                         " idx=" + std::to_string(*create_retry_attempt));
                m_create_null_retry_states.erase(key);
            }
            else if (const auto state_it = m_create_null_retry_states.find(key); state_it != m_create_null_retry_states.end())
            {
                const auto failure_it = m_phase4_last_failure_reason.find(key);
                const auto failure_reason = (failure_it != m_phase4_last_failure_reason.end()) ? failure_it->second : std::string{};
                if (failure_reason == "CreateTextComponent")
                {
                    auto& state = state_it->second;
                    if (state.attempt_idx < m_create_null_retry_delays_ms.size())
                    {
                        ++state.attempt_idx;
                        state.next_due = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(m_create_null_retry_delays_ms[state.attempt_idx - 1]);
                    }
                    else
                    {
                        log_line("[apply-retry] exhausted key=" + key);
                        m_create_null_retry_states.erase(state_it);
                    }
                }
                else
                {
                    log_line("[apply-retry] canceled key=" + key + " reason=non_create_failure");
                    m_create_null_retry_states.erase(state_it);
                }
            }
        }
        trace_behavior_sm("restore_result",
                          "key=" + key +
                          " stableId=" + stable_id +
                          " applied=" + std::string{applied ? "true" : "false"});
    }
    auto SignTextMod::is_restore_scan_world_active() -> bool
    {
        if (is_dedicated_server_process(std::filesystem::current_path(), m_mod_root))
        {
            return true;
        }

        auto* controller = try_get_primary_player_controller();
        auto* controller_actor = controller && controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
        if (!controller_actor || !controller_actor->GetWorld())
        {
            return false;
        }

        auto* world = controller_actor->GetWorld();
        if (is_world_authoritative(world))
        {
            return true;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        if (runtime_role_lower == "localclientpending")
        {
            const auto world_name = lower_ascii(narrow_ascii(world->GetName()));
            const bool transitional =
                world_name.empty() ||
                world_name == "none" ||
                world_name.find("transition") != std::string::npos ||
                world_name.find("lobby") != std::string::npos ||
                world_name.find("entrance") != std::string::npos;
            if (!transitional)
            {
                return true;
            }
        }

        const auto routed_world_id = build_world_id_for_actor(controller_actor);
        return is_hex_world_id(routed_world_id);
    }

    auto SignTextMod::is_localclient_prune_ready(bool authority_source_resolved, std::string* out_reason) -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };

        if (!m_sidecar_authoritative || is_dedicated_runtime_process())
        {
            set_reason("not_localclient_authoritative_path");
            return true;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        if (runtime_role_lower != "localclient")
        {
            set_reason("runtime_role_not_localclient");
            return false;
        }
        if (!m_session_ready_latched || !m_role_lock_acquired)
        {
            set_reason("session_ready_or_role_lock_pending");
            return false;
        }
        if (m_worldid_bind_phase != WorldIdBindPhase::StableIdLatched || !is_hex_world_id(m_worldid_latched_id))
        {
            set_reason("worldid_bind_not_stable");
            return false;
        }

        if (!authority_source_resolved)
        {
            set_reason("authority_source_pending");
            return false;
        }

        auto* controller = try_get_primary_player_controller();
        if (!controller)
        {
            set_reason("no_player_controller");
            return false;
        }

        auto* controller_actor = controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
        if (!controller_actor || !controller_actor->GetWorld())
        {
            set_reason("controller_world_unavailable");
            return false;
        }

        UObject* controlled_pawn = nullptr;
        const bool got_pawn_from_fn = invoke_object_return_no_param(
            controller,
            STR("GetPawn"),
            STR("/Script/Engine.Controller:GetPawn"),
            controlled_pawn);
        if (!got_pawn_from_fn || !controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "Pawn");
        }
        if (!controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "AcknowledgedPawn");
        }
        if (!controlled_pawn)
        {
            set_reason("controlled_pawn_missing");
            return false;
        }

        set_reason("ready");
        return true;
    }

    auto SignTextMod::is_world_id_latched_for_authoritative_localclient_bind(std::string* out_reason) -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };

        if (is_dedicated_runtime_process())
        {
            set_reason("dedicated_runtime");
            return true;
        }

        if (!m_session_ready_latched)
        {
            set_reason("session_ready_not_latched");
            return false;
        }

        auto* controller = try_get_primary_player_controller();
        auto* controller_actor = controller && controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
        if (!controller_actor || !controller_actor->GetWorld())
        {
            set_reason("controller_world_unavailable");
            return false;
        }

        std::filesystem::path profile_root{};
        if (!m_save_profile_root.empty() && std::filesystem::exists(m_save_profile_root))
        {
            profile_root = std::filesystem::path{m_save_profile_root};
        }
        else
        {
            const auto profile_roots = collect_local_client_save_profile_roots();
            if (!profile_roots.empty())
            {
                profile_root = profile_roots.front();
            }
        }

        std::string connected_world_id{};
        if (auto connected_island_id = try_latest_connected_island_id_from_local_log();
            connected_island_id.has_value() && is_hex_world_id(*connected_island_id))
        {
            connected_world_id = *connected_island_id;
        }
        std::string profile_world_id{};
        if (!profile_root.empty())
        {
            if (auto chosen = choose_world_id_for_profile(profile_root); chosen.has_value() && is_hex_world_id(*chosen))
            {
                profile_world_id = *chosen;
            }
        }
        std::string session_world_id{};
        if (is_hex_world_id(m_session_ready_world_id))
        {
            session_world_id = m_session_ready_world_id;
        }

        std::string candidate_world_id{};
        std::string candidate_source{};
        if (!connected_world_id.empty())
        {
            candidate_world_id = connected_world_id;
            candidate_source = "connected_island_id";
        }
        else if (!session_world_id.empty())
        {
            candidate_world_id = session_world_id;
            candidate_source = "session_ready_world_id";
        }
        else if (!profile_world_id.empty())
        {
            candidate_world_id = profile_world_id;
            candidate_source = "profile_worlds_root";
        }

        if (candidate_world_id.empty() || !is_hex_world_id(candidate_world_id))
        {
            set_reason("no_hex_candidate");
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool generation_signal_recent =
            m_worldid_generation_in_progress &&
            m_worldid_generation_last_signal.time_since_epoch().count() != 0 &&
            (now - m_worldid_generation_last_signal) <= std::chrono::seconds(6);
        const uint32_t required_seen = generation_signal_recent ? 4u : 3u;

        if (m_worldid_bind_phase == WorldIdBindPhase::StableIdLatched &&
            is_hex_world_id(m_worldid_latched_id))
        {
            if (lower_ascii(m_worldid_latched_id) == lower_ascii(candidate_world_id))
            {
                set_reason("stable_latched");
                return true;
            }
            const auto now_log = std::chrono::steady_clock::now();
            const auto block_reason = std::string{"existing_world_protection|"} + m_worldid_latched_id + "|" + candidate_world_id;
            const bool should_log =
                m_worldid_last_defer_reason != block_reason ||
                m_worldid_last_defer_log.time_since_epoch().count() == 0 ||
                (now_log - m_worldid_last_defer_log) >= std::chrono::seconds(3);
            if (should_log)
            {
                log_line("[worldid] switch_blocked reason=existing_world_protection current=" +
                         m_worldid_latched_id + " incoming=" + candidate_world_id);
                m_worldid_last_defer_reason = block_reason;
                m_worldid_last_defer_log = now_log;
            }
            set_reason("existing_world_protection");
            return false;
        }

        if (m_worldid_bind_phase == WorldIdBindPhase::Unbound ||
            lower_ascii(m_worldid_provisional_id) != lower_ascii(candidate_world_id))
        {
            m_worldid_bind_phase = WorldIdBindPhase::ProvisionalIdSeen;
            m_worldid_provisional_id = candidate_world_id;
            m_worldid_stability_seen_count = 1;
            m_worldid_stability_last_observed = now;
            log_line("[worldid] provisional_seen id=" + candidate_world_id + " source=" + candidate_source);
            log_line("[worldid] stability_progress id=" + candidate_world_id +
                     " seen=1 required=" + std::to_string(required_seen));
            set_reason("provisional_seeded");
            return false;
        }

        if (m_worldid_stability_last_observed.time_since_epoch().count() == 0 ||
            (now - m_worldid_stability_last_observed) >= std::chrono::milliseconds(350))
        {
            m_worldid_stability_last_observed = now;
            ++m_worldid_stability_seen_count;
            log_line("[worldid] stability_progress id=" + candidate_world_id +
                     " seen=" + std::to_string(m_worldid_stability_seen_count) +
                     " required=" + std::to_string(required_seen));
        }

        if (m_worldid_stability_seen_count < required_seen)
        {
            const std::string defer_reason = generation_signal_recent ? "generation_in_progress" : "stability_window";
            const bool should_log =
                m_worldid_last_defer_reason != defer_reason ||
                m_worldid_last_defer_log.time_since_epoch().count() == 0 ||
                (now - m_worldid_last_defer_log) >= std::chrono::seconds(3);
            if (should_log)
            {
                log_line("[worldid] latch_deferred reason=" + defer_reason);
                m_worldid_last_defer_reason = defer_reason;
                m_worldid_last_defer_log = now;
            }
            set_reason(defer_reason);
            return false;
        }

        m_worldid_bind_phase = WorldIdBindPhase::StableIdLatched;
        m_worldid_latched_id = candidate_world_id;
        m_worldid_generation_in_progress = false;
        m_worldid_last_defer_reason.clear();
        m_worldid_last_defer_log = {};
        log_line("[worldid] stable_latched id=" + candidate_world_id +
                 " epoch=" + std::to_string(m_session_epoch) +
                 " reason=session_ready_consensus");
        set_reason("stable_latched");
        return true;
    }

    auto SignTextMod::tick_r5_readiness_markers() -> void
    {
        constexpr auto k_poll_interval = std::chrono::seconds(1);
        const auto now = std::chrono::steady_clock::now();
        const auto ttl = std::chrono::seconds(std::clamp(m_destroy_confirm_ttl_sec, 2u, 30u));
        if (m_destroy_signal_last_poll.time_since_epoch().count() != 0 &&
            (now - m_destroy_signal_last_poll) < k_poll_interval)
        {
            return;
        }
        m_destroy_signal_last_poll = now;

        for (auto it = m_recent_destroy_guid_signals.begin(); it != m_recent_destroy_guid_signals.end();)
        {
            if ((now - it->second) > ttl)
            {
                it = m_recent_destroy_guid_signals.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = m_recent_destroy_slot_confirmations.begin(); it != m_recent_destroy_slot_confirmations.end();)
        {
            if ((now - it->second) > ttl)
            {
                const auto slot = it->first;
                it = m_recent_destroy_slot_confirmations.erase(it);
                log_line("[save] destroy_signal_expired guidIndex=" + slot);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = m_recent_construct_slot_signals.begin(); it != m_recent_construct_slot_signals.end();)
        {
            if ((now - it->second.seen_at) > ttl)
            {
                it = m_recent_construct_slot_signals.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (m_destroy_signal_log_path.empty() || !std::filesystem::exists(m_destroy_signal_log_path))
        {
            for (const auto& candidate : collect_r5_log_candidates(m_destroy_signal_log_path))
            {
                if (std::filesystem::exists(candidate))
                {
                    m_destroy_signal_log_path = candidate;
                    m_destroy_signal_log_initialized = false;
                    break;
                }
            }
        }

        if (m_destroy_signal_log_path.empty() || !std::filesystem::exists(m_destroy_signal_log_path))
        {
            return;
        }

        std::error_code size_ec{};
        const auto size = std::filesystem::file_size(m_destroy_signal_log_path, size_ec);
        if (size_ec)
        {
            return;
        }

        if (!m_destroy_signal_log_initialized || m_destroy_signal_log_offset > size)
        {
            m_destroy_signal_log_initialized = true;
            m_destroy_signal_log_offset = size;
            log_line("[save] destroy_signal_r5log armed path=" + m_destroy_signal_log_path.string() +
                     " offset=" + std::to_string(static_cast<unsigned long long>(size)));
            return;
        }

        if (size == m_destroy_signal_log_offset)
        {
            return;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const bool localclient_runtime =
            runtime_role_lower == "localclient" ||
            runtime_role_lower == "localclientpending";
        const bool dedicated_authoritative_runtime =
            is_dedicated_runtime_process() &&
            m_sidecar_authoritative;
        // Parse destroy/construct signals in:
        // 1) LocalClient runtime states (including Pending), so short-lived evidence
        //    is not lost during authority-route bootstrap churn.
        // 2) Dedicated authoritative runtime, so authoritative prune decisions can be
        //    committed server-side and propagated to clients.
        const bool parse_destroy_construct_signals =
            dedicated_authoritative_runtime ||
            (!is_dedicated_runtime_process() && localclient_runtime);

        std::ifstream input{m_destroy_signal_log_path, std::ios::binary};
        if (!input)
        {
            return;
        }
        input.seekg(static_cast<std::streamoff>(m_destroy_signal_log_offset), std::ios::beg);
        const auto bytes_to_read = std::min<uintmax_t>(size - m_destroy_signal_log_offset, 262144);
        std::string chunk{};
        chunk.resize(static_cast<size_t>(bytes_to_read));
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        chunk.resize(static_cast<size_t>(std::max<std::streamsize>(0, input.gcount())));
        m_destroy_signal_log_offset = size;
        if (chunk.empty())
        {
            return;
        }

        static const std::regex slot_token_rx{R"((BuildingBlock)\|([A-Fa-f0-9]{16,32})\|([0-9]+))", std::regex::icase};
        static const std::regex guid_key_value_rx{R"((?:BuildingGuid|Guid|ActorGuid)\s*[:=]\s*([A-Fa-f0-9]{16,32}))", std::regex::icase};
        static const std::regex guid_any_rx{R"(\b([A-Fa-f0-9]{32})\b)"};
        static const std::regex index_key_value_rx{R"((?:Block(?:Index)?|Index|Slot)\s*[:=]\s*([0-9]+))", std::regex::icase};
        static const std::regex construct_block_rx{
            R"(Construct\s+BuildingBlock\s+([0-9]+)\s+from\s+building\s+([A-Fa-f0-9]{16,32}))",
            std::regex::icase};
        std::istringstream lines{chunk};
        std::string line{};
        bool definitive_start_reset_fired = false;
        const auto gameplay_world_hint_from_line = [](const std::string& lower_line) -> std::string {
            auto extract_token = [&](size_t start_pos) -> std::string {
                if (start_pos == std::string::npos || start_pos >= lower_line.size())
                {
                    return {};
                }
                size_t end_pos = start_pos;
                while (end_pos < lower_line.size())
                {
                    const char ch = lower_line[end_pos];
                    if (std::isspace(static_cast<unsigned char>(ch)) ||
                        ch == ',' || ch == ';' || ch == ')' || ch == ']' || ch == '"' || ch == '\'')
                    {
                        break;
                    }
                    ++end_pos;
                }
                return lower_line.substr(start_pos, end_pos - start_pos);
            };

            size_t maps_pos = lower_line.find("/game/maps/");
            if (maps_pos != std::string::npos)
            {
                return extract_token(maps_pos);
            }
            maps_pos = lower_line.find("game/maps/");
            if (maps_pos != std::string::npos)
            {
                return extract_token(maps_pos);
            }
            return {};
        };
        const auto is_non_gameplay_hint = [](const std::string& hint_lower) -> bool {
            return hint_lower.empty() ||
                   hint_lower.find("lobby") != std::string::npos ||
                   hint_lower.find("transition") != std::string::npos ||
                   hint_lower.find("entrance") != std::string::npos;
        };
        const auto try_begin_definitive_reset = [&](const std::string& category,
                                                    const std::string& signal,
                                                    const std::string& world_hint) -> bool {
            constexpr auto k_definitive_reset_debounce = std::chrono::seconds(30);
            const auto signature = category + "|" + signal + "|" + world_hint;
            const bool within_debounce =
                m_definitive_session_reset_last_trigger.time_since_epoch().count() != 0 &&
                (now - m_definitive_session_reset_last_trigger) < k_definitive_reset_debounce &&
                m_definitive_session_reset_last_category == category &&
                m_definitive_session_reset_last_signature == signature;
            if (within_debounce)
            {
                log_line("[session] reset_suppressed reason=debounce_30s category=" + category +
                         " signal=" + signal +
                         " worldHint=" + (world_hint.empty() ? "none" : world_hint));
                return false;
            }

            m_definitive_session_reset_last_trigger = now;
            m_definitive_session_reset_last_category = category;
            m_definitive_session_reset_last_signature = signature;
            return true;
        };
        while (std::getline(lines, line))
        {
            const auto line_lower = lower_ascii(line);

            const bool advisory_generation_marker =
                line_lower.find("createisland") != std::string::npos ||
                line_lower.find("createworlddescription") != std::string::npos;
            if (advisory_generation_marker)
            {
                m_worldid_generation_in_progress = true;
                m_worldid_generation_last_signal = now;
                std::smatch world_id_match{};
                if (std::regex_search(line, world_id_match, guid_any_rx) && world_id_match.size() >= 2)
                {
                    auto marker_id = world_id_match[1].str();
                    std::transform(marker_id.begin(), marker_id.end(), marker_id.begin(), [](unsigned char c) {
                        return static_cast<char>(std::toupper(c));
                    });
                    if (is_hex_world_id(marker_id) && marker_id != m_worldid_generation_last_marker_id)
                    {
                        m_worldid_generation_last_marker_id = marker_id;
                        log_line("[worldid] provisional_seen id=" + marker_id + " source=r5_generation_marker");
                    }
                }
            }

            if (!definitive_start_reset_fired)
            {
                std::string world_hint{};
                const bool level_open_or_loadmap =
                    line_lower.find("ur5datakeeper::openlevel") != std::string::npos ||
                    line_lower.find("logload: loadmap:") != std::string::npos ||
                    line_lower.find("loadmap:") != std::string::npos;
                if (level_open_or_loadmap)
                {
                    world_hint = gameplay_world_hint_from_line(line_lower);
                    if (!is_non_gameplay_hint(world_hint))
                    {
                        m_definitive_session_start_candidate_active = true;
                        m_definitive_session_start_candidate_signal = "level_open_started";
                        m_definitive_session_start_candidate_world_hint = world_hint;
                    }
                }
                const bool bringing_world_up_for_play =
                    line_lower.find("bringing world") != std::string::npos &&
                    line_lower.find("up for play") != std::string::npos;
                if (bringing_world_up_for_play)
                {
                    world_hint = gameplay_world_hint_from_line(line_lower);
                    if (!is_non_gameplay_hint(world_hint))
                    {
                        m_definitive_session_start_candidate_active = true;
                        m_definitive_session_start_candidate_signal = "world_play_begin";
                        m_definitive_session_start_candidate_world_hint = world_hint;
                    }
                }

                if (m_definitive_session_start_candidate_active)
                {
                    std::string ready_reason{};
                    const bool start_confirmed = is_session_ready_for_role_resolution(&ready_reason);
                    if (start_confirmed)
                    {
                        const auto detected_signal = m_definitive_session_start_candidate_signal.empty()
                            ? std::string{"world_play_begin_confirmed"}
                            : m_definitive_session_start_candidate_signal + "_confirmed";
                        const auto detected_world_hint = m_definitive_session_start_candidate_world_hint;
                        if (try_begin_definitive_reset("start", detected_signal, detected_world_hint))
                        {
                            log_line("[session] definitive_start_detected signal=" + detected_signal +
                                     " worldHint=" + (detected_world_hint.empty() ? "none" : detected_world_hint));
                            reset_session_state("definitive_session_start");
                            log_line("[session] reset_extended_clears applied=true");
                            definitive_start_reset_fired = true;
                        }
                        m_definitive_session_start_candidate_active = false;
                        m_definitive_session_start_candidate_signal.clear();
                        m_definitive_session_start_candidate_world_hint.clear();
                    }
                }
            }

            if (line_lower.find("world client") != std::string::npos &&
                !m_hosted_ready_world_client_seen)
            {
                m_hosted_ready_world_client_seen = true;
                log_line("[save] hosted_ready_signal signal=world_client");
            }
            if (line_lower.find("openlevel") != std::string::npos ||
                line_lower.find("loadmap:") != std::string::npos)
            {
                log_line("[save] hosted_ready_signal signal=level_open_started");
            }
            if (line_lower.find("bringing world") != std::string::npos &&
                line_lower.find("up for play") != std::string::npos)
            {
                log_line("[save] hosted_ready_signal signal=world_play_begin");
            }
            if (line_lower.find("onplayerisready") != std::string::npos &&
                line_lower.find("character bp_") != std::string::npos &&
                line_lower.find("character nullptr") == std::string::npos &&
                !m_hosted_ready_player_ready_seen)
            {
                m_hosted_ready_player_ready_seen = true;
                log_line("[save] hosted_ready_signal signal=player_ready");
            }
            if (line_lower.find("datakeeper is ready to play") != std::string::npos &&
                !m_hosted_ready_datakeeper_seen)
            {
                m_hosted_ready_datakeeper_seen = true;
                log_line("[save] hosted_ready_signal signal=datakeeper_ready");
            }
            if (line_lower.find("hide loading screen") != std::string::npos &&
                !m_hosted_ready_hide_loading_seen)
            {
                m_hosted_ready_hide_loading_seen = true;
                log_line("[save] hosted_ready_signal signal=hide_loading");
            }
            if (!m_hosted_ready_sequence_complete &&
                m_hosted_ready_world_client_seen &&
                m_hosted_ready_player_ready_seen &&
                m_hosted_ready_datakeeper_seen &&
                m_hosted_ready_hide_loading_seen)
            {
                m_hosted_ready_sequence_complete = true;
                log_line("[save] hosted_ready_sequence complete=true");
                if (m_session_ready_latched && lower_ascii(m_runtime_role) == "localclientpending")
                {
                    tick_localclient_role_resolution();
                }
            }

            const bool farewell_transition =
                line_lower.find("change state readytoplay => sentfarewell") != std::string::npos ||
                line_lower.find("change state readytoplay => saidfarewell") != std::string::npos;
            if (farewell_transition)
            {
                if (try_begin_definitive_reset("end", "sent_farewell", "none"))
                {
                    log_line("[session] definitive_end_detected signal=sent_farewell");
                    arm_phase7_definitive_teardown("sent_farewell");
                }
            }
            const bool request_exit =
                line_lower.find("requestexit(") != std::string::npos ||
                line_lower.find("engine exit requested") != std::string::npos;
            if (request_exit)
            {
                if (try_begin_definitive_reset("end", "engine_request_exit", "none"))
                {
                    log_line("[session] definitive_end_detected signal=engine_request_exit");
                    arm_phase7_definitive_teardown("engine_request_exit");
                }
            }

            if (!parse_destroy_construct_signals)
            {
                continue;
            }

            const bool destroy_rule = line_lower.find("destroyrule::do_impl") != std::string::npos;
            const bool construct_rule = line_lower.find("constructrule::do_impl") != std::string::npos;
            if (!destroy_rule && !construct_rule)
            {
                continue;
            }

            std::string guid{};
            std::string index{};
            std::smatch match{};
            if (construct_rule && std::regex_search(line, match, construct_block_rx) && match.size() >= 3)
            {
                index = match[1].str();
                guid = match[2].str();
            }
            else if (std::regex_search(line, match, slot_token_rx) && match.size() >= 4)
            {
                guid = match[2].str();
                index = match[3].str();
            }
            else
            {
                if (std::regex_search(line, match, guid_key_value_rx) && match.size() >= 2)
                {
                    guid = match[1].str();
                }
                else if (std::regex_search(line, match, guid_any_rx) && match.size() >= 2)
                {
                    guid = match[1].str();
                }
                if (std::regex_search(line, match, index_key_value_rx) && match.size() >= 2)
                {
                    index = match[1].str();
                }
            }

            if (guid.empty())
            {
                continue;
            }
            std::transform(guid.begin(), guid.end(), guid.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

            if (destroy_rule)
            {
                m_recent_destroy_guid_signals[guid] = now;
                log_line("[save] destroy_signal_seen guid=" + guid);
                for (const auto& [slot_key, signal] : m_recent_construct_slot_signals)
                {
                    if ((now - signal.seen_at) > ttl)
                    {
                        continue;
                    }
                    const auto guid_delim = slot_key.find('|');
                    if (guid_delim == std::string::npos || guid_delim == 0)
                    {
                        continue;
                    }
                    const auto slot_guid = slot_key.substr(0, guid_delim);
                    if (lower_ascii(slot_guid) != lower_ascii(guid))
                    {
                        continue;
                    }
                    const auto index_value = slot_key.substr(guid_delim + 1);
                    m_recent_destroy_slot_confirmations[slot_key] = now;
                    log_line("[save] destroy_construct_correlated guid=" + guid + " index=" + index_value);
                }
            }
            if (construct_rule)
            {
                if (!index.empty())
                {
                    const auto slot_key = make_building_slot_key(guid, index);
                    m_recent_construct_slot_signals[slot_key] = RecentConstructSignal{
                        now,
                        active_storage_world_id(m_role_lock_world_id.empty() ? m_session_ready_world_id : m_role_lock_world_id),
                        m_session_epoch};
                    log_line("[save] construct_signal_seen guid=" + guid + " index=" + index);
                    const auto guid_it = m_recent_destroy_guid_signals.find(guid);
                    if (guid_it != m_recent_destroy_guid_signals.end() && (now - guid_it->second) <= ttl)
                    {
                        m_recent_destroy_slot_confirmations[slot_key] = now;
                        log_line("[save] destroy_construct_correlated guid=" + guid + " index=" + index);
                    }
                }
            }
        }
    }

    auto SignTextMod::refresh_recent_destroy_signals_from_r5_log() -> void
    {
        tick_r5_readiness_markers();
    }

    auto SignTextMod::has_recent_destroy_confirmation(const std::string& stable_id, const std::string& expected_world_id) -> bool
    {
        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const bool localclient_authoritative_runtime =
            !is_dedicated_runtime_process() &&
            runtime_role_lower == "localclient";
        const bool dedicated_authoritative_runtime =
            is_dedicated_runtime_process() &&
            m_sidecar_authoritative;
        if (!m_sidecar_authoritative ||
            (!localclient_authoritative_runtime && !dedicated_authoritative_runtime))
        {
            return false;
        }
        if (stable_id.empty() || expected_world_id.empty())
        {
            return false;
        }
        if (m_role_lock_acquired && !m_role_lock_world_id.empty())
        {
            if (lower_ascii(m_role_lock_world_id) != lower_ascii(expected_world_id))
            {
                return false;
            }
        }
        if (localclient_authoritative_runtime)
        {
            auto* controller = try_get_primary_player_controller();
            auto* controller_actor = controller && controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
            if (!controller_actor)
            {
                return false;
            }
            const auto active_world_id = active_storage_world_id(build_world_id_for_actor(controller_actor));
            if (active_world_id.empty() || lower_ascii(active_world_id) != lower_ascii(expected_world_id))
            {
                return false;
            }
        }
        else
        {
            const auto active_world_id = active_storage_world_id(
                !m_role_lock_world_id.empty() ? m_role_lock_world_id : m_world_folder_id);
            if (active_world_id.empty() || lower_ascii(active_world_id) != lower_ascii(expected_world_id))
            {
                return false;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const auto slot = try_extract_building_slot_from_stable_id(stable_id);
        if (!slot.has_value())
        {
            return false;
        }
        const auto slot_key = make_building_slot_key(slot->first, slot->second);
        const auto ttl = std::chrono::seconds(std::clamp(m_destroy_confirm_ttl_sec, 2u, 30u));
        if (const auto found = m_recent_destroy_slot_confirmations.find(slot_key);
            found != m_recent_destroy_slot_confirmations.end())
        {
            if ((now - found->second) <= ttl)
            {
                return true;
            }
            m_recent_destroy_slot_confirmations.erase(found);
        }
        return false;
    }

    auto SignTextMod::maybe_handle_new_construct_overrides_stale_record(
        AActor* actor,
        const std::string& key,
        const std::string& stable_id,
        const std::string& world_id,
        bool first_seen_live_after_ready,
        bool is_ready_baseline_key) -> bool
    {
        if (!actor || key.empty() || stable_id.empty() || world_id.empty())
        {
            return false;
        }
        if (!m_session_ready_latched)
        {
            return false;
        }
        const bool has_existing_construct_hold =
            m_first_seen_construct_hold_states.find(key) != m_first_seen_construct_hold_states.end();
        if (!first_seen_live_after_ready && !has_existing_construct_hold)
        {
            return false;
        }

        const bool authoritative_localclient =
            m_sidecar_authoritative && lower_ascii(m_runtime_role) == "localclient";
        const bool authoritative_dedicated =
            m_sidecar_authoritative && is_dedicated_runtime_process();
        if (!authoritative_localclient && !authoritative_dedicated)
        {
            return false;
        }

        const auto slot = try_extract_building_slot_from_stable_id(stable_id);
        if (!slot.has_value())
        {
            return false;
        }
        const auto slot_key = make_building_slot_key(slot->first, slot->second);
        const auto now = std::chrono::steady_clock::now();
        const auto ttl = std::chrono::seconds(std::clamp(m_destroy_confirm_ttl_sec, 2u, 30u));
        bool fresh_construct_for_active_world = false;
        if (const auto found_construct = m_recent_construct_slot_signals.find(slot_key);
            found_construct != m_recent_construct_slot_signals.end())
        {
            const auto& signal = found_construct->second;
            if ((now - signal.seen_at) <= ttl &&
                signal.session_epoch == m_session_epoch &&
                !signal.world_id.empty() &&
                lower_ascii(signal.world_id) == lower_ascii(world_id))
            {
                fresh_construct_for_active_world = true;
            }
        }

        if (should_hold_restore_for_first_seen_post_ready(
                key, world_id, first_seen_live_after_ready, is_ready_baseline_key))
        {
            return true;
        }

        if (!fresh_construct_for_active_world || is_ready_baseline_key)
        {
            log_line("[save] restore_allowed_no_construct_correlation key=" + key);
            return false;
        }

        m_first_seen_construct_hold_states.erase(key);
        log_line("[save] stale_restore_blocked key=" + key + " reason=new_construct_overrides_stale_record");
        auto found = m_labels.find(key);
        if (found == m_labels.end())
        {
            return true;
        }

        if (m_sidecar_authoritative)
        {
            broadcast_bridge_clear(found->second.stable_id, found->second.world_id, "new_construct_overrides_stale_record");
        }
        m_labels.erase(found);
        m_rendered_text_cache.erase(key);
        m_component_name_cache.erase(key);
        m_phase4_next_retry.erase(key);
        m_create_null_retry_states.erase(key);
        m_phase4_last_failure_reason.erase(key);
        m_missing_label_scan_counts.erase(key);
        m_suspect_rebuild_states.erase(key);
        const bool removed_component = destroy_managed_text_component(actor, key);
        log_line("[save] stale_record_pruned_on_construct key=" + key +
                 " removedComponent=" + std::string{removed_component ? "true" : "false"});
        save_sidecar_json("new_construct_overrides_stale_record", key, stable_id, world_id);
        return true;
    }

    auto SignTextMod::should_hold_restore_for_first_seen_post_ready(
        const std::string& key,
        const std::string& world_id,
        bool first_seen_live_after_ready,
        bool is_ready_baseline_key) -> bool
    {
        if (!m_session_ready_latched ||
            is_ready_baseline_key ||
            key.empty() ||
            world_id.empty())
        {
            if (is_ready_baseline_key)
            {
                m_first_seen_construct_hold_states.erase(key);
            }
            return false;
        }

        const bool authoritative_localclient =
            m_sidecar_authoritative && lower_ascii(m_runtime_role) == "localclient";
        const bool authoritative_dedicated =
            m_sidecar_authoritative && is_dedicated_runtime_process();
        if (!authoritative_localclient && !authoritative_dedicated)
        {
            m_first_seen_construct_hold_states.erase(key);
            return false;
        }

        auto existing_it = m_first_seen_construct_hold_states.find(key);
        if (existing_it == m_first_seen_construct_hold_states.end())
        {
            if (!first_seen_live_after_ready)
            {
                return false;
            }
            FirstSeenConstructHoldState initial_state{};
            initial_state.world_id = world_id;
            initial_state.session_epoch = m_session_epoch;
            initial_state.first_seen_scan_cycle = m_restore_scan_cycle_counter;
            initial_state.hold_logged = false;
            existing_it = m_first_seen_construct_hold_states.emplace(key, std::move(initial_state)).first;
        }

        auto& hold_state = existing_it->second;
        if (hold_state.session_epoch != m_session_epoch ||
            lower_ascii(hold_state.world_id) != lower_ascii(world_id))
        {
            if (!first_seen_live_after_ready)
            {
                m_first_seen_construct_hold_states.erase(existing_it);
                return false;
            }
            hold_state = {};
            hold_state.world_id = world_id;
            hold_state.session_epoch = m_session_epoch;
            hold_state.first_seen_scan_cycle = m_restore_scan_cycle_counter;
            hold_state.hold_logged = false;
        }

        constexpr uint64_t k_construct_hold_scans = 2;
        const uint64_t elapsed_scans =
            (m_restore_scan_cycle_counter >= hold_state.first_seen_scan_cycle)
                ? (m_restore_scan_cycle_counter - hold_state.first_seen_scan_cycle)
                : 0;
        if (elapsed_scans < k_construct_hold_scans)
        {
            if (!hold_state.hold_logged)
            {
                log_line("[save] restore_deferred_construct_hold key=" + key +
                         " worldId=" + world_id +
                         " elapsedScans=" + std::to_string(elapsed_scans) +
                         " holdScans=" + std::to_string(k_construct_hold_scans));
                hold_state.hold_logged = true;
            }
            return true;
        }

        m_first_seen_construct_hold_states.erase(key);
        return false;
    }

    auto SignTextMod::reset_localclient_role_lock_restore_pass_state() -> void
    {
        m_role_lock_restore_pass1_pending = false;
        m_role_lock_restore_pass1_done = false;
        m_role_lock_restore_pass2_pending = false;
        m_role_lock_restore_pass2_done = false;
        m_role_lock_restore_epoch = 0;
        m_role_lock_restore_pass2_due = {};
    }

    auto SignTextMod::schedule_localclient_role_lock_restore_passes(const std::string& reason) -> void
    {
        if (is_dedicated_runtime_process())
        {
            return;
        }
        if (!m_sidecar_authoritative || lower_ascii(m_runtime_role) != "localclient")
        {
            return;
        }
        if (!m_session_ready_latched || !m_role_lock_acquired)
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        m_role_lock_restore_pass1_pending = true;
        m_role_lock_restore_pass1_done = false;
        m_role_lock_restore_pass2_pending = true;
        m_role_lock_restore_pass2_done = false;
        m_role_lock_restore_epoch = m_session_epoch;
        m_role_lock_restore_pass2_due = now + std::chrono::seconds(8);
        log_line("[save] role_lock_restore_scheduled reason=" + reason +
                 " epoch=" + std::to_string(m_role_lock_restore_epoch) +
                 " pass2DelaySec=8");
    }

    auto SignTextMod::maybe_run_localclient_role_lock_restore_passes(const std::chrono::steady_clock::time_point now) -> void
    {
        if (is_dedicated_runtime_process() ||
            !m_sidecar_authoritative ||
            lower_ascii(m_runtime_role) != "localclient" ||
            !m_session_ready_latched ||
            !m_role_lock_acquired)
        {
            return;
        }
        if (m_role_lock_restore_epoch != m_session_epoch)
        {
            return;
        }

        int pass_to_run = 0;
        if (m_role_lock_restore_pass1_pending && !m_role_lock_restore_pass1_done)
        {
            pass_to_run = 1;
        }
        else if (m_role_lock_restore_pass2_pending &&
                 !m_role_lock_restore_pass2_done &&
                 m_role_lock_restore_pass2_due.time_since_epoch().count() != 0 &&
                 now >= m_role_lock_restore_pass2_due)
        {
            pass_to_run = 2;
        }

        if (pass_to_run == 0)
        {
            return;
        }

        uint32_t matched_labels = 0;
        uint32_t restore_calls = 0;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->IsA(AActor::StaticClass()))
            {
                return LoopAction::Continue;
            }
            auto* actor = Cast<AActor>(object);
            if (!actor || !is_probable_label_actor(actor))
            {
                return LoopAction::Continue;
            }
            const auto stable_id = extract_stable_id(actor);
            const auto actor_world_id = build_world_id_for_actor(actor);
            configure_sidecar_for_actor(actor, actor_world_id);
            const auto world_id = active_storage_world_id(actor_world_id);
            const auto key = build_storage_key(world_id, stable_id);
            if (m_labels.find(key) == m_labels.end())
            {
                return LoopAction::Continue;
            }
            ++matched_labels;
            restore_known_text_if_any(actor, stable_id, true);
            ++restore_calls;
            return LoopAction::Continue;
        });

        if (pass_to_run == 1)
        {
            m_role_lock_restore_pass1_done = true;
            m_role_lock_restore_pass1_pending = false;
        }
        else
        {
            m_role_lock_restore_pass2_done = true;
            m_role_lock_restore_pass2_pending = false;
        }

        log_line("[save] role_lock_restore_pass pass=" + std::to_string(pass_to_run) +
                 " matched=" + std::to_string(matched_labels) +
                 " restoreCalls=" + std::to_string(restore_calls) +
                 " epoch=" + std::to_string(m_session_epoch));
    }

    auto SignTextMod::mark_suspect_rebuild(
        const std::string& key,
        const std::string& stable_id,
        const std::string& world_id,
        uintptr_t old_actor_ptr,
        uintptr_t new_actor_ptr,
        const std::chrono::steady_clock::time_point now) -> void
    {
        auto& state = m_suspect_rebuild_states[key];
        const bool first = state.first_detected.time_since_epoch().count() == 0;
        if (first ||
            state.replacement_actor_ptr != new_actor_ptr ||
            state.old_actor_ptr != old_actor_ptr)
        {
            state.stable_id = stable_id;
            state.world_id = world_id;
            state.session_epoch = m_session_epoch;
            state.old_actor_ptr = old_actor_ptr;
            state.replacement_actor_ptr = new_actor_ptr;
            state.stable_scan_hits = 1;
            state.live_scans_post_ready = 0;
            state.last_live_scan_cycle = 0;
            state.unsuppress_fallback_issued = false;
            state.first_detected = now;
            log_line("[save] suspect_rebuild mark key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + world_id +
                     " epoch=" + std::to_string(m_session_epoch) +
                     " oldPtr=" + std::to_string(static_cast<unsigned long long>(old_actor_ptr)) +
                     " newPtr=" + std::to_string(static_cast<unsigned long long>(new_actor_ptr)));
            trace_behavior_sm("suspect_rebuild_mark",
                              "key=" + key +
                              " stableId=" + stable_id +
                              " worldId=" + world_id +
                              " epoch=" + std::to_string(m_session_epoch));
        }
        else
        {
            ++state.stable_scan_hits;
        }
        state.last_seen = now;
        state.suppress_until = now + std::chrono::seconds(12);
    }

    auto SignTextMod::expire_suspect_rebuild_states(const std::chrono::steady_clock::time_point now) -> void
    {
        constexpr auto k_suspect_timeout = std::chrono::seconds(20);
        for (auto it = m_suspect_rebuild_states.begin(); it != m_suspect_rebuild_states.end();)
        {
            if (it->second.first_detected.time_since_epoch().count() != 0 &&
                (now - it->second.first_detected) > k_suspect_timeout)
            {
                log_line("[save] suspect_rebuild rollback key=" + it->first +
                         " stableId=" + it->second.stable_id +
                         " worldId=" + it->second.world_id +
                         " reason=timeout_no_inprocess_confirmation");
                trace_behavior_sm("suspect_rebuild_rollback",
                                  "key=" + it->first +
                                  " stableId=" + it->second.stable_id +
                                  " worldId=" + it->second.world_id +
                                  " reason=timeout_no_inprocess_confirmation");
                it = m_suspect_rebuild_states.erase(it);
                continue;
            }
            ++it;
        }
    }

    auto SignTextMod::maybe_promote_suspect_rebuild_to_prune(
        const std::string& key,
        const std::string& stable_id,
        const std::string& world_id,
        const std::chrono::steady_clock::time_point now) -> SuspectRebuildDecision
    {
        auto found = m_suspect_rebuild_states.find(key);
        if (found == m_suspect_rebuild_states.end())
        {
            return SuspectRebuildDecision::None;
        }

        auto& state = found->second;
        if (state.first_detected.time_since_epoch().count() == 0)
        {
            return SuspectRebuildDecision::None;
        }
        if (state.session_epoch != m_session_epoch ||
            state.world_id != world_id ||
            state.stable_id != stable_id)
        {
            return SuspectRebuildDecision::None;
        }

        const bool hosted_localclient =
            !is_dedicated_runtime_process() &&
            m_sidecar_authoritative &&
            m_bridge_role == BridgeRole::ListenHost;
        const bool dedicated_authoritative_runtime =
            is_dedicated_runtime_process() &&
            m_sidecar_authoritative;

        if (hosted_localclient && m_session_ready_latched)
        {
            if (state.last_live_scan_cycle != m_restore_scan_cycle_counter)
            {
                state.last_live_scan_cycle = m_restore_scan_cycle_counter;
                ++state.live_scans_post_ready;
            }
        }

        if (has_recent_destroy_confirmation(stable_id, world_id))
        {
            log_line("[save] suspect_rebuild confirmed key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + world_id +
                     " reason=destroy_confirmed_r5log" +
                     " stableHits=" + std::to_string(state.stable_scan_hits));
            trace_behavior_sm("suspect_rebuild_confirmed",
                              "key=" + key +
                              " stableId=" + stable_id +
                              " worldId=" + world_id +
                              " reason=destroy_confirmed_r5log");
            m_suspect_rebuild_states.erase(found);
            return SuspectRebuildDecision::PromotePrune;
        }

        const bool old_actor_gone = !is_actor_pointer_live(reinterpret_cast<AActor*>(state.old_actor_ptr));
        const bool replacement_live = is_actor_pointer_live(reinterpret_cast<AActor*>(state.replacement_actor_ptr));
        const bool stable_replacement = state.stable_scan_hits >= 2;
        const bool matured = (now - state.first_detected) >= std::chrono::seconds(2);
        const bool local_or_hosted_gameplay_window_active =
            (m_last_player_activity.time_since_epoch().count() != 0 &&
             (now - m_last_player_activity) <= std::chrono::seconds(20)) ||
            m_hosted_ready_sequence_complete;
        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const auto authority_mode_lower = lower_ascii(m_authority_mode);
        const bool authority_source_resolved =
            runtime_role_lower != "localclientpending" &&
            authority_mode_lower != "worldauthoritypending";
        const bool dedicated_rebuild_promotion_window_active =
            dedicated_authoritative_runtime &&
            m_restore_scan_has_seen_live_labels &&
            authority_source_resolved;
        const bool promotion_window_active =
            dedicated_authoritative_runtime
                ? dedicated_rebuild_promotion_window_active
                : local_or_hosted_gameplay_window_active;
        const bool trusted_context =
            m_session_ready_latched &&
            state.session_epoch == m_session_epoch &&
            state.world_id == world_id;

        if (trusted_context &&
            old_actor_gone &&
            replacement_live &&
            stable_replacement &&
            matured &&
            promotion_window_active)
        {
            const std::string reason =
                hosted_localclient
                    ? "destroy_confirmed_inprocess_hosted"
                    : (dedicated_authoritative_runtime
                           ? "inprocess_confirmation_dedicated"
                           : "inprocess_confirmation");
            log_line("[save] suspect_rebuild confirmed key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + world_id +
                     " reason=" + reason +
                     " stableHits=" + std::to_string(state.stable_scan_hits));
            trace_behavior_sm("suspect_rebuild_confirmed",
                              "key=" + key +
                              " stableId=" + stable_id +
                              " worldId=" + world_id +
                              " reason=" + reason);
            m_suspect_rebuild_states.erase(found);
            return SuspectRebuildDecision::PromotePrune;
        }

        constexpr uint32_t k_hosted_unsuppress_scans = 3;
        if (hosted_localclient &&
            m_session_ready_latched &&
            !state.unsuppress_fallback_issued &&
            state.live_scans_post_ready >= k_hosted_unsuppress_scans)
        {
            state.unsuppress_fallback_issued = true;
            state.suppress_until = now;
            log_line("[save] suspect_rebuild unsuppress_fallback key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + world_id +
                     " epoch=" + std::to_string(state.session_epoch) +
                     " scansPostReady=" + std::to_string(state.live_scans_post_ready) +
                     " reason=live_stable_no_trusted_destroy_confirmation");
            trace_behavior_sm("suspect_rebuild_unsuppress_fallback",
                              "key=" + key +
                              " stableId=" + stable_id +
                              " worldId=" + world_id +
                              " scansPostReady=" + std::to_string(state.live_scans_post_ready));
            return SuspectRebuildDecision::UnsuppressRestoreOnce;
        }

        return SuspectRebuildDecision::None;
    }

    auto SignTextMod::tick_localclient_role_resolution() -> void
    {
        const auto log_blocked = [&](const std::string& reason) {
            if (m_pending_resolution_last_block_reason != reason)
            {
                m_pending_resolution_last_block_reason = reason;
                log_line("[role] pending_resolution_blocked reason=" + reason +
                         " runtimeRole=" + m_runtime_role +
                         " authorityMode=" + m_authority_mode);
                trace_behavior_sm("role_pending_blocked",
                                  "reason=" + reason +
                                  " runtimeRole=" + m_runtime_role +
                                  " authorityMode=" + m_authority_mode);
            }
        };
        if (is_dedicated_runtime_process())
        {
            return;
        }
        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        if (runtime_role_lower != "localclientpending")
        {
            m_pending_role_watchdog_started = {};
            m_pending_role_watchdog_logged = false;
            m_pending_resolution_last_block_reason.clear();
            m_pending_resolution_last_controller_signature.clear();
            return;
        }
        if (!m_session_ready_latched)
        {
            log_blocked("session_ready_not_latched");
            return;
        }

        auto* controller = try_get_primary_player_controller();
        if (!controller || !controller->IsA(AActor::StaticClass()))
        {
            log_blocked("no_valid_player_controller");
            return;
        }
        auto* controller_actor = Cast<AActor>(controller);
        auto* world = controller_actor ? controller_actor->GetWorld() : nullptr;
        if (!world)
        {
            log_blocked("controller_world_unavailable");
            return;
        }

        UObject* controlled_pawn = nullptr;
        const bool got_pawn_from_fn = invoke_object_return_no_param(
            controller,
            STR("GetPawn"),
            STR("/Script/Engine.Controller:GetPawn"),
            controlled_pawn);
        if (!got_pawn_from_fn || !controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "AcknowledgedPawn");
        }
        const bool pawn_valid = controlled_pawn != nullptr;

        const auto world_name = lower_ascii(narrow_ascii(world->GetName()));
        if (world_name.empty() ||
            world_name == "none" ||
            world_name.find("transition") != std::string::npos ||
            world_name.find("lobby") != std::string::npos ||
            world_name.find("entrance") != std::string::npos)
        {
            log_blocked("controller_world_transitional");
            return;
        }

        int controller_score = 0;
        bool is_local_controller = false;
        if (invoke_bool_return_no_param(
                controller,
                STR("IsLocalController"),
                STR("/Script/Engine.Controller:IsLocalController"),
                is_local_controller) &&
            is_local_controller)
        {
            controller_score += 200;
        }
        if (pawn_valid)
        {
            controller_score += 120;
        }
        if (world_name.find("lobby") != std::string::npos ||
            world_name.find("transition") != std::string::npos ||
            world_name.find("entrance") != std::string::npos)
        {
            controller_score -= 120;
        }
        else
        {
            controller_score += 60;
        }

        const auto authority_mode_lower = lower_ascii(m_authority_mode);
        const bool authority_source_resolved = authority_mode_lower != "worldauthoritypending";
        const bool hosted_ready_resolved = m_hosted_ready_world_client_seen &&
            m_hosted_ready_player_ready_seen &&
            m_hosted_ready_datakeeper_seen &&
            m_hosted_ready_hide_loading_seen;
        bool can_resolve_role = authority_source_resolved || hosted_ready_resolved;
        const auto now = std::chrono::steady_clock::now();

        const auto controller_signature =
            world_name + "|score=" + std::to_string(controller_score) +
            "|pawn=" + std::string{pawn_valid ? "1" : "0"};
        if (m_pending_resolution_last_controller_signature != controller_signature)
        {
            m_pending_resolution_last_controller_signature = controller_signature;
            log_line("[controller] candidate score=" + std::to_string(controller_score) +
                     " world=" + world_name +
                     " pawnValid=" + std::string{pawn_valid ? "true" : "false"} +
                     " selected=true");
        }

        if (!can_resolve_role)
        {
            log_blocked("authority_or_hosted_ready_not_resolved");
            if (m_hosted_ready_hide_loading_seen &&
                m_pending_role_watchdog_started.time_since_epoch().count() == 0)
            {
                m_pending_role_watchdog_started = now;
            }
            if (!m_pending_role_watchdog_logged &&
                m_pending_role_watchdog_started.time_since_epoch().count() != 0 &&
                (now - m_pending_role_watchdog_started) >= std::chrono::seconds(20))
            {
                m_pending_role_watchdog_logged = true;
                log_line("[role] pending_watchdog runtimeRole=" + m_runtime_role +
                         " authorityMode=" + m_authority_mode +
                         " world=" + world_name +
                         " hideLoadingSeen=true");
            }
            if (m_pending_role_watchdog_logged)
            {
                can_resolve_role = true;
                log_line("[role] pending_resolution_attempt runtimeRole=" + m_runtime_role +
                         " authorityMode=" + m_authority_mode +
                         " hostedReady=false world=" + world_name +
                         " forcedByWatchdog=true");
                trace_behavior_sm("role_pending_attempt",
                                  "runtimeRole=" + m_runtime_role +
                                  " authorityMode=" + m_authority_mode +
                                  " hostedReady=false world=" + world_name +
                                  " forcedByWatchdog=true");
            }
            else
            {
                return;
            }
        }

        if (!(m_pending_role_watchdog_logged && !hosted_ready_resolved && !authority_source_resolved))
        {
            log_line("[role] pending_resolution_attempt runtimeRole=" + m_runtime_role +
                     " authorityMode=" + m_authority_mode +
                     " hostedReady=" + std::string{hosted_ready_resolved ? "true" : "false"} +
                     " world=" + world_name);
            trace_behavior_sm("role_pending_attempt",
                              "runtimeRole=" + m_runtime_role +
                              " authorityMode=" + m_authority_mode +
                              " hostedReady=" + std::string{hosted_ready_resolved ? "true" : "false"} +
                              " world=" + world_name);
        }
        m_pending_resolution_last_block_reason.clear();

        const auto controller_world_id = build_world_id_for_actor(controller_actor);
        std::string worldid_latch_reason{};
        if (!is_world_id_latched_for_authoritative_localclient_bind(&worldid_latch_reason))
        {
            log_blocked("worldid_latch_pending_" + worldid_latch_reason);
            return;
        }

        const auto latched_world_id = m_worldid_latched_id;
        configure_sidecar_for_actor(controller_actor, latched_world_id);
        if (lower_ascii(m_runtime_role) == "localclient")
        {
            m_pending_role_watchdog_started = {};
            m_pending_role_watchdog_logged = false;
            log_line("[role] resolved runtimeRole=" + m_runtime_role + " reason=session_ready");
            log_line("[role] pending_resolution_success runtimeRole=" + m_runtime_role +
                     " authorityMode=" + m_authority_mode +
                     " bridgeRole=" + bridge_role_name(m_bridge_role) +
                     " worldId=" + latched_world_id);
            trace_behavior_sm("role_pending_success",
                              "runtimeRole=" + m_runtime_role +
                              " authorityMode=" + m_authority_mode +
                              " bridgeRole=" + bridge_role_name(m_bridge_role) +
                              " worldId=" + latched_world_id +
                              " path=existing_localclient");
            return;
        }

        // Fallback for hosted/local churn where authority probing lags behind readiness.
        std::filesystem::path profile_root{};
        if (!m_save_profile_root.empty() && std::filesystem::exists(m_save_profile_root))
        {
            profile_root = std::filesystem::path{m_save_profile_root};
        }
        else
        {
            const auto profile_roots = collect_local_client_save_profile_roots();
            if (!profile_roots.empty())
            {
                profile_root = profile_roots.front();
            }
        }
        auto world_folder_id = m_worldid_latched_id;
        if (!is_hex_world_id(world_folder_id))
        {
            log_blocked("worldid_latch_invalid_for_fallback_route");
            return;
        }
        const auto data_root = !profile_root.empty()
            ? profile_root / "WindroseTextSigns" / world_folder_id
            : m_mod_root / "Cache" / "LocalAuthoritative" / world_folder_id;
        set_sidecar_route(
            data_root,
            "LocalClient",
            profile_root.empty() ? "LocalProfileAuthoritativeFallbackModRoot" : "LocalProfileAuthoritative",
            "LocalClientSoloOrHostedAuthoritative",
            profile_root.empty() ? "authoritative-fallback" : "authoritative",
            true,
            profile_root,
            world_folder_id,
            "pending_role_resolution");
        configure_bridge_role("pending_role_resolution");
        maybe_acquire_role_lock(now, "pending_role_resolution");
        log_line("[role] resolved runtimeRole=" + m_runtime_role + " reason=session_ready");
        log_line("[role] pending_resolution_success runtimeRole=" + m_runtime_role +
                 " authorityMode=" + m_authority_mode +
                 " bridgeRole=" + bridge_role_name(m_bridge_role) +
                 " worldId=" + world_folder_id);
        trace_behavior_sm("role_pending_success",
                          "runtimeRole=" + m_runtime_role +
                          " authorityMode=" + m_authority_mode +
                          " bridgeRole=" + bridge_role_name(m_bridge_role) +
                          " worldId=" + world_folder_id +
                          " path=fallback_route_set");
        m_pending_role_watchdog_started = {};
        m_pending_role_watchdog_logged = false;
    }

    auto SignTextMod::reset_session_state(const std::string& reason) -> void
    {
        const auto old_epoch = m_session_epoch;
        const bool had_ready = m_session_ready_latched;
        const bool had_locks = m_role_lock_acquired || m_bridge_route_lock_acquired;
        const bool force_reset = reason == "definitive_session_start";
        const bool preserve_hosted_ready_markers =
            reason == "world_inactive" && lower_ascii(m_runtime_role) == "localclientpending";
        const bool had_ready_markers =
            m_hosted_ready_world_client_seen ||
            m_hosted_ready_player_ready_seen ||
            m_hosted_ready_datakeeper_seen ||
            m_hosted_ready_hide_loading_seen ||
            m_hosted_ready_sequence_complete;
        if (!force_reset &&
            !had_ready &&
            !had_locks &&
            !had_ready_markers &&
            m_bridge_role == BridgeRole::Unknown)
        {
            return;
        }
        ++m_session_epoch;
        m_session_ready_latched = false;
        m_session_ready_world_id.clear();
        m_worldid_bind_phase = WorldIdBindPhase::Unbound;
        m_worldid_provisional_id.clear();
        m_worldid_latched_id.clear();
        m_worldid_stability_seen_count = 0;
        m_worldid_stability_last_observed = {};
        m_worldid_generation_last_signal = {};
        m_worldid_generation_last_marker_id.clear();
        m_worldid_generation_in_progress = false;
        m_worldid_last_defer_reason.clear();
        m_worldid_last_defer_log = {};
        m_definitive_session_start_candidate_active = false;
        m_definitive_session_start_candidate_signal.clear();
        m_definitive_session_start_candidate_world_hint.clear();
        m_pending_role_watchdog_started = {};
        m_pending_role_watchdog_logged = false;
        m_pending_resolution_last_block_reason.clear();
        m_pending_resolution_last_controller_signature.clear();
        if (!preserve_hosted_ready_markers)
        {
            m_hosted_ready_world_client_seen = false;
            m_hosted_ready_player_ready_seen = false;
            m_hosted_ready_datakeeper_seen = false;
            m_hosted_ready_hide_loading_seen = false;
            m_hosted_ready_sequence_complete = false;
        }
        m_hosted_post_ready_reconcile_done = false;
        const bool definitive_teardown =
            m_phase7_definitive_teardown_armed ||
            reason.rfind("definitive_", 0) == 0;
        if (definitive_teardown)
        {
            force_close_phase7_for_teardown("session_reset_" + reason);
            invalidate_phase7_umg_widget_cache("session_reset_" + reason);
        }
        else
        {
            log_line("[phase7] teardown_suppressed reason=non_definitive_state detail=session_reset_" + reason);
            m_phase7_teardown_pending = false;
            m_phase7_teardown_pending_reason.clear();
            m_phase7_definitive_teardown_armed = false;
            m_phase7_definitive_teardown_reason.clear();
        }
        m_phase7_umg_prewarm_attempted = false;
        m_phase7_umg_prewarm_succeeded = false;
        m_phase7_umg_prewarm_next_try = {};
        m_phase7_teardown_skip_logged = false;
        m_phase7_teardown_suppressed_last_reason.clear();
        m_phase7_teardown_suppressed_last_log = {};
        m_phase7_guard_fail_started = {};
        m_phase7_guard_fail_reason.clear();
        m_phase7_guard_hysteresis_logged = false;
        m_phase7_stale_epoch_last_log = {};
        m_phase7_stale_epoch_last_detail.clear();
        m_pending_world_inactive_ignored_logged = false;
        m_locked_world_inactive_ignored_logged = false;
        m_world_inactive_since = {};
        m_ready_baseline_live_keys.clear();
        m_ready_baseline_capture_remaining_scans = 0;
        m_first_seen_construct_hold_states.clear();
        m_suspect_rebuild_states.clear();
        m_recent_construct_slot_signals.clear();
        m_restore_skip_guard_log_buckets.clear();
        m_phase4_last_failure_reason.clear();
        m_create_null_retry_states.clear();
        m_bridge_route_retry_consumed = false;
        m_localclient_controller_probe_cached = nullptr;
        m_localclient_controller_probe_cache_valid = false;
        m_localclient_controller_probe_last = {};
        m_destroy_signal_log_path.clear();
        m_destroy_signal_log_offset = 0;
        m_destroy_signal_log_initialized = false;
        reset_visual_verify_debug_state();
        reset_role_route_locks("session_reset_" + reason);
        reset_bridge_snapshot_state("session_reset_" + reason);
        if (!is_dedicated_runtime_process())
        {
            m_bridge_role = BridgeRole::Unknown;
            NativeBridge::instance().set_role(m_bridge_role);
        }
        if (had_ready || had_locks)
        {
            log_line("[session] reset reason=" + reason +
                     " oldEpoch=" + std::to_string(old_epoch) +
                     " newEpoch=" + std::to_string(m_session_epoch));
        }
    }

    auto SignTextMod::is_session_ready_for_role_resolution(std::string* out_reason) -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };

        if (is_dedicated_runtime_process())
        {
            set_reason("dedicated_runtime");
            return true;
        }

        auto* controller = try_get_primary_player_controller();
        auto* controller_actor = controller && controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
        if (!controller_actor)
        {
            set_reason("no_valid_player_controller");
            return false;
        }
        auto* world = controller_actor->GetWorld();
        if (!world)
        {
            set_reason("controller_world_unavailable");
            return false;
        }
        const auto world_name = lower_ascii(narrow_ascii(world->GetName()));
        if (world_name.empty() ||
            world_name == "none" ||
            world_name.find("transition") != std::string::npos ||
            world_name.find("lobby") != std::string::npos ||
            world_name.find("entrance") != std::string::npos)
        {
            set_reason("controller_world_transition");
            return false;
        }

        UObject* controlled_pawn = nullptr;
        const bool got_pawn_from_fn = invoke_object_return_no_param(
            controller,
            STR("GetPawn"),
            STR("/Script/Engine.Controller:GetPawn"),
            controlled_pawn);
        if (!got_pawn_from_fn || !controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "Pawn");
        }
        if (!controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "AcknowledgedPawn");
        }
        if (!controlled_pawn)
        {
            set_reason("controlled_pawn_missing");
            return false;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const auto authority_mode_lower = lower_ascii(m_authority_mode);
        const bool authority_source_resolved =
            runtime_role_lower != "localclientpending" &&
            authority_mode_lower != "worldauthoritypending";
        const bool local_ready_equivalent =
            m_hosted_ready_sequence_complete ||
            (m_hosted_ready_datakeeper_seen && m_hosted_ready_hide_loading_seen);
        const bool gameplay_ready_inprocess =
            controlled_pawn != nullptr &&
            !world_name.empty() &&
            world_name != "none" &&
            world_name.find("transition") == std::string::npos &&
            world_name.find("lobby") == std::string::npos &&
            world_name.find("entrance") == std::string::npos;
        const bool local_ready_signals_seen =
            m_hosted_ready_datakeeper_seen ||
            m_hosted_ready_hide_loading_seen ||
            m_hosted_ready_player_ready_seen ||
            m_hosted_ready_sequence_complete;
        const bool pending_localclient_inprocess_fallback =
            runtime_role_lower == "localclientpending" &&
            authority_mode_lower == "worldauthoritypending" &&
            gameplay_ready_inprocess &&
            local_ready_signals_seen;
        if (pending_localclient_inprocess_fallback)
        {
            set_reason("ready_inprocess_pending_fallback");
            return true;
        }
        if (!authority_source_resolved && !local_ready_equivalent)
        {
            set_reason("authority_or_local_ready_pending");
            return false;
        }

        set_reason("ready");
        return true;
    }

    auto SignTextMod::maybe_run_hosted_post_ready_reconcile() -> void
    {
        std::string stability_reason{};
        if (!is_localclient_runtime_stable_for_post_ready(&stability_reason))
        {
            return;
        }
        if (m_hosted_post_ready_reconcile_done)
        {
            return;
        }
        if (!m_sidecar_authoritative || m_bridge_role != BridgeRole::ListenHost)
        {
            return;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const auto authority_mode_lower = lower_ascii(m_authority_mode);
        const bool authority_source_resolved =
            runtime_role_lower != "localclientpending" &&
            authority_mode_lower != "worldauthoritypending";
        if (!is_localclient_prune_ready(authority_source_resolved, nullptr))
        {
            return;
        }
        if (!m_hosted_ready_sequence_complete)
        {
            return;
        }

        uint32_t matched_labels = 0;
        uint32_t restore_calls = 0;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->IsA(AActor::StaticClass()))
            {
                return LoopAction::Continue;
            }
            auto* actor = Cast<AActor>(object);
            if (!actor || !is_probable_label_actor(actor))
            {
                return LoopAction::Continue;
            }
            const auto stable_id = extract_stable_id(actor);
            const auto actor_world_id = build_world_id_for_actor(actor);
            configure_sidecar_for_actor(actor, actor_world_id);
            const auto world_id = active_storage_world_id(actor_world_id);
            const auto key = build_storage_key(world_id, stable_id);
            if (m_labels.find(key) == m_labels.end())
            {
                return LoopAction::Continue;
            }
            ++matched_labels;
            restore_known_text_if_any(actor, stable_id, true);
            ++restore_calls;
            return LoopAction::Continue;
        });

        if (restore_calls > 0)
        {
            m_hosted_post_ready_reconcile_done = true;
            log_line("[save] hosted_post_ready_reconcile matched=" + std::to_string(matched_labels) +
                     " restoreCalls=" + std::to_string(restore_calls) +
                     " readySequence=true");
        }
    }

    auto SignTextMod::is_localclient_runtime_stable_for_post_ready(std::string* out_reason) -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };

        if (is_dedicated_runtime_process())
        {
            set_reason("dedicated_runtime");
            return false;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const auto authority_mode_lower = lower_ascii(m_authority_mode);
        if (runtime_role_lower != "localclient")
        {
            set_reason("runtime_role_not_localclient");
            return false;
        }
        if (authority_mode_lower == "worldauthoritypending")
        {
            set_reason("authority_pending");
            return false;
        }

        auto* controller = try_get_primary_player_controller();
        if (!controller || !controller->IsA(AActor::StaticClass()))
        {
            set_reason("no_valid_player_controller");
            return false;
        }
        auto* controller_actor = Cast<AActor>(controller);
        auto* world = controller_actor ? controller_actor->GetWorld() : nullptr;
        if (!world)
        {
            set_reason("controller_world_unavailable");
            return false;
        }
        const auto world_name = lower_ascii(narrow_ascii(world->GetName()));
        if (world_name.find("transition") != std::string::npos ||
            world_name.find("lobby") != std::string::npos ||
            world_name.find("entrance") != std::string::npos)
        {
            set_reason("controller_world_transition");
            return false;
        }

        UObject* controlled_pawn = nullptr;
        const bool got_pawn_from_fn = invoke_object_return_no_param(
            controller,
            STR("GetPawn"),
            STR("/Script/Engine.Controller:GetPawn"),
            controlled_pawn);
        if (!got_pawn_from_fn || !controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "Pawn");
        }
        if (!controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "AcknowledgedPawn");
        }
        if (!controlled_pawn)
        {
            set_reason("controlled_pawn_missing");
            return false;
        }

        set_reason("stable");
        return true;
    }

    auto SignTextMod::reset_visual_verify_debug_state() -> void
    {
        m_visual_verify_session_ready = false;
        m_visual_verify_pass1_done = false;
        m_visual_verify_pass2_done = false;
        m_visual_verify_pass3_done = false;
        m_visual_verify_motion_logged = false;
        m_visual_verify_pass1_scan_cycle = 0;
        m_visual_verify_ready_at = {};
        m_visual_verify_ready_pawn_loc = FVector(0.0, 0.0, 0.0);
        m_visual_verify_ready_pawn_loc_valid = false;
        m_visual_verify_expected_keys.clear();
        m_visual_verify_last_result.clear();
        m_visual_verify_last_tier.clear();
        m_visual_verify_recently_rendered_streak.clear();
        m_visual_verify_no_render_streak.clear();
        m_visual_verify_last_render_time_seen.clear();
    }

    auto SignTextMod::run_localclient_visual_verify_pass(
        int pass_number,
        bool apply_before_verify,
        bool force_reapply,
        const std::string& reason) -> void
    {
        if (pass_number <= 0)
        {
            return;
        }
        if (!m_sidecar_authoritative || is_dedicated_runtime_process())
        {
            return;
        }
        if (lower_ascii(m_runtime_role) != "localclient")
        {
            return;
        }

        auto* controller = try_get_primary_player_controller();
        if (!controller)
        {
            return;
        }
        auto* controller_actor = controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
        if (!controller_actor)
        {
            return;
        }

        UObject* controlled_pawn = nullptr;
        const bool got_pawn_from_fn = invoke_object_return_no_param(
            controller,
            STR("GetPawn"),
            STR("/Script/Engine.Controller:GetPawn"),
            controlled_pawn);
        if (!got_pawn_from_fn || !controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "Pawn");
        }
        if (!controlled_pawn)
        {
            controlled_pawn = get_object_property_if_present(controller, "AcknowledgedPawn");
        }
        auto* pawn_actor = controlled_pawn && controlled_pawn->IsA(AActor::StaticClass()) ? Cast<AActor>(controlled_pawn) : nullptr;

        const auto controller_world_id = build_world_id_for_actor(controller_actor);
        const auto active_world_id = active_storage_world_id(controller_world_id);
        if (!active_world_id.empty() && m_visual_verify_expected_keys.empty())
        {
            for (const auto& [key, rec] : m_labels)
            {
                if (rec.world_id != active_world_id || rec.text.empty() || !is_confirmed_label_text_kind(rec.kind))
                {
                    continue;
                }
                m_visual_verify_expected_keys.insert(key);
            }
        }

        std::unordered_map<std::string, AActor*> actors_by_key{};
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->IsA(AActor::StaticClass()))
            {
                return LoopAction::Continue;
            }
            auto* actor = Cast<AActor>(object);
            if (!actor || !is_probable_label_actor(actor))
            {
                return LoopAction::Continue;
            }
            const auto stable_id = extract_stable_id(actor);
            const auto actor_world_id = build_world_id_for_actor(actor);
            const auto key = build_storage_key(active_storage_world_id(actor_world_id), stable_id);
            if (m_visual_verify_expected_keys.find(key) == m_visual_verify_expected_keys.end())
            {
                return LoopAction::Continue;
            }
            actors_by_key[key] = actor;
            return LoopAction::Continue;
        });

        uint32_t pass_count = 0;
        uint32_t warn_count = 0;
        uint32_t fail_count = 0;
        const auto view = get_player_viewpoint_reflective(controller);
        const auto view_forward = view.valid ? vec_normalize(rotation_to_forward(view.rotation)) : FVector(0.0, 0.0, 0.0);
        const float max_dist = std::max(100.0f, static_cast<float>(safe_stoi(config_string_value("WTS_MAX_TARGET_DISTANCE", "1000"), 1000)));
        auto read_last_render_time = [&](UObject* component, float& out_value) -> bool {
            float on_screen = 0.0f;
            if (get_float_property_if_present(component, "lastrendertimeonscreen", on_screen))
            {
                out_value = on_screen;
                return true;
            }

            float any_render = 0.0f;
            if (get_float_property_if_present(component, "lastrendertime", any_render))
            {
                out_value = any_render;
                return true;
            }

            return false;
        };

        log_line("[visual-verify] pass_start pass=" + std::to_string(pass_number) +
                 " reason=" + reason +
                 " expectedKeys=" + std::to_string(m_visual_verify_expected_keys.size()) +
                 " forceReapply=" + std::string{force_reapply ? "true" : "false"} +
                 " applyBeforeVerify=" + std::string{apply_before_verify ? "true" : "false"});

        for (const auto& key : m_visual_verify_expected_keys)
        {
            const auto rec_it = m_labels.find(key);
            if (rec_it == m_labels.end())
            {
                continue;
            }
            const auto& rec = rec_it->second;
            auto* actor = actors_by_key.count(key) > 0 ? actors_by_key[key] : nullptr;

            bool component_exists = false;
            bool component_registered = false;
            bool component_attached = false;
            bool component_pending_kill = false;
            bool render_state_created = false;
            bool hidden_in_game = true;
            bool visible = false;
            bool text_non_empty = false;
            bool text_matches_expected = false;
            bool projectable = false;
            bool in_front = false;
            bool in_viewport = false;
            bool within_fov = false;
            bool recently_rendered = false;
            bool recently_rendered_consecutive_frames = false;
            bool last_render_time_sampled_before = false;
            bool last_render_time_sampled_after = false;
            bool last_render_time_advanced = false;
            float last_render_time_before = 0.0f;
            float last_render_time_after = 0.0f;
            float last_render_time_delta = 0.0f;
            float screen_x = 0.0f;
            float screen_y = 0.0f;
            int32_t viewport_w = 0;
            int32_t viewport_h = 0;
            double distance = 0.0;

            std::string observed_text{};
            auto* component_before_apply = actor ? find_managed_text_component(actor, key) : nullptr;
            if (component_before_apply)
            {
                last_render_time_sampled_before = read_last_render_time(component_before_apply, last_render_time_before);
            }
            if (apply_before_verify && actor)
            {
                restore_known_text_if_any(actor, rec.stable_id, true);
            }

            auto* component = actor ? find_managed_text_component(actor, key) : nullptr;
            component_exists = component != nullptr;
            if (component)
            {
                (void)invoke_bool_return_no_param(
                    component,
                    STR("IsRegistered"),
                    STR("/Script/Engine.ActorComponent:IsRegistered"),
                    component_registered);

                auto* attach_parent = get_object_property_if_present(component, "AttachParent");
                auto* owner = get_object_property_if_present(component, "Owner");
                component_attached = (attach_parent != nullptr) || (owner != nullptr);

                bool pending_func = false;
                if (invoke_bool_return_no_param(
                        component,
                        STR("IsPendingKill"),
                        STR("/Script/CoreUObject.Object:IsPendingKill"),
                        pending_func))
                {
                    component_pending_kill = pending_func;
                }
                bool pending_prop = false;
                if (get_bool_property_if_present(component, "bpendingkill", pending_prop) && pending_prop)
                {
                    component_pending_kill = true;
                }
                bool destroyed_prop = false;
                if (get_bool_property_if_present(component, "bbeingdestroyed", destroyed_prop) && destroyed_prop)
                {
                    component_pending_kill = true;
                }

                bool render_state_value = false;
                if (invoke_bool_return_no_param(
                        component,
                        STR("IsRenderStateCreated"),
                        STR("/Script/Engine.PrimitiveComponent:IsRenderStateCreated"),
                        render_state_value))
                {
                    render_state_created = render_state_value;
                }

                (void)get_bool_property_if_present(component, "bhiddeningame", hidden_in_game);
                (void)get_bool_property_if_present(component, "bvisible", visible);

                if (invoke_get_text_render_value(component, observed_text))
                {
                    text_non_empty = !observed_text.empty();
                    text_matches_expected = (observed_text == rec.text);
                }

                bool recent_render_value = false;
                if (invoke_bool_return_with_float_param(
                        component,
                        STR("WasRecentlyRendered"),
                        STR("/Script/Engine.PrimitiveComponent:WasRecentlyRendered"),
                        0.35f,
                        recent_render_value))
                {
                    recently_rendered = recent_render_value;
                }

                if (last_render_time_sampled_before || m_visual_verify_last_render_time_seen.find(key) != m_visual_verify_last_render_time_seen.end())
                {
                    float baseline = last_render_time_before;
                    if (!last_render_time_sampled_before)
                    {
                        baseline = m_visual_verify_last_render_time_seen[key];
                    }
                    last_render_time_sampled_after = read_last_render_time(component, last_render_time_after);
                    if (last_render_time_sampled_after)
                    {
                        last_render_time_delta = last_render_time_after - baseline;
                        last_render_time_advanced = last_render_time_delta > 0.0001f;
                        m_visual_verify_last_render_time_seen[key] = last_render_time_after;
                    }
                }
                else
                {
                    last_render_time_sampled_after = read_last_render_time(component, last_render_time_after);
                    if (last_render_time_sampled_after)
                    {
                        m_visual_verify_last_render_time_seen[key] = last_render_time_after;
                    }
                }
            }

            if (actor && view.valid)
            {
                const auto actor_loc = actor->K2_GetActorLocation();
                const auto to_actor = vec_sub(actor_loc, view.location);
                distance = vec_len(to_actor);
                const auto dir = vec_normalize(to_actor);
                const auto dot = vec_dot(view_forward, dir);
                in_front = dot > 0.0;
                within_fov = dot >= 0.5; // approximately 60deg half-angle

                bool projected = false;
                if (invoke_project_world_to_screen(controller, actor_loc, screen_x, screen_y, projected))
                {
                    projectable = projected;
                    if (invoke_get_viewport_size(controller, viewport_w, viewport_h) && viewport_w > 0 && viewport_h > 0)
                    {
                        in_viewport =
                            screen_x >= 0.0f && screen_x <= static_cast<float>(viewport_w) &&
                            screen_y >= 0.0f && screen_y <= static_cast<float>(viewport_h);
                    }
                    else
                    {
                        in_viewport = projected;
                    }
                }
            }

            const bool visible_state_ok = !hidden_in_game && visible;
            const bool screen_ok = in_front && within_fov && distance <= max_dist && projectable && in_viewport;
            auto& recent_streak = m_visual_verify_recently_rendered_streak[key];
            if (recently_rendered)
            {
                ++recent_streak;
            }
            else
            {
                recent_streak = 0;
            }
            recently_rendered_consecutive_frames = recent_streak >= 2;

            const bool strong_last_render_signal = last_render_time_advanced && screen_ok;
            int strong_signal_count = 0;
            strong_signal_count += render_state_created ? 1 : 0;
            strong_signal_count += strong_last_render_signal ? 1 : 0;
            strong_signal_count += recently_rendered_consecutive_frames ? 1 : 0;

            const bool pass = component_exists &&
                component_registered &&
                component_attached &&
                !component_pending_kill &&
                visible_state_ok &&
                text_non_empty &&
                text_matches_expected &&
                screen_ok &&
                strong_signal_count >= 2;

            auto& no_render_streak = m_visual_verify_no_render_streak[key];
            if (pass)
            {
                no_render_streak = 0;
            }
            else
            {
                ++no_render_streak;
            }
            const bool repeated_no_render = no_render_streak >= 3;
            const bool hard_fail = !pass && (component_pending_kill || repeated_no_render);
            const std::string tier = pass ? "PASS" : (hard_fail ? "FAIL" : "WARN");

            ++pass_count;
            if (tier == "WARN")
            {
                ++warn_count;
            }
            else if (tier == "FAIL")
            {
                ++fail_count;
            }
            const bool had_prior = m_visual_verify_last_tier.find(key) != m_visual_verify_last_tier.end();
            const auto prior_tier = had_prior ? m_visual_verify_last_tier[key] : std::string{};
            const bool changed = had_prior ? (prior_tier != tier) : false;
            m_visual_verify_last_tier[key] = tier;
            m_visual_verify_last_result[key] = pass;

            log_line("[visual-verify] pass=" + std::to_string(pass_number) +
                     " key=" + key +
                     " stableId=" + rec.stable_id +
                     " result=" + tier +
                     " changed=" + std::string{had_prior ? (changed ? "true" : "false") : "n/a"} +
                     " exists=" + std::string{component_exists ? "true" : "false"} +
                     " registered=" + std::string{component_registered ? "true" : "false"} +
                     " attached=" + std::string{component_attached ? "true" : "false"} +
                     " pendingKill=" + std::string{component_pending_kill ? "true" : "false"} +
                     " renderStateCreated=" + std::string{render_state_created ? "true" : "false"} +
                     " hiddenInGame=" + std::string{hidden_in_game ? "true" : "false"} +
                     " visible=" + std::string{visible ? "true" : "false"} +
                     " textMatch=" + std::string{text_matches_expected ? "true" : "false"} +
                     " textNonEmpty=" + std::string{text_non_empty ? "true" : "false"} +
                     " projectable=" + std::string{projectable ? "true" : "false"} +
                     " inFront=" + std::string{in_front ? "true" : "false"} +
                     " inViewport=" + std::string{in_viewport ? "true" : "false"} +
                     " withinFov=" + std::string{within_fov ? "true" : "false"} +
                     " dist=" + std::to_string(distance) +
                     " screen=" + std::to_string(screen_x) + "," + std::to_string(screen_y) +
                     " viewport=" + std::to_string(viewport_w) + "x" + std::to_string(viewport_h) +
                     " recentlyRendered=" + std::string{recently_rendered ? "true" : "false"} +
                     " recentlyRenderedStreak=" + std::to_string(recent_streak) +
                     " recentlyRenderedConsecutive=" + std::string{recently_rendered_consecutive_frames ? "true" : "false"} +
                     " lastRenderBefore=" + std::to_string(last_render_time_before) +
                     " lastRenderAfter=" + std::to_string(last_render_time_after) +
                     " lastRenderDelta=" + std::to_string(last_render_time_delta) +
                     " lastRenderAdvanced=" + std::string{strong_last_render_signal ? "true" : "false"} +
                     " strongSignals=" + std::to_string(strong_signal_count) +
                     " noRenderStreak=" + std::to_string(no_render_streak) +
                     " expectedChars=" + std::to_string(rec.text.size()) +
                     " observedChars=" + std::to_string(observed_text.size()));

            if (force_reapply && actor)
            {
                restore_known_text_if_any(actor, rec.stable_id, true);
                log_line("[visual-verify] forced_reapply pass=" + std::to_string(pass_number) +
                         " key=" + key +
                         " stableId=" + rec.stable_id +
                         " reason=" + reason);
            }
        }

        log_line("[visual-verify] pass_complete pass=" + std::to_string(pass_number) +
                 " reason=" + reason +
                 " checked=" + std::to_string(pass_count) +
                 " warned=" + std::to_string(warn_count) +
                 " failed=" + std::to_string(fail_count) +
                 " forcedReapply=" + std::string{force_reapply ? "true" : "false"});
    }

    auto SignTextMod::tick_localclient_visual_verify_debug(std::chrono::steady_clock::time_point now) -> void
    {
        std::string stability_reason{};
        if (!is_localclient_runtime_stable_for_post_ready(&stability_reason))
        {
            if (m_visual_verify_session_ready)
            {
                log_line("[visual-verify] session_ready_lost reset=true");
            }
            reset_visual_verify_debug_state();
            return;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const auto authority_mode_lower = lower_ascii(m_authority_mode);
        const bool authority_source_resolved =
            runtime_role_lower != "localclientpending" &&
            authority_mode_lower != "worldauthoritypending";
        const bool session_ready =
            is_restore_scan_world_active() &&
            is_localclient_prune_ready(authority_source_resolved, nullptr);

        if (!session_ready)
        {
            if (m_visual_verify_session_ready)
            {
                log_line("[visual-verify] session_ready_lost reset=true");
            }
            reset_visual_verify_debug_state();
            return;
        }

        if (!m_visual_verify_session_ready)
        {
            m_visual_verify_session_ready = true;
            m_visual_verify_ready_at = now;
            m_visual_verify_pass1_scan_cycle = m_restore_scan_cycle_counter;
            m_visual_verify_expected_keys.clear();
            m_visual_verify_last_result.clear();
            m_visual_verify_last_tier.clear();
            m_visual_verify_recently_rendered_streak.clear();
            m_visual_verify_no_render_streak.clear();
            m_visual_verify_last_render_time_seen.clear();

            auto* controller = try_get_primary_player_controller();
            UObject* controlled_pawn = nullptr;
            if (controller)
            {
                const bool got_pawn_from_fn = invoke_object_return_no_param(
                    controller,
                    STR("GetPawn"),
                    STR("/Script/Engine.Controller:GetPawn"),
                    controlled_pawn);
                if (!got_pawn_from_fn || !controlled_pawn)
                {
                    controlled_pawn = get_object_property_if_present(controller, "Pawn");
                }
                if (!controlled_pawn)
                {
                    controlled_pawn = get_object_property_if_present(controller, "AcknowledgedPawn");
                }
            }
            if (controlled_pawn && controlled_pawn->IsA(AActor::StaticClass()))
            {
                auto* pawn_actor = Cast<AActor>(controlled_pawn);
                if (pawn_actor)
                {
                    m_visual_verify_ready_pawn_loc = pawn_actor->K2_GetActorLocation();
                    m_visual_verify_ready_pawn_loc_valid = true;
                }
            }

            log_line("[visual-verify] session_ready passPlan=1_immediate,2_after_scan_plus2,3_on_player_motion forceReapplyDebug=" +
                     std::string{m_visual_verify_debug_force_reapply ? "true" : "false"} +
                     " motionReapplyEnabled=" + std::string{m_localclient_motion_reapply_enabled ? "true" : "false"});
        }

        if (!m_visual_verify_pass1_done)
        {
            run_localclient_visual_verify_pass(1, true, false, "session_ready");
            m_visual_verify_pass1_done = true;
            m_visual_verify_pass1_scan_cycle = m_restore_scan_cycle_counter;
        }

        if (m_visual_verify_pass1_done &&
            !m_visual_verify_pass2_done &&
            m_restore_scan_cycle_counter >= (m_visual_verify_pass1_scan_cycle + 2))
        {
            run_localclient_visual_verify_pass(2, false, m_visual_verify_debug_force_reapply, "scan_plus_2");
            m_visual_verify_pass2_done = true;
        }

        if (m_localclient_motion_reapply_enabled && m_visual_verify_pass1_done && !m_visual_verify_pass3_done)
        {
            auto* controller = try_get_primary_player_controller();
            UObject* controlled_pawn = nullptr;
            if (controller)
            {
                const bool got_pawn_from_fn = invoke_object_return_no_param(
                    controller,
                    STR("GetPawn"),
                    STR("/Script/Engine.Controller:GetPawn"),
                    controlled_pawn);
                if (!got_pawn_from_fn || !controlled_pawn)
                {
                    controlled_pawn = get_object_property_if_present(controller, "Pawn");
                }
                if (!controlled_pawn)
                {
                    controlled_pawn = get_object_property_if_present(controller, "AcknowledgedPawn");
                }
            }
            if (controlled_pawn && controlled_pawn->IsA(AActor::StaticClass()) && m_visual_verify_ready_pawn_loc_valid)
            {
                auto* pawn_actor = Cast<AActor>(controlled_pawn);
                if (pawn_actor)
                {
                    const auto current_loc = pawn_actor->K2_GetActorLocation();
                    const auto delta = vec_sub(current_loc, m_visual_verify_ready_pawn_loc);
                    const auto delta_len = vec_len(delta);
                    if (delta_len >= 100.0)
                    {
                        if (!m_visual_verify_motion_logged)
                        {
                            log_line("[visual-verify] player_motion_detected delta=" + std::to_string(delta_len));
                            m_visual_verify_motion_logged = true;
                        }
                        run_localclient_visual_verify_pass(3, false, m_visual_verify_debug_force_reapply, "player_motion");
                        m_visual_verify_pass3_done = true;
                    }
                }
            }
        }
    }

    auto SignTextMod::on_update() -> void
    {
        if (!m_unreal_ready)
        {
            return;
        }

        if (!is_dedicated_runtime_process())
        {
            maybe_run_phase7_bootstrap_sanitize();
            // Hotkey reliability fallback:
            // capture hardware edge directly in case callback registration misses events
            // while input focus/context changes.
            const bool hotkey_is_down = ((GetAsyncKeyState(m_hotkey_vk) & 0x8000) != 0);
            if (hotkey_is_down && !m_hotkey_poll_was_down)
            {
                m_hotkey_requested.store(true);
                m_last_player_activity = std::chrono::steady_clock::now();
                if (m_f8_latency_breakdown_enabled)
                {
                    m_f8_latency_trace.active = true;
                    m_f8_latency_trace.target_seen = false;
                    m_f8_latency_trace.construct_seen = false;
                    m_f8_latency_trace.edge = m_last_player_activity;
                    m_f8_latency_trace.target = {};
                    m_f8_latency_trace.construct = {};
                    ++m_f8_latency_trace.press_id;
                }
                log_line("[input] hotkey polled_edge key=" + m_hotkey_name);
            }
            m_hotkey_poll_was_down = hotkey_is_down;

            tick_pending_hotkey();
            if (m_phase7_definitive_teardown_armed && !m_phase7_definitive_teardown_reason.empty())
            {
                force_close_phase7_for_teardown(m_phase7_definitive_teardown_reason);
            }
            if (m_phase7_teardown_pending)
            {
                const bool had_widget = (m_phase7_umg_widget != nullptr);
                if (had_widget)
                {
                    close_phase7_umg_editor(true);
                }
                log_line("[phase7] teardown_committed removed=" +
                         std::string{m_phase7_last_close_removed ? "true" : "false"} +
                         " reason=" + (m_phase7_teardown_pending_reason.empty() ? "unknown" : m_phase7_teardown_pending_reason));
                reset_phase7_runtime_state();
            }
            std::string phase7_guard_reason{};
            if (is_phase7_runtime_interaction_safe(&phase7_guard_reason))
            {
                m_phase7_guard_fail_started = {};
                m_phase7_guard_fail_reason.clear();
                m_phase7_guard_hysteresis_logged = false;
                tick_phase7_umg_editor();
            }
            else
            {
                const auto now = std::chrono::steady_clock::now();
                const bool should_log =
                    m_phase7_teardown_suppressed_last_reason != phase7_guard_reason ||
                    m_phase7_teardown_suppressed_last_log.time_since_epoch().count() == 0 ||
                    (now - m_phase7_teardown_suppressed_last_log) >= std::chrono::seconds(2);
                if (should_log)
                {
                    log_line("[phase7] teardown_suppressed reason=non_definitive_state detail=" + phase7_guard_reason);
                    m_phase7_teardown_suppressed_last_reason = phase7_guard_reason;
                    m_phase7_teardown_suppressed_last_log = now;
                }
            }
        }
        tick_pending_fallback_hotkeys();
        tick_file_triggers();
        if (!is_dedicated_runtime_process())
        {
            // Always parse readiness markers in LocalClient states, including Pending.
            tick_r5_readiness_markers();
            if (!m_session_ready_latched)
            {
                std::string ready_reason{};
                if (is_session_ready_for_role_resolution(&ready_reason))
                {
                    m_session_ready_latched = true;
                    m_phase7_teardown_skip_logged = false;
                    m_ready_baseline_live_keys.clear();
                    m_ready_baseline_capture_remaining_scans = 2;
                    auto* controller = try_get_primary_player_controller();
                    auto* controller_actor = controller && controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
                    m_session_ready_world_id = controller_actor ? build_world_id_for_actor(controller_actor) : std::string{};
                    log_line("[session] ready_latched epoch=" + std::to_string(m_session_epoch) +
                             " world=" + (m_session_ready_world_id.empty() ? "unknown" : m_session_ready_world_id) +
                             " reason=" + (ready_reason.empty() ? "unknown" : ready_reason));
                    trace_behavior_sm("session_ready_latched",
                                      "epoch=" + std::to_string(m_session_epoch) +
                                      " world=" + (m_session_ready_world_id.empty() ? "unknown" : m_session_ready_world_id));
                }
            }
            if (m_session_ready_latched && lower_ascii(m_runtime_role) == "localclientpending")
            {
                tick_localclient_role_resolution();
            }
            if (m_session_ready_latched)
            {
                maybe_prewarm_phase7_umg_editor();
            }
        }
        tick_bridge();
        const auto now = std::chrono::steady_clock::now();
        std::string localclient_stability_reason{};
        if (is_localclient_runtime_stable_for_post_ready(&localclient_stability_reason))
        {
            maybe_run_localclient_role_lock_restore_passes(now);
            maybe_run_hosted_post_ready_reconcile();
            tick_localclient_visual_verify_debug(now);
        }
        else if (m_sidecar_authoritative && !is_dedicated_runtime_process())
        {
            if (m_localclient_stability_skip_last_reason != localclient_stability_reason ||
                m_localclient_stability_skip_last_log.time_since_epoch().count() == 0 ||
                (now - m_localclient_stability_skip_last_log) >= std::chrono::seconds(2))
            {
                log_line("[save] localclient_post_ready_skip reason=" + localclient_stability_reason +
                         " runtimeRole=" + m_runtime_role +
                         " authorityMode=" + m_authority_mode);
                m_localclient_stability_skip_last_log = now;
                m_localclient_stability_skip_last_reason = localclient_stability_reason;
            }
        }

        if (now - m_last_restore_scan > std::chrono::seconds(2))
        {
            m_last_restore_scan = now;
            if (!is_restore_scan_world_active())
            {
                const auto runtime_role_lower = lower_ascii(m_runtime_role);
                const bool pending_localclient = runtime_role_lower == "localclientpending";
                const bool locked_localclient_session =
                    !is_dedicated_runtime_process() &&
                    m_session_ready_latched &&
                    m_role_lock_acquired;
                if (locked_localclient_session)
                {
                    if (!m_locked_world_inactive_ignored_logged)
                    {
                        log_line("[session] reset_ignored reason=locked_session_transient_world_inactive");
                        m_locked_world_inactive_ignored_logged = true;
                    }
                    m_world_inactive_since = {};
                }
                else if (!pending_localclient)
                {
                    constexpr auto k_world_inactive_reset_debounce = std::chrono::seconds(3);
                    if (m_world_inactive_since.time_since_epoch().count() == 0)
                    {
                        m_world_inactive_since = now;
                    }
                    if ((now - m_world_inactive_since) >= k_world_inactive_reset_debounce)
                    {
                        const bool client_runtime = !is_dedicated_runtime_process();
                        if (client_runtime && !m_phase7_definitive_teardown_armed)
                        {
                            log_line("[session] reset_ignored reason=non_definitive_world_inactive");
                        }
                        else
                        {
                            reset_session_state("world_inactive");
                        }
                        m_world_inactive_since = {};
                        m_locked_world_inactive_ignored_logged = false;
                    }
                }
                else if (!m_pending_world_inactive_ignored_logged)
                {
                    log_line("[role] pending_world_inactive_ignored reason=awaiting_hosted_ready_or_role_resolution");
                    m_pending_world_inactive_ignored_logged = true;
                }
                if (!locked_localclient_session)
                {
                    m_consecutive_empty_label_scans = 0;
                    m_restore_scan_has_seen_live_labels = false;
                    m_live_label_actor_ptrs.clear();
                    m_missing_label_scan_counts.clear();
                    m_seen_live_label_keys.clear();
                    m_first_seen_construct_hold_states.clear();
                    // Hard reset transient render/component caches across logout/map travel.
                    // Without this, reconnect can retain stale "already rendered" state and
                    // skip reapplying visible text even though actors/components were rebuilt.
                    m_rendered_text_cache.clear();
                    m_phase4_last_failure_reason.clear();
                    m_component_name_cache.clear();
                    m_phase4_next_retry.clear();
                    m_create_null_retry_states.clear();
                    m_label_text_visual_logged_keys.clear();
                    m_dedicated_restore_active_since = {};
                    m_dedicated_restore_stable_since = {};
                    m_dedicated_last_probable_label_count = 0;
                    reset_visual_verify_debug_state();
                    if (!pending_localclient)
                    {
                        m_pending_world_inactive_ignored_logged = false;
                        m_ready_baseline_live_keys.clear();
                        m_ready_baseline_capture_remaining_scans = 0;
                    }
                    if (!m_restore_scan_wait_logged)
                    {
                        log_line("[save] restore_scan waiting reason=no_active_world");
                        m_restore_scan_wait_logged = true;
                    }
                }
                return;
            }
            if (m_restore_scan_wait_logged)
            {
                log_line("[save] restore_scan active");
                m_restore_scan_wait_logged = false;
            }
            m_world_inactive_since = {};
            m_locked_world_inactive_ignored_logged = false;
            m_pending_world_inactive_ignored_logged = false;
            ++m_restore_scan_cycle_counter;

            std::unordered_set<std::string> present_label_keys{};
            std::unordered_map<std::string, uint32_t> present_world_counts{};
            const bool had_seen_live_labels_before_scan = m_restore_scan_has_seen_live_labels;
            expire_suspect_rebuild_states(now);
            const auto runtime_role_lower = lower_ascii(m_runtime_role);
            const auto authority_mode_lower = lower_ascii(m_authority_mode);
            const bool authority_source_resolved =
                runtime_role_lower != "localclientpending" &&
                authority_mode_lower != "worldauthoritypending";
            const bool remote_bridge_unsynced =
                !m_sidecar_authoritative &&
                m_bridge_role == BridgeRole::RemoteClient &&
                !m_bridge_snapshot_received;
            const bool localclient_authoritative =
                m_sidecar_authoritative &&
                !is_dedicated_runtime_process();
            const bool dedicated_authoritative_runtime =
                m_sidecar_authoritative &&
                is_dedicated_runtime_process();
            const bool authoritative_destroy_confirmation_runtime =
                localclient_authoritative || dedicated_authoritative_runtime;
            uint32_t scan_actor_count = 0;
            uint32_t scan_probable_label_count = 0;
            uint32_t scan_buildingish_count = 0;
            std::vector<std::string> scan_samples{};
            const auto arm_rebuild_render_guard = [&](const std::string& key_to_guard) {
                const auto guard_until = std::chrono::steady_clock::now() + std::chrono::seconds(12);
                auto& next_retry = m_phase4_next_retry[key_to_guard];
                const auto current = std::chrono::steady_clock::now();
                if (next_retry.time_since_epoch().count() == 0 || current >= next_retry)
                {
                    next_retry = guard_until;
                }
            };
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (!object || !object->IsA(AActor::StaticClass()))
                {
                    return LoopAction::Continue;
                }
                auto* actor = Cast<AActor>(object);
                ++scan_actor_count;
                if (scan_samples.size() < 6)
                {
                    const auto actor_full = narrow_ascii(actor->GetFullName());
                    const auto actor_class = actor->GetClassPrivate()
                        ? narrow_ascii(actor->GetClassPrivate()->GetFullName())
                        : std::string{};
                    const auto haystack = lower_ascii(actor_full + " " + actor_class);
                    if (haystack.find("building") != std::string::npos ||
                        haystack.find("wallplaque") != std::string::npos ||
                        haystack.find("lables") != std::string::npos ||
                        haystack.find("label") != std::string::npos ||
                        haystack.find("plaque") != std::string::npos)
                    {
                        ++scan_buildingish_count;
                        scan_samples.push_back("class=" + actor_class + " actor=" + actor_full);
                    }
                }
                if (!actor || !is_probable_label_actor(actor))
                {
                    return LoopAction::Continue;
                }
                ++scan_probable_label_count;
                const auto stable_id = extract_stable_id(actor);
                const auto actor_world_id = build_world_id_for_actor(actor);
                configure_sidecar_for_actor(actor, actor_world_id);
                const auto world_id = active_storage_world_id(actor_world_id);
                const auto key = build_storage_key(world_id, stable_id);
                const bool first_seen_live_after_ready =
                    m_session_ready_latched &&
                    m_seen_live_label_keys.find(key) == m_seen_live_label_keys.end();
                bool is_ready_baseline_key =
                    m_ready_baseline_live_keys.find(key) != m_ready_baseline_live_keys.end();
                if (m_ready_baseline_live_keys.find(key) == m_ready_baseline_live_keys.end() &&
                    m_session_ready_latched &&
                    m_ready_baseline_capture_remaining_scans > 0)
                {
                    m_ready_baseline_live_keys.insert(key);
                    is_ready_baseline_key = true;
                }
                const auto actor_ptr = reinterpret_cast<uintptr_t>(actor);
                bool pruned_rebuilt_label = false;
                bool forced_unsuppress_restore_once = false;
                if (m_restore_scan_has_seen_live_labels)
                {
                    if (const auto ptr_it = m_live_label_actor_ptrs.find(key);
                        ptr_it != m_live_label_actor_ptrs.end() && ptr_it->second != actor_ptr)
                    {
                        if (const auto found = m_labels.find(key); found != m_labels.end())
                        {
                            const auto miss_it = m_missing_label_scan_counts.find(key);
                            const uint32_t miss_count = (miss_it != m_missing_label_scan_counts.end()) ? miss_it->second : 0;
                            constexpr uint32_t k_rebuild_prune_missing_scan_threshold = 4;
                            const bool seen_live_this_session = m_seen_live_label_keys.find(key) != m_seen_live_label_keys.end();
                            const bool destroy_confirmed_r5log =
                                authoritative_destroy_confirmation_runtime &&
                                has_recent_destroy_confirmation(found->second.stable_id, found->second.world_id);
                            std::string localclient_prune_ready_reason{};
                            const bool localclient_prune_ready =
                                is_localclient_prune_ready(authority_source_resolved, &localclient_prune_ready_reason);
                            if (authoritative_destroy_confirmation_runtime)
                            {
                                mark_suspect_rebuild(
                                    key,
                                    found->second.stable_id,
                                    found->second.world_id,
                                    ptr_it->second,
                                    actor_ptr,
                                    now);
                            }
                            if (destroy_confirmed_r5log)
                            {
                                log_line("[save] prune_rebuilt_label key=" + key +
                                         " stableId=" + found->second.stable_id +
                                         " worldId=" + found->second.world_id +
                                         " reason=destroy_confirmed_r5log" +
                                         " trustedDestroyConfirmed=true" +
                                         " missCount=" + std::to_string(miss_count));
                                trace_behavior_sm("prune_commit",
                                                  "path=restore_scan_rebuild key=" + key +
                                                  " stableId=" + found->second.stable_id +
                                                  " worldId=" + found->second.world_id +
                                                  " reason=destroy_confirmed_r5log trustedDestroyConfirmed=true");
                                if (m_sidecar_authoritative)
                                {
                                    broadcast_bridge_clear(found->second.stable_id, found->second.world_id, "prune_rebuilt_label");
                                }
                                m_labels.erase(found);
                                m_rendered_text_cache.erase(key);
                                m_component_name_cache.erase(key);
                                m_phase4_next_retry.erase(key);
                                m_create_null_retry_states.erase(key);
                                m_phase4_last_failure_reason.erase(key);
                                m_missing_label_scan_counts.erase(key);
                                m_first_seen_construct_hold_states.erase(key);
                                if (m_text_buffer_bound_key == key)
                                {
                                    m_text_buffer.fill('\0');
                                    m_text_buffer_bound_key.clear();
                                }
                                save_sidecar_json("prune_rebuilt_label", key, stable_id, world_id);
                                m_suspect_rebuild_states.erase(key);
                                pruned_rebuilt_label = true;
                            }
                            else if (authoritative_destroy_confirmation_runtime)
                            {
                                const auto suspect_decision = maybe_promote_suspect_rebuild_to_prune(
                                    key,
                                    found->second.stable_id,
                                    found->second.world_id,
                                    now);
                                if (suspect_decision == SuspectRebuildDecision::PromotePrune)
                                {
                                    log_line("[save] prune_rebuilt_label key=" + key +
                                             " stableId=" + found->second.stable_id +
                                             " worldId=" + found->second.world_id +
                                             " reason=suspect_rebuild_confirmed" +
                                             " trustedDestroyConfirmed=true" +
                                             " missCount=" + std::to_string(miss_count));
                                    trace_behavior_sm("prune_commit",
                                                      "path=restore_scan_rebuild key=" + key +
                                                      " stableId=" + found->second.stable_id +
                                                      " worldId=" + found->second.world_id +
                                                      " reason=suspect_rebuild_confirmed trustedDestroyConfirmed=true");
                                    if (m_sidecar_authoritative)
                                    {
                                        broadcast_bridge_clear(found->second.stable_id, found->second.world_id, "prune_rebuilt_label");
                                    }
                                    m_labels.erase(found);
                                    m_rendered_text_cache.erase(key);
                                    m_component_name_cache.erase(key);
                                    m_phase4_next_retry.erase(key);
                                    m_create_null_retry_states.erase(key);
                                    m_phase4_last_failure_reason.erase(key);
                                    m_missing_label_scan_counts.erase(key);
                                    m_first_seen_construct_hold_states.erase(key);
                                    if (m_text_buffer_bound_key == key)
                                    {
                                        m_text_buffer.fill('\0');
                                        m_text_buffer_bound_key.clear();
                                    }
                                    save_sidecar_json("prune_rebuilt_label", key, stable_id, world_id);
                                    pruned_rebuilt_label = true;
                                }
                            }
                            else if (localclient_authoritative && !localclient_prune_ready)
                            {
                                log_line("[save] prune_rebuilt_label deferred key=" + key +
                                         " stableId=" + found->second.stable_id +
                                         " worldId=" + found->second.world_id +
                                         " reason=localclient_readiness_guard" +
                                         " localClientPruneReady=false" +
                                         " localClientReadyReason=" + localclient_prune_ready_reason +
                                         " seenLiveThisSession=" + std::string{seen_live_this_session ? "true" : "false"} +
                                         " missCount=" + std::to_string(miss_count));
                                trace_behavior_sm("prune_deferred",
                                                  "path=restore_scan_rebuild key=" + key +
                                                  " stableId=" + found->second.stable_id +
                                                  " worldId=" + found->second.world_id +
                                                  " reason=localclient_readiness_guard trustedDestroyConfirmed=false");
                            }
                            else if (localclient_authoritative && !seen_live_this_session)
                            {
                                log_line("[save] prune_rebuilt_label deferred key=" + key +
                                         " stableId=" + found->second.stable_id +
                                         " worldId=" + found->second.world_id +
                                         " reason=authoritative_seen_live_guard" +
                                         " localClientReadyReason=" + localclient_prune_ready_reason +
                                         " seenLiveThisSession=false" +
                                         " missCount=" + std::to_string(miss_count));
                                trace_behavior_sm("prune_deferred",
                                                  "path=restore_scan_rebuild key=" + key +
                                                  " stableId=" + found->second.stable_id +
                                                  " worldId=" + found->second.world_id +
                                                  " reason=authoritative_seen_live_guard trustedDestroyConfirmed=false");
                            }
                            else if (miss_count < k_rebuild_prune_missing_scan_threshold)
                            {
                                const bool removed_component = destroy_managed_text_component(actor, key);
                                m_rendered_text_cache.erase(key);
                                arm_rebuild_render_guard(key);
                                log_line("[save] prune_rebuilt_label deferred key=" + key +
                                         " stableId=" + found->second.stable_id +
                                         " worldId=" + found->second.world_id +
                                         " reason=insufficient_miss_count missCount=" + std::to_string(miss_count) +
                                         " threshold=" + std::to_string(k_rebuild_prune_missing_scan_threshold) +
                                         " removedComponent=" + std::string{removed_component ? "true" : "false"} +
                                         " renderGuardSec=12");
                                trace_behavior_sm("prune_deferred",
                                                  "path=restore_scan_rebuild key=" + key +
                                                  " stableId=" + found->second.stable_id +
                                                  " worldId=" + found->second.world_id +
                                                  " reason=insufficient_miss_count trustedDestroyConfirmed=false");
                                m_component_name_cache.erase(key);
                            }
                            else if (remote_bridge_unsynced)
                            {
                                const bool removed_component = destroy_managed_text_component(actor, key);
                                m_rendered_text_cache.erase(key);
                                arm_rebuild_render_guard(key);
                                log_line("[save] prune_rebuilt_label deferred key=" + key +
                                         " stableId=" + found->second.stable_id +
                                         " worldId=" + found->second.world_id +
                                         " reason=bridge_unsynced" +
                                         " removedComponent=" + std::string{removed_component ? "true" : "false"} +
                                         " renderGuardSec=12");
                                trace_behavior_sm("prune_deferred",
                                                  "path=restore_scan_rebuild key=" + key +
                                                  " stableId=" + found->second.stable_id +
                                                  " worldId=" + found->second.world_id +
                                                  " reason=bridge_unsynced trustedDestroyConfirmed=false");
                                m_component_name_cache.erase(key);
                            }
                            else if (localclient_authoritative)
                            {
                                const bool removed_component = destroy_managed_text_component(actor, key);
                                m_rendered_text_cache.erase(key);
                                arm_rebuild_render_guard(key);
                                log_line("[save] prune_rebuilt_label deferred key=" + key +
                                         " stableId=" + found->second.stable_id +
                                         " worldId=" + found->second.world_id +
                                         " reason=suspect_rebuild_soft_prune" +
                                         " trustedDestroyConfirmed=false" +
                                         " missCount=" + std::to_string(miss_count) +
                                         " threshold=" + std::to_string(k_rebuild_prune_missing_scan_threshold) +
                                         " removedComponent=" + std::string{removed_component ? "true" : "false"} +
                                         " renderGuardSec=12");
                                trace_behavior_sm("prune_deferred",
                                                  "path=restore_scan_rebuild key=" + key +
                                                  " stableId=" + found->second.stable_id +
                                                  " worldId=" + found->second.world_id +
                                                  " reason=suspect_rebuild_soft_prune trustedDestroyConfirmed=false");
                                m_component_name_cache.erase(key);
                            }
                            else
                            {
                                log_line("[save] prune_rebuilt_label key=" + key +
                                         " stableId=" + found->second.stable_id +
                                         " worldId=" + found->second.world_id +
                                         " reason=actor_instance_changed");
                                trace_behavior_sm("prune_commit",
                                                  "path=restore_scan_rebuild key=" + key +
                                                  " stableId=" + found->second.stable_id +
                                                  " worldId=" + found->second.world_id +
                                                  " reason=actor_instance_changed trustedDestroyConfirmed=" +
                                                      std::string{localclient_authoritative ? "false" : "n/a"});
                                if (m_sidecar_authoritative)
                                {
                                    broadcast_bridge_clear(found->second.stable_id, found->second.world_id, "prune_rebuilt_label");
                                }
                                m_labels.erase(found);
                                m_rendered_text_cache.erase(key);
                                m_component_name_cache.erase(key);
                                m_phase4_next_retry.erase(key);
                                m_create_null_retry_states.erase(key);
                                m_phase4_last_failure_reason.erase(key);
                                m_missing_label_scan_counts.erase(key);
                                m_first_seen_construct_hold_states.erase(key);
                                if (m_text_buffer_bound_key == key)
                                {
                                    m_text_buffer.fill('\0');
                                    m_text_buffer_bound_key.clear();
                                }
                                save_sidecar_json("prune_rebuilt_label", key, stable_id, world_id);
                                pruned_rebuilt_label = true;
                            }
                        }
                    }
                }
                m_live_label_actor_ptrs[key] = actor_ptr;
                present_label_keys.insert(key);
                ++present_world_counts[world_id];
                m_seen_live_label_keys.insert(key);
                m_missing_label_scan_counts.erase(key);
                if (!pruned_rebuilt_label)
                {
                    if (authoritative_destroy_confirmation_runtime)
                    {
                        const auto suspect_decision = maybe_promote_suspect_rebuild_to_prune(
                            key,
                            stable_id,
                            world_id,
                            now);
                        if (suspect_decision == SuspectRebuildDecision::PromotePrune)
                        {
                            if (const auto found = m_labels.find(key); found != m_labels.end())
                            {
                                log_line("[save] prune_rebuilt_label key=" + key +
                                         " stableId=" + found->second.stable_id +
                                         " worldId=" + found->second.world_id +
                                         " reason=suspect_rebuild_confirmed" +
                                         " trustedDestroyConfirmed=true" +
                                         " missCount=0");
                                trace_behavior_sm("prune_commit",
                                                  "path=restore_scan_rebuild key=" + key +
                                                  " stableId=" + found->second.stable_id +
                                                  " worldId=" + found->second.world_id +
                                                  " reason=suspect_rebuild_confirmed trustedDestroyConfirmed=true");
                                if (m_sidecar_authoritative)
                                {
                                    broadcast_bridge_clear(found->second.stable_id, found->second.world_id, "prune_rebuilt_label");
                                }
                                m_labels.erase(found);
                                m_rendered_text_cache.erase(key);
                                m_component_name_cache.erase(key);
                                m_phase4_next_retry.erase(key);
                                m_create_null_retry_states.erase(key);
                                m_phase4_last_failure_reason.erase(key);
                                m_first_seen_construct_hold_states.erase(key);
                                if (m_text_buffer_bound_key == key)
                                {
                                    m_text_buffer.fill('\0');
                                    m_text_buffer_bound_key.clear();
                                }
                                save_sidecar_json("prune_rebuilt_label", key, stable_id, world_id);
                                pruned_rebuilt_label = true;
                            }
                        }
                        else if (suspect_decision == SuspectRebuildDecision::UnsuppressRestoreOnce)
                        {
                            m_phase4_next_retry.erase(key);
                            m_create_null_retry_states.erase(key);
                            m_phase4_last_failure_reason.erase(key);
                            forced_unsuppress_restore_once = true;
                            log_line("[save] prune_rebuilt_label deferred key=" + key +
                                     " stableId=" + stable_id +
                                     " worldId=" + world_id +
                                     " reason=suspect_unsuppress_fallback" +
                                     " trustedDestroyConfirmed=false");
                            trace_behavior_sm("prune_deferred",
                                              "path=restore_scan_rebuild key=" + key +
                                              " stableId=" + stable_id +
                                              " worldId=" + world_id +
                                              " reason=suspect_unsuppress_fallback trustedDestroyConfirmed=false");
                        }
                    }
                }
                if (!pruned_rebuilt_label)
                {
                    const bool stale_restore_blocked = maybe_handle_new_construct_overrides_stale_record(
                        actor,
                        key,
                        stable_id,
                        world_id,
                        first_seen_live_after_ready,
                        is_ready_baseline_key);
                    if (!stale_restore_blocked)
                    {
                        if (forced_unsuppress_restore_once)
                        {
                            restore_known_text_if_any(actor, stable_id, true);
                        }
                        else
                        {
                            restore_known_text_if_any(actor, stable_id);
                        }
                    }
                }
                return LoopAction::Continue;
            });

            if (!present_label_keys.empty())
            {
                m_consecutive_empty_label_scans = 0;
                m_restore_scan_has_seen_live_labels = true;
            }
            else
            {
                ++m_consecutive_empty_label_scans;
            }
            if (m_session_ready_latched && m_ready_baseline_capture_remaining_scans > 0)
            {
                --m_ready_baseline_capture_remaining_scans;
            }

            const bool dedicated_authoritative_prune_mode =
                m_sidecar_authoritative && is_dedicated_runtime_process();
            if (dedicated_authoritative_prune_mode)
            {
                if (m_dedicated_restore_active_since.time_since_epoch().count() == 0)
                {
                    m_dedicated_restore_active_since = now;
                }
                constexpr uint32_t k_dedicated_stable_probable_label_delta = 1;
                if (scan_probable_label_count == 0)
                {
                    m_dedicated_restore_stable_since = {};
                }
                else
                {
                    const auto diff = std::abs(
                        static_cast<int32_t>(scan_probable_label_count) -
                        static_cast<int32_t>(m_dedicated_last_probable_label_count));
                    if (m_dedicated_last_probable_label_count == 0 ||
                        diff > static_cast<int32_t>(k_dedicated_stable_probable_label_delta))
                    {
                        m_dedicated_restore_stable_since = now;
                    }
                    else if (m_dedicated_restore_stable_since.time_since_epoch().count() == 0)
                    {
                        m_dedicated_restore_stable_since = now;
                    }
                }
                m_dedicated_last_probable_label_count = scan_probable_label_count;
            }
            else
            {
                m_dedicated_restore_active_since = {};
                m_dedicated_restore_stable_since = {};
                m_dedicated_last_probable_label_count = 0;
            }

            if (is_dedicated_runtime_process() &&
                (now - m_last_restore_scan_diag > std::chrono::seconds(20) ||
                 (present_label_keys.empty() && m_consecutive_empty_label_scans == 3)))
            {
                m_last_restore_scan_diag = now;
                log_line("[save] restore_scan_diag actorCount=" + std::to_string(scan_actor_count) +
                         " buildingishSamples=" + std::to_string(scan_buildingish_count) +
                         " probableLabels=" + std::to_string(scan_probable_label_count) +
                         " presentKeys=" + std::to_string(present_label_keys.size()) +
                         " presentWorlds=" + std::to_string(present_world_counts.size()) +
                         " labelsJson=" + std::to_string(m_labels.size()) +
                         " emptyScans=" + std::to_string(m_consecutive_empty_label_scans));
                for (size_t i = 0; i < scan_samples.size(); ++i)
                {
                    log_line("[save] restore_scan_sample index=" + std::to_string(i) + " " + scan_samples[i]);
                }
            }

            // Destroy cleanup:
            // - only prune while at least one live label is visible in the current world scan.
            // - this avoids deleting all persisted records during disconnect/map travel when
            //   no labels are visible temporarily.
            // - after loading or changing sidecar route, wait one complete live-label scan
            //   before pruning; Solo can expose labels before every persisted label resolves.
            // - if all remaining text-sign records are missing while other labels are live,
            //   prune them all; that is the normal "player destroyed every text sign" case.
            const bool allow_prune = !present_label_keys.empty() && had_seen_live_labels_before_scan && !remote_bridge_unsynced;
            if (allow_prune)
            {
                m_bootstrap_prune_phase_observed = true;
                constexpr uint32_t k_prune_missing_scan_threshold = 4;
                constexpr uint32_t k_min_live_labels_in_world_for_prune = 2;
                constexpr auto k_dedicated_prune_active_world_grace = std::chrono::seconds(120);
                constexpr auto k_dedicated_prune_stable_window = std::chrono::seconds(30);
                std::unordered_set<std::string> keys_to_prune{};
                uint32_t considered_missing_labels = 0;
                uint32_t blocked_by_world_count = 0;
                uint32_t blocked_by_live_guard = 0;
                uint32_t blocked_by_dedicated_warmup_guard = 0;
                uint32_t blocked_by_localclient_readiness = 0;
                uint32_t blocked_by_post_ready_role_lock = 0;
                const bool require_seen_live_for_authoritative_prune =
                    m_sidecar_authoritative && !is_dedicated_runtime_process();
                const bool destructive_prune_allowed =
                    is_dedicated_runtime_process() ||
                    (m_session_ready_latched && m_role_lock_acquired);
                const bool post_ready_inventory_prune_window =
                    m_session_ready_latched &&
                    m_role_lock_acquired &&
                    m_ready_baseline_capture_remaining_scans == 0;
                std::string localclient_prune_ready_reason{};
                const bool localclient_prune_ready =
                    is_localclient_prune_ready(authority_source_resolved, &localclient_prune_ready_reason);
                const bool dedicated_active_world_grace_satisfied =
                    dedicated_authoritative_prune_mode &&
                    m_dedicated_restore_active_since.time_since_epoch().count() != 0 &&
                    (now - m_dedicated_restore_active_since) >= k_dedicated_prune_active_world_grace;
                const bool dedicated_stable_window_satisfied =
                    dedicated_authoritative_prune_mode &&
                    m_dedicated_restore_stable_since.time_since_epoch().count() != 0 &&
                    (now - m_dedicated_restore_stable_since) >= k_dedicated_prune_stable_window;
                const bool dedicated_never_seen_prune_ready =
                    !dedicated_authoritative_prune_mode ||
                    (authority_source_resolved &&
                     dedicated_active_world_grace_satisfied &&
                     dedicated_stable_window_satisfied);
                for (const auto& [key, rec] : m_labels)
                {
                    if (present_label_keys.find(key) != present_label_keys.end())
                    {
                        continue;
                    }
                    ++considered_missing_labels;
                    const bool trusted_destroy_confirmed =
                        authoritative_destroy_confirmation_runtime &&
                        has_recent_destroy_confirmation(rec.stable_id, rec.world_id);
                    const auto world_it = present_world_counts.find(rec.world_id);
                    if (world_it == present_world_counts.end() || world_it->second < k_min_live_labels_in_world_for_prune)
                    {
                        ++blocked_by_world_count;
                        continue;
                    }
                    if (!destructive_prune_allowed)
                    {
                        ++blocked_by_post_ready_role_lock;
                        continue;
                    }
                    const auto miss_it = m_missing_label_scan_counts.find(key);
                    const uint32_t existing_miss_count = (miss_it != m_missing_label_scan_counts.end()) ? miss_it->second : 0;
                    const uint32_t next_miss_count = existing_miss_count + 1;
                    const bool orphan_inventory_candidate =
                        localclient_authoritative &&
                        !trusted_destroy_confirmed &&
                        post_ready_inventory_prune_window &&
                        next_miss_count >= k_prune_missing_scan_threshold;
                    if (require_seen_live_for_authoritative_prune &&
                        !localclient_prune_ready &&
                        !trusted_destroy_confirmed &&
                        !orphan_inventory_candidate)
                    {
                        ++blocked_by_localclient_readiness;
                        continue;
                    }
                    const bool seen_live_this_session = m_seen_live_label_keys.find(key) != m_seen_live_label_keys.end();
                    if ((!m_sidecar_authoritative && !seen_live_this_session) ||
                        (require_seen_live_for_authoritative_prune &&
                         !seen_live_this_session &&
                         !trusted_destroy_confirmed &&
                         !orphan_inventory_candidate))
                    {
                        ++blocked_by_live_guard;
                        continue;
                    }
                    if (dedicated_authoritative_prune_mode &&
                        !dedicated_never_seen_prune_ready &&
                        !seen_live_this_session)
                    {
                        ++blocked_by_dedicated_warmup_guard;
                        continue;
                    }
                    uint32_t& miss_count = m_missing_label_scan_counts[key];
                    ++miss_count;
                    if (miss_count >= k_prune_missing_scan_threshold)
                    {
                        keys_to_prune.insert(key);
                    }
                }
                if (!keys_to_prune.empty())
                {
                    log_line("[save] prune_destroyed_label eval candidates=" + std::to_string(considered_missing_labels) +
                             " ready=" + std::to_string(keys_to_prune.size()) +
                             " blockedWorldCount=" + std::to_string(blocked_by_world_count) +
                             " blockedLiveGuard=" + std::to_string(blocked_by_live_guard) +
                             " blockedPostReadyRoleLock=" + std::to_string(blocked_by_post_ready_role_lock) +
                             " blockedLocalClientReady=" + std::to_string(blocked_by_localclient_readiness) +
                             " blockedDedicatedWarmup=" + std::to_string(blocked_by_dedicated_warmup_guard) +
                             " threshold=" + std::to_string(k_prune_missing_scan_threshold));
                }

                uint32_t pruned_count = 0;
                for (const auto& key : keys_to_prune)
                {
                    auto found = m_labels.find(key);
                    if (found == m_labels.end())
                    {
                        continue;
                    }
                    const bool trusted_destroy_confirmed =
                        authoritative_destroy_confirmation_runtime &&
                        has_recent_destroy_confirmation(found->second.stable_id, found->second.world_id);
                    const auto miss_it = m_missing_label_scan_counts.find(key);
                    const uint32_t miss_count = (miss_it != m_missing_label_scan_counts.end()) ? miss_it->second : 0;
                    const bool post_ready_inventory_window =
                        m_session_ready_latched &&
                        m_role_lock_acquired &&
                        m_ready_baseline_capture_remaining_scans == 0;
                    const bool orphan_inventory_prune_allowed =
                        localclient_authoritative &&
                        !trusted_destroy_confirmed &&
                        post_ready_inventory_window &&
                        miss_count >= k_prune_missing_scan_threshold;
                    if (localclient_authoritative && !trusted_destroy_confirmed && !orphan_inventory_prune_allowed)
                    {
                        log_line("[save] prune_destroyed_label deferred key=" + key +
                                 " stableId=" + found->second.stable_id +
                                 " worldId=" + found->second.world_id +
                                 " reason=destroy_unconfirmed_localclient" +
                                 " trustedDestroyConfirmed=false");
                        trace_behavior_sm("prune_deferred",
                                          "path=destroy_scan key=" + key +
                                          " stableId=" + found->second.stable_id +
                                          " worldId=" + found->second.world_id +
                                          " reason=destroy_unconfirmed_localclient trustedDestroyConfirmed=false");
                        continue;
                    }
                    const std::string prune_reason =
                        orphan_inventory_prune_allowed ? "orphan_not_in_world_inventory" : "destroy_confirmed";
                    log_line("[save] prune_destroyed_label key=" + key +
                             " stableId=" + found->second.stable_id +
                             " worldId=" + found->second.world_id +
                             " reason=" + prune_reason +
                             " trustedDestroyConfirmed=" + std::string{
                                 authoritative_destroy_confirmation_runtime
                                     ? (trusted_destroy_confirmed ? "true" : "false")
                                     : "n/a"} +
                             " missCount=" + std::to_string(miss_count));
                    trace_behavior_sm("prune_commit",
                                      "path=destroy_scan key=" + key +
                                      " stableId=" + found->second.stable_id +
                                      " worldId=" + found->second.world_id +
                                      " reason=" + prune_reason +
                                      " trustedDestroyConfirmed=" + std::string{
                                          authoritative_destroy_confirmation_runtime
                                              ? (trusted_destroy_confirmed ? "true" : "false")
                                              : "n/a"});
                    if (m_sidecar_authoritative)
                    {
                        broadcast_bridge_clear(found->second.stable_id, found->second.world_id, "prune_destroyed_label");
                    }
                    m_labels.erase(found);
                    m_rendered_text_cache.erase(key);
                    m_component_name_cache.erase(key);
                    m_phase4_next_retry.erase(key);
                    m_create_null_retry_states.erase(key);
                    m_phase4_last_failure_reason.erase(key);
                    m_seen_live_label_keys.erase(key);
                    m_live_label_actor_ptrs.erase(key);
                    m_missing_label_scan_counts.erase(key);
                    m_first_seen_construct_hold_states.erase(key);
                    if (m_text_buffer_bound_key == key)
                    {
                        m_text_buffer.fill('\0');
                        m_text_buffer_bound_key.clear();
                    }
                    ++pruned_count;
                }

                if (pruned_count > 0)
                {
                    m_prune_deferred_logged = false;
                    m_last_prune_defer_reason.clear();
                    save_sidecar_json(
                        "prune_destroyed_label_batch",
                        "batch:" + std::to_string(pruned_count),
                        "batch",
                        "batch");
                }
                else
                {
                    std::string defer_reason{};
                    if (blocked_by_localclient_readiness > 0)
                    {
                        defer_reason = "localclient_prune_readiness_guard";
                    }
                    else if (blocked_by_post_ready_role_lock > 0)
                    {
                        defer_reason = "post_ready_role_lock_guard";
                    }
                    else if (blocked_by_dedicated_warmup_guard > 0)
                    {
                        defer_reason = "dedicated_never_seen_warmup_guard";
                    }
                    else if (blocked_by_live_guard > 0)
                    {
                        defer_reason = require_seen_live_for_authoritative_prune
                            ? "never_seen_live_this_session_authoritative"
                            : "never_seen_live_this_session";
                    }
                    else if (blocked_by_world_count > 0)
                    {
                        defer_reason = "insufficient_live_labels_in_world";
                    }

                    if (!defer_reason.empty())
                    {
                        if (!m_prune_deferred_logged || m_last_prune_defer_reason != defer_reason)
                        {
                            log_line("[save] prune_destroyed_label deferred reason=" + defer_reason +
                                     " authoritySourceResolved=" + std::string{authority_source_resolved ? "true" : "false"} +
                                     " localClientPruneReady=" + std::string{localclient_prune_ready ? "true" : "false"} +
                                     " localClientReadyReason=" + localclient_prune_ready_reason +
                                     " sessionReadyLatched=" + std::string{m_session_ready_latched ? "true" : "false"} +
                                     " roleLockAcquired=" + std::string{m_role_lock_acquired ? "true" : "false"} +
                                     " blockedPostReadyRoleLock=" + std::to_string(blocked_by_post_ready_role_lock) +
                                     " dedicatedActiveGraceOk=" + std::string{dedicated_active_world_grace_satisfied ? "true" : "false"} +
                                     " dedicatedStableWindowOk=" + std::string{dedicated_stable_window_satisfied ? "true" : "false"} +
                                     " blockedLocalClientReady=" + std::to_string(blocked_by_localclient_readiness) +
                                     " blockedLiveGuard=" + std::to_string(blocked_by_live_guard) +
                                     " blockedDedicatedWarmup=" + std::to_string(blocked_by_dedicated_warmup_guard) +
                                     " blockedWorldCount=" + std::to_string(blocked_by_world_count) +
                                     " candidates=" + std::to_string(considered_missing_labels));
                            m_prune_deferred_logged = true;
                            m_last_prune_defer_reason = defer_reason;
                        }
                    }
                    else
                    {
                        m_prune_deferred_logged = false;
                        m_last_prune_defer_reason.clear();
                    }
                }
            }
            else
            {
                m_bootstrap_prune_phase_observed = true;
                std::string defer_reason = "no_live_labels_visible";
                if (remote_bridge_unsynced)
                {
                    defer_reason = "bridge_unsynced";
                }
                else if (!had_seen_live_labels_before_scan)
                {
                    defer_reason = "warmup_scan_guard";
                }
                if (!m_prune_deferred_logged)
                {
                    log_line("[save] prune_destroyed_label deferred reason=" + defer_reason +
                             " count=" +
                             std::to_string(m_consecutive_empty_label_scans));
                    m_prune_deferred_logged = true;
                    m_last_prune_defer_reason = defer_reason;
                }
                else if (m_last_prune_defer_reason != defer_reason)
                {
                    log_line("[save] prune_destroyed_label deferred reason=" + defer_reason +
                             " count=" + std::to_string(m_consecutive_empty_label_scans));
                    m_last_prune_defer_reason = defer_reason;
                }
            }
            if (m_bootstrap_begin_logged &&
                !m_bootstrap_end_logged &&
                m_bootstrap_prune_phase_observed &&
                authority_source_resolved)
            {
                log_line("[session] bootstrap_end stage=ready_for_player_input runtimeRole=" + m_runtime_role +
                         " authorityMode=" + m_authority_mode +
                         " sidecarKind=" + m_sidecar_kind +
                         " sidecarPath=" + m_sidecar_path.string() +
                         " bridgeRole=" + bridge_role_name(m_bridge_role) +
                         " labels=" + std::to_string(m_labels.size()));
                m_bootstrap_end_logged = true;
            }
        }

    }

    auto SignTextMod::render_ui() -> void
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_last_ui_tick_log.time_since_epoch().count() == 0 || (now - m_last_ui_tick_log) > std::chrono::seconds(3))
        {
            m_last_ui_tick_log = now;
            log_line("[ui] tab_render_tick selected=" +
                     std::string{(m_selected.has_value() ? "1" : "0")} +
                     " records=" + std::to_string(m_labels.size()));
        }

        ImGui::Text("WindroseTextSigns prototype");
        ImGui::Separator();
        ImGui::Text("Hotkey: %s", m_hotkey_name.c_str());
        ImGui::Text("Clear text: open editor, delete text, press Enter");
        ImGui::Text("Saved records: %zu", m_labels.size());
        ImGui::Text("Phase 7 native probe: %s", m_phase7_native_supported ? "supported" : "not supported");
        ImGui::TextWrapped("Native probe detail: %s", m_phase7_native_probe_summary.empty() ? "not run" : m_phase7_native_probe_summary.c_str());

        if (m_phase7_native_editor_open)
        {
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "Native editor instance is open (diagnostic runtime-created UserWidget).");
            if (ImGui::Button("Close Native Editor (Restore Game Input)"))
            {
                close_phase7_native_editor(true);
            }
        }

        if (m_phase7_native_supported && !m_phase7_imgui_fallback_enabled)
        {
            ImGui::TextDisabled("ImGui editor is fallback-only and currently disabled because native probe passed.");
            if (ImGui::Button("Enable ImGui Fallback Editor (Dev Only)"))
            {
                m_phase7_imgui_fallback_enabled = true;
                log_line("[phase7] imgui_fallback manually enabled from dev tab");
            }
        }
        else if (m_phase7_imgui_fallback_enabled)
        {
            if (ImGui::Button("Disable ImGui Fallback Editor"))
            {
                m_phase7_imgui_fallback_enabled = false;
                log_line("[phase7] imgui_fallback manually disabled from dev tab");
            }
        }

        if (ImGui::Button("Target Label From Camera (hotkey logic)"))
        {
            m_hotkey_requested.store(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Sidecar"))
        {
            load_sidecar_json();
        }

        if (!m_selected.has_value())
        {
            ImGui::TextDisabled("Look at Wooden Label then press %s.", m_hotkey_name.c_str());
            return;
        }
        if (!ensure_selected_actor_valid("render_ui"))
        {
            ImGui::TextDisabled("Selected label no longer exists. Look at a Wooden Label and press %s.", m_hotkey_name.c_str());
            return;
        }

        if (!m_phase7_imgui_fallback_enabled)
        {
            ImGui::Separator();
            ImGui::TextDisabled("ImGui text editor is disabled (Phase 7 native-first mode).");
            ImGui::TextDisabled("Use %s to invoke native path, or enable fallback above for diagnostics.", m_hotkey_name.c_str());
            return;
        }

        ImGui::Separator();
        ImGui::Text("Selected ID: %s", m_selected->stable_id.c_str());
        ImGui::Text("Asset: %s", m_selected->asset.c_str());
        ImGui::Text("Distance: %.1f", m_selected->distance);

        const auto actor_world_id = m_selected->world_id.empty() ? build_world_id_for_actor(m_selected->actor) : m_selected->world_id;
        configure_sidecar_for_actor(m_selected->actor, actor_world_id);
        const auto world_id = active_storage_world_id(actor_world_id);
        const auto key = build_storage_key(world_id, m_selected->stable_id);
        if (m_text_buffer_bound_key != key)
        {
            if (const auto found = m_labels.find(key); found != m_labels.end())
            {
                std::snprintf(m_text_buffer.data(), m_text_buffer.size(), "%s", found->second.text.c_str());
            }
            else
            {
                m_text_buffer.fill('\0');
            }
            m_text_buffer_bound_key = key;
        }
        if (auto found = m_labels.find(key); found != m_labels.end())
        {
            static bool live_surface_tune = true;
            float axis_value = std::clamp(found->second.surface_axis, 0.0f, 1.0f);
            int sign_value = (found->second.surface_sign < 0) ? -1 : 1;
            float depth_value = found->second.depth_offset;
            float align_x_value = found->second.align_x;
            float align_y_value = found->second.align_y;
            float font_size_value = std::max(1.0f, found->second.font_size);
            float color_value[4] = {
                std::clamp(found->second.color_r, 0.0f, 1.0f),
                std::clamp(found->second.color_g, 0.0f, 1.0f),
                std::clamp(found->second.color_b, 0.0f, 1.0f),
                std::clamp(found->second.color_a, 0.0f, 1.0f)};

            ImGui::Separator();
            ImGui::Text("Surface Debug Tuning");
            ImGui::Checkbox("Live surface tuning", &live_surface_tune);
            bool axis_changed = ImGui::DragFloat("surfaceAxis Blend (0.00=X, 1.00=Y)", &axis_value, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine();
            ImGui::Text("surfaceSign");
            ImGui::SameLine();
            if (ImGui::SmallButton("-1")) { sign_value = -1; }
            ImGui::SameLine();
            if (ImGui::SmallButton("+1")) { sign_value = +1; }
            bool depth_changed = ImGui::DragFloat("Depth Off Surface", &depth_value, 0.05f, -50.0f, 50.0f, "%.2f");
            bool align_x_changed = ImGui::DragFloat("Surface Align X", &align_x_value, 0.05f, -100.0f, 100.0f, "%.2f");
            bool align_y_changed = ImGui::DragFloat("Surface Align Y (Height)", &align_y_value, 0.05f, -100.0f, 100.0f, "%.2f");
            bool font_changed = ImGui::DragFloat("Font Size", &font_size_value, 0.10f, 1.0f, 300.0f, "%.2f");
            bool color_changed = ImGui::ColorEdit4("Text Color RGBA", color_value);

            const bool sign_changed = (sign_value != found->second.surface_sign);
            if (axis_changed || sign_changed || depth_changed || align_x_changed || align_y_changed || font_changed || color_changed)
            {
                axis_value = std::round(std::clamp(axis_value, 0.0f, 1.0f) * 100.0f) / 100.0f;
                found->second.surface_axis = axis_value;
                found->second.surface_sign = sign_value;
                found->second.depth_offset = depth_value;
                found->second.align_x = align_x_value;
                found->second.align_y = align_y_value;
                found->second.font_size = std::max(1.0f, font_size_value);
                found->second.color_r = std::clamp(color_value[0], 0.0f, 1.0f);
                found->second.color_g = std::clamp(color_value[1], 0.0f, 1.0f);
                found->second.color_b = std::clamp(color_value[2], 0.0f, 1.0f);
                found->second.color_a = std::clamp(color_value[3], 0.0f, 1.0f);
                found->second.last_seen_utc = now_utc();

                if (live_surface_tune)
                {
                    save_sidecar_json("surface_tune_live", key, found->second.stable_id, found->second.world_id);
                    const bool rendered = apply_text_to_actor_component(m_selected->actor, found->second.text);
                    std::ostringstream axis_text{};
                    axis_text << std::fixed << std::setprecision(2) << found->second.surface_axis;
                    log_line("[phase4] surface_tune_live key=" + key +
                             " axis=" + axis_text.str() +
                             " sign=" + std::to_string(found->second.surface_sign) +
                             " depth=" + std::to_string(found->second.depth_offset) +
                             " alignX=" + std::to_string(found->second.align_x) +
                             " alignY=" + std::to_string(found->second.align_y) +
                             " fontSize=" + std::to_string(found->second.font_size) +
                             " rendered=" + std::string{rendered ? "true" : "false"});
                }
            }

            if (ImGui::Button("Apply Surface Tuning Now"))
            {
                save_sidecar_json("surface_tune_apply", key, found->second.stable_id, found->second.world_id);
                const bool rendered = apply_text_to_actor_component(m_selected->actor, found->second.text);
                std::ostringstream axis_text{};
                axis_text << std::fixed << std::setprecision(2) << found->second.surface_axis;
                log_line("[phase4] surface_tune_apply key=" + key +
                         " axis=" + axis_text.str() +
                         " sign=" + std::to_string(found->second.surface_sign) +
                         " rendered=" + std::string{rendered ? "true" : "false"});
            }
        }
        else
        {
            ImGui::TextDisabled("Surface debug tuning appears after first Apply creates a saved record.");
        }

        ImGui::InputTextMultiline("Text", m_text_buffer.data(), m_text_buffer.size(), ImVec2(-1.0f, 100.0f));
        if (ImGui::Button("Apply"))
        {
            log_line("[ui] apply_clicked key=" + key + " stableId=" + m_selected->stable_id +
                     " worldId=" + world_id);
            apply_text_to_selected_label(std::string{m_text_buffer.data()});
            if (const auto found = m_labels.find(key); found != m_labels.end())
            {
                std::snprintf(m_text_buffer.data(), m_text_buffer.size(), "%s", found->second.text.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            log_line("[ui] clear_clicked key=" + key + " stableId=" + m_selected->stable_id +
                     " worldId=" + world_id);
            clear_text_on_selected_label();
            m_text_buffer.fill('\0');
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip Surface Side"))
        {
            const auto actor_world_id = m_selected->world_id.empty() ? build_world_id_for_actor(m_selected->actor) : m_selected->world_id;
            configure_sidecar_for_actor(m_selected->actor, actor_world_id);
            const auto world_id = active_storage_world_id(actor_world_id);
            const auto key = build_storage_key(world_id, m_selected->stable_id);
            if (auto found = m_labels.find(key); found != m_labels.end())
            {
                found->second.surface_sign = (found->second.surface_sign < 0) ? 1 : -1;
                found->second.last_seen_utc = now_utc();
                save_sidecar_json("flip_surface", key, found->second.stable_id, found->second.world_id);
                const bool rendered = apply_text_to_actor_component(m_selected->actor, found->second.text);
                log_line("[phase4] flip_surface key=" + key +
                         " sign=" + std::to_string(found->second.surface_sign) +
                         " rendered=" + std::string{rendered ? "true" : "false"});
            }
            else
            {
                log_line("[phase4] flip_surface skipped key=" + key + " reason=record_not_found");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel Target"))
        {
            m_selected.reset();
        }

        ImGui::Separator();
        ImGui::TextWrapped("Phase 4 prototype: runtime TextRender create/update/remove is active with diagnostic logging.");
    }
}
