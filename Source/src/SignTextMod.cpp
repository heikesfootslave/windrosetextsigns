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
#include <cstring>
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
#include <Unreal/Property/FEnumProperty.hpp>
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
    extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentProcessId();
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
    extern "C" __declspec(dllimport) int __stdcall GetModuleHandleExW(unsigned long dwFlags, const wchar_t* lpModuleName, void** phModule);
    extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64();
    constexpr int k_wh_keyboard_ll = 13;
    constexpr int k_wh_mouse_ll = 14;
    constexpr unsigned int k_wm_keydown = 0x0100;
    constexpr unsigned int k_wm_keyup = 0x0101;
    constexpr unsigned int k_wm_syskeydown = 0x0104;
    constexpr unsigned int k_wm_syskeyup = 0x0105;
    constexpr unsigned int k_wm_lbuttondown = 0x0201;
    constexpr unsigned int k_wm_lbuttonup = 0x0202;
    constexpr unsigned int k_wm_rbuttondown = 0x0204;
    constexpr unsigned int k_wm_rbuttonup = 0x0205;
    constexpr unsigned int k_wm_mbuttondown = 0x0207;
    constexpr unsigned int k_wm_mbuttonup = 0x0208;
    constexpr unsigned int k_wm_mousewheel = 0x020A;
    constexpr unsigned int k_wm_mousehwheel = 0x020E;
    constexpr unsigned int k_wm_quit = 0x0012;
    constexpr unsigned long k_get_module_handle_ex_flag_from_address = 0x00000004;
    constexpr unsigned long k_get_module_handle_ex_flag_unchanged_refcount = 0x00000002;
    constexpr unsigned long k_th32cs_snapprocess = 0x00000002;
    constexpr int k_default_hotkey_vk = 0x77;
    constexpr int k_vk_return = 0x0D;
    constexpr int k_vk_escape = 0x1B;
    constexpr int k_vk_shift = 0x10;
    std::atomic<bool>* g_phase7_keyboard_capture_active{};
    std::atomic<bool>* g_phase7_enter_requested{};
    std::atomic<bool>* g_phase7_escape_requested{};
    std::atomic<uint64_t>* g_phase7_mouse_capture_arm_until_ms{};
    std::atomic<bool>* g_phase7_mouse_first_down_consumed{};
    std::atomic<bool>* g_phase7_force_full_mouse_consume{};

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

    extern "C" WtsLresult __stdcall phase7_mouse_capture_proc(int code, WtsWparam w_param, WtsLparam l_param)
    {
        if (code >= 0 &&
            g_phase7_keyboard_capture_active &&
            g_phase7_keyboard_capture_active->load(std::memory_order_relaxed) &&
            l_param != 0)
        {
            const bool mouse_down =
                (w_param == k_wm_lbuttondown) ||
                (w_param == k_wm_rbuttondown) ||
                (w_param == k_wm_mbuttondown);
            const bool consume_mouse_event =
                mouse_down ||
                (w_param == k_wm_lbuttonup) ||
                (w_param == k_wm_rbuttonup) ||
                (w_param == k_wm_mbuttonup) ||
                (w_param == k_wm_mousewheel) ||
                (w_param == k_wm_mousehwheel);

            if (consume_mouse_event)
            {
                const bool force_full_mouse_consume =
                    g_phase7_force_full_mouse_consume &&
                    g_phase7_force_full_mouse_consume->load(std::memory_order_relaxed);
                if (mouse_down && g_phase7_mouse_capture_arm_until_ms && g_phase7_mouse_first_down_consumed)
                {
                    const auto arm_until_ms = g_phase7_mouse_capture_arm_until_ms->load(std::memory_order_relaxed);
                    const auto now_ms = static_cast<uint64_t>(GetTickCount64());
                    const bool arm_until_consumed = (arm_until_ms == UINT64_MAX);
                    if (arm_until_ms > 0 && (arm_until_consumed || now_ms <= arm_until_ms))
                    {
                        bool expected = false;
                        if (g_phase7_mouse_first_down_consumed->compare_exchange_strong(
                                expected,
                                true,
                                std::memory_order_relaxed,
                                std::memory_order_relaxed))
                        {
                            return 1;
                        }
                    }
                }
                if (force_full_mouse_consume)
                {
                    return 1;
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

    auto trim_ascii_copy(std::string value) -> std::string
    {
        const auto is_ws = [](unsigned char ch) {
            return std::isspace(ch) != 0;
        };
        while (!value.empty() && is_ws(static_cast<unsigned char>(value.front())))
        {
            value.erase(value.begin());
        }
        while (!value.empty() && is_ws(static_cast<unsigned char>(value.back())))
        {
            value.pop_back();
        }
        return value;
    }

    auto parse_semicolon_kv_message(const std::string& payload) -> std::unordered_map<std::string, std::string>
    {
        std::unordered_map<std::string, std::string> fields{};
        std::string_view remaining{payload};
        while (!remaining.empty())
        {
            const auto sep = remaining.find(';');
            std::string_view token = (sep == std::string_view::npos) ? remaining : remaining.substr(0, sep);
            remaining = (sep == std::string_view::npos) ? std::string_view{} : remaining.substr(sep + 1);
            token = token.substr(0, token.find_first_of("\r\n"));
            if (token.empty())
            {
                continue;
            }
            const auto eq = token.find('=');
            if (eq == std::string_view::npos || eq == 0 || eq + 1 >= token.size())
            {
                continue;
            }
            auto key = trim_ascii_copy(std::string{token.substr(0, eq)});
            auto value = trim_ascii_copy(std::string{token.substr(eq + 1)});
            if (key.empty() || value.empty())
            {
                continue;
            }
            if (value.size() >= 2 &&
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\'')))
            {
                value = value.substr(1, value.size() - 2);
            }
            fields[key] = value;
        }
        return fields;
    }

    auto field_with_alias(
        const std::unordered_map<std::string, std::string>& fields,
        const std::initializer_list<const char*> aliases) -> std::string
    {
        for (const auto* alias : aliases)
        {
            if (!alias)
            {
                continue;
            }
            if (const auto it = fields.find(alias); it != fields.end())
            {
                return it->second;
            }
        }
        return {};
    }

    auto parse_bridge_message(const std::string& payload) -> std::unordered_map<std::string, std::string>
    {
        std::unordered_map<std::string, std::string> fields{};
        const std::vector<std::string> string_fields = {
            "mod", "type", "session", "key", "stableId", "worldId",
            "text", "asset", "kind", "backingAsset", "lastSeen", "reason", "schema", "writer",
            "snapshotId", "relayHost", "token", "probeHost", "probeSource",
            "runtimeRole", "runtime_role", "role", "bridgeRole", "bridge_role", "authoritative", "isAuthoritative"};
        for (const auto& name : string_fields)
        {
            if (auto value = bridge_message_field(payload, name); value.has_value())
            {
                fields[name] = *value;
            }
        }
        const std::vector<std::string> number_fields = {
            "revision", "surfaceAxis", "surfaceSign", "depthOffset", "alignX",
            "alignY", "fontSize", "colorR", "colorG", "colorB", "colorA", "snapshotCount",
            "requesterRevision", "relayPort", "relayPid", "relayEpoch", "epoch"};
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
    auto current_executable_path() -> std::filesystem::path;

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

    enum class R5LogProcessFamily : uint8_t
    {
        Unknown = 0,
        Client,
        Server
    };

    auto detect_r5_log_process_family() -> R5LogProcessFamily
    {
        const auto exe_path = current_executable_path();
        const auto exe_name = lower_ascii_path_token(exe_path.filename().string());
        if (exe_name.find("windroseserver") != std::string::npos)
        {
            return R5LogProcessFamily::Server;
        }
        if (exe_name.find("windrose-win64-shipping") != std::string::npos ||
            exe_name.find("r5-win64-shipping") != std::string::npos)
        {
            return R5LogProcessFamily::Client;
        }

        const auto cwd_lower = lower_ascii_path_token(std::filesystem::current_path().string());
        if (cwd_lower.find("\\windowsserver\\") != std::string::npos ||
            cwd_lower.find("\\serverfiles\\") != std::string::npos)
        {
            return R5LogProcessFamily::Server;
        }
        return R5LogProcessFamily::Unknown;
    }

    auto try_get_r5_root_from_executable() -> std::optional<std::filesystem::path>
    {
        const auto exe_path = current_executable_path();
        if (exe_path.empty())
        {
            return std::nullopt;
        }

        const auto exe_dir = exe_path.parent_path();
        if (exe_dir.empty())
        {
            return std::nullopt;
        }

        // Canonical packaged layout: <R5Root>\Binaries\Win64\<exe>
        const auto win64_name = lower_ascii_path_token(exe_dir.filename().string());
        const auto binaries_dir = exe_dir.parent_path();
        const auto binaries_name = lower_ascii_path_token(binaries_dir.filename().string());
        if (win64_name == "win64" && binaries_name == "binaries")
        {
            const auto root = binaries_dir.parent_path();
            if (!root.empty())
            {
                return root;
            }
        }

        // Fallback: ascend to nearest folder that contains Saved\Logs.
        std::error_code ec{};
        for (auto current = exe_dir; !current.empty(); current = current.parent_path())
        {
            if (std::filesystem::exists(current / "Saved" / "Logs", ec))
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

    auto file_contains_marker_prefix(
        const std::filesystem::path& path,
        const std::string& marker_lower,
        size_t max_bytes = (256 * 1024)) -> bool
    {
        std::ifstream input{path, std::ios::binary};
        if (!input.is_open())
        {
            return false;
        }

        std::string chunk{};
        chunk.resize(max_bytes);
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        chunk.resize(static_cast<size_t>(std::max<std::streamsize>(0, input.gcount())));
        if (chunk.empty())
        {
            return false;
        }

        auto lowered = chunk;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return lowered.find(marker_lower) != std::string::npos;
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

        const auto family = detect_r5_log_process_family();
        const auto r5_root_opt = try_get_r5_root_from_executable();
        if (!preferred.empty())
        {
            bool accept_preferred = true;
            if (family == R5LogProcessFamily::Server)
            {
                accept_preferred = false;
                if (r5_root_opt.has_value())
                {
                    const auto expected_logs_dir = normalized_path_for_compare(*r5_root_opt / "Saved" / "Logs");
                    const auto preferred_parent = normalized_path_for_compare(preferred.parent_path());
                    if (preferred_parent == expected_logs_dir)
                    {
                        accept_preferred = true;
                    }
                }
                if (!accept_preferred &&
                    file_contains_marker_prefix(preferred, "executablename: windroseserver-win64-shipping.exe"))
                {
                    accept_preferred = true;
                }
            }
            if (accept_preferred)
            {
                append_unique_path(candidates, preferred);
            }
        }

        if (r5_root_opt.has_value())
        {
            append_unique_path(candidates, *r5_root_opt / "Saved" / "Logs" / "R5.log");
        }

        if (family == R5LogProcessFamily::Server)
        {
            // Hosted/Dedicated server hardening:
            // never prioritize LocalAppData client logs for a server process.
            if (r5_root_opt.has_value())
            {
                const auto logs_dir = *r5_root_opt / "Saved" / "Logs";
                std::error_code ec{};
                if (std::filesystem::exists(logs_dir, ec))
                {
                    std::vector<std::filesystem::directory_entry> log_files{};
                    for (const auto& entry : std::filesystem::directory_iterator(logs_dir, ec))
                    {
                        if (ec || !entry.is_regular_file())
                        {
                            continue;
                        }
                        if (lower_ascii_path_token(entry.path().extension().string()) == ".log")
                        {
                            const auto file_name_lower = lower_ascii_path_token(entry.path().filename().string());
                            if (file_name_lower.find("_r5.log") != std::string::npos)
                            {
                                continue;
                            }
                            log_files.push_back(entry);
                        }
                    }

                    std::sort(log_files.begin(), log_files.end(), [](const auto& lhs, const auto& rhs) {
                        std::error_code lhs_ec{};
                        std::error_code rhs_ec{};
                        return std::filesystem::last_write_time(lhs.path(), lhs_ec) >
                            std::filesystem::last_write_time(rhs.path(), rhs_ec);
                    });

                    size_t inspected = 0;
                    for (const auto& entry : log_files)
                    {
                        if (inspected++ >= 12)
                        {
                            break;
                        }
                        if (file_contains_marker_prefix(
                                entry.path(),
                                "executablename: windroseserver-win64-shipping.exe"))
                        {
                            append_unique_path(candidates, entry.path());
                        }
                    }
                }
            }
            return candidates;
        }

        // Client-family fallback candidate after process-relative path.
        const auto local_app_data = get_env_var("LOCALAPPDATA");
        if (!local_app_data.empty())
        {
            append_unique_path(candidates, std::filesystem::path{local_app_data} / "R5" / "Saved" / "Logs" / "R5.log");
        }

        // Unknown-family fallback only.
        if (family == R5LogProcessFamily::Unknown)
        {
            const auto cwd = std::filesystem::current_path();
            append_unique_path(candidates, cwd / ".." / ".." / "Saved" / "Logs" / "R5.log");
            append_unique_path(candidates, cwd / "R5" / "Saved" / "Logs" / "R5.log");
            append_unique_path(candidates, cwd / "Saved" / "Logs" / "R5.log");
        }
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

    auto has_local_windrose_and_server_process_evidence() -> bool
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

        bool windrose_server_seen = false;
        bool windrose_client_seen = false;
        do
        {
            const auto exe_name = wts_lower_ascii_from_wide(entry.szExeFile);
            if (exe_name.find("windroseserver") != std::string::npos)
            {
                windrose_server_seen = true;
            }
            if (exe_name == "r5-win64-shipping.exe")
            {
                windrose_client_seen = true;
            }
            if (windrose_server_seen && windrose_client_seen)
            {
                cached_result = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));

        CloseHandle(snapshot);
        return cached_result;
    }

    auto try_latest_remoteaddr_host_from_log_window(
        const std::filesystem::path& log_path,
        const uintmax_t window_start_offset) -> std::optional<std::string>
    {
        if (log_path.empty())
        {
            return std::nullopt;
        }

        std::error_code ec{};
        const auto file_size = std::filesystem::file_size(log_path, ec);
        if (ec || file_size == 0)
        {
            return std::nullopt;
        }

        const uintmax_t start_offset = std::min<uintmax_t>(window_start_offset, file_size);
        if (start_offset >= file_size)
        {
            return std::nullopt;
        }

        std::ifstream input{log_path, std::ios::binary};
        if (!input.is_open())
        {
            return std::nullopt;
        }

        input.seekg(static_cast<std::streamoff>(start_offset), std::ios::beg);
        if (!input.good())
        {
            return std::nullopt;
        }

        std::string content{};
        constexpr size_t k_max_read_bytes = 8 * 1024 * 1024;
        content.reserve(k_max_read_bytes);

        std::array<char, 64 * 1024> buffer{};
        while (input.good() && content.size() < k_max_read_bytes)
        {
            const auto remaining = k_max_read_bytes - content.size();
            const auto chunk_size = static_cast<std::streamsize>(std::min<size_t>(buffer.size(), remaining));
            input.read(buffer.data(), chunk_size);
            const auto got = input.gcount();
            if (got <= 0)
            {
                break;
            }
            content.append(buffer.data(), static_cast<size_t>(got));
        }

        if (content.empty())
        {
            return std::nullopt;
        }

        static const std::regex remoteaddr_endpoint_rx{
            R"(\bRemoteAddr:\s*([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)\b)",
            std::regex::icase};
        std::optional<std::string> last_host{};
        for (auto it = std::sregex_iterator(content.begin(), content.end(), remoteaddr_endpoint_rx);
             it != std::sregex_iterator{};
             ++it)
        {
            if (it->size() < 2)
            {
                continue;
            }
            const auto ip = (*it)[1].str();
            if (parse_ipv4_octets(ip).has_value())
            {
                last_host = ip;
            }
        }

        return last_host;
    }

    struct RouteEndpoint
    {
        std::string host{};
        uint16_t port{0};
        std::string source{};
    };

    auto try_latest_definitive_route_endpoint_from_log_window(
        const std::filesystem::path& log_path,
        const uintmax_t window_start_offset) -> std::optional<RouteEndpoint>
    {
        if (log_path.empty())
        {
            return std::nullopt;
        }

        std::error_code ec{};
        const auto file_size = std::filesystem::file_size(log_path, ec);
        if (ec || file_size == 0)
        {
            return std::nullopt;
        }

        const uintmax_t start_offset = std::min<uintmax_t>(window_start_offset, file_size);
        if (start_offset >= file_size)
        {
            return std::nullopt;
        }

        std::ifstream input{log_path, std::ios::binary};
        if (!input.is_open())
        {
            return std::nullopt;
        }

        input.seekg(static_cast<std::streamoff>(start_offset), std::ios::beg);
        if (!input.good())
        {
            return std::nullopt;
        }

        std::string content{};
        constexpr size_t k_max_read_bytes = 8 * 1024 * 1024;
        content.reserve(k_max_read_bytes);

        std::array<char, 64 * 1024> buffer{};
        while (input.good() && content.size() < k_max_read_bytes)
        {
            const auto remaining = k_max_read_bytes - content.size();
            const auto chunk_size = static_cast<std::streamsize>(std::min<size_t>(buffer.size(), remaining));
            input.read(buffer.data(), chunk_size);
            const auto got = input.gcount();
            if (got <= 0)
            {
                break;
            }
            content.append(buffer.data(), static_cast<size_t>(got));
        }

        if (content.empty())
        {
            return std::nullopt;
        }

        static const std::regex coop_serverurl_endpoint_rx{
            R"(\bCoop server connected.*ServerUrl\s*'([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)')",
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

        std::optional<RouteEndpoint> last_serverurl{};
        std::optional<RouteEndpoint> last_other{};
        std::istringstream lines{content};
        std::string line{};
        std::smatch match{};
        const auto parse_int_fallback = [](const std::string& text, int fallback) {
            try
            {
                size_t consumed = 0;
                const int value = std::stoi(text, &consumed, 10);
                if (consumed != text.size())
                {
                    return fallback;
                }
                return value;
            }
            catch (...)
            {
                return fallback;
            }
        };
        while (std::getline(lines, line))
        {
            auto capture = [&](const std::regex& rx, const std::string& source, std::optional<RouteEndpoint>& sink) {
                if (!std::regex_search(line, match, rx) || match.size() < 3)
                {
                    return false;
                }
                const std::string host = match[1].str();
                const int port_i = parse_int_fallback(match[2].str(), 0);
                if (!parse_ipv4_octets(host).has_value() || port_i <= 0 || port_i > 65535)
                {
                    return false;
                }
                RouteEndpoint endpoint{};
                endpoint.host = host;
                endpoint.port = static_cast<uint16_t>(port_i);
                endpoint.source = source;
                sink = endpoint;
                return true;
            };

            if (capture(coop_serverurl_endpoint_rx, "coop_serverurl", last_serverurl))
            {
                continue;
            }
            if (capture(browse_endpoint_rx, "browse_endpoint", last_other))
            {
                continue;
            }
            if (capture(loadmap_endpoint_rx, "loadmap_endpoint", last_other))
            {
                continue;
            }
            (void)capture(remoteaddr_endpoint_rx, "remoteaddr_endpoint", last_other);
        }

        if (last_serverurl.has_value())
        {
            return last_serverurl;
        }
        return last_other;
    }

    auto discover_bridge_route_from_r5_log(const std::filesystem::path& preferred) -> BridgeRouteDiscovery
    {
        static const std::regex candidate_rx{
            R"(\b(?:UDP|TCP)\s+([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)\s+[A-Fa-f0-9]+\s+\d+\s+(host|srflx)\b)",
            std::regex::icase};
        static const std::regex direct_server_address_rx{
            R"(Start direct connection to server\.\s*ServerAddress\s+([0-9]{1,3}(?:\.[0-9]{1,3}){3}))",
            std::regex::icase};
        static const std::regex coop_serverurl_direct_rx{
            R"(\bCoop server connected.*ServerUrl\s*'([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)')",
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
                if (std::regex_search(line, direct_match, coop_serverurl_direct_rx) && direct_match.size() >= 2)
                {
                    append_fallback_candidate(direct_match[1].str(), "coop_serverurl");
                }
            }

            BridgeRouteDiscovery result{};
            result.log_path = candidate_path;
            result.remote_host_candidate = remote_hosts.empty() ? std::string{} : remote_hosts.front();
            result.remote_public_candidate = remote_publics.empty() ? std::string{} : remote_publics.front();
            result.local_host_summary = summarize_ips(local_hosts);
            result.fallback_direct_candidates = fallback_direct_candidates;
            const bool process_same_machine_evidence = has_local_windrose_and_server_process_evidence();
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
        out.runtime_role = dedicated_server ? "ServerRolePending" : "LocalClientPending";
        out.authority_mode = dedicated_server ? "ServerRoleClassificationPending" : "WorldAuthorityPending";
        out.data_mode = dedicated_server ? "ServerClassificationPending" : "LocalClientStartupCache";
        out.sidecar_kind = dedicated_server ? "pending" : "cache";
        out.authoritative = false;

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
                    ? profile_root / "WindroseTextSigns" / "ServerPending" / *world_id
                    : profile_root / "WindroseTextSigns" / "StartupCache" / *world_id;
                return out;
            }
        }

        if (!profile_roots.empty())
        {
            out.profile_root = profile_roots.front();
            out.world_id = "unknown-world";
            out.data_root = dedicated_server
                ? out.profile_root / "WindroseTextSigns" / "ServerPending" / out.world_id
                : out.profile_root / "WindroseTextSigns" / "StartupCache" / out.world_id;
            out.data_mode = dedicated_server ? "ServerClassificationPendingWorld" : "LocalClientStartupCachePendingWorld";
            out.sidecar_kind = dedicated_server ? "pending-world" : "cache-pending-world";
            out.authoritative = false;
            return out;
        }

        out.profile_root = mod_root;
        out.world_id = "unknown-world";
        out.data_root = mod_root / "Cache";
        out.data_mode = dedicated_server ? "ServerClassificationPendingFallbackModRoot" : "LocalClientStartupCacheFallbackModRoot";
        out.sidecar_kind = dedicated_server ? "pending-fallback-modroot" : "cache-fallback-modroot";
        out.authoritative = false;
        out.data_root /= dedicated_server ? "ServerPending" : "StartupCache";
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

    auto fit_text_for_plaque(
        std::string_view input_text,
        float char_width_factor,
        float font_min,
        float font_max) -> AutoSizeResult
    {
        const float k_font_min = std::clamp(font_min, 1.0f, 512.0f);
        const float k_font_max = std::max(k_font_min, std::clamp(font_max, 1.0f, 512.0f));
        constexpr int k_rows_min = 1;
        constexpr int k_rows_max = 4;
        constexpr float k_horizontal_budget = 150.0f;
        constexpr float k_line_step_factor = 0.40f;
        constexpr float k_vertical_budget = 24.0f;
        const float width_factor = std::clamp(char_width_factor, 0.20f, 2.00f);

        AutoSizeResult best{};
        best.font_size = k_font_min;
        best.rows = 1;
        best.char_limit = 12;
        best.truncated = false;

        const auto input_lines = split_lines_preserve_breaks(input_text);
        const bool has_explicit_line_breaks = (input_lines.size() > 1);
        const bool single_line_single_word_input =
            input_lines.size() == 1 &&
            split_words(input_lines.front()).size() == 1;
        const int explicit_rows = static_cast<int>(input_lines.size());
        int input_total_chars = 0;
        for (const auto& line : input_lines)
        {
            input_total_chars += static_cast<int>(line.size());
        }

        bool found_valid = false;
        AutoSizeResult best_valid{};
        int best_valid_coverage = -1;
        float best_valid_font = k_font_min;
        int best_valid_added_rows = 999;

        AutoSizeResult best_fallback{};
        bool found_fallback = false;
        int best_fallback_coverage = -1;
        float best_fallback_font = k_font_min;

        if (input_lines.empty())
        {
            best.wrapped_text.clear();
            return best;
        }

        for (float font = k_font_max; font >= k_font_min; font -= 0.5f)
        {
            const float estimated_char_width = std::max(0.001f, font * width_factor);
            const float chars_f = k_horizontal_budget / estimated_char_width;
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
                // Preserve single words on one line whenever possible.
                // If a line contains exactly one word and it does not fit the current
                // width, force a smaller font first instead of splitting that word.
                // This applies both to explicit multi-line input and single-line single-word
                // input (e.g., "Hardwood"). Intra-word splitting is only allowed at the
                // absolute minimum font as a last-resort fallback.
                const bool single_word_line = line_words.size() == 1;
                const bool preserve_single_word_line =
                    single_word_line &&
                    (has_explicit_line_breaks || single_line_single_word_input);
                if (preserve_single_word_line &&
                    static_cast<int>(line_words.front().size()) > char_limit &&
                    font > (k_font_min + 0.001f))
                {
                    candidate.truncated = true;
                    break;
                }
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
            int candidate_coverage_chars = 0;
            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (i > 0)
                {
                    candidate.wrapped_text.push_back('\n');
                }
                candidate.wrapped_text += rows[i];
                candidate_coverage_chars += static_cast<int>(rows[i].size());
            }

            const float vertical_usage = static_cast<float>(candidate.rows) * (font * k_line_step_factor);
            const bool candidate_valid =
                !candidate.truncated &&
                candidate.rows >= k_rows_min &&
                candidate.rows <= k_rows_max &&
                vertical_usage <= k_vertical_budget;

            if (candidate_valid)
            {
                const int added_rows = std::max(0, candidate.rows - explicit_rows);
                bool better_valid = false;
                if (!found_valid)
                {
                    better_valid = true;
                }
                else if (candidate_coverage_chars > best_valid_coverage)
                {
                    better_valid = true;
                }
                else if (candidate_coverage_chars == best_valid_coverage && font > best_valid_font)
                {
                    better_valid = true;
                }
                else if (candidate_coverage_chars == best_valid_coverage && font == best_valid_font)
                {
                    if (has_explicit_line_breaks)
                    {
                        if (added_rows < best_valid_added_rows)
                        {
                            better_valid = true;
                        }
                        else if (added_rows == best_valid_added_rows && candidate.rows < best_valid.rows)
                        {
                            better_valid = true;
                        }
                    }
                    else if (candidate.rows < best_valid.rows)
                    {
                        better_valid = true;
                    }
                }

                if (better_valid)
                {
                    found_valid = true;
                    best_valid = candidate;
                    best_valid_coverage = candidate_coverage_chars;
                    best_valid_font = font;
                    best_valid_added_rows = added_rows;
                }
            }

            // Fallback candidate path (used only if no valid fit exists):
            // keep as much text as possible, then prefer larger font.
            bool better_fallback = false;
            if (!found_fallback)
            {
                better_fallback = true;
            }
            else if (candidate_coverage_chars > best_fallback_coverage)
            {
                better_fallback = true;
            }
            else if (candidate_coverage_chars == best_fallback_coverage && font > best_fallback_font)
            {
                better_fallback = true;
            }
            else if (candidate_coverage_chars == best_fallback_coverage && font == best_fallback_font)
            {
                const bool candidate_full_coverage = (candidate_coverage_chars >= input_total_chars);
                const bool best_fallback_full_coverage = (best_fallback_coverage >= input_total_chars);
                if (candidate_full_coverage && !best_fallback_full_coverage)
                {
                    better_fallback = true;
                }
            }

            if (better_fallback)
            {
                found_fallback = true;
                best_fallback = candidate;
                best_fallback_coverage = candidate_coverage_chars;
                best_fallback_font = font;
            }
        }

        if (found_valid)
        {
            best = best_valid;
        }
        else if (found_fallback)
        {
            best = best_fallback;
        }
        else
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

    auto is_uobject_reflection_safe(UObject* candidate) -> bool;

    auto get_player_viewpoint_reflective(UObject* player_controller) -> Viewpoint
    {
        Viewpoint out{};
        if (!player_controller || !is_uobject_reflection_safe(player_controller))
        {
            return out;
        }
        if (!player_controller->IsA(AActor::StaticClass()))
        {
            return out;
        }
        auto* controller_actor = Cast<AActor>(player_controller);
        if (!controller_actor || !controller_actor->GetWorld())
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

        const int32_t structure_size = fn->GetStructureSize();
        if (structure_size <= 0 || structure_size > (64 * 1024))
        {
            return out;
        }
        const int32_t param_bytes = std::max<int32_t>(structure_size, 256);
        std::vector<uint8_t> params(static_cast<size_t>(param_bytes), 0);
        if (!is_uobject_reflection_safe(player_controller))
        {
            return out;
        }
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
        if (path_name)
        {
            if (auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path_name))
            {
                return fn;
            }
        }
        if (context && in_chain_name && is_uobject_reflection_safe(context))
        {
            if (auto* fn = context->GetFunctionByNameInChain(in_chain_name))
            {
                return fn;
            }
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
        if (!context || !is_uobject_reflection_safe(context))
        {
            return false;
        }
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!fn || !is_uobject_reflection_safe(context))
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

    struct EnumInvokeDiag
    {
        bool has_unhandled_param{false};
        std::string fn_name{};
        std::string prop_class{};
        int32_t prop_size{0};
    };

    auto invoke_with_enum_byte_or_int_param_cached(UObject* context, UFunction* fn, uint8_t value, EnumInvokeDiag* out_diag) -> bool
    {
        if (!context || !fn)
        {
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(std::max<int32_t>(fn->GetStructureSize(), 16)), 0);
        bool assigned = false;
        bool captured_unhandled = false;

        for_each_property_in_chain_compat(fn, [&](FProperty* prop) {
            if (!prop || prop->HasAnyPropertyFlags(CPF_ReturnParm) || prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                return;
            }

            const auto prop_hash = prop->GetClass().HashObject();
            const auto prop_size = prop->GetSize();

            if (prop_hash == FEnumProperty::StaticClass().HashObject())
            {
                if (auto* raw_ptr = prop->ContainerPtrToValuePtr<uint8_t>(params.data()))
                {
                    std::memset(raw_ptr, 0, static_cast<size_t>(std::max<int32_t>(prop_size, 0)));
                    const uint64_t enum_value = static_cast<uint64_t>(value);
                    std::memcpy(raw_ptr, &enum_value, static_cast<size_t>(std::min<int32_t>(prop_size, static_cast<int32_t>(sizeof(enum_value)))));
                    assigned = true;
                }
                return;
            }
            if (prop_hash == FByteProperty::StaticClass().HashObject())
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<uint8_t>(params.data()))
                {
                    *value_ptr = value;
                    assigned = true;
                }
                return;
            }
            if (prop_size == static_cast<int32_t>(sizeof(int32)))
            {
                if (auto* value_ptr = prop->ContainerPtrToValuePtr<int32>(params.data()))
                {
                    *value_ptr = static_cast<int32>(value);
                    assigned = true;
                }
                return;
            }

            if (!captured_unhandled && out_diag)
            {
                captured_unhandled = true;
                out_diag->has_unhandled_param = true;
                out_diag->fn_name = RC::to_string(fn->GetFullName());
                out_diag->prop_class = RC::to_string(prop->GetName());
                out_diag->prop_size = prop_size;
            }
        });

        if (!assigned)
        {
            if (out_diag && !out_diag->has_unhandled_param)
            {
                out_diag->has_unhandled_param = true;
                out_diag->fn_name = RC::to_string(fn->GetFullName());
                out_diag->prop_class = "none";
                out_diag->prop_size = 0;
            }
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

    auto get_controller_pawn_property_only(UObject* controller) -> UObject*
    {
        if (!controller || !is_uobject_reflection_safe(controller))
        {
            return nullptr;
        }
        if (auto* pawn = get_object_property_if_present(controller, "Pawn"); pawn)
        {
            return pawn;
        }
        return get_object_property_if_present(controller, "AcknowledgedPawn");
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
        m_role_lock_start_signal.clear();
        m_bridge_route_lock_acquired = false;
        m_bridge_route_locked_host.clear();
        m_bridge_route_loopback_same_machine_ok = false;
        m_bridge_route_rejected_candidates_logged.clear();
        m_bridge_route_fallback_candidates_logged.clear();
        m_bridge_route_bootstrap_pause_logged = false;
        m_session_window_open = false;
        m_definitive_session_start_seen = false;
        m_definitive_session_start_signal.clear();
        m_definitive_session_exit_signal.clear();
        m_session_window_log_path.clear();
        m_session_window_start_offset = 0;
        m_session_window_end_offset = 0;
        m_session_window_blocked_last_log = {};
        m_session_window_blocked_last_signature.clear();
        reset_server_role_classification_state("bootstrap_open_log");
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
        log_line("[phase4-font] config asset=" + config_string_value("WTS_WORLD_TEXT_FONT_ASSET", "none") +
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
        log_line("[autosize] defaults_profile_deferred reason=await_ready_latch");
        log_line("[autosize] char_width_factor=" + std::to_string(m_autosize_char_width_factor));
        log_line("[autosize] row_gap_factor=" + std::to_string(m_row_gap_factor));
        log_line("[autosize] row_gap_factors row2=" + std::to_string(m_row_gap_factor_2) +
                 " row3=" + std::to_string(m_row_gap_factor_3) +
                 " row4=" + std::to_string(m_row_gap_factor_4));
        log_line("[autosize] row_offsets_1=" + std::to_string(m_row_offsets_1[0]));
        log_line("[autosize] row_offsets_2=" + std::to_string(m_row_offsets_2[0]) + "," + std::to_string(m_row_offsets_2[1]));
        log_line("[autosize] row_offsets_3=" + std::to_string(m_row_offsets_3[0]) + "," + std::to_string(m_row_offsets_3[1]) + "," + std::to_string(m_row_offsets_3[2]));
        log_line("[autosize] row_offsets_4=" + std::to_string(m_row_offsets_4[0]) + "," + std::to_string(m_row_offsets_4[1]) + "," + std::to_string(m_row_offsets_4[2]) + "," + std::to_string(m_row_offsets_4[3]));
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
            request_hotkey_press("keydown_callback");
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
        log_line("[input] Registered hotkeys: " + m_hotkey_name + "=target/open_or_close_editor");
    }

    auto SignTextMod::request_hotkey_press(const char* source) -> bool
    {
        const std::string source_name = (source && *source) ? source : "unknown";
        if (m_hotkey_require_release_before_next_press)
        {
            log_line("[input] hotkey_request_ignored reason=await_release key=" + m_hotkey_name +
                     " source=" + source_name);
            return false;
        }
        if (m_hotkey_action_in_flight)
        {
            log_line("[input] hotkey_request_ignored reason=in_flight key=" + m_hotkey_name +
                     " source=" + source_name);
            return false;
        }

        m_hotkey_action_in_flight = true;
        m_hotkey_require_release_before_next_press = true;
        m_hotkey_requested.store(true);
        log_line("[input] hotkey_request_accepted key=" + m_hotkey_name +
                 " source=" + source_name);
        return true;
    }

    auto SignTextMod::install_phase7_keyboard_capture_hook() -> void
    {
        g_phase7_keyboard_capture_active = &m_phase7_keyboard_capture_active;
        g_phase7_enter_requested = &m_phase7_enter_requested;
        g_phase7_escape_requested = &m_phase7_escape_requested;
        g_phase7_mouse_capture_arm_until_ms = &m_phase7_mouse_capture_arm_until_ms;
        g_phase7_mouse_first_down_consumed = &m_phase7_mouse_first_down_consumed;
        g_phase7_force_full_mouse_consume = &m_phase7_force_full_mouse_consume;

        if (m_phase7_keyboard_hook_thread.joinable())
        {
            return;
        }

        m_phase7_keyboard_hook_stop.store(false);
        m_phase7_keyboard_hook_installed.store(false);
        m_phase7_keyboard_hook_thread_id.store(0);
        m_phase7_keyboard_hook_thread = std::thread([this]() {
            const auto thread_id = GetCurrentThreadId();
            m_phase7_keyboard_hook_thread_id.store(thread_id);

            void* hook_module = nullptr;
            const unsigned long hook_module_flags =
                k_get_module_handle_ex_flag_from_address |
                k_get_module_handle_ex_flag_unchanged_refcount;
            GetModuleHandleExW(
                hook_module_flags,
                reinterpret_cast<const wchar_t*>(reinterpret_cast<uintptr_t>(&phase7_keyboard_capture_proc)),
                &hook_module);

            WtsHhook keyboard_hook = SetWindowsHookExW(k_wh_keyboard_ll, phase7_keyboard_capture_proc, hook_module, 0);
            WtsHhook mouse_hook = SetWindowsHookExW(k_wh_mouse_ll, phase7_mouse_capture_proc, hook_module, 0);
            if (!keyboard_hook || !mouse_hook)
            {
                if (keyboard_hook)
                {
                    UnhookWindowsHookEx(keyboard_hook);
                }
                if (mouse_hook)
                {
                    UnhookWindowsHookEx(mouse_hook);
                }
                m_phase7_keyboard_hook_installed.store(false);
                m_phase7_keyboard_hook_thread_id.store(0);
                return;
            }

            m_phase7_keyboard_hook_installed.store(true);

            WtsMsg msg{};
            while (!m_phase7_keyboard_hook_stop.load(std::memory_order_relaxed))
            {
                const int rc = GetMessageW(&msg, nullptr, 0, 0);
                if (rc <= 0)
                {
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            UnhookWindowsHookEx(mouse_hook);
            UnhookWindowsHookEx(keyboard_hook);
            m_phase7_keyboard_hook_installed.store(false);
            m_phase7_keyboard_hook_thread_id.store(0);
        });
    }

    auto SignTextMod::uninstall_phase7_keyboard_capture_hook() -> void
    {
        m_phase7_keyboard_capture_active.store(false);
        m_phase7_mouse_capture_arm_until_ms.store(0);
        m_phase7_mouse_first_down_consumed.store(false);
        m_phase7_force_full_mouse_consume.store(false);
        m_phase7_keyboard_hook_stop.store(true);
        const auto hook_thread_id = m_phase7_keyboard_hook_thread_id.load(std::memory_order_relaxed);
        if (hook_thread_id != 0)
        {
            PostThreadMessageW(hook_thread_id, k_wm_quit, 0, 0);
        }
        if (m_phase7_keyboard_hook_thread.joinable())
        {
            m_phase7_keyboard_hook_thread.join();
        }
        m_phase7_keyboard_hook_stop.store(false);
        m_phase7_keyboard_hook_installed.store(false);
        m_phase7_keyboard_hook_thread_id.store(0);
        g_phase7_keyboard_capture_active = nullptr;
        g_phase7_enter_requested = nullptr;
        g_phase7_escape_requested = nullptr;
        g_phase7_mouse_capture_arm_until_ms = nullptr;
        g_phase7_mouse_first_down_consumed = nullptr;
        g_phase7_force_full_mouse_consume = nullptr;
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
            if (enable_ui_mode)
            {
                install_phase7_keyboard_capture_hook();
                m_phase7_keyboard_capture_active.store(true);
            }
            else
            {
                m_phase7_keyboard_capture_active.store(false);
                m_phase7_mouse_capture_arm_until_ms.store(0);
                m_phase7_mouse_first_down_consumed.store(false);
                m_phase7_force_full_mouse_consume.store(false);
            }
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
            // Prefer UIOnly so mouse clicks stay owned by the editor widget and do not
            // pass through to gameplay bindings underneath.
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

        if (enable_ui_mode)
        {
            install_phase7_keyboard_capture_hook();
            // Hook install is async; wait briefly so first-click capture is armed
            // before the editor is interactable.
            const auto hook_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
            while (!m_phase7_keyboard_hook_installed.load(std::memory_order_relaxed) &&
                   std::chrono::steady_clock::now() < hook_deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            m_phase7_keyboard_capture_active.store(true);
            // Do not hard-consume all mouse input; that blocks text box interaction.
            // Keep first-click arm swallow as the passthrough guard.
            m_phase7_force_full_mouse_consume.store(false);
        }
        else
        {
            m_phase7_keyboard_capture_active.store(false);
            m_phase7_mouse_capture_arm_until_ms.store(0);
            m_phase7_mouse_first_down_consumed.store(false);
            m_phase7_force_full_mouse_consume.store(false);
        }

        const bool ll_hook_installed = m_phase7_keyboard_hook_installed.load(std::memory_order_relaxed);
        const bool ll_hook_active = enable_ui_mode && ll_hook_installed;
        const bool ll_force_mouse = enable_ui_mode && m_phase7_force_full_mouse_consume.load(std::memory_order_relaxed);
        log_line("[phase7-umg] input_capture enable=" + std::string{enable_ui_mode ? "true" : "false"} +
                 " inputMode=" + std::string{input_mode_applied ? "true" : "false"} +
                 " appliedMode=" + applied_mode_name +
                 " cursor=" + std::string{cursor_set ? "true" : "false"} +
                 " ignoreLook=" + std::string{look_ignored ? "true" : "false"} +
                 " ignoreMove=" + std::string{move_ignored ? "true" : "false"} +
                 " llHookInstalled=" + std::string{ll_hook_installed ? "true" : "false"} +
                 " llHookActive=" + std::string{ll_hook_active ? "true" : "false"} +
                 " llForceMouseConsume=" + std::string{ll_force_mouse ? "true" : "false"});
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
        m_phase7_mouse_first_down_consumed.store(false);
        m_phase7_mouse_capture_arm_until_ms.store(UINT64_MAX);
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
        m_phase7_mouse_capture_arm_until_ms.store(0);
        m_phase7_mouse_first_down_consumed.store(false);
        m_phase7_force_full_mouse_consume.store(false);
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

    auto SignTextMod::apply_phase7_umg_debug_scales(const char* reason) -> void
    {
        if (!m_phase7_umg_hint || !m_phase7_umg_status)
        {
            return;
        }
        if (!is_uobject_reflection_safe(m_phase7_umg_hint) ||
            !is_uobject_reflection_safe(m_phase7_umg_status))
        {
            return;
        }

        m_phase7_debug_hint_render_scale = std::clamp(m_phase7_debug_hint_render_scale, 0.25f, 2.00f);
        m_phase7_debug_status_render_scale = std::clamp(m_phase7_debug_status_render_scale, 0.25f, 2.00f);
        const bool hint_ok = invoke_set_vector2d_value(
            m_phase7_umg_hint, STR("SetRenderScale"), STR("/Script/UMG.Widget:SetRenderScale"),
            m_phase7_debug_hint_render_scale, m_phase7_debug_hint_render_scale);
        const bool status_ok = invoke_set_vector2d_value(
            m_phase7_umg_status, STR("SetRenderScale"), STR("/Script/UMG.Widget:SetRenderScale"),
            m_phase7_debug_status_render_scale, m_phase7_debug_status_render_scale);

        if (reason)
        {
            std::ostringstream hint_text{};
            hint_text << std::fixed << std::setprecision(2) << m_phase7_debug_hint_render_scale;
            std::ostringstream status_text{};
            status_text << std::fixed << std::setprecision(2) << m_phase7_debug_status_render_scale;
            log_line("[phase7-umg] debug_render_scale_apply reason=" + std::string{reason} +
                     " hint=" + hint_text.str() +
                     " status=" + status_text.str() +
                     " hintOk=" + std::string{hint_ok ? "true" : "false"} +
                     " statusOk=" + std::string{status_ok ? "true" : "false"});
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
        m_phase7_umg_status = nullptr;
        m_phase7_umg_apply_button = nullptr;
        m_phase7_umg_clear_button = nullptr;
        m_phase7_umg_cancel_button = nullptr;
        m_phase7_fn_add_to_viewport = nullptr;
        m_phase7_fn_remove_from_parent = nullptr;
        m_phase7_fn_set_keyboard_focus = nullptr;
        m_phase7_fn_set_focus = nullptr;
        m_phase7_fn_set_visibility = nullptr;
        m_phase7_umg_in_viewport = false;
        m_phase7_umg_open_pending = false;
        m_phase7_open_sla_violation_logged = false;
        m_phase7_open_pending_since = {};
        m_phase7_ui_input_mode_active = false;
        m_phase7_active_epoch = 0;
        m_phase7_teardown_pending = false;
        m_phase7_teardown_pending_reason.clear();
        m_phase7_watchdog_logged = false;
        m_phase7_status_dirty = true;
        m_phase7_last_status_ui_refresh = {};
        m_phase7_last_status_role_text.clear();
        m_phase7_last_status_network_text.clear();
        m_phase7_last_status_log = {};
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
        auto* status = create_umg_widget_object(tree, m_phase7_class_text_block, "WTS_Status");

        if (!root || !frame || !background || !panel || !title || !divider || !input_frame || !input_background || !editor || !text_box || !hint || !status)
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
                     " hint=" + std::string{hint ? "1" : "0"} +
                     " status=" + std::string{status ? "1" : "0"});
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
        const bool status_text = invoke_umg_set_text(status, "Status\nRole: Error: Not locked\nNetwork: Error - Not connected to Server");
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
        const bool status_color =
            invoke_set_rgba_value(status, STR("SetColorAndOpacity"), nullptr, 0.82f, 0.80f, 0.75f, 1.0f) ||
            invoke_set_rgba_value(status, STR("SetForegroundColor"), nullptr, 0.82f, 0.80f, 0.75f, 1.0f);
        const bool status_pivot = invoke_set_vector2d_value(
            status, STR("SetRenderTransformPivot"), STR("/Script/UMG.Widget:SetRenderTransformPivot"),
            0.0f, 0.0f);
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
        const bool hint_scale = invoke_set_vector2d_value(
            hint, STR("SetRenderScale"), STR("/Script/UMG.Widget:SetRenderScale"),
            m_phase7_debug_hint_render_scale, m_phase7_debug_hint_render_scale);
        const bool status_scale = invoke_set_vector2d_value(
            status, STR("SetRenderScale"), STR("/Script/UMG.Widget:SetRenderScale"),
            m_phase7_debug_status_render_scale, m_phase7_debug_status_render_scale);
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
        const bool hint_pos = invoke_set_vector2d_value(hint_slot, STR("SetPosition"), nullptr, 192.0f, 352.0f);
        const bool hint_size = invoke_set_vector2d_value(hint_slot, STR("SetSize"), nullptr, 180.0f, 52.0f);
        auto* status_slot = invoke_add_child(panel, status);
        const bool status_pos = invoke_set_vector2d_value(status_slot, STR("SetPosition"), nullptr, 52.0f, 292.0f);
        const bool status_size = invoke_set_vector2d_value(status_slot, STR("SetSize"), nullptr, 320.0f, 56.0f);

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
            status_slot && status_pos && status_size &&
            input_frame_content && input_background_content && set_content;
        if (!layout_ok)
        {
            log_line("[phase7-umg] open_failed reason=layout_guard rootSet=" + std::string{root_set ? "true" : "false"} +
                     " slot=" + std::string{(frame_slot && slot_pos && slot_size && slot_align) ? "true" : "false"} +
                     " content=" + std::string{(frame_content && background_content && input_frame_content && input_background_content && set_content) ? "true" : "false"} +
                     " title=" + std::string{(title_slot && title_pos && title_size) ? "true" : "false"} +
                     " divider=" + std::string{(divider_slot && divider_pos && divider_size) ? "true" : "false"} +
                     " input=" + std::string{(input_slot && input_pos && input_size) ? "true" : "false"} +
                     " hint=" + std::string{(hint_slot && hint_pos && hint_size) ? "true" : "false"} +
                     " status=" + std::string{(status_slot && status_pos && status_size) ? "true" : "false"});
            return false;
        }

        m_phase7_umg_widget = widget;
        m_phase7_umg_text_box = text_box;
        m_phase7_umg_title = title;
        m_phase7_umg_hint = hint;
        m_phase7_umg_status = status;
        cache_phase7_umg_function_pointers();
        apply_phase7_umg_debug_scales("prewarm_init");

        // Prewarm should build the widget tree only; do not add/show during loading/bootstrap.
        m_phase7_umg_in_viewport = false;
        log_line(std::string{"[phase7-umg] prewarm_result built=true added=false collapsed=true focus=false"} +
                 " style=" + std::string{(title_text && hint_text && status_text && input_text && title_color && hint_color && status_color && input_color &&
                                          status_pivot &&
                                          frame_color && background_color && divider_color && input_frame_color && input_background_color &&
                                          frame_padding && background_padding && input_frame_padding && input_background_padding &&
                                          editor_width && editor_height && root_opacity && frame_opacity && background_opacity &&
                                          editor_opacity && input_opacity && frame_scale && title_scale && hint_scale && status_scale && input_scale) ? "true" : "false"});
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
        bool collapsed = false;
        if (built && m_phase7_umg_widget)
        {
            if (!m_phase7_umg_in_viewport)
            {
                m_phase7_umg_in_viewport =
                    invoke_with_int_param_cached(m_phase7_umg_widget, m_phase7_fn_add_to_viewport, 1000) ||
                    invoke_add_to_viewport(m_phase7_umg_widget, 1000);
            }
            collapsed = invoke_phase7_set_visibility(1, "prewarm_collapse");
        }

        install_phase7_keyboard_capture_hook();
        const bool hook_installed = m_phase7_keyboard_hook_installed.load(std::memory_order_relaxed);

        // Prewarm is complete only when build+attach+hook-install are done. This keeps
        // F8 open path fast and avoids one-time hook startup cost during interaction.
        const bool prewarm_ready = built && m_phase7_umg_in_viewport && hook_installed;
        m_phase7_umg_prewarm_succeeded = prewarm_ready;
        m_phase7_umg_prewarm_next_try = now + std::chrono::seconds(prewarm_ready ? 60 : 3);
        log_line("[phase7-umg] prewarm_try success=" + std::string{prewarm_ready ? "true" : "false"} +
                 " built=" + std::string{built ? "true" : "false"} +
                 " attached=" + std::string{m_phase7_umg_in_viewport ? "true" : "false"} +
                 " collapsed=" + std::string{collapsed ? "true" : "false"} +
                 " llHookInstalled=" + std::string{hook_installed ? "true" : "false"} +
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
        mark_phase7_status_dirty("open_editor");
        refresh_phase7_umg_status(true, "open_editor");
        const bool input_text = invoke_umg_set_text(m_phase7_umg_text_box, std::string{m_text_buffer.data()});
        if (m_f8_latency_breakdown_enabled && m_f8_latency_trace.active && !m_f8_latency_trace.construct_seen)
        {
            m_f8_latency_trace.construct = std::chrono::steady_clock::now();
            m_f8_latency_trace.construct_seen = true;
        }

        // Keep the widget collapsed while we acquire input mode and focus to avoid
        // the first click racing through to gameplay before UI capture is settled.
        const bool collapsed = invoke_phase7_set_visibility(1, "open_collapse");
        const bool input_mode_pre = set_phase7_game_and_ui_input_mode(true);
        bool added = m_phase7_umg_in_viewport;
        if (!m_phase7_umg_in_viewport)
        {
            added = invoke_with_int_param_cached(m_phase7_umg_widget, m_phase7_fn_add_to_viewport, 1000) ||
                    invoke_add_to_viewport(m_phase7_umg_widget, 1000);
            m_phase7_umg_in_viewport = added;
        }
        const bool input_mode_post = input_mode_pre ? true : set_phase7_game_and_ui_input_mode(true);
        const bool ll_hook_installed = m_phase7_keyboard_hook_installed.load(std::memory_order_relaxed);
        // Swallow the very first post-open mouse down. Keep this armed until consumed
        // so delayed user clicks are still protected from passthrough.
        m_phase7_mouse_first_down_consumed.store(false);
        m_phase7_mouse_capture_arm_until_ms.store(UINT64_MAX);
        bool focus_keyboard = false;
        bool focus_widget = false;
        bool visible = false;
        const bool ready_to_show = added && input_mode_post && ll_hook_installed;
        if (ready_to_show)
        {
            focus_keyboard = invoke_no_param_cached(m_phase7_umg_text_box, m_phase7_fn_set_keyboard_focus) ||
                             invoke_no_param(m_phase7_umg_text_box, STR("SetKeyboardFocus"), STR("/Script/UMG.Widget:SetKeyboardFocus"));
            focus_widget = invoke_no_param_cached(m_phase7_umg_text_box, m_phase7_fn_set_focus) ||
                           invoke_no_param(m_phase7_umg_text_box, STR("SetFocus"), STR("/Script/UMG.Widget:SetFocus"));
            visible = invoke_phase7_set_visibility(0, "open_visible");
        }

        log_line("[phase7-umg] open_result added=" + std::string{added ? "true" : "false"} +
                 " inputModePre=" + std::string{input_mode_pre ? "true" : "false"} +
                 " inputModePost=" + std::string{input_mode_post ? "true" : "false"} +
                 " llHookInstalled=" + std::string{ll_hook_installed ? "true" : "false"} +
                 " readyToShow=" + std::string{ready_to_show ? "true" : "false"} +
                 " visible=" + std::string{visible ? "true" : "false"} +
                 " collapsedFirst=" + std::string{collapsed ? "true" : "false"} +
                 " inputText=" + std::string{input_text ? "true" : "false"} +
                 " focusKeyboard=" + std::string{focus_keyboard ? "true" : "false"} +
                 " focusWidget=" + std::string{focus_widget ? "true" : "false"} +
                 " firstClickArm=until_consumed" +
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
            log_line("[phase7-umg] f8_open_total_ms=" + std::to_string(edge_to_open_ms));
            if (edge_to_open_ms > 1000)
            {
                log_line("[phase7-umg] open_sla_violation thresholdMs=1000 totalMs=" + std::to_string(edge_to_open_ms) +
                         " edge_to_target_ms=" + std::to_string(edge_to_target_ms) +
                         " target_to_construct_children_ok_ms=" + std::to_string(target_to_construct_ms) +
                         " construct_children_ok_to_open_result_ms=" + std::to_string(construct_to_open_ms) +
                         " readyToShow=" + std::string{ready_to_show ? "true" : "false"} +
                         " llHookInstalled=" + std::string{ll_hook_installed ? "true" : "false"} +
                         " inputModePost=" + std::string{input_mode_post ? "true" : "false"});
            }
            m_f8_latency_trace.active = false;
        }

        if (!added)
        {
            return false;
        }
        if (!ready_to_show)
        {
            m_phase7_umg_open_pending = true;
            m_phase7_open_sla_violation_logged = false;
            m_phase7_open_pending_since = std::chrono::steady_clock::now();
            log_line("[phase7-umg] open_deferred reason=await_ready_gate llHookInstalled=" +
                     std::string{ll_hook_installed ? "true" : "false"} +
                     " inputModePost=" + std::string{input_mode_post ? "true" : "false"} +
                     " added=" + std::string{added ? "true" : "false"});
        }
        else
        {
            m_phase7_umg_open_pending = false;
            m_phase7_open_sla_violation_logged = false;
            m_phase7_open_pending_since = {};
        }
        m_phase7_teardown_skip_logged = false;
        m_phase7_enter_was_down = false;
        m_phase7_escape_was_down = false;
        m_phase7_shift_was_down = false;
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

    auto SignTextMod::tick_phase7_umg_open_pending() -> void
    {
        if (!m_phase7_umg_open_pending)
        {
            return;
        }
        if (!m_phase7_umg_widget || !m_phase7_umg_text_box ||
            !is_uobject_reflection_safe(m_phase7_umg_widget) ||
            !is_uobject_reflection_safe(m_phase7_umg_text_box))
        {
            m_phase7_umg_open_pending = false;
            m_phase7_open_sla_violation_logged = false;
            m_phase7_open_pending_since = {};
            invalidate_phase7_umg_widget_cache("open_pending_invalid_widget");
            return;
        }

        const bool input_mode_ready = set_phase7_game_and_ui_input_mode(true);
        const bool ll_hook_installed = m_phase7_keyboard_hook_installed.load(std::memory_order_relaxed);
        const bool ready_to_show = input_mode_ready && ll_hook_installed;
        if (ready_to_show)
        {
            const bool focus_keyboard = invoke_no_param_cached(m_phase7_umg_text_box, m_phase7_fn_set_keyboard_focus) ||
                                        invoke_no_param(m_phase7_umg_text_box, STR("SetKeyboardFocus"), STR("/Script/UMG.Widget:SetKeyboardFocus"));
            const bool focus_widget = invoke_no_param_cached(m_phase7_umg_text_box, m_phase7_fn_set_focus) ||
                                      invoke_no_param(m_phase7_umg_text_box, STR("SetFocus"), STR("/Script/UMG.Widget:SetFocus"));
            const bool visible = invoke_phase7_set_visibility(0, "open_pending_visible");
            m_phase7_umg_open_pending = false;
            m_phase7_open_sla_violation_logged = false;
            const auto now = std::chrono::steady_clock::now();
            const long long total_ms =
                (m_phase7_open_pending_since.time_since_epoch().count() == 0)
                    ? 0
                    : std::chrono::duration_cast<std::chrono::milliseconds>(now - m_phase7_open_pending_since).count();
            m_phase7_open_pending_since = {};
            log_line("[phase7-umg] open_pending_resolved readyToShow=true visible=" +
                     std::string{visible ? "true" : "false"} +
                     " focusKeyboard=" + std::string{focus_keyboard ? "true" : "false"} +
                     " focusWidget=" + std::string{focus_widget ? "true" : "false"} +
                     " pendingMs=" + std::to_string(total_ms));
            return;
        }

        if (m_phase7_open_pending_since.time_since_epoch().count() != 0 && !m_phase7_open_sla_violation_logged)
        {
            const auto now = std::chrono::steady_clock::now();
            const long long pending_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_phase7_open_pending_since).count();
            if (pending_ms > 1000)
            {
                log_line("[phase7-umg] open_sla_violation thresholdMs=1000 pendingMs=" + std::to_string(pending_ms) +
                         " reason=await_ready_gate llHookInstalled=" + std::string{ll_hook_installed ? "true" : "false"} +
                         " inputModeReady=" + std::string{input_mode_ready ? "true" : "false"});
                m_phase7_open_sla_violation_logged = true;
            }
        }
    }

    auto SignTextMod::invoke_phase7_set_visibility(uint8_t value, const char* source_tag) -> bool
    {
        if (!m_phase7_umg_widget || !is_uobject_reflection_safe(m_phase7_umg_widget))
        {
            return false;
        }

        cache_phase7_umg_function_pointers();

        EnumInvokeDiag diag_cached{};
        const bool cached_ok = invoke_with_enum_byte_or_int_param_cached(m_phase7_umg_widget, m_phase7_fn_set_visibility, value, &diag_cached);
        if (cached_ok)
        {
            return true;
        }

        if (!m_phase7_set_visibility_param_unhandled_logged && diag_cached.has_unhandled_param)
        {
            log_line("[phase7-umg] set_visibility_param_unhandled fn=" + diag_cached.fn_name +
                     " propClass=" + diag_cached.prop_class +
                     " size=" + std::to_string(diag_cached.prop_size));
            m_phase7_set_visibility_param_unhandled_logged = true;
        }

        // Hardening: retry with a fresh function lookup in case cached pointer went stale.
        auto* fresh_fn = find_function_by_chain_or_path(
            m_phase7_umg_widget,
            STR("SetVisibility"),
            STR("/Script/UMG.Widget:SetVisibility"));
        if (fresh_fn)
        {
            m_phase7_fn_set_visibility = fresh_fn;
            EnumInvokeDiag diag_fresh{};
            const bool fresh_ok = invoke_with_enum_byte_or_int_param_cached(m_phase7_umg_widget, m_phase7_fn_set_visibility, value, &diag_fresh);
            if (fresh_ok)
            {
                return true;
            }
            if (!m_phase7_set_visibility_param_unhandled_logged && diag_fresh.has_unhandled_param)
            {
                log_line("[phase7-umg] set_visibility_param_unhandled fn=" + diag_fresh.fn_name +
                         " propClass=" + diag_fresh.prop_class +
                         " size=" + std::to_string(diag_fresh.prop_size));
                m_phase7_set_visibility_param_unhandled_logged = true;
            }
        }

        if (source_tag && *source_tag)
        {
            log_line("[phase7-umg] set_visibility_failed source=" + std::string{source_tag} +
                     " value=" + std::to_string(static_cast<int>(value)));
        }
        return false;
    }

    auto SignTextMod::close_phase7_umg_editor(bool restore_game_input) -> void
    {
        m_phase7_keyboard_capture_active.store(false);
        m_phase7_mouse_capture_arm_until_ms.store(0);
        m_phase7_mouse_first_down_consumed.store(false);
        m_phase7_force_full_mouse_consume.store(false);
        m_phase7_umg_open_pending = false;
        m_phase7_open_sla_violation_logged = false;
        m_phase7_open_pending_since = {};
        m_phase7_last_close_removed = false;
        if (m_phase7_umg_widget)
        {
            bool hidden = false;
            bool visible_after_attempt = true;
            for (int attempt = 1; attempt <= 3; ++attempt)
            {
                hidden = invoke_phase7_set_visibility(1, "close_hide_attempt");
                bool is_visible = true;
                const bool got_visible = invoke_bool_return_no_param(
                    m_phase7_umg_widget,
                    STR("IsVisible"),
                    STR("/Script/UMG.Widget:IsVisible"),
                    is_visible);
                visible_after_attempt = got_visible ? is_visible : true;
                log_line("[phase7-umg] close_hide_attempt attempt=" + std::to_string(attempt) +
                         " hiddenCall=" + std::string{hidden ? "true" : "false"} +
                         " gotIsVisible=" + std::string{got_visible ? "true" : "false"} +
                         " isVisible=" + std::string{visible_after_attempt ? "true" : "false"});
                if (hidden && got_visible && !visible_after_attempt)
                {
                    break;
                }
                if (attempt < 3)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            // Fast path: keep attached and hidden. If still visible after retries,
            // force RemoveFromParent as safety net.
            bool removed = false;
            if (visible_after_attempt)
            {
                removed =
                    invoke_no_param_cached(m_phase7_umg_widget, m_phase7_fn_remove_from_parent) ||
                    invoke_no_param(
                        m_phase7_umg_widget,
                        STR("RemoveFromParent"),
                        STR("/Script/UMG.Widget:RemoveFromParent"));
            }
            m_phase7_last_close_removed = removed;
            m_phase7_umg_in_viewport = !removed;
            log_line("[phase7-umg] close hidden=" + std::string{hidden ? "true" : "false"} +
                     " visibleAfterRetries=" + std::string{visible_after_attempt ? "true" : "false"} +
                     " removedWidget=" + std::string{removed ? "true" : "false"});
        }
        if (restore_game_input)
        {
            const bool restored = set_phase7_game_and_ui_input_mode(false);
            log_line("[phase7-umg] close restoreInput=" + std::string{restored ? "true" : "false"});
        }
        m_phase7_enter_was_down = false;
        m_phase7_escape_was_down = false;
        m_phase7_shift_was_down = false;
        m_phase7_enter_requested.store(false);
        m_phase7_escape_requested.store(false);
        m_phase7_umg_last_text.clear();
        m_phase7_active_epoch = 0;
        m_phase7_watchdog_logged = false;
        m_phase7_status_dirty = true;
        m_phase7_last_status_ui_refresh = {};
    }

    auto SignTextMod::reset_phase7_runtime_state() -> void
    {
        if (m_phase7_umg_widget && is_uobject_reflection_safe(m_phase7_umg_widget))
        {
            (void)invoke_phase7_set_visibility(1, "reset_hide");
            (void)(invoke_no_param_cached(m_phase7_umg_widget, m_phase7_fn_remove_from_parent) ||
                   invoke_no_param(m_phase7_umg_widget, STR("RemoveFromParent"), STR("/Script/UMG.Widget:RemoveFromParent")));
        }
        m_phase7_keyboard_capture_active.store(false);
        m_phase7_mouse_capture_arm_until_ms.store(0);
        m_phase7_mouse_first_down_consumed.store(false);
        m_phase7_force_full_mouse_consume.store(false);
        m_phase7_enter_requested.store(false);
        m_phase7_escape_requested.store(false);
        m_phase7_enter_was_down = false;
        m_phase7_escape_was_down = false;
        m_phase7_shift_was_down = false;
        m_phase7_ui_input_mode_active = false;
        m_phase7_umg_in_viewport = false;
        m_phase7_umg_open_pending = false;
        m_phase7_open_sla_violation_logged = false;
        m_phase7_open_pending_since = {};
        m_phase7_umg_last_text.clear();
        m_hotkey_requested.store(false);
        m_hotkey_retry_remaining = 0;
        m_hotkey_action_in_flight = false;
        m_hotkey_require_release_before_next_press = false;
        m_phase7_native_editor_open = false;
        m_phase7_native_widget = nullptr;
        m_phase7_umg_widget = nullptr;
        m_phase7_umg_text_box = nullptr;
        m_phase7_umg_title = nullptr;
        m_phase7_umg_hint = nullptr;
        m_phase7_umg_status = nullptr;
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
        m_phase7_status_dirty = true;
        m_phase7_last_status_ui_refresh = {};
        m_phase7_last_status_role_text.clear();
        m_phase7_last_status_network_text.clear();
        m_phase7_last_status_log = {};
    }

    auto SignTextMod::arm_phase7_definitive_teardown(const std::string& reason) -> void
    {
        const bool had_phase7_state =
            m_phase7_native_widget ||
            m_phase7_umg_widget ||
            m_phase7_native_editor_open ||
            (m_phase7_active_epoch != 0) ||
            m_phase7_umg_open_pending ||
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
            (m_phase7_active_epoch != 0) ||
            m_phase7_umg_open_pending ||
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
            m_phase7_shift_was_down = false;
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

    auto SignTextMod::mark_phase7_status_dirty(const char* reason) -> void
    {
        m_phase7_status_dirty = true;
        if (m_phase7_active_epoch != 0 &&
            m_phase7_umg_widget &&
            m_phase7_umg_status &&
            is_uobject_reflection_safe(m_phase7_umg_widget) &&
            is_uobject_reflection_safe(m_phase7_umg_status))
        {
            refresh_phase7_umg_status(true, reason);
        }
    }

    auto SignTextMod::is_current_world_snapshot_ready() const -> bool
    {
        return m_bridge_snapshot_received &&
            (!is_hex_world_id(m_world_folder_id) || m_bridge_snapshot_world_id == m_world_folder_id);
    }

    auto SignTextMod::build_phase7_role_status_text() const -> std::string
    {
        if (!m_role_lock_acquired)
        {
            return "Error: Not locked";
        }

        const auto locked_role_lower = lower_ascii(
            m_role_lock_runtime_role.empty() ? m_runtime_role : m_role_lock_runtime_role);
        if (locked_role_lower == "remoteclient")
        {
            return "Remote Client";
        }
        if (locked_role_lower == "localclient")
        {
            // Role status is derived from lock-captured state only.
            const auto start_signal_lower = lower_ascii(m_role_lock_start_signal);
            const bool hosted_context =
                start_signal_lower == "client_readytoplay_to_startcoophostserver";
            return hosted_context ? "Host" : "Solo";
        }
        return "Error: Not locked";
    }

    auto SignTextMod::build_phase7_network_status_text() const -> std::string
    {
        const auto role = build_phase7_role_status_text();
        if (role == "Solo")
        {
            return (m_last_sidecar_load_ok && m_last_sidecar_save_ok)
                ? "Solo"
                : "Error - Sign Text file not saved";
        }
        if (role == "Host")
        {
            const bool connected =
                m_hosted_authority_route_active &&
                m_bridge_route_lock_acquired &&
                !m_bridge_health_unhealthy;
            return connected
                ? "Connected to Host Server"
                : "Error - Not connected to host server";
        }
        if (role == "Remote Client")
        {
            const bool connected =
                m_bridge_route_lock_acquired &&
                !m_bridge_health_unhealthy &&
                is_current_world_snapshot_ready();
            return connected
                ? "Connected to Server"
                : "Error - Not connected to Server";
        }
        return "Error - Not connected to Server";
    }

    auto SignTextMod::refresh_phase7_umg_status(bool force, const char* reason) -> void
    {
        (void)reason;
        if (!m_phase7_umg_widget || !m_phase7_umg_status ||
            !is_uobject_reflection_safe(m_phase7_umg_widget) ||
            !is_uobject_reflection_safe(m_phase7_umg_status))
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!force &&
            !m_phase7_status_dirty &&
            m_phase7_last_status_ui_refresh.time_since_epoch().count() != 0 &&
            (now - m_phase7_last_status_ui_refresh) < std::chrono::seconds(2))
        {
            return;
        }

        const auto role_text = build_phase7_role_status_text();
        const auto network_text = build_phase7_network_status_text();
        const std::string status_text =
            "Status\nRole: " + role_text + "\nNetwork: " + network_text;
        const bool set_ok = invoke_umg_set_text(m_phase7_umg_status, status_text);
        m_phase7_last_status_ui_refresh = now;
        m_phase7_status_dirty = false;
        if (!set_ok)
        {
            return;
        }

        if (role_text != m_phase7_last_status_role_text ||
            network_text != m_phase7_last_status_network_text)
        {
            const auto locked_role_lower = lower_ascii(
                m_role_lock_runtime_role.empty() ? m_runtime_role : m_role_lock_runtime_role);
            std::string legacy_role = "Error: Not locked";
            if (m_role_lock_acquired)
            {
                if (locked_role_lower == "remoteclient")
                {
                    legacy_role = "Remote Client";
                }
                else if (locked_role_lower == "localclient")
                {
                    const auto current_start_signal_lower = lower_ascii(m_definitive_session_start_signal);
                    const bool hosted_context_legacy =
                        is_local_hosted_runtime() ||
                        current_start_signal_lower.find("startcoophostserver") != std::string::npos;
                    legacy_role = hosted_context_legacy ? "Host" : "Solo";
                }
            }

            log_line("[phase7-status-role] locked=" + std::string{m_role_lock_acquired ? "true" : "false"} +
                     " runtimeRole=" + (m_role_lock_runtime_role.empty() ? m_runtime_role : m_role_lock_runtime_role) +
                     " displayRole=" + role_text +
                     " epoch=" + std::to_string(m_session_epoch) +
                     " worldId=" + (m_world_folder_id.empty() ? "unknown" : m_world_folder_id));
            if (legacy_role != role_text)
            {
                log_line("[phase7-status-role] legacy_disagree legacy=" + legacy_role +
                         " authoritative=" + role_text +
                         " epoch=" + std::to_string(m_session_epoch));
            }

            m_phase7_last_status_role_text = role_text;
            m_phase7_last_status_network_text = network_text;
            log_line("[phase7-status] role=" + role_text +
                     " network=" + network_text +
                     " epoch=" + std::to_string(m_session_epoch) +
                     " worldId=" + (m_world_folder_id.empty() ? "unknown" : m_world_folder_id) +
                     " routeLocked=" + std::string{m_bridge_route_lock_acquired ? "true" : "false"} +
                     " health=" + std::string{m_bridge_health_unhealthy ? "degraded" : "ok"} +
                     " snapshot=" + std::string{is_current_world_snapshot_ready() ? "true" : "false"});
        }
    }

    auto SignTextMod::tick_phase7_umg_editor() -> void
    {
        if (!m_phase7_umg_widget)
        {
            return;
        }
        if (!m_phase7_umg_text_box ||
            !is_uobject_reflection_safe(m_phase7_umg_widget) ||
            !is_uobject_reflection_safe(m_phase7_umg_text_box))
        {
            log_line("[phase7-umg] action_ignored reason=invalid_widget_refs");
            close_phase7_umg_editor(true);
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

        refresh_phase7_umg_status(false, "tick");

        const short enter_state = GetAsyncKeyState(k_vk_return);
        const short escape_state = GetAsyncKeyState(k_vk_escape);
        const bool enter_down = ((enter_state & 0x8000) != 0);
        const bool escape_down = ((escape_state & 0x8000) != 0);
        const bool enter_pressed_since_poll = ((enter_state & 0x0001) != 0);
        const bool escape_pressed_since_poll = ((escape_state & 0x0001) != 0);
        const short shift_state = GetAsyncKeyState(k_vk_shift);
        const bool shift_down = ((shift_state & 0x8000) != 0);
        const bool shift_pressed_since_poll = ((shift_state & 0x0001) != 0);
        const bool shift_was_down = m_phase7_shift_was_down;
        const bool enter_requested = m_phase7_enter_requested.exchange(false);
        const bool escape_requested = m_phase7_escape_requested.exchange(false);
        const bool enter_edge = (enter_down && !m_phase7_enter_was_down) || enter_pressed_since_poll;
        const bool escape_edge = (escape_down && !m_phase7_escape_was_down) || escape_pressed_since_poll;
        m_phase7_enter_was_down = enter_down;
        m_phase7_escape_was_down = escape_down;
        m_phase7_shift_was_down = shift_down;

        std::string live_text{};
        const bool live_read = read_text_property_value_no_process_event(m_phase7_umg_text_box, live_text) ||
            invoke_get_text_value(m_phase7_umg_text_box, live_text);
        const bool newline_added =
            live_read &&
            count_line_breaks(live_text) > count_line_breaks(m_phase7_umg_last_text);
        const bool explicit_enter_intent = enter_requested || enter_edge || enter_pressed_since_poll;
        const bool implicit_enter_via_newline =
            newline_added &&
            !explicit_enter_intent &&
            !shift_down &&
            !shift_pressed_since_poll &&
            !shift_was_down;
        const bool apply_pressed = (explicit_enter_intent && !shift_down) || implicit_enter_via_newline;
        const bool cancel_pressed = escape_requested || escape_edge;
        if (enter_requested || escape_requested || enter_edge || escape_edge || enter_pressed_since_poll || escape_pressed_since_poll)
        {
            m_phase7_last_interaction_at = now;
        }

        if (!apply_pressed && !cancel_pressed)
        {
            if (newline_added && !explicit_enter_intent)
            {
                log_line("[phase7-umg] apply_candidate_via_newline accepted=" +
                         std::string{implicit_enter_via_newline ? "true" : "false"} +
                         " enterRequested=" +
                         std::string{enter_requested ? "true" : "false"} +
                         " enterEdge=" + std::string{enter_edge ? "true" : "false"} +
                         " enterAsync=" + std::string{enter_pressed_since_poll ? "true" : "false"} +
                         " shiftDown=" + std::string{shift_down ? "true" : "false"} +
                         " shiftWasDown=" + std::string{shift_was_down ? "true" : "false"} +
                         " shiftAsync=" + std::string{shift_pressed_since_poll ? "true" : "false"});
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
                read = read_text_property_value_no_process_event(m_phase7_umg_text_box, text) ||
                    invoke_get_text_value(m_phase7_umg_text_box, text);
            }
            if (implicit_enter_via_newline)
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
                     " implicitNewlineApply=" + std::string{implicit_enter_via_newline ? "true" : "false"} +
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
            if (!is_uobject_reflection_safe(object))
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

            UObject* controlled_pawn = get_controller_pawn_property_only(object);
            if (controlled_pawn)
            {
                score += 120;
            }
            else
            {
                // Property-only lookup avoids reflective GetPawn invocation during churn.
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
        if (!context || !is_uobject_reflection_safe(context))
        {
            return false;
        }
        auto* fn = context->GetFunctionByNameInChain(STR("GetText"));
        if (!fn || !is_uobject_reflection_safe(context))
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
        if (!context || !is_uobject_reflection_safe(context))
        {
            return false;
        }
        auto* fn = find_function_by_chain_or_path(
            context,
            STR("GetText"),
            STR("/Script/Engine.TextRenderComponent:GetText"));
        if (!fn || !is_uobject_reflection_safe(context))
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
        if (!context || !is_uobject_reflection_safe(context))
        {
            return false;
        }
        auto* fn = find_function_by_chain_or_path(context, in_chain_name, path_name);
        if (!fn || !is_uobject_reflection_safe(context))
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
        if (!player_controller || !is_uobject_reflection_safe(player_controller))
        {
            return false;
        }
        auto* fn = find_function_by_chain_or_path(
            player_controller,
            STR("ProjectWorldLocationToScreen"),
            STR("/Script/Engine.PlayerController:ProjectWorldLocationToScreen"));
        if (!fn || !is_uobject_reflection_safe(player_controller))
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
            is_worldid_bound_for_current_epoch() &&
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
        const bool role_or_authority_change =
            m_runtime_role != runtime_role ||
            m_data_mode != data_mode ||
            m_authority_mode != authority_mode ||
            m_sidecar_kind != sidecar_kind ||
            m_sidecar_authoritative != authoritative;
        if (world_changed)
        {
            // Keep role/bridge/route locks sticky within the epoch.
            // A world-id path migration (for example pending-world -> real world id)
            // must not unlock and force route re-selection.
            log_line("[role] world_change_preserve_lock reason=" + reason +
                     " runtimeRole=" + m_runtime_role +
                     " authorityMode=" + m_authority_mode +
                     " oldWorldId=" + m_world_folder_id +
                     " newWorldId=" + world_folder_id +
                     " roleOrAuthorityChange=" + std::string{role_or_authority_change ? "true" : "false"});
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
        if (m_role_lock_acquired &&
            !m_world_folder_id.empty() &&
            m_role_lock_world_id != m_world_folder_id)
        {
            // Keep lock metadata aligned with definitive world-id path migration
            // without releasing/reacquiring role or route locks.
            m_role_lock_world_id = m_world_folder_id;
            log_line("[role] lock_worldid_updated reason=sidecar_route_world_change worldId=" + m_role_lock_world_id);
        }

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
            m_phase4_last_apply_success_at.clear();
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
            // Route path migration inside the same epoch (for example pending-world -> real world id)
            // must not clear route probe/gate/lock state. Reset only snapshot/cache payload state.
            if (m_role_lock_acquired && reason.rfind("session_reset_", 0) != 0)
            {
                reset_bridge_snapshot_payload_state("sidecar_route_change_locked_session");
            }
            else
            {
                reset_bridge_snapshot_payload_state("sidecar_route_change");
            }
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
        std::string world_bound_reason{};
        if (!is_world_bound_operation_allowed("configure_sidecar_for_actor", &world_bound_reason))
        {
            return;
        }
        note_world_bound_operation_resumed("configure_sidecar_for_actor");

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
        if (is_worldid_bound_for_current_epoch())
        {
            return m_worldid_latched_id;
        }
        if (!m_world_folder_id.empty() &&
            m_world_folder_id != "unknown-world" &&
            m_world_folder_id != "pending-world")
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
            const bool umg_open = (m_phase7_active_epoch != 0) || m_phase7_ui_input_mode_active || m_phase7_umg_open_pending;
            const bool native_open = m_phase7_native_editor_open;
            const bool imgui_open = m_ui_open;
            if (umg_open || native_open || imgui_open)
            {
                if (umg_open)
                {
                    close_phase7_umg_editor(true);
                }
                if (native_open)
                {
                    close_phase7_native_editor(true);
                }
                m_ui_open = false;
                m_hotkey_retry_remaining = 0;
                m_hotkey_action_in_flight = false;
                if (m_f8_latency_breakdown_enabled && m_f8_latency_trace.active)
                {
                    m_f8_latency_trace.active = false;
                }
                log_line("[input] hotkey toggle_close editorClosed=true umgOpen=" + std::string{umg_open ? "true" : "false"} +
                         " nativeOpen=" + std::string{native_open ? "true" : "false"} +
                         " imguiOpen=" + std::string{imgui_open ? "true" : "false"});
                return;
            }

            // One keypress should survive transient viewpoint/controller hiccups.
            // 16 attempts at 60ms gives a ~960ms retry window.
            // This is longer than the original ~480ms budget (8 attempts) so F8 is
            // more resilient during brief controller/camera readiness delays, while
            // still keeping selection response close to a 1-second ceiling.
            m_hotkey_retry_remaining = 16;
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
                m_ui_open = false;
                m_hotkey_action_in_flight = false;
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
            m_hotkey_action_in_flight = false;
            return;
        }

        const bool native_opened = open_phase7_native_editor_for_selection();
        if (native_opened)
        {
            m_ui_open = false;
            m_hotkey_action_in_flight = false;
            return;
        }

        m_ui_open = m_phase7_imgui_fallback_enabled;
        m_hotkey_action_in_flight = false;
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
        m_last_sidecar_load_ok = false;
        mark_phase7_status_dirty("sidecar_load_start");
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
            bool read_ok{false};
            bool schema_valid{false};
            bool has_labels_node{false};
            bool is_primary{false};
            bool is_bak{false};
            bool is_tmp{false};
            bool is_snapshot{false};
            int source_rank{0};
            int64_t mtime_ticks{0};
        };

        std::vector<ParsedCandidate> parsed_candidates{};
        std::string content{};
        for (const auto& path : candidates)
        {
            if (!std::filesystem::exists(path))
            {
                continue;
            }
            ParsedCandidate parsed{};
            parsed.path = path;
            parsed.is_primary = (path == m_sidecar_path);
            parsed.is_bak = (path == std::filesystem::path(backup_path));
            parsed.is_tmp = (path == std::filesystem::path(temp_path));
            parsed.is_snapshot = !parsed.is_primary && !parsed.is_bak && !parsed.is_tmp;
            parsed.source_rank = parsed.is_bak ? 3 : (parsed.is_snapshot ? 2 : (parsed.is_tmp ? 1 : 0));
            std::error_code mtime_ec{};
            if (const auto mtime = std::filesystem::last_write_time(path, mtime_ec); !mtime_ec)
            {
                parsed.mtime_ticks = static_cast<int64_t>(mtime.time_since_epoch().count());
            }

            if (!read_text_file(path, content))
            {
                log_line("[save] load_candidate_rejected unreadable path=" + path.string());
                parsed_candidates.push_back(std::move(parsed));
                continue;
            }
            parsed.read_ok = true;
            parsed.has_labels_node = (content.find("\"labels\"") != std::string::npos);
            if (!parsed.has_labels_node)
            {
                log_line("[save] load_candidate_rejected invalid_json_shape path=" + path.string());
                parsed_candidates.push_back(std::move(parsed));
                continue;
            }
            parsed.schema_valid = true;
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
            mark_phase7_status_dirty("sidecar_load_missing");
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
        const bool primary_valid = primary_candidate && primary_candidate->schema_valid;
        const bool primary_exists = std::filesystem::exists(m_sidecar_path);
        if (primary_valid)
        {
            chosen = primary_candidate;
            if (primary_candidate->labels.empty())
            {
                log_line("[save] load_primary_valid_empty_preserved path=" + m_sidecar_path.string() +
                         " reason=primary_valid_empty");
            }
        }
        else
        {
            ParsedCandidate* best_recovery = nullptr;
            for (auto& candidate : parsed_candidates)
            {
                if (candidate.is_primary || !candidate.schema_valid || candidate.labels.empty())
                {
                    continue;
                }
                if (!best_recovery)
                {
                    best_recovery = &candidate;
                    continue;
                }
                if (candidate.revision != best_recovery->revision)
                {
                    if (candidate.revision > best_recovery->revision)
                    {
                        best_recovery = &candidate;
                    }
                    continue;
                }
                if (candidate.parsed_rows != best_recovery->parsed_rows)
                {
                    if (candidate.parsed_rows > best_recovery->parsed_rows)
                    {
                        best_recovery = &candidate;
                    }
                    continue;
                }
                if (candidate.mtime_ticks != best_recovery->mtime_ticks)
                {
                    if (candidate.mtime_ticks > best_recovery->mtime_ticks)
                    {
                        best_recovery = &candidate;
                    }
                    continue;
                }
                if (candidate.source_rank > best_recovery->source_rank)
                {
                    best_recovery = &candidate;
                }
            }
            if (best_recovery)
            {
                chosen = best_recovery;
                restored_from_backup = true;
                log_line("[save] auto_restore_triggered_primary_invalid reason=" +
                         std::string{primary_exists ? "primary_invalid" : "primary_missing"} +
                         " source=" + chosen->path.string() +
                         " revision=" + std::to_string(chosen->revision) +
                         " parsedRows=" + std::to_string(chosen->parsed_rows));
            }
            else if (primary_candidate && primary_candidate->read_ok)
            {
                chosen = primary_candidate;
                log_line("[save] auto_restore_skipped_primary_invalid reason=no_nonempty_recovery path=" + m_sidecar_path.string());
            }
        }

        if (!chosen)
        {
            log_line("[save] load_failed no_candidate_chosen path=" + m_sidecar_path.string());
            m_labels.clear();
            mark_phase7_status_dirty("sidecar_load_no_candidate");
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
        m_last_sidecar_load_ok = true;
        mark_phase7_status_dirty("sidecar_load_done");
        if (m_session_ready_latched &&
            m_role_lock_acquired &&
            is_worldid_bound_for_current_epoch())
        {
            queue_first_authoritative_render_pass("load_done", m_world_folder_id);
        }

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
        const bool world_guard_bypass =
            reason == "auto_restore_from_backup" ||
            reason == "write_recovery_candidate";
        if (!world_guard_bypass)
        {
            std::string world_bound_reason{};
            if (!is_world_bound_operation_allowed("save_sidecar_json:" + reason, &world_bound_reason))
            {
                return;
            }
            note_world_bound_operation_resumed("save_sidecar_json");
        }

        if (m_sidecar_authoritative && !is_authoritative_write_allowed())
        {
            log_line("[guardrail] write_refused reason=" + reason +
                     " key=" + key +
                     " cause=authoritative_role_or_path_not_confirmed runtimeRole=" + m_runtime_role +
                     " authorityMode=" + m_authority_mode +
                     " path=" + m_sidecar_path.string());
            m_last_sidecar_save_ok = false;
            mark_phase7_status_dirty("sidecar_save_write_refused_authoritative");
            return;
        }
        if (!m_sidecar_authoritative && !is_cache_path_allowed())
        {
            log_line("[guardrail] write_refused reason=" + reason +
                     " key=" + key +
                     " cause=cache_path_not_confirmed runtimeRole=" + m_runtime_role +
                     " sidecarKind=" + m_sidecar_kind +
                     " path=" + m_sidecar_path.string());
            m_last_sidecar_save_ok = false;
            mark_phase7_status_dirty("sidecar_save_write_refused_cache_path");
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
            m_last_sidecar_save_ok = false;
            mark_phase7_status_dirty("sidecar_save_write_refused_empty_guard");
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
            m_last_sidecar_save_ok = false;
            mark_phase7_status_dirty("sidecar_save_mkdir_failed");
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
            m_last_sidecar_save_ok = false;
            mark_phase7_status_dirty("sidecar_save_open_failed");
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
            m_last_sidecar_save_ok = false;
            mark_phase7_status_dirty("sidecar_save_atomic_replace_failed");
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
        m_last_sidecar_save_ok = true;
        mark_phase7_status_dirty("sidecar_save_done");
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
        if (!has_viable_remote_route_for_snapshot())
        {
            log_line("[bridge-route] send_blocked type=snapshot_request reason=no_valid_committed_route" +
                     std::string(" role=") + bridge_role_name(m_bridge_role) +
                     " routeHost=" + (m_bridge_remote_server_host.empty() ? "none" : m_bridge_remote_server_host) +
                     " routeLocked=" + std::string{m_bridge_route_lock_acquired ? "true" : "false"});
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
                << ",\"requesterRevision\":" << m_revision
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

    auto SignTextMod::is_hosted_client_authority_context() const -> bool
    {
        return m_bridge_role == BridgeRole::RemoteClient &&
            m_sidecar_authoritative &&
            lower_ascii(m_runtime_role) == "localclient" &&
            is_local_hosted_runtime();
    }

    auto SignTextMod::is_hosted_server_relay_context() const -> bool
    {
        return m_bridge_role == BridgeRole::DedicatedServer &&
            !m_sidecar_authoritative &&
            is_dedicated_runtime_process();
    }

    auto SignTextMod::resolve_hosted_server_advertise_host() const -> std::string
    {
        std::string host = "127.0.0.1";
        const auto endpoint =
            try_latest_remoteaddr_host_from_log_window(
                m_server_role_log_path.empty() ? m_session_window_log_path : m_server_role_log_path,
                m_session_window_open ? m_session_window_start_offset : static_cast<uintmax_t>(0));
        if (endpoint.has_value() && !endpoint->empty())
        {
            host = *endpoint;
        }
        return host;
    }

    auto SignTextMod::resolve_hosted_server_endpoint_path() const -> std::filesystem::path
    {
        const auto local_app_data = get_env_var("LOCALAPPDATA");
        if (local_app_data.empty())
        {
            return {};
        }
        return std::filesystem::path{local_app_data} / "R5" / "Saved" / "WindroseTextSigns" / "HostedServerBridgeEndpoint.json";
    }

    auto SignTextMod::publish_hosted_server_endpoint(const std::string& reason) -> bool
    {
        if (!is_hosted_server_relay_context())
        {
            return false;
        }

        const auto path = resolve_hosted_server_endpoint_path();
        if (path.empty())
        {
            return false;
        }
        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        const auto port = NativeBridge::instance().server_bound_port();
        const auto host = resolve_hosted_server_advertise_host();
        const auto pid = static_cast<unsigned long long>(GetCurrentProcessId());
        std::ostringstream payload{};
        payload << "{\n"
                << "  \"epoch\": " << m_session_epoch << ",\n"
                << "  \"pid\": " << pid << ",\n"
                << "  \"worldId\": \"" << escape_json(m_world_folder_id) << "\",\n"
                << "  \"host\": \"" << escape_json(host) << "\",\n"
                << "  \"port\": " << static_cast<unsigned int>(port == 0 ? m_bridge_udp_port : port) << "\n"
                << "}\n";
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            return false;
        }
        out << payload.str();
        out.close();
        const auto effective_port = static_cast<uint16_t>(port == 0 ? m_bridge_udp_port : port);
        const bool changed =
            !m_hosted_server_endpoint_advertised ||
            m_hosted_server_endpoint_host != host ||
            m_hosted_server_endpoint_port != effective_port;
        m_hosted_server_endpoint_advertised = true;
        m_hosted_server_endpoint_host = host;
        m_hosted_server_endpoint_port = effective_port;
        if (changed)
        {
            log_line("[bridge-hosted] hosted_server_endpoint_published epoch=" + std::to_string(m_session_epoch) +
                     " host=" + host +
                     " port=" + std::to_string(static_cast<unsigned int>(effective_port)) +
                     " pid=" + std::to_string(pid) +
                     " path=" + path.string() +
                     " reason=" + reason);
        }
        return true;
    }

    auto SignTextMod::apply_bridge_remote_endpoint(
        const std::string& host,
        const uint16_t port,
        const std::string& reason) -> bool
    {
        const auto trimmed_host = trim_copy_ascii(host);
        if (trimmed_host.empty() || port == 0)
        {
            return false;
        }
        if (m_bridge_role == BridgeRole::RemoteClient && !m_bridge_route_gate_open)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto reason_key = "gate_not_open|" + trimmed_host + ":" + std::to_string(static_cast<unsigned int>(port));
            if (m_bridge_route_wait_last_reason != reason_key ||
                m_bridge_route_wait_last_log.time_since_epoch().count() == 0 ||
                (now - m_bridge_route_wait_last_log) >= std::chrono::seconds(5))
            {
                log_line("[bridge-route] route_lock_ignored reason=gate_not_open incoming=" + trimmed_host + ":" +
                         std::to_string(static_cast<unsigned int>(port)));
                m_bridge_route_wait_last_reason = reason_key;
                m_bridge_route_wait_last_log = now;
            }
            return false;
        }
        if (m_bridge_route_lock_acquired &&
            !m_bridge_route_locked_host.empty() &&
            m_bridge_route_locked_host != trimmed_host)
        {
            log_line("[bridge-route] route_lock_ignored reason=lock_sticky incoming=" + trimmed_host + ":" +
                     std::to_string(static_cast<unsigned int>(port)) +
                     " locked=" + m_bridge_route_locked_host + ":" + std::to_string(m_bridge_udp_port));
            return false;
        }
        if (m_bridge_remote_server_host == trimmed_host &&
            m_bridge_udp_port == static_cast<int>(port) &&
            m_bridge_route_lock_acquired &&
            m_bridge_route_locked_host == trimmed_host)
        {
            return false;
        }

        const bool staged_changed =
            !m_bridge_route_staged_active ||
            m_bridge_route_staged_host != trimmed_host ||
            m_bridge_udp_port != static_cast<int>(port) ||
            m_bridge_route_staged_source != reason;
        m_bridge_remote_server_host = trimmed_host;
        m_bridge_udp_port = static_cast<int>(port);
        NativeBridge::instance().set_remote_server(m_bridge_remote_server_host, port);
        m_bridge_route_staged_active = true;
        m_bridge_route_staged_host = m_bridge_remote_server_host;
        m_bridge_route_staged_source = reason;
        if (staged_changed)
        {
            log_line("[bridge-route] route_endpoint_staged host=" + m_bridge_remote_server_host +
                     " port=" + std::to_string(static_cast<unsigned int>(port)) +
                     " reason=" + reason);
        }
        return staged_changed;
    }

    auto SignTextMod::consume_hosted_server_endpoint(const std::string& reason) -> bool
    {
        if (!is_hosted_client_authority_context())
        {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (m_hosted_server_endpoint_last_read.time_since_epoch().count() != 0 &&
            (now - m_hosted_server_endpoint_last_read) < std::chrono::milliseconds(500))
        {
            return false;
        }
        m_hosted_server_endpoint_last_read = now;

        const auto path = resolve_hosted_server_endpoint_path();
        if (path.empty())
        {
            return false;
        }
        std::string content{};
        if (!read_text_file(path, content))
        {
            return false;
        }

        auto host_value = bridge_message_field(content, "host");
        auto port_value = bridge_message_number(content, "port");
        if (!host_value.has_value() || !port_value.has_value())
        {
            return false;
        }
        const auto parsed_port = static_cast<uint16_t>(std::clamp(safe_stoi(*port_value, m_bridge_udp_port), 1, 65535));
        const std::string parsed_host = unescape_json(*host_value);
        const bool changed = apply_bridge_remote_endpoint(
            parsed_host,
            parsed_port,
            "hosted_server_endpoint_file:" + reason);
        if (changed)
        {
            m_hosted_server_endpoint_advertised = true;
            m_hosted_server_endpoint_host = parsed_host;
            m_hosted_server_endpoint_port = parsed_port;
            log_line("[bridge-hosted] hosted_server_endpoint_consumed source=file epoch=" + std::to_string(m_session_epoch) +
                     " host=" + parsed_host +
                     " port=" + std::to_string(static_cast<unsigned int>(parsed_port)) +
                     " path=" + path.string());
        }
        return changed;
    }

    auto SignTextMod::ensure_hosted_client_authority_route(const std::string& reason) -> bool
    {
        if (!is_hosted_client_authority_context())
        {
            return false;
        }
        bool route_changed = consume_hosted_server_endpoint(reason);
        if (!m_hosted_authority_route_active || !m_bridge_route_lock_acquired || m_bridge_route_locked_host.empty())
        {
            route_changed = apply_bridge_remote_endpoint("127.0.0.1", static_cast<uint16_t>(m_bridge_udp_port), "hosted_server_fallback_loopback") || route_changed;
        }
        if (route_changed || !m_hosted_authority_route_active)
        {
            m_hosted_authority_route_active = true;
            log_line("[bridge-hosted] hosted_authority_route_activated epoch=" + std::to_string(m_session_epoch) +
                     " worldId=" + m_world_folder_id +
                     " host=" + m_bridge_remote_server_host +
                     " port=" + std::to_string(m_bridge_udp_port) +
                     " reason=" + reason);
        }
        return true;
    }

    auto SignTextMod::send_hosted_authority_payload_to_server(
        const std::string& payload,
        const std::string& type,
        const std::string& stable_id,
        const std::string& world_id,
        const std::string& reason) -> bool
    {
        std::string world_bound_reason{};
        if (!is_world_bound_operation_allowed("send_hosted_authority_payload_to_server:" + type, &world_bound_reason))
        {
            return false;
        }
        note_world_bound_operation_resumed("send_hosted_authority_payload_to_server");

        if (!ensure_hosted_client_authority_route(reason))
        {
            return false;
        }
        if (!has_viable_remote_route_for_snapshot())
        {
            log_line("[bridge-route] send_blocked type=" + type +
                     " reason=no_valid_committed_route role=" + bridge_role_name(m_bridge_role) +
                     " routeHost=" + (m_bridge_remote_server_host.empty() ? "none" : m_bridge_remote_server_host) +
                     " routeLocked=" + std::string{m_bridge_route_lock_acquired ? "true" : "false"} +
                     " context=hosted_authority_to_server");
            return false;
        }
        const bool sent = NativeBridge::instance().send_to_server(payload);
        log_line("[bridge-hosted] hosted_authority_emit_delta type=" + type +
                 " stableId=" + (stable_id.empty() ? "none" : stable_id) +
                 " worldId=" + (world_id.empty() ? "none" : world_id) +
                 " sent=" + std::string{sent ? "true" : "false"} +
                 " epoch=" + std::to_string(m_session_epoch) +
                 " reason=" + reason);
        return sent;
    }

    auto SignTextMod::ensure_hosted_server_authority_route(const std::string& reason) -> bool
    {
        if (!is_hosted_server_relay_context())
        {
            return false;
        }
        const auto bound_port = NativeBridge::instance().server_bound_port();
        if (!m_hosted_server_authority_route_configured)
        {
            m_hosted_server_authority_route_configured = true;
            log_line("[bridge-hosted] hosted_server_authority_route_set reason=" + reason +
                     " host=dynamic_known_client" +
                     " port=" + std::to_string(bound_port == 0 ? static_cast<unsigned int>(m_bridge_udp_port)
                                                               : static_cast<unsigned int>(bound_port)));
        }
        (void)publish_hosted_server_endpoint("route_set:" + reason);
        return true;
    }

    auto SignTextMod::send_hosted_server_control_message(const std::string& type, const std::string& reason) -> bool
    {
        if (!ensure_hosted_server_authority_route(reason))
        {
            return false;
        }

        std::ostringstream payload{};
        const auto relay_host = resolve_hosted_server_advertise_host();
        const auto relay_port = NativeBridge::instance().server_bound_port() == 0
            ? static_cast<uint16_t>(m_bridge_udp_port)
            : NativeBridge::instance().server_bound_port();
        const auto relay_pid = static_cast<unsigned long long>(GetCurrentProcessId());
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"" << escape_json(type) << "\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"worldId\":\"" << escape_json(m_world_folder_id) << "\""
                << ",\"writer\":\"HostedServerRelay\""
                << ",\"relayHost\":\"" << escape_json(relay_host) << "\""
                << ",\"relayPort\":" << static_cast<unsigned int>(relay_port)
                << ",\"relayPid\":" << relay_pid
                << ",\"relayEpoch\":" << m_session_epoch
                << ",\"relayCacheInitialized\":" << (m_hosted_server_cache_initialized ? "true" : "false")
                << ",\"relayCacheRevision\":" << m_hosted_server_cache_revision
                << ",\"reason\":\"" << escape_json(reason) << "\""
                << ",\"epoch\":" << m_session_epoch
                << "}";
        const auto payload_text = payload.str();
        const bool sent = NativeBridge::instance().broadcast_to_clients(payload_text);
        (void)publish_hosted_server_endpoint("control_message:" + type);
        m_hosted_server_endpoint_advertised = true;
        m_hosted_server_endpoint_host = relay_host;
        m_hosted_server_endpoint_port = relay_port;
        log_line("[bridge-hosted] hosted_server_relay_forward type=" + type +
                 " reason=" + reason +
                 " sent=" + std::string{sent ? "true" : "false"} +
                 " role=" + bridge_role_name(m_bridge_role) +
                 " authority=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                 " worldId=" + m_world_folder_id +
                 " host=" + relay_host +
                 " port=" + std::to_string(static_cast<unsigned int>(relay_port)) +
                 " pid=" + std::to_string(relay_pid));
        return sent;
    }

    auto SignTextMod::relay_payload_to_hosted_authority(
        const std::string& payload,
        const std::string& type,
        const std::string& stable_id,
        const std::string& world_id,
        const std::string& reason) -> bool
    {
        std::string world_bound_reason{};
        if (!is_world_bound_operation_allowed("relay_payload_to_hosted_authority:" + type, &world_bound_reason))
        {
            return false;
        }
        note_world_bound_operation_resumed("relay_payload_to_hosted_authority");

        if (!ensure_hosted_server_authority_route(reason))
        {
            return false;
        }
        const bool sent = NativeBridge::instance().broadcast_to_clients(payload);
        log_line("[bridge-hosted] hosted_edit_request_relayed_to_authority type=" + type +
                 " stableId=" + (stable_id.empty() ? "none" : stable_id) +
                 " worldId=" + (world_id.empty() ? "none" : world_id) +
                 " sent=" + std::string{sent ? "true" : "false"} +
                 " role=" + bridge_role_name(m_bridge_role) +
                 " authority=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                 " reason=" + reason);
        return sent;
    }

    auto SignTextMod::send_bridge_record_request(const std::string& request_type, const LabelRecord& rec) -> bool
    {
        if (m_bridge_role != BridgeRole::RemoteClient)
        {
            return false;
        }
        if (!has_viable_remote_route_for_snapshot())
        {
            log_line("[bridge-route] send_blocked type=" + request_type +
                     " reason=no_valid_committed_route role=" + bridge_role_name(m_bridge_role) +
                     " routeHost=" + (m_bridge_remote_server_host.empty() ? "none" : m_bridge_remote_server_host) +
                     " routeLocked=" + std::string{m_bridge_route_lock_acquired ? "true" : "false"} +
                     " stableId=" + rec.stable_id +
                     " worldId=" + rec.world_id);
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
                << ",\"writer\":\"RemoteClientEditRequest\""
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
        if (m_bridge_role != BridgeRole::DedicatedServer &&
            m_bridge_role != BridgeRole::ListenHost &&
            !is_hosted_client_authority_context())
        {
            return;
        }
        std::string world_bound_reason{};
        if (!is_world_bound_operation_allowed("broadcast_bridge_record", &world_bound_reason))
        {
            return;
        }
        note_world_bound_operation_resumed("broadcast_bridge_record");
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
                << ",\"writer\":\"HostedClientAuthority\""
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
                << ",\"colorR\":" << rec.color_r
                << ",\"colorG\":" << rec.color_g
                << ",\"colorB\":" << rec.color_b
                << ",\"colorA\":" << rec.color_a
                << ",\"lastSeen\":\"" << escape_json(rec.last_seen_utc) << "\""
                << ",\"reason\":\"" << escape_json(reason) << "\""
                << "}";
        const auto payload_text = payload.str();
        const bool can_direct_broadcast =
            (m_bridge_role == BridgeRole::DedicatedServer || m_bridge_role == BridgeRole::ListenHost);
        const bool sent_clients = can_direct_broadcast
            ? NativeBridge::instance().broadcast_to_clients(payload_text)
            : false;
        bool sent_hosted_server = false;
        if (is_hosted_client_authority_context())
        {
            sent_hosted_server = send_hosted_authority_payload_to_server(
                payload_text,
                "upsert",
                rec.stable_id,
                rec.world_id,
                reason);
        }
        log_line("[bridge] broadcast_upsert reason=" + reason +
                 " stableId=" + rec.stable_id +
                 " worldId=" + rec.world_id +
                 " revision=" + std::to_string(m_revision) +
                 " sentClients=" + std::string{sent_clients ? "true" : "false"} +
                 " sentHostedServer=" + std::string{sent_hosted_server ? "true" : "false"} +
                 " knownClients=" + std::to_string(NativeBridge::instance().known_client_count()));
    }

    auto SignTextMod::broadcast_bridge_clear(const std::string& stable_id, const std::string& world_id, const std::string& reason) -> void
    {
        if (m_bridge_role != BridgeRole::DedicatedServer &&
            m_bridge_role != BridgeRole::ListenHost &&
            !is_hosted_client_authority_context())
        {
            return;
        }
        std::string world_bound_reason{};
        if (!is_world_bound_operation_allowed("broadcast_bridge_clear", &world_bound_reason))
        {
            return;
        }
        note_world_bound_operation_resumed("broadcast_bridge_clear");
        std::ostringstream payload{};
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"clear\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"writer\":\"HostedClientAuthority\""
                << ",\"revision\":" << m_revision
                << ",\"stableId\":\"" << escape_json(stable_id) << "\""
                << ",\"worldId\":\"" << escape_json(world_id) << "\""
                << ",\"reason\":\"" << escape_json(reason) << "\""
                << "}";
        const auto payload_text = payload.str();
        const bool can_direct_broadcast =
            (m_bridge_role == BridgeRole::DedicatedServer || m_bridge_role == BridgeRole::ListenHost);
        const bool sent_clients = can_direct_broadcast
            ? NativeBridge::instance().broadcast_to_clients(payload_text)
            : false;
        bool sent_hosted_server = false;
        if (is_hosted_client_authority_context())
        {
            sent_hosted_server = send_hosted_authority_payload_to_server(
                payload_text,
                "clear",
                stable_id,
                world_id,
                reason);
        }
        log_line("[bridge] broadcast_clear reason=" + reason +
                 " stableId=" + stable_id +
                 " worldId=" + world_id +
                 " revision=" + std::to_string(m_revision) +
                 " sentClients=" + std::string{sent_clients ? "true" : "false"} +
                 " sentHostedServer=" + std::string{sent_hosted_server ? "true" : "false"} +
                 " knownClients=" + std::to_string(NativeBridge::instance().known_client_count()));
    }

    auto SignTextMod::broadcast_bridge_snapshot(const std::string& reason) -> void
    {
        if (m_bridge_role != BridgeRole::DedicatedServer &&
            m_bridge_role != BridgeRole::ListenHost &&
            !is_hosted_client_authority_context())
        {
            return;
        }
        std::string world_bound_reason{};
        if (!is_world_bound_operation_allowed("broadcast_bridge_snapshot", &world_bound_reason))
        {
            return;
        }
        note_world_bound_operation_resumed("broadcast_bridge_snapshot");
        const auto snapshot_count = static_cast<uint32_t>(m_labels.size());
        const auto snapshot_id = make_bridge_snapshot_id(m_session_id, m_revision, snapshot_count);
        if (is_hosted_client_authority_context())
        {
            const bool route_ready = ensure_hosted_client_authority_route("snapshot_push_begin");
            log_line("[bridge-hosted] hosted_authority_snapshot_push_begin revision=" + std::to_string(m_revision) +
                     " records=" + std::to_string(snapshot_count) +
                     " routeReady=" + std::string{route_ready ? "true" : "false"} +
                     " epoch=" + std::to_string(m_session_epoch) +
                     " reason=" + reason);
        }
        uint32_t count = 0;
        for (const auto& [_, rec] : m_labels)
        {
            broadcast_bridge_record(rec, reason, snapshot_id, static_cast<int>(snapshot_count));
            ++count;
        }
        std::ostringstream payload{};
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"snapshot_end\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"writer\":\"HostedClientAuthority\""
                << ",\"revision\":" << m_revision
                << ",\"snapshotId\":\"" << escape_json(snapshot_id) << "\""
                << ",\"snapshotCount\":" << snapshot_count
                << ",\"worldId\":\"" << escape_json(m_world_folder_id) << "\""
                << ",\"reason\":\"" << escape_json(reason) << "\""
                << "}";
        const auto payload_text = payload.str();
        const bool can_direct_broadcast =
            (m_bridge_role == BridgeRole::DedicatedServer || m_bridge_role == BridgeRole::ListenHost);
        const bool sent_clients = can_direct_broadcast
            ? NativeBridge::instance().broadcast_to_clients(payload_text)
            : false;
        bool sent_hosted_server = false;
        if (is_hosted_client_authority_context())
        {
            sent_hosted_server = send_hosted_authority_payload_to_server(
                payload_text,
                "snapshot_end",
                "",
                m_world_folder_id,
                reason);
        }
        log_line("[bridge] broadcast_snapshot reason=" + reason +
                 " records=" + std::to_string(count) +
                 " revision=" + std::to_string(m_revision) +
                 " sentClients=" + std::string{sent_clients ? "true" : "false"} +
                 " sentHostedServer=" + std::string{sent_hosted_server ? "true" : "false"});
        if (is_hosted_client_authority_context())
        {
            log_line("[bridge-hosted] hosted_authority_snapshot_push_end records=" + std::to_string(count) +
                     " revision=" + std::to_string(m_revision) +
                     " sentClients=" + std::string{sent_clients ? "true" : "false"} +
                     " sentHostedServer=" + std::string{sent_hosted_server ? "true" : "false"} +
                     " epoch=" + std::to_string(m_session_epoch) +
                     " reason=" + reason);
        }
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
        LabelRecord existing_record{};
        const auto world_id = is_hex_world_id(m_world_folder_id)
            ? m_world_folder_id
            : unescape_json(fields.count("worldId") ? fields.at("worldId") : "unknown-world");
        const auto key = build_storage_key(world_id, stable_id);
        bool has_existing_record = false;
        bool existing_is_confirmed_label_text = false;
        if (const auto existing = m_labels.find(key); existing != m_labels.end())
        {
            rec = existing->second;
            existing_record = existing->second;
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
        // fontSize from bridge payloads is ignored intentionally.
        // Runtime render size is recomputed per-client from text + local profile.
        rec.color_r = fields.count("colorR") ? std::clamp(safe_stof(fields.at("colorR"), 0.393822f), 0.0f, 1.0f) : rec.color_r;
        rec.color_g = fields.count("colorG") ? std::clamp(safe_stof(fields.at("colorG"), 0.393822f), 0.0f, 1.0f) : rec.color_g;
        rec.color_b = fields.count("colorB") ? std::clamp(safe_stof(fields.at("colorB"), 0.393822f), 0.0f, 1.0f) : rec.color_b;
        rec.color_a = fields.count("colorA") ? std::clamp(safe_stof(fields.at("colorA"), 1.0f), 0.0f, 1.0f) : rec.color_a;
        rec.last_seen_utc = now_utc();
        const auto same_float = [](const float lhs, const float rhs) {
            return std::fabs(lhs - rhs) <= 0.0001f;
        };
        const bool changed_record =
            !has_existing_record ||
            rec.stable_id != existing_record.stable_id ||
            rec.world_id != existing_record.world_id ||
            rec.text != existing_record.text ||
            rec.asset != existing_record.asset ||
            rec.kind != existing_record.kind ||
            rec.backing_asset != existing_record.backing_asset ||
            !same_float(rec.surface_axis, existing_record.surface_axis) ||
            rec.surface_sign != existing_record.surface_sign ||
            !same_float(rec.depth_offset, existing_record.depth_offset) ||
            !same_float(rec.align_x, existing_record.align_x) ||
            !same_float(rec.align_y, existing_record.align_y) ||
            !same_float(rec.color_r, existing_record.color_r) ||
            !same_float(rec.color_g, existing_record.color_g) ||
            !same_float(rec.color_b, existing_record.color_b) ||
            !same_float(rec.color_a, existing_record.color_a);
        m_labels[key] = rec;
        save_sidecar_json("bridge_set", key, stable_id, world_id);
        broadcast_bridge_record(rec, "bridge_set");
        bool local_render_attempted = false;
        bool local_render_applied = false;
        bool local_render_deferred = false;
        size_t local_actor_hits = 0;
        std::string local_render_reason = "not_needed";
        if (should_render_world_text_components() && is_confirmed_label_text_kind(rec.kind))
        {
            bool has_cached_text = false;
            bool cached_text_matches = false;
            if (const auto cached = m_rendered_text_cache.find(key); cached != m_rendered_text_cache.end())
            {
                has_cached_text = true;
                cached_text_matches = cached->second == rec.text;
            }
            const bool needs_local_render_pass =
                changed_record ||
                m_hosted_authority_local_apply_deferred_keys.count(key) > 0 ||
                !has_cached_text ||
                !cached_text_matches;
            if (needs_local_render_pass)
            {
                std::string world_bound_reason{};
                const bool world_ready_for_render =
                    m_session_ready_latched &&
                    m_role_lock_acquired &&
                    is_world_bound_operation_allowed("hosted_authority_local_render_apply:" + stable_id, &world_bound_reason);
                if (!world_ready_for_render)
                {
                    local_render_deferred = true;
                    local_render_reason = world_bound_reason.empty() ? "world_not_ready" : world_bound_reason;
                    m_hosted_authority_local_apply_deferred_keys.insert(key);
                }
                else
                {
                    local_render_attempted = true;
                    UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                        if (!object || !object->IsA(AActor::StaticClass()))
                        {
                            return LoopAction::Continue;
                        }
                        auto* actor = Cast<AActor>(object);
                        if (actor && is_probable_label_actor(actor) && extract_stable_id(actor) == stable_id)
                        {
                            ++local_actor_hits;
                            local_render_applied = apply_text_to_actor_component(actor, rec.text) || local_render_applied;
                        }
                        return LoopAction::Continue;
                    });
                    if (local_actor_hits == 0)
                    {
                        local_render_deferred = true;
                        local_render_reason = "no_live_actor_hit";
                        m_hosted_authority_local_apply_deferred_keys.insert(key);
                    }
                    else
                    {
                        local_render_reason = local_render_applied ? "applied" : "attempted_no_component_change";
                        m_hosted_authority_local_apply_deferred_keys.erase(key);
                    }
                }
            }
            else
            {
                local_render_reason = "idempotent_no_change";
            }
        }
        if (is_hosted_client_authority_context())
        {
            log_line("[bridge-hosted] hosted_authority_local_render_apply key=" + key +
                     " stableId=" + stable_id +
                     " changed=" + std::string{changed_record ? "true" : "false"} +
                     " attempted=" + std::string{local_render_attempted ? "true" : "false"} +
                     " applied=" + std::string{local_render_applied ? "true" : "false"} +
                     " actorHits=" + std::to_string(local_actor_hits) +
                     " deferred=" + std::string{local_render_deferred ? "true" : "false"} +
                     " reason=" + local_render_reason);
            log_line("[bridge-hosted] hosted_authority_applied_edit action=apply key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + world_id +
                     " revision=" + std::to_string(m_revision) +
                     " epoch=" + std::to_string(m_session_epoch));
        }
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
        m_hosted_authority_local_apply_deferred_keys.erase(key);
        m_phase4_next_retry.erase(key);
        m_create_null_retry_states.erase(key);
        m_phase4_last_failure_reason.erase(key);
        if (should_render_world_text_components())
        {
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (!object || !object->IsA(AActor::StaticClass()))
                {
                    return LoopAction::Continue;
                }
                auto* actor = Cast<AActor>(object);
                if (actor && is_probable_label_actor(actor) && extract_stable_id(actor) == stable_id)
                {
                    destroy_managed_text_component(actor, key);
                }
                return LoopAction::Continue;
            });
        }
        save_sidecar_json("bridge_clear", key, stable_id, world_id);
        broadcast_bridge_clear(stable_id, world_id, "bridge_clear");
        if (is_hosted_client_authority_context())
        {
            log_line("[bridge-hosted] hosted_authority_applied_edit action=clear key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + world_id +
                     " revision=" + std::to_string(m_revision) +
                     " epoch=" + std::to_string(m_session_epoch));
        }
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
        const auto writer = unescape_json(fields.count("writer") ? fields.at("writer") : "");
        const auto stable_id = unescape_json(stable_it->second);
        const auto local_world_id = (!m_world_folder_id.empty() && m_world_folder_id != "unknown-world")
            ? m_world_folder_id
            : unescape_json(fields.count("worldId") ? fields.at("worldId") : "unknown-world");
        const uint64_t incoming_revision = fields.count("revision")
            ? static_cast<uint64_t>(safe_stoi(fields.at("revision"), static_cast<int>(m_revision)))
            : m_revision;
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
        // fontSize from bridge payloads is ignored intentionally.
        // Runtime render size is recomputed per-client from text + local profile.
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
            !same_float(existing->second.color_r, rec.color_r) ||
            !same_float(existing->second.color_g, rec.color_g) ||
            !same_float(existing->second.color_b, rec.color_b) ||
            !same_float(existing->second.color_a, rec.color_a);

        std::ostringstream dedupe_signature{};
        dedupe_signature << rec.stable_id << '\x1f' << rec.world_id << '\x1f'
                         << rec.text << '\x1f' << rec.asset << '\x1f'
                         << rec.kind << '\x1f' << rec.backing_asset << '\x1f'
                         << std::fixed << std::setprecision(4)
                         << rec.surface_axis << '\x1f' << rec.surface_sign << '\x1f'
                         << rec.depth_offset << '\x1f' << rec.align_x << '\x1f' << rec.align_y << '\x1f'
                         << rec.color_r << '\x1f' << rec.color_g << '\x1f'
                         << rec.color_b << '\x1f' << rec.color_a;
        const uint64_t content_hash = std::hash<std::string>{}(dedupe_signature.str());
        const auto prior_delta_it = m_remote_delta_applied_state.find(key);
        const bool has_prior_delta_state = prior_delta_it != m_remote_delta_applied_state.end();
        const uint64_t prior_delta_revision = has_prior_delta_state ? prior_delta_it->second.revision : 0;
        const uint64_t prior_delta_hash = has_prior_delta_state ? prior_delta_it->second.content_hash : 0;
        const bool stale_authoritative_delta =
            has_prior_delta_state &&
            incoming_revision < prior_delta_revision;
        if (stale_authoritative_delta)
        {
            log_line("[bridge] delta_skip_stale key=" + key +
                     " stableId=" + stable_id +
                     " incomingRevision=" + std::to_string(incoming_revision) +
                     " priorRevision=" + std::to_string(prior_delta_revision));
            return;
        }
        const bool duplicate_authoritative_delta =
            has_prior_delta_state &&
            incoming_revision == prior_delta_revision &&
            content_hash == prior_delta_hash;
        const bool nochange_authoritative_delta =
            !changed &&
            has_prior_delta_state &&
            incoming_revision > prior_delta_revision &&
            content_hash == prior_delta_hash;

        m_labels[key] = rec;
        m_remote_delta_applied_state[key] = RemoteDeltaApplyState{incoming_revision, content_hash};
        m_revision = std::max<uint64_t>(m_revision, incoming_revision);
        if (changed)
        {
            save_sidecar_json("bridge_cache_upsert", key, stable_id, local_world_id);
        }
        std::string world_bound_reason{};
        const bool world_bound_ready_for_render =
            is_world_bound_operation_allowed("bridge_remote_render_upsert", &world_bound_reason);
        bool render_suppressed_by_rebuild_guard = false;
        if (const auto retry = m_phase4_next_retry.find(key); retry != m_phase4_next_retry.end())
        {
            render_suppressed_by_rebuild_guard = std::chrono::steady_clock::now() < retry->second;
        }
        const bool render_suppressed_pre_ready = !world_bound_ready_for_render;
        if (render_suppressed_pre_ready)
        {
            m_remote_cached_replay_pending_after_ready = true;
            log_line("[bridge] client_upsert_render_queued key=" + key +
                     " stableId=" + stable_id +
                     " reason=" + world_bound_reason +
                     " worldId=" + (m_world_folder_id.empty() ? "unknown" : m_world_folder_id));
        }
        if (is_confirmed_label_text_kind(rec.kind) &&
            !render_suppressed_by_rebuild_guard &&
            !render_suppressed_pre_ready)
        {
            if (duplicate_authoritative_delta)
            {
                log_line("[bridge] delta_skip_duplicate key=" + key +
                         " stableId=" + stable_id +
                         " revision=" + std::to_string(incoming_revision));
            }
            else if (nochange_authoritative_delta)
            {
                log_line("[bridge] delta_skip_nochange key=" + key +
                         " stableId=" + stable_id +
                         " incomingRevision=" + std::to_string(incoming_revision) +
                         " priorRevision=" + std::to_string(prior_delta_revision));
            }
            else
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
        }
        m_bridge_snapshot_received = true;
        m_bridge_snapshot_world_id = local_world_id;
        mark_bridge_healthy("client_upsert_applied");
        log_line("[bridge] client_upsert_applied key=" + key +
                 " stableId=" + stable_id +
                 " localWorldId=" + local_world_id +
                 " changed=" + std::string{changed ? "true" : "false"} +
                 " duplicate=" + std::string{duplicate_authoritative_delta ? "true" : "false"} +
                 " nochange=" + std::string{nochange_authoritative_delta ? "true" : "false"} +
                 " ackedPending=" + std::string{acked_pending ? "true" : "false"} +
                 " renderSuppressed=" + std::string{(render_suppressed_by_rebuild_guard || render_suppressed_pre_ready) ? "true" : "false"} +
                 " pending=" + std::to_string(m_bridge_pending_request_keys.size()) +
                 " textChars=" + std::to_string(rec.text.size()));
        if (!m_sidecar_authoritative &&
            writer == "HostedClientAuthority" &&
            !duplicate_authoritative_delta &&
            !nochange_authoritative_delta)
        {
            log_line("[bridge-hosted] hosted_remote_applied_authoritative_delta type=upsert key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + local_world_id +
                     " revision=" + std::to_string(m_revision) +
                     " epoch=" + std::to_string(m_session_epoch));
        }
        if (!m_sidecar_authoritative &&
            (writer == "HostedClientAuthority" || !snapshot_id.empty()))
        {
            queue_first_authoritative_render_pass("client_upsert", local_world_id);
        }

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
        const auto writer = unescape_json(fields.count("writer") ? fields.at("writer") : "");
        const auto stable_id = unescape_json(stable_it->second);
        const auto local_world_id = (!m_world_folder_id.empty() && m_world_folder_id != "unknown-world")
            ? m_world_folder_id
            : unescape_json(fields.count("worldId") ? fields.at("worldId") : "unknown-world");
        const uint64_t incoming_revision = fields.count("revision")
            ? static_cast<uint64_t>(safe_stoi(fields.at("revision"), static_cast<int>(m_revision)))
            : m_revision;
        const auto key = build_storage_key(local_world_id, stable_id);
        const bool already_cleared = (m_labels.find(key) == m_labels.end());
        const bool duplicate_clear =
            already_cleared &&
            m_remote_delta_applied_state.count(key) > 0 &&
            m_remote_delta_applied_state.at(key).revision == incoming_revision &&
            m_remote_delta_applied_state.at(key).content_hash == 0;
        if (duplicate_clear)
        {
            log_line("[bridge] client_clear_ignored reason=idempotent_duplicate key=" + key +
                     " stableId=" + stable_id +
                     " revision=" + std::to_string(incoming_revision));
            return;
        }
        const bool acked_pending = m_bridge_pending_request_keys.erase(key) > 0;
        m_labels.erase(key);
        m_rendered_text_cache.erase(key);
        m_component_name_cache.erase(key);
        m_phase4_next_retry.erase(key);
        m_create_null_retry_states.erase(key);
        m_phase4_last_failure_reason.erase(key);
        m_remote_delta_applied_state[key] = RemoteDeltaApplyState{incoming_revision, 0};
        m_revision = std::max<uint64_t>(m_revision, incoming_revision);
        save_sidecar_json("bridge_cache_clear", key, stable_id, local_world_id);
        std::string world_bound_reason{};
        const bool world_bound_ready_for_render =
            is_world_bound_operation_allowed("bridge_remote_render_clear", &world_bound_reason);
        if (world_bound_ready_for_render)
        {
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
        }
        else
        {
            m_remote_cached_replay_pending_after_ready = true;
            log_line("[bridge] client_clear_render_queued key=" + key +
                     " stableId=" + stable_id +
                     " reason=" + world_bound_reason +
                     " worldId=" + (m_world_folder_id.empty() ? "unknown" : m_world_folder_id));
        }
        m_bridge_snapshot_received = true;
        m_bridge_snapshot_world_id = local_world_id;
        mark_bridge_healthy("client_clear_applied");
        log_line("[bridge] client_clear_applied key=" + key +
                 " stableId=" + stable_id +
                 " localWorldId=" + local_world_id +
                 " ackedPending=" + std::string{acked_pending ? "true" : "false"} +
                 " pending=" + std::to_string(m_bridge_pending_request_keys.size()));
        if (!m_sidecar_authoritative && writer == "HostedClientAuthority")
        {
            log_line("[bridge-hosted] hosted_remote_applied_authoritative_delta type=clear key=" + key +
                     " stableId=" + stable_id +
                     " worldId=" + local_world_id +
                     " revision=" + std::to_string(m_revision) +
                     " epoch=" + std::to_string(m_session_epoch));
        }
        if (!m_sidecar_authoritative && writer == "HostedClientAuthority")
        {
            queue_first_authoritative_render_pass("client_clear", local_world_id);
        }
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
        const auto looks_like_json_mod = payload.find("\"mod\":\"WindroseTextSigns\"") != std::string::npos;
        const auto looks_like_kv_mod = payload.find("mod=WindroseTextSigns") != std::string::npos;
        if (!looks_like_json_mod && !looks_like_kv_mod)
        {
            return;
        }
        auto fields = parse_bridge_message(payload);
        if (fields.find("type") == fields.end())
        {
            const auto kv_fields = parse_semicolon_kv_message(payload);
            for (const auto& [k, v] : kv_fields)
            {
                if (fields.find(k) == fields.end())
                {
                    fields.emplace(k, v);
                }
            }
        }
        const auto type_it = fields.find("type");
        if (type_it == fields.end())
        {
            log_line("[bridge] payload_rejected reason=missing_type bytes=" + std::to_string(payload.size()));
            return;
        }
        const auto type = unescape_json(type_it->second);
        const auto stable_id = unescape_json(fields.count("stableId") ? fields.at("stableId") : "");
        const auto world_id = unescape_json(fields.count("worldId") ? fields.at("worldId") : "");
        const auto writer = unescape_json(fields.count("writer") ? fields.at("writer") : "");
        const auto parse_bool_field = [&](const std::string& name, const bool fallback) -> bool {
            const auto it = fields.find(name);
            if (it == fields.end())
            {
                return fallback;
            }
            const auto raw = lower_ascii(unescape_json(it->second));
            if (raw == "true" || raw == "1" || raw == "yes")
            {
                return true;
            }
            if (raw == "false" || raw == "0" || raw == "no")
            {
                return false;
            }
            return fallback;
        };
        const auto parse_u64_field = [&](const std::string& name, const uint64_t fallback) -> uint64_t {
            const auto it = fields.find(name);
            if (it == fields.end())
            {
                return fallback;
            }
            try
            {
                return static_cast<uint64_t>(std::stoull(unescape_json(it->second)));
            }
            catch (...)
            {
                return fallback;
            }
        };
        if (type == "route_probe")
        {
            const auto runtime_role_lower = lower_ascii(m_runtime_role);
            const bool server_runtime = is_dedicated_runtime_process();
            const bool hosted_server_runtime = runtime_role_lower == "hostedserver";
            const bool dedicated_server_runtime = runtime_role_lower == "dedicatedserver";
            const bool can_ack_route_probe =
                server_runtime &&
                (m_bridge_role == BridgeRole::DedicatedServer || hosted_server_runtime || dedicated_server_runtime);
            if (can_ack_route_probe)
            {
                const auto token = unescape_json(fields.count("token") ? fields.at("token") : "");
                const auto probe_host = unescape_json(fields.count("probeHost") ? fields.at("probeHost") : "");
                const auto probe_source = unescape_json(fields.count("probeSource") ? fields.at("probeSource") : "");
                std::ostringstream ack{};
                ack << "mod=WindroseTextSigns"
                    << ";schema=bridge.v1"
                    << ";type=route_probe_ack"
                    << ";session=" << m_session_id
                    << ";worldId=" << m_world_folder_id
                    << ";token=" << token
                    << ";probeHost=" << probe_host
                    << ";probeSource=" << probe_source
                    << ";runtimeRole=" << m_runtime_role
                    << ";bridgeRole=" << bridge_role_name(m_bridge_role)
                    << ";authoritative=" << (m_sidecar_authoritative ? "true" : "false")
                    << ";writer=BridgeRouteProbeResponder"
                    << ";epoch=" << m_session_epoch;
                const bool sent = NativeBridge::instance().broadcast_to_clients(ack.str());
                log_line("[bridge-route] route_probe_ack_emit token=" + token +
                         " probeHost=" + (probe_host.empty() ? "none" : probe_host) +
                         " probeSource=" + (probe_source.empty() ? "none" : probe_source) +
                         " sent=" + std::string{sent ? "true" : "false"});
            }
            return;
        }
        if (type == "route_probe_ack")
        {
            const auto token = unescape_json(field_with_alias(fields, {"token"}));
            const auto ack_session = unescape_json(field_with_alias(fields, {"session"}));
            const auto ack_probe_host = unescape_json(field_with_alias(fields, {"probeHost", "probe_host"}));
            const auto ack_runtime_role = lower_ascii(unescape_json(field_with_alias(fields, {"runtimeRole", "runtime_role", "role"})));
            const auto ack_bridge_role = lower_ascii(unescape_json(field_with_alias(fields, {"bridgeRole", "bridge_role"})));
            const auto ack_authoritative = lower_ascii(unescape_json(field_with_alias(fields, {"authoritative", "isAuthoritative"})));
            const auto ack_epoch = parse_u64_field("epoch", 0);
            const bool ack_from_server_role =
                ack_runtime_role == "dedicatedserver" ||
                ack_runtime_role == "hostedserver" ||
                ack_bridge_role == "dedicatedserver";
            const bool ack_identity_match =
                m_bridge_route_probe_active &&
                m_bridge_route_probe_waiting_ack &&
                !m_bridge_route_probe_token.empty() &&
                token == m_bridge_route_probe_token &&
                !m_bridge_route_probe_host.empty() &&
                !ack_probe_host.empty() &&
                lower_ascii(ack_probe_host) == lower_ascii(m_bridge_route_probe_host);
            if (m_bridge_route_probe_active &&
                m_bridge_route_probe_waiting_ack &&
                !m_bridge_route_probe_token.empty() &&
                token == m_bridge_route_probe_token &&
                !m_bridge_route_lock_acquired)
            {
                const auto keys_runtime = field_with_alias(fields, {"runtimeRole", "runtime_role", "role"});
                const auto keys_bridge = field_with_alias(fields, {"bridgeRole", "bridge_role"});
                if (keys_runtime.empty() && keys_bridge.empty() && ack_authoritative.empty())
                {
                    log_line("[bridge-route] route_probe_ack_parse_missing_fields token=" + (token.empty() ? "none" : token) +
                             " keys=none");
                }
                log_line("[bridge-route] route_probe_ack_parse_ok token=" + (token.empty() ? "none" : token) +
                         " epoch=" + std::to_string(ack_epoch) +
                         " session=" + (ack_session.empty() ? "none" : ack_session) +
                         " probeHost=" + (ack_probe_host.empty() ? "none" : ack_probe_host) +
                         " runtimeRole=" + (keys_runtime.empty() ? "missing" : keys_runtime) +
                         " bridgeRole=" + (keys_bridge.empty() ? "missing" : keys_bridge) +
                         " authoritative=" + (ack_authoritative.empty() ? "missing" : ack_authoritative) +
                         " sessionMismatch=" + std::string{
                             (!ack_session.empty() && !m_session_id.empty() && ack_session != m_session_id) ? "true" : "false"} +
                         " epochMismatch=" + std::string{
                             (ack_epoch != 0 && ack_epoch != static_cast<uint64_t>(m_session_epoch)) ? "true" : "false"});
                if (!ack_identity_match)
                {
                    log_line("[bridge-route] route_probe_result candidate=" + m_bridge_route_probe_host +
                             ":" + std::to_string(m_bridge_udp_port) +
                             " result=rejected_identity_mismatch token=" + (token.empty() ? "none" : token) +
                             " epoch=" + std::to_string(ack_epoch) +
                             " session=" + (ack_session.empty() ? "none" : ack_session) +
                             " probeHost=" + (ack_probe_host.empty() ? "none" : ack_probe_host));
                    return;
                }
                if (!ack_from_server_role)
                {
                    log_line("[bridge-route] route_probe_result candidate=" + m_bridge_route_probe_host +
                             ":" + std::to_string(m_bridge_udp_port) +
                             " result=rejected_non_server_ack runtimeRole=" +
                             (ack_runtime_role.empty() ? "none" : ack_runtime_role) +
                             " bridgeRole=" + (ack_bridge_role.empty() ? "none" : ack_bridge_role));
                    return;
                }
                log_line("[bridge-route] route_probe_result candidate=" + m_bridge_route_probe_host +
                         ":" + std::to_string(m_bridge_udp_port) +
                         " result=success");
                commit_locked_route_from_probe(
                    m_bridge_route_probe_host,
                    m_bridge_route_probe_source.empty() ? "unknown" : m_bridge_route_probe_source);
            }
            return;
        }
        const bool hosted_control_type = (type == "hosted_server_hello" || type == "hosted_server_resync_request");
        const auto incoming_session = unescape_json(fields.count("session") ? fields.at("session") : "");
        if (m_bridge_role == BridgeRole::ListenHost &&
            !hosted_control_type &&
            !incoming_session.empty() &&
            !m_session_id.empty() &&
            incoming_session == m_session_id)
        {
            log_line("[bridge] payload_ignored type=" + type +
                     " role=ListenHost reason=self_originated session=" + incoming_session);
            return;
        }
        if (type == "hosted_server_hello")
        {
            const auto relay_host = unescape_json(fields.count("relayHost") ? fields.at("relayHost") : "");
            const auto relay_port = static_cast<uint16_t>(std::clamp(
                safe_stoi(fields.count("relayPort") ? fields.at("relayPort") : "0", 0),
                0,
                65535));
            if (!relay_host.empty() && relay_port > 0)
            {
                if (m_bridge_role == BridgeRole::RemoteClient && !m_sidecar_authoritative)
                {
                    (void)apply_bridge_remote_endpoint(relay_host, relay_port, "hosted_server_hello_remote");
                }
                else if (is_hosted_client_authority_context())
                {
                    (void)apply_bridge_remote_endpoint(relay_host, relay_port, "hosted_server_hello_authority");
                }
            }
            if (is_hosted_client_authority_context())
            {
                (void)ensure_hosted_client_authority_route("hosted_server_hello");
                const bool relay_cache_initialized = parse_bool_field("relayCacheInitialized", false);
                const uint64_t relay_cache_revision = parse_u64_field("relayCacheRevision", 0);
                const bool world_mismatch =
                    is_hex_world_id(m_world_folder_id) &&
                    is_hex_world_id(world_id) &&
                    lower_ascii(world_id) != lower_ascii(m_world_folder_id);
                const bool should_push_snapshot =
                    !relay_cache_initialized ||
                    relay_cache_revision < m_revision ||
                    world_mismatch;
                log_line("[bridge-hosted] hosted_server_hello_received epoch=" + std::to_string(m_session_epoch) +
                         " role=" + bridge_role_name(m_bridge_role) +
                         " authority=true worldId=" + m_world_folder_id +
                         " relayCacheInitialized=" + std::string{relay_cache_initialized ? "true" : "false"} +
                         " relayCacheRevision=" + std::to_string(relay_cache_revision));
                if (should_push_snapshot)
                {
                    broadcast_bridge_snapshot("hosted_server_hello");
                }
                else
                {
                    log_line("[bridge-hosted] hosted_authority_snapshot_skip reason=server_cache_fresh trigger=hosted_server_hello" +
                             std::string(" relayCacheRevision=") + std::to_string(relay_cache_revision) +
                             " localRevision=" + std::to_string(m_revision) +
                             " worldId=" + m_world_folder_id);
                }
            }
            return;
        }
        if (type == "hosted_server_resync_request")
        {
            const auto relay_host = unescape_json(fields.count("relayHost") ? fields.at("relayHost") : "");
            const auto relay_port = static_cast<uint16_t>(std::clamp(
                safe_stoi(fields.count("relayPort") ? fields.at("relayPort") : "0", 0),
                0,
                65535));
            if (!relay_host.empty() && relay_port > 0)
            {
                if (m_bridge_role == BridgeRole::RemoteClient && !m_sidecar_authoritative)
                {
                    (void)apply_bridge_remote_endpoint(relay_host, relay_port, "hosted_server_resync_remote");
                }
                else if (is_hosted_client_authority_context())
                {
                    (void)apply_bridge_remote_endpoint(relay_host, relay_port, "hosted_server_resync_authority");
                }
            }
            if (is_hosted_client_authority_context())
            {
                (void)ensure_hosted_client_authority_route("hosted_server_resync_request");
                const bool relay_cache_initialized = parse_bool_field("relayCacheInitialized", false);
                const uint64_t relay_cache_revision = parse_u64_field("relayCacheRevision", 0);
                const bool should_push_snapshot = !relay_cache_initialized || relay_cache_revision < m_revision;
                log_line("[bridge-hosted] hosted_resync_request epoch=" + std::to_string(m_session_epoch) +
                         " role=" + bridge_role_name(m_bridge_role) +
                         " authority=true worldId=" + m_world_folder_id +
                         " relayCacheInitialized=" + std::string{relay_cache_initialized ? "true" : "false"} +
                         " relayCacheRevision=" + std::to_string(relay_cache_revision));
                if (should_push_snapshot)
                {
                    broadcast_bridge_snapshot("hosted_server_resync_request");
                }
                else
                {
                    log_line("[bridge-hosted] hosted_authority_snapshot_skip reason=server_cache_fresh trigger=hosted_server_resync_request" +
                             std::string(" relayCacheRevision=") + std::to_string(relay_cache_revision) +
                             " localRevision=" + std::to_string(m_revision) +
                             " worldId=" + m_world_folder_id);
                }
            }
            return;
        }
        if (type == "snapshot_request")
        {
            const uint64_t requester_revision = fields.count("requesterRevision")
                ? static_cast<uint64_t>(safe_stoi(fields.at("requesterRevision"), 0))
                : 0;
            const auto requester_session = unescape_json(fields.count("session") ? fields.at("session") : "");
            const std::string requester_key = requester_session.empty() ? std::string{"unknown-session"} : requester_session;
            const auto now_tp = std::chrono::steady_clock::now();
            auto& requester_state = m_snapshot_requester_state_by_session[requester_key];
            requester_state.last_request_at = now_tp;
            const uint64_t requester_prev_revision = requester_state.last_requester_revision_seen;
            const bool requester_revision_regressed = requester_revision < requester_prev_revision;
            if (requester_revision > requester_prev_revision)
            {
                requester_state.last_requester_revision_seen = requester_revision;
            }
            log_line("[bridge] snapshot_request_received fromSession=" + unescape_json(fields.count("session") ? fields.at("session") : "") +
                     " requesterRevision=" + std::to_string(requester_revision));
            if (is_hosted_server_relay_context() && !m_hosted_server_cache_initialized)
            {
                constexpr auto k_resync_cooldown = std::chrono::seconds(10);
                const bool resync_cooldown_elapsed =
                    m_hosted_server_last_resync_request_sent.time_since_epoch().count() == 0 ||
                    (now_tp - m_hosted_server_last_resync_request_sent) >= k_resync_cooldown;
                if (!m_hosted_server_resync_in_flight || resync_cooldown_elapsed)
                {
                    const bool sent_resync = send_hosted_server_control_message("hosted_server_resync_request", "snapshot_request_no_cache");
                    if (sent_resync)
                    {
                        m_hosted_server_resync_in_flight = true;
                        m_hosted_server_last_resync_request_sent = now_tp;
                    }
                }
                std::ostringstream unavailable{};
                unavailable << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"snapshot_unavailable\""
                            << ",\"session\":\"" << escape_json(m_session_id) << "\""
                            << ",\"writer\":\"HostedServerRelay\""
                            << ",\"worldId\":\"" << escape_json(m_world_folder_id) << "\""
                            << ",\"reason\":\"snapshot_request_no_cache\""
                            << "}";
                constexpr auto k_unavailable_throttle = std::chrono::seconds(10);
                const std::string throttle_key = requester_session.empty() ? std::string{"unknown-session"} : requester_session;
                const auto last_it = m_hosted_server_snapshot_unavailable_last_by_session.find(throttle_key);
                const bool should_send_unavailable =
                    last_it == m_hosted_server_snapshot_unavailable_last_by_session.end() ||
                    (now_tp - last_it->second) >= k_unavailable_throttle;
                if (should_send_unavailable)
                {
                    const bool sent = NativeBridge::instance().broadcast_to_clients(unavailable.str());
                    m_hosted_server_snapshot_unavailable_last_by_session[throttle_key] = now_tp;
                    log_line("[bridge-hosted] hosted_server_snapshot_request_timeout_no_cache role=" + bridge_role_name(m_bridge_role) +
                             " authority=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                             " sent=" + std::string{sent ? "true" : "false"} +
                             " requesterSession=" + throttle_key +
                             " epoch=" + std::to_string(m_session_epoch));
                }
                else
                {
                    log_line("[bridge-hosted] hosted_server_snapshot_unavailable_throttled requesterSession=" + throttle_key +
                             " epoch=" + std::to_string(m_session_epoch));
                }
                return;
            }

            const uint64_t authoritative_revision = is_hosted_server_relay_context()
                ? m_hosted_server_cache_revision
                : m_revision;
            constexpr auto k_snapshot_repeat_cooldown = std::chrono::seconds(20);
            const bool recently_served_same_revision =
                requester_state.last_snapshot_served_at.time_since_epoch().count() != 0 &&
                (now_tp - requester_state.last_snapshot_served_at) < k_snapshot_repeat_cooldown &&
                requester_state.last_snapshot_served_revision == authoritative_revision &&
                authoritative_revision > 0;
            if (requester_revision == 0 && recently_served_same_revision)
            {
                log_line("[bridge] snapshot_suppressed_cooldown reason=requester_zero_after_sync requesterSession=" +
                         requester_key +
                         " requesterRevision=0 servedRevision=" + std::to_string(requester_state.last_snapshot_served_revision) +
                         " authoritativeRevision=" + std::to_string(authoritative_revision));
                return;
            }
            if (requester_revision_regressed && recently_served_same_revision)
            {
                log_line("[bridge] snapshot_suppressed_cooldown reason=requester_revision_regressed requesterSession=" +
                         requester_key +
                         " requesterRevision=" + std::to_string(requester_revision) +
                         " requesterPrevRevision=" + std::to_string(requester_prev_revision) +
                         " servedRevision=" + std::to_string(requester_state.last_snapshot_served_revision));
                return;
            }

            if (is_hosted_server_relay_context() &&
                m_hosted_server_cache_initialized &&
                requester_revision >= m_hosted_server_cache_revision)
            {
                log_line("[bridge-hosted] hosted_server_snapshot_skip reason=requester_fresh requesterRevision=" +
                         std::to_string(requester_revision) +
                         " cacheRevision=" + std::to_string(m_hosted_server_cache_revision) +
                         " epoch=" + std::to_string(m_session_epoch));
                requester_state.last_requester_revision_seen =
                    std::max<uint64_t>(requester_state.last_requester_revision_seen, requester_revision);
                return;
            }
            if (!is_hosted_server_relay_context() &&
                m_sidecar_authoritative &&
                requester_revision >= m_revision)
            {
                log_line("[bridge] snapshot_skip reason=requester_fresh requesterRevision=" +
                         std::to_string(requester_revision) +
                         " localRevision=" + std::to_string(m_revision));
                requester_state.last_requester_revision_seen =
                    std::max<uint64_t>(requester_state.last_requester_revision_seen, requester_revision);
                return;
            }
            broadcast_bridge_snapshot("snapshot_request");
            requester_state.last_snapshot_served_revision = authoritative_revision;
            requester_state.last_snapshot_served_at = now_tp;
            if (is_hosted_server_relay_context())
            {
                log_line("[bridge-hosted] hosted_server_snapshot_served_from_cache revision=" +
                         std::to_string(m_hosted_server_cache_revision) +
                         " records=" + std::to_string(m_labels.size()) +
                         " requesterSession=" + requester_key +
                         " requesterRevision=" + std::to_string(requester_revision) +
                         " epoch=" + std::to_string(m_session_epoch));
            }
            else
            {
                log_line("[bridge] snapshot_served requesterSession=" + requester_key +
                         " requesterRevision=" + std::to_string(requester_revision) +
                         " authoritativeRevision=" + std::to_string(authoritative_revision));
            }
            return;
        }
        if (is_hosted_server_relay_context() && (type == "set" || type == "clear_request"))
        {
            log_line("[bridge-hosted] hosted_edit_request_received type=" + type +
                     " stableId=" + (stable_id.empty() ? "none" : stable_id) +
                     " worldId=" + (world_id.empty() ? "none" : world_id) +
                     " writer=" + (writer.empty() ? "none" : writer) +
                     " epoch=" + std::to_string(m_session_epoch));
            (void)relay_payload_to_hosted_authority(payload, type, stable_id, world_id, "remote_edit_request");
            return;
        }
        if (is_hosted_server_relay_context() && (type == "upsert" || type == "clear" || type == "snapshot_end"))
        {
            if (writer != "HostedClientAuthority")
            {
                log_line("[bridge-hosted] hosted_server_reject_non_authority_sender type=" + type +
                         " stableId=" + (stable_id.empty() ? "none" : stable_id) +
                         " worldId=" + (world_id.empty() ? "none" : world_id) +
                         " writer=" + (writer.empty() ? "none" : writer) +
                         " epoch=" + std::to_string(m_session_epoch));
                return;
            }
            const bool forwarded = NativeBridge::instance().broadcast_to_clients(payload);
            log_line("[bridge-hosted] hosted_server_relay_forward type=" + type +
                     " stableId=" + (stable_id.empty() ? "none" : stable_id) +
                     " worldId=" + (world_id.empty() ? "none" : world_id) +
                     " sent=" + std::string{forwarded ? "true" : "false"} +
                     " writer=" + writer +
                     " knownClients=" + std::to_string(NativeBridge::instance().known_client_count()) +
                     " epoch=" + std::to_string(m_session_epoch));
            m_hosted_server_cache_initialized = true;
            m_hosted_server_resync_in_flight = false;
            if (fields.count("revision"))
            {
                m_hosted_server_cache_revision = std::max<uint64_t>(
                    m_hosted_server_cache_revision,
                    static_cast<uint64_t>(safe_stoi(fields.at("revision"), static_cast<int>(m_hosted_server_cache_revision))));
            }
            log_line("[bridge-hosted] hosted_server_cache_updated revision=" + std::to_string(m_hosted_server_cache_revision) +
                     " records=" + std::to_string(m_labels.size()) +
                     " type=" + type +
                     " epoch=" + std::to_string(m_session_epoch));
        }
        std::string remote_pre_ready_ignore_reason{};
        std::string remote_pre_ready_authority_path{};
        if (should_ignore_remote_inbound_before_ready(type, &remote_pre_ready_ignore_reason, &remote_pre_ready_authority_path))
        {
            log_line("[bridge] remote_inbound_ignored_pre_ready type=" + type +
                     " authorityPath=" + remote_pre_ready_authority_path +
                     " reason=" + remote_pre_ready_ignore_reason +
                     " epoch=" + std::to_string(m_session_epoch) +
                     " worldId=" + m_world_folder_id);
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
                    if (!m_sidecar_authoritative)
                    {
                        queue_first_authoritative_render_pass("snapshot_end_complete", m_world_folder_id);
                        stop_remote_post_ready_resync("snapshot_end_complete", true);
                        log_line("[bridge] snapshot_applied_success type=snapshot_end_complete epoch=" +
                                 std::to_string(m_session_epoch) +
                                 " worldId=" + m_world_folder_id +
                                 " revision=" + std::to_string(m_revision));
                    }
                }
                else
                {
                    log_line("[bridge] snapshot_reconcile_deferred reason=incomplete_snapshot id=" + snapshot_id);
                    log_line("[bridge] snapshot_apply_failed reason=incomplete_snapshot id=" + snapshot_id +
                             " seen=" + std::to_string(m_bridge_snapshot_seen_keys.size()) +
                             " expected=" + std::to_string(m_bridge_snapshot_expected_count) +
                             " epoch=" + std::to_string(m_session_epoch));
                }
                return;
            }

            m_bridge_snapshot_received = true;
            m_bridge_snapshot_world_id = m_world_folder_id;
            mark_bridge_healthy("snapshot_end_legacy");
            reconcile_bridge_snapshot("snapshot_end_legacy");
            if (!m_sidecar_authoritative)
            {
                queue_first_authoritative_render_pass("snapshot_end_legacy", m_world_folder_id);
                stop_remote_post_ready_resync("snapshot_end_legacy", true);
                log_line("[bridge] snapshot_applied_success type=snapshot_end_legacy epoch=" +
                         std::to_string(m_session_epoch) +
                         " worldId=" + m_world_folder_id +
                         " revision=" + std::to_string(m_revision));
            }
            log_line("[bridge] snapshot_end revision=" + std::to_string(m_revision) +
                     " role=" + bridge_role_name(m_bridge_role) +
                     " legacy=true");
            return;
        }
        if (type == "snapshot_unavailable")
        {
            const auto unavailable_reason = unescape_json(fields.count("reason") ? fields.at("reason") : "unknown");
            log_line("[bridge] snapshot_unavailable reason=" + unavailable_reason +
                     " role=" + bridge_role_name(m_bridge_role) +
                     " worldId=" + m_world_folder_id);
            if (m_bridge_role == BridgeRole::RemoteClient && unavailable_reason == "snapshot_request_no_cache")
            {
                constexpr auto k_no_cache_backoff = std::chrono::seconds(20);
                const auto now_tp = std::chrono::steady_clock::now();
                const auto proposed_next = now_tp + k_no_cache_backoff;
                if (proposed_next > m_remote_snapshot_no_cache_backoff_until)
                {
                    m_remote_snapshot_no_cache_backoff_until = proposed_next;
                }
                if (m_bridge_next_snapshot_request < m_remote_snapshot_no_cache_backoff_until)
                {
                    m_bridge_next_snapshot_request = m_remote_snapshot_no_cache_backoff_until;
                }
                log_line("[bridge-hosted] remote_snapshot_backoff_applied reason=snapshot_request_no_cache" +
                         std::string(" nextRequestInSec=") +
                         std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                             m_remote_snapshot_no_cache_backoff_until - now_tp).count()) +
                         " epoch=" + std::to_string(m_session_epoch));
            }
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

        const auto now = std::chrono::steady_clock::now();
        if (m_bridge_route_lock_acquired)
        {
            return;
        }
        if (m_bridge_route_retry_consumed)
        {
            return;
        }

        if (!m_bridge_route_gate_open)
        {
            if (!m_bridge_route_gate_pending_logged)
            {
                m_bridge_route_gate_pending_logged = true;
                log_line("[bridge-route] route_lock_gate_pending reason=awaiting_coopconnectionverified");
            }
            m_bridge_route_probe_candidates = build_route_probe_candidates();
            return;
        }

        const auto try_lock_from_definitive_endpoint = [&](const std::string& reason_tag) -> bool {
            std::filesystem::path authority_log_path = m_session_window_log_path;
            if (authority_log_path.empty())
            {
                authority_log_path = m_bridge_route_log_path;
            }
            const uintmax_t authority_window_start =
                m_session_window_open ? m_session_window_start_offset : static_cast<uintmax_t>(0);
            const auto endpoint = try_latest_definitive_route_endpoint_from_log_window(authority_log_path, authority_window_start);
            if (!endpoint.has_value())
            {
                return false;
            }
            log_line("[bridge-route] definitive_endpoint_candidate host=" + endpoint->host +
                     " port=" + std::to_string(static_cast<unsigned int>(endpoint->port)) +
                     " source=" + endpoint->source +
                     " reason=" + reason_tag);
            return apply_bridge_remote_endpoint(
                endpoint->host,
                endpoint->port,
                "definitive_endpoint_" + endpoint->source + "_" + reason_tag);
        };

        auto handle_probe_exhausted = [&](const std::string& details) {
            m_bridge_route_probe_active = false;
            m_bridge_route_probe_waiting_ack = false;
            if (try_lock_from_definitive_endpoint(details))
            {
                return;
            }
            if (!m_session_ready_latched)
            {
                const bool should_log_wait =
                    m_bridge_route_wait_last_reason != details ||
                    m_bridge_route_wait_last_log.time_since_epoch().count() == 0 ||
                    (now - m_bridge_route_wait_last_log) >= std::chrono::seconds(3);
                if (should_log_wait)
                {
                    log_line("[bridge-route] route_lock_pending reason=awaiting_definitive_endpoint details=" + details);
                    m_bridge_route_wait_last_reason = details;
                    m_bridge_route_wait_last_log = now;
                }
                return;
            }

            if (!m_bridge_route_retry_consumed)
            {
                m_bridge_route_retry_consumed = true;
                log_line("[bridge-route] route_lock_failed_no_reachable_endpoint details=final_check_after_ready_latched");
            }
        };

        if (!m_bridge_route_probe_active)
        {
            m_bridge_route_probe_candidates = build_route_probe_candidates();
            m_bridge_route_probe_index = 0;
            m_bridge_route_probe_active = true;
            if (m_bridge_route_probe_candidates.empty())
            {
                handle_probe_exhausted("no_candidates_after_gate_open");
                return;
            }
            if (!start_next_route_probe(now))
            {
                handle_probe_exhausted("probe_send_failed_all_candidates");
            }
            return;
        }

        if (m_bridge_route_probe_waiting_ack && now >= m_bridge_route_probe_deadline)
        {
            log_line("[bridge-route] route_probe_result candidate=" + m_bridge_route_probe_host +
                     ":" + std::to_string(m_bridge_udp_port) + " result=timeout");
            m_bridge_route_probe_waiting_ack = false;
            ++m_bridge_route_probe_index;
            if (!start_next_route_probe(now))
            {
                handle_probe_exhausted("all_candidates_timed_out");
            }
        }
        return;

        if (m_bridge_route_lock_acquired && !m_bridge_route_force_non_loopback)
        {
            return;
        }
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

        std::filesystem::path authority_log_path = m_session_window_log_path;
        if (authority_log_path.empty())
        {
            authority_log_path = !discovered.log_path.empty() ? discovered.log_path : m_bridge_route_log_path;
        }
        const uintmax_t authority_window_start =
            m_session_window_open ? m_session_window_start_offset : static_cast<uintmax_t>(0);
        const auto authoritative_host =
            try_latest_remoteaddr_host_from_log_window(authority_log_path, authority_window_start);

        auto discovered_candidates = discovered.ordered_candidates;
        if (discovered_candidates.empty() && discovered.found && !discovered.host.empty())
        {
            discovered_candidates.push_back(discovered.host);
        }

        std::vector<std::string> viable_candidates{};
        viable_candidates.reserve(discovered_candidates.size() + 1);
        if (authoritative_host.has_value() && !authoritative_host->empty())
        {
            append_unique_ip(viable_candidates, *authoritative_host);
        }

        const auto local_hosts = parse_comma_separated_ips(discovered.local_host_summary);
        const bool public_fallback_window_valid =
            m_session_window_open ||
            authority_window_start > 0;

        // Fallback order:
        // 1) public endpoint candidates (srflx/public) in current epoch/window
        // 2) private LAN same-subnet candidates
        // 3) loopback with explicit local process evidence
        for (const auto& candidate : discovered_candidates)
        {
            if (candidate == "127.0.0.1")
            {
                continue;
            }
            if (!is_public_ipv4(candidate))
            {
                continue;
            }
            if (!public_fallback_window_valid)
            {
                const std::string reject_key = candidate + ":public_fallback_window_closed";
                if (m_bridge_route_rejected_candidates_logged.insert(reject_key).second)
                {
                    log_line("[bridge-route] candidate_rejected host=" + candidate +
                             " reason=public_fallback_window_closed");
                }
                continue;
            }
            append_unique_ip(viable_candidates, candidate);
        }

        for (const auto& candidate : discovered_candidates)
        {
            if (candidate == "127.0.0.1")
            {
                continue;
            }
            if (!is_private_ipv4(candidate))
            {
                continue;
            }
            if (!is_private_candidate_on_local_subnet(candidate, local_hosts))
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

        bool loopback_candidate_seen = false;
        for (const auto& candidate : discovered_candidates)
        {
            if (candidate == "127.0.0.1")
            {
                loopback_candidate_seen = true;
                break;
            }
        }
        if (!loopback_candidate_seen)
        {
            for (const auto& fallback : discovered.fallback_direct_candidates)
            {
                if (fallback.first == "127.0.0.1")
                {
                    loopback_candidate_seen = true;
                    break;
                }
            }
        }
        if (loopback_candidate_seen && has_local_windrose_and_server_process_evidence() && !m_bridge_route_force_non_loopback)
        {
            append_unique_ip(viable_candidates, "127.0.0.1");
        }
        else if (loopback_candidate_seen && m_bridge_route_rejected_candidates_logged.insert("127.0.0.1:missing_windrose_and_server_evidence").second)
        {
            log_line("[bridge-route] candidate_rejected host=127.0.0.1 reason=missing_local_windrose_and_server_process_evidence");
        }

        if (viable_candidates.empty())
        {
            const auto wait_reason = std::string{"no_viable_candidate|"} + summarize_ips(discovered_candidates) +
                                     "|" + (discovered.local_host_summary.empty() ? "none" : discovered.local_host_summary);
            const bool should_log_wait =
                m_bridge_route_wait_last_reason != wait_reason ||
                m_bridge_route_wait_last_log.time_since_epoch().count() == 0 ||
                (now - m_bridge_route_wait_last_log) >= std::chrono::seconds(15);
            if (should_log_wait)
            {
                log_line("[bridge-route] waiting reason=no_viable_candidate candidates=" + summarize_ips(discovered_candidates) +
                         " localHosts=" + (discovered.local_host_summary.empty() ? "none" : discovered.local_host_summary) +
                         " authoritativeRemoteAddr=" + (authoritative_host.has_value() ? *authoritative_host : std::string{"none"}));
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
            (void)apply_bridge_remote_endpoint(
                selected_host,
                static_cast<uint16_t>(m_bridge_udp_port),
                "legacy_discovery_first_viable_candidate");
            log_line("[bridge-route] route_lock_pending reason=probe_required_for_legacy_discovery host=" +
                     selected_host + ":" + std::to_string(m_bridge_udp_port));
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

        (void)apply_bridge_remote_endpoint(
            selected_host,
            static_cast<uint16_t>(m_bridge_udp_port),
            "legacy_discovery_selected_host");
        std::string selection_reason = "fallback_unknown";
        if (authoritative_host.has_value() && selected_host == *authoritative_host)
        {
            selection_reason = "authoritative_remoteaddr_endpoint";
        }
        else if (selected_host == "127.0.0.1")
        {
            selection_reason = "loopback_local_evidence";
        }
        else if (is_public_ipv4(selected_host))
        {
            selection_reason = "public_fallback";
        }
        else if (is_private_ipv4(selected_host))
        {
            selection_reason = "private_same_subnet";
        }
        log_line("[bridge-route] discovered host=" + m_bridge_remote_server_host +
                 " reason=" + selection_reason +
                 " port=" + std::to_string(m_bridge_udp_port) +
                 " logPath=" + (discovered.log_path.empty() ? "unknown" : discovered.log_path.string()) +
                 " remoteHostCandidate=" + (discovered.remote_host_candidate.empty() ? "none" : discovered.remote_host_candidate) +
                 " remotePublicCandidate=" + (discovered.remote_public_candidate.empty() ? "none" : discovered.remote_public_candidate) +
                 " localHosts=" + discovered.local_host_summary +
                 " authoritativeRemoteAddr=" + (authoritative_host.has_value() ? *authoritative_host : std::string{"none"}) +
                 " candidates=" + summarize_ips(viable_candidates));
        log_line("[bridge-route] route_lock_pending reason=probe_required_for_legacy_discovery host=" +
                 selected_host + ":" + std::to_string(m_bridge_udp_port));
    }

    auto SignTextMod::build_route_probe_candidates() -> std::vector<std::pair<std::string, std::string>>
    {
        std::vector<std::pair<std::string, std::string>> candidates{};
        const auto discovered = discover_bridge_route_from_r5_log(m_bridge_route_log_path);
        if (!discovered.log_path.empty())
        {
            m_bridge_route_log_path = discovered.log_path;
        }
        std::filesystem::path authority_log_path = m_session_window_log_path;
        if (authority_log_path.empty())
        {
            authority_log_path = !discovered.log_path.empty() ? discovered.log_path : m_bridge_route_log_path;
        }
        const uintmax_t authority_window_start =
            m_session_window_open ? m_session_window_start_offset : static_cast<uintmax_t>(0);
        const auto authoritative_host =
            try_latest_remoteaddr_host_from_log_window(authority_log_path, authority_window_start);

        auto add_candidate = [&](const std::string& host, const std::string& source) {
            if (!parse_ipv4_octets(host).has_value())
            {
                return;
            }
            for (const auto& existing : candidates)
            {
                if (existing.first == host)
                {
                    return;
                }
            }
            candidates.emplace_back(host, source);
        };

        // Any externally provided endpoint (hosted endpoint file, hello/resync relay, definitive endpoint)
        // is staged first and must still pass route probe before lock commit.
        if (m_bridge_route_staged_active && !m_bridge_route_staged_host.empty())
        {
            add_candidate(m_bridge_route_staged_host, "staged:" + m_bridge_route_staged_source);
        }

        // Same-machine loopback first only with explicit evidence.
        if (discovered.same_machine_evidence && has_local_windrose_and_server_process_evidence())
        {
            add_candidate("127.0.0.1", "loopback_same_machine");
        }

        std::vector<std::string> ordered = discovered.ordered_candidates;
        if (authoritative_host.has_value() && !authoritative_host->empty())
        {
            ordered.insert(ordered.begin(), *authoritative_host);
        }

        // Private first, then public, then fallback loopback.
        for (const auto& host : ordered)
        {
            if (host == "127.0.0.1")
            {
                continue;
            }
            if (is_private_ipv4(host))
            {
                add_candidate(host, "private");
            }
        }
        for (const auto& host : ordered)
        {
            if (host == "127.0.0.1")
            {
                continue;
            }
            if (is_public_ipv4(host))
            {
                add_candidate(host, "public");
            }
        }
        if (candidates.empty() && discovered.same_machine_evidence && has_local_windrose_and_server_process_evidence())
        {
            add_candidate("127.0.0.1", "loopback_fallback");
        }
        return candidates;
    }

    auto SignTextMod::start_next_route_probe(const std::chrono::steady_clock::time_point now) -> bool
    {
        if (m_bridge_route_probe_index >= m_bridge_route_probe_candidates.size())
        {
            return false;
        }

        const auto& next = m_bridge_route_probe_candidates[m_bridge_route_probe_index];
        const auto& host = next.first;
        const auto& source = next.second;
        m_bridge_route_probe_host = host;
        m_bridge_route_probe_source = source;
        m_bridge_route_probe_token = std::to_string(m_session_epoch) + "-" +
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()) +
            "-" + std::to_string(static_cast<unsigned long long>(GetCurrentProcessId()));

        m_bridge_remote_server_host = host;
        NativeBridge::instance().set_remote_server(m_bridge_remote_server_host, static_cast<uint16_t>(m_bridge_udp_port));

        std::ostringstream payload{};
        payload << "{\"mod\":\"WindroseTextSigns\",\"schema\":\"bridge.v1\",\"type\":\"route_probe\""
                << ",\"session\":\"" << escape_json(m_session_id) << "\""
                << ",\"worldId\":\"" << escape_json(m_world_folder_id) << "\""
                << ",\"token\":\"" << escape_json(m_bridge_route_probe_token) << "\""
                << ",\"probeHost\":\"" << escape_json(host) << "\""
                << ",\"probeSource\":\"" << escape_json(source) << "\""
                << ",\"epoch\":" << m_session_epoch
                << "}";
        const bool sent = NativeBridge::instance().send_to_server(payload.str());
        m_bridge_route_probe_waiting_ack = sent;
        m_bridge_route_probe_deadline = now + std::chrono::milliseconds(700);
        log_line("[bridge-route] route_probe_start epoch=" + std::to_string(m_session_epoch) +
                 " candidate=" + host + ":" + std::to_string(m_bridge_udp_port) +
                 " class=" + source +
                 " sent=" + std::string{sent ? "true" : "false"});
        if (!sent)
        {
            log_line("[bridge-route] route_probe_result candidate=" + host + ":" + std::to_string(m_bridge_udp_port) +
                     " result=send_failed");
            m_bridge_route_probe_waiting_ack = false;
            ++m_bridge_route_probe_index;
            return start_next_route_probe(now);
        }
        return true;
    }

    auto SignTextMod::commit_locked_route_from_probe(const std::string& host, const std::string& source) -> void
    {
        m_bridge_remote_server_host = host;
        NativeBridge::instance().set_remote_server(
            m_bridge_remote_server_host,
            static_cast<uint16_t>(m_bridge_udp_port));
        m_bridge_route_lock_acquired = true;
        m_bridge_route_locked_host = host;
        m_bridge_route_staged_active = false;
        m_bridge_route_staged_host.clear();
        m_bridge_route_staged_source.clear();
        m_bridge_route_loopback_same_machine_ok = (host == "127.0.0.1");
        m_bridge_route_force_non_loopback = false;
        m_bridge_route_recovery_logged = false;
        m_bridge_route_wait_last_reason.clear();
        m_bridge_route_wait_last_log = {};
        m_bridge_route_probe_active = false;
        m_bridge_route_probe_waiting_ack = false;
        m_bridge_next_snapshot_request = std::chrono::steady_clock::now();
        m_bridge_snapshot_received = false;
        m_bridge_snapshot_retry_attempts = 0;
        m_bridge_sync_wait_started = std::chrono::steady_clock::now();
        log_line("[bridge-route] route_lock_committed host=" + host +
                 " port=" + std::to_string(m_bridge_udp_port) +
                 " reason=first_probe_success class=" + source);
        mark_phase7_status_dirty("route_lock_committed");
        trace_behavior_sm("route_lock_acquired",
                          "host=" + host + " reason=probe_success class=" + source);
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
        m_remote_snapshot_no_cache_backoff_until = {};
        if (was_unhealthy)
        {
            log_line("[bridge-health] recovered reason=" + reason +
                     " worldId=" + m_world_folder_id +
                     " pending=" + std::to_string(m_bridge_pending_request_keys.size()));
        }
        mark_phase7_status_dirty("bridge_recovered");
    }

    auto SignTextMod::reset_bridge_snapshot_payload_state(const std::string& reason) -> void
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
        m_hosted_authority_route_active = false;
        m_hosted_server_authority_route_configured = false;
        m_hosted_server_cache_initialized = false;
        m_hosted_server_cache_revision = 0;
        m_remote_delta_applied_state.clear();
        m_hosted_server_next_hello = {};
        m_hosted_server_next_resync_request = {};
        m_hosted_server_last_resync_request_sent = {};
        m_hosted_server_resync_in_flight = false;
        m_hosted_server_snapshot_unavailable_last_by_session.clear();
        m_snapshot_requester_state_by_session.clear();
        m_remote_snapshot_no_cache_backoff_until = {};
        m_remote_post_ready_resync_active = false;
        m_remote_post_ready_resync_in_flight = false;
        m_remote_post_ready_resync_bootstrap_done = false;
        m_remote_post_ready_resync_attempts = 0;
        m_remote_post_ready_resync_started = {};
        m_remote_post_ready_resync_next_due = {};
        m_remote_post_ready_resync_last_send = {};
        m_remote_post_ready_resync_last_reason.clear();
        m_hosted_server_endpoint_advertised = false;
        m_hosted_server_endpoint_host.clear();
        m_hosted_server_endpoint_port = 0;
        m_hosted_server_endpoint_last_read = {};
    }

    auto SignTextMod::reset_bridge_snapshot_state(const std::string& reason) -> void
    {
        reset_bridge_snapshot_payload_state(reason);
        reset_route_probe_state("reset_bridge_snapshot_state");
    }

    auto SignTextMod::reset_route_probe_state(const std::string& reason) -> void
    {
        (void)reason;
        m_bridge_route_gate_open = false;
        m_bridge_route_gate_pending_logged = false;
        m_bridge_route_probe_active = false;
        m_bridge_route_probe_waiting_ack = false;
        m_bridge_route_probe_candidates.clear();
        m_bridge_route_probe_index = 0;
        m_bridge_route_probe_token.clear();
        m_bridge_route_probe_host.clear();
        m_bridge_route_probe_source.clear();
        m_bridge_route_staged_active = false;
        m_bridge_route_staged_host.clear();
        m_bridge_route_staged_source.clear();
        m_bridge_route_probe_deadline = {};
    }

    auto SignTextMod::reset_server_role_classification_state(const std::string& reason) -> void
    {
        (void)reason;
        m_server_role_classification_pending = false;
        m_server_role_signal_executable_seen = false;
        m_server_role_signal_hosted_ini_seen = false;
        m_server_role_signal_host_ready_seen = false;
        m_server_role_window_start_offset = 0;
        m_server_role_window_end_offset = 0;
        m_server_role_log_path.clear();
        m_server_role_classification_started = {};
        m_server_role_pending_last_log = {};
    }

    auto SignTextMod::maybe_begin_server_role_classification(
        const std::filesystem::path& log_path,
        const uintmax_t window_start_offset) -> void
    {
        if (!is_dedicated_runtime_process() || m_role_lock_acquired)
        {
            return;
        }
        if (!m_server_role_classification_pending)
        {
            m_server_role_classification_pending = true;
            m_server_role_signal_executable_seen = false;
            m_server_role_signal_hosted_ini_seen = false;
            m_server_role_signal_host_ready_seen = false;
            m_server_role_classification_started = std::chrono::steady_clock::now();
            m_server_role_window_start_offset = window_start_offset;
            m_server_role_window_end_offset = window_start_offset;
            m_server_role_log_path = log_path;
            log_line("[role] server_role_classification_pending epoch=" + std::to_string(m_session_epoch) +
                     " runtimeRole=" + m_runtime_role +
                     " bridgeRole=" + bridge_role_name(m_bridge_role) +
                     " authorityMode=" + m_authority_mode +
                     " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                     " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                     " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                     " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                     " signal=classification_window_opened");
        }
        else if (!log_path.empty() && m_server_role_log_path.empty())
        {
            m_server_role_log_path = log_path;
        }
    }

    auto SignTextMod::maybe_observe_server_role_signal(
        const std::string& line_lower,
        const uintmax_t line_window_start,
        const uintmax_t line_window_end) -> void
    {
        if (!m_server_role_classification_pending || m_role_lock_acquired)
        {
            return;
        }

        m_server_role_window_end_offset = line_window_end;
        if (line_lower.find("executablename: windroseserver-win64-shipping.exe") != std::string::npos)
        {
            m_server_role_signal_executable_seen = true;
            m_server_role_window_start_offset = line_window_start;
            log_line("[role] server_role_classification_pending epoch=" + std::to_string(m_session_epoch) +
                     " runtimeRole=" + m_runtime_role +
                     " bridgeRole=" + bridge_role_name(m_bridge_role) +
                     " authorityMode=" + m_authority_mode +
                     " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                     " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                     " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                     " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                     " signal=server_executable");
            return;
        }

        if (m_server_role_signal_executable_seen &&
            line_lower.find("-ini:game:[/script/r5datakeepers.r5datakeeper_settings]") != std::string::npos)
        {
            m_server_role_signal_hosted_ini_seen = true;
            log_line("[role] server_role_classification_pending epoch=" + std::to_string(m_session_epoch) +
                     " runtimeRole=" + m_runtime_role +
                     " bridgeRole=" + bridge_role_name(m_bridge_role) +
                     " authorityMode=" + m_authority_mode +
                     " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                     " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                     " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                     " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                     " signal=hosted_marker_ini");
            return;
        }

        if (m_server_role_signal_executable_seen &&
            line_lower.find("host server is ready for owner to connect") != std::string::npos)
        {
            m_server_role_signal_host_ready_seen = true;
            log_line("[role] server_role_classification_pending epoch=" + std::to_string(m_session_epoch) +
                     " runtimeRole=" + m_runtime_role +
                     " bridgeRole=" + bridge_role_name(m_bridge_role) +
                     " authorityMode=" + m_authority_mode +
                     " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                     " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                     " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                     " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                     " signal=host_ready");
        }
    }

    auto SignTextMod::apply_server_role_classification(const bool hosted_server_relay, const std::string& reason) -> void
    {
        const auto cwd = std::filesystem::current_path();
        const auto profile_roots = collect_dedicated_save_profile_roots(cwd, m_mod_root);
        std::filesystem::path profile_root{};
        if (!profile_roots.empty())
        {
            profile_root = profile_roots.front();
        }
        else if (!m_save_profile_root.empty() && std::filesystem::exists(m_save_profile_root))
        {
            profile_root = std::filesystem::path{m_save_profile_root};
        }
        std::string world_id = is_hex_world_id(m_world_folder_id) ? m_world_folder_id : std::string{"unknown-world"};
        if (!is_hex_world_id(world_id) && is_hex_world_id(m_worldid_latched_id))
        {
            world_id = m_worldid_latched_id;
        }
        if (!is_hex_world_id(world_id) && !profile_root.empty())
        {
            if (auto chosen = choose_world_id_for_profile(profile_root); chosen.has_value())
            {
                world_id = *chosen;
            }
        }
        if (!is_hex_world_id(world_id))
        {
            world_id = "unknown-world";
        }

        std::filesystem::path data_root{};
        if (!profile_root.empty())
        {
            data_root = hosted_server_relay
                ? profile_root / "WindroseTextSigns" / "HostedRelayCache" / world_id
                : profile_root / "WindroseTextSigns" / world_id;
        }
        else
        {
            data_root = hosted_server_relay
                ? m_mod_root / "Cache" / "HostedRelayCache" / world_id
                : m_mod_root / "Cache" / "DedicatedServer" / world_id;
        }

        if (hosted_server_relay)
        {
            set_sidecar_route(
                data_root,
                "HostedServer",
                profile_root.empty() ? "HostedServerRelayCacheFallbackModRoot" : "HostedServerRelayCache",
                "HostedServerRelayNonAuthoritative",
                profile_root.empty() ? "relay-cache-fallback" : "relay-cache",
                false,
                profile_root,
                world_id,
                reason);
        }
        else
        {
            set_sidecar_route(
                data_root,
                "DedicatedServer",
                profile_root.empty() ? "ServerAuthoritativeFallbackModRoot" : "ServerAuthoritative",
                "DedicatedServerAuthoritative",
                profile_root.empty() ? "authoritative-fallback-modroot" : "authoritative",
                true,
                profile_root,
                world_id,
                reason);
        }
    }

    auto SignTextMod::maybe_commit_server_role_classification(
        const std::chrono::steady_clock::time_point now,
        const std::string& reason) -> void
    {
        (void)reason;
        if (!is_dedicated_runtime_process() || m_role_lock_acquired || !m_server_role_classification_pending)
        {
            return;
        }

        const bool hosted_chain_complete =
            m_server_role_signal_executable_seen &&
            m_server_role_signal_hosted_ini_seen &&
            m_server_role_signal_host_ready_seen;
        const bool dedicated_ready =
            m_server_role_signal_executable_seen &&
            m_server_role_signal_host_ready_seen &&
            !m_server_role_signal_hosted_ini_seen;

        if (hosted_chain_complete)
        {
            apply_server_role_classification(true, "server_role_classification_hosted");
            if (!m_session_window_open || !m_definitive_session_start_seen)
            {
                reset_session_state("definitive_session_start");
                open_session_window("hosted_server_role_chain_complete", m_server_role_log_path, m_server_role_window_end_offset);
            }
            configure_bridge_role("server_role_classification_hosted");
            maybe_acquire_role_lock(now, "server_role_classification_hosted");
            log_line("[role] server_role_classification_hosted epoch=" + std::to_string(m_session_epoch) +
                     " runtimeRole=" + m_runtime_role +
                     " bridgeRole=" + bridge_role_name(m_bridge_role) +
                     " authorityMode=" + m_authority_mode +
                     " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                     " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                     " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                     " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                     " signal=hosted_chain_complete");
            if (m_role_lock_acquired)
            {
                log_line("[role] server_role_lock_committed epoch=" + std::to_string(m_session_epoch) +
                         " runtimeRole=" + m_runtime_role +
                         " bridgeRole=" + bridge_role_name(m_bridge_role) +
                         " authorityMode=" + m_authority_mode +
                         " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                         " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                         " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                         " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                         " signal=role_lock_committed");
            }
            reset_server_role_classification_state("hosted_committed");
            return;
        }

        if (dedicated_ready)
        {
            apply_server_role_classification(false, "server_role_classification_dedicated");
            if (!m_session_window_open || !m_definitive_session_start_seen)
            {
                reset_session_state("definitive_session_start");
                open_session_window("dedicated_server_role_chain_complete", m_server_role_log_path, m_server_role_window_end_offset);
            }
            configure_bridge_role("server_role_classification_dedicated");
            maybe_acquire_role_lock(now, "server_role_classification_dedicated");
            log_line("[role] server_role_classification_dedicated epoch=" + std::to_string(m_session_epoch) +
                     " runtimeRole=" + m_runtime_role +
                     " bridgeRole=" + bridge_role_name(m_bridge_role) +
                     " authorityMode=" + m_authority_mode +
                     " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                     " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                     " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                     " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                     " signal=dedicated_window_elapsed");
            if (m_role_lock_acquired)
            {
                log_line("[role] server_role_lock_committed epoch=" + std::to_string(m_session_epoch) +
                         " runtimeRole=" + m_runtime_role +
                         " bridgeRole=" + bridge_role_name(m_bridge_role) +
                         " authorityMode=" + m_authority_mode +
                         " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                         " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                         " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                         " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                         " signal=role_lock_committed");
            }
            reset_server_role_classification_state("dedicated_committed");
            return;
        }

        if (m_server_role_pending_last_log.time_since_epoch().count() == 0 ||
            (now - m_server_role_pending_last_log) >= std::chrono::seconds(2))
        {
            std::string signal = "awaiting_server_executable";
            if (m_server_role_signal_executable_seen && !m_server_role_signal_host_ready_seen)
            {
                signal = "awaiting_host_ready";
            }
            else if (m_server_role_signal_executable_seen && m_server_role_signal_host_ready_seen && !m_server_role_signal_hosted_ini_seen)
            {
                signal = "awaiting_chain_resolution";
            }
            log_line("[role] server_role_lock_skipped_pending_evidence epoch=" + std::to_string(m_session_epoch) +
                     " runtimeRole=" + m_runtime_role +
                     " bridgeRole=" + bridge_role_name(m_bridge_role) +
                     " authorityMode=" + m_authority_mode +
                     " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                     " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                     " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                     " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                     " signal=" + signal);
            m_server_role_pending_last_log = now;
        }
    }

    auto SignTextMod::configure_bridge_role(const std::string& reason) -> void
    {
        const auto now = std::chrono::steady_clock::now();
        const bool session_reset_reason = reason.rfind("session_reset_", 0) == 0;
        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        BridgeRole desired = BridgeRole::Unknown;
        const bool server_pending_classification =
            (runtime_role_lower == "serverrolepending" ||
             lower_ascii(m_authority_mode) == "serverroleclassificationpending");
        // Resolve bridge role from resolved runtime role first.
        if (runtime_role_lower == "hostedserver")
        {
            desired = BridgeRole::DedicatedServer;
        }
        else if (runtime_role_lower == "dedicatedserver")
        {
            desired = BridgeRole::DedicatedServer;
        }
        else if (server_pending_classification)
        {
            if (m_server_role_pending_last_log.time_since_epoch().count() == 0 ||
                (now - m_server_role_pending_last_log) >= std::chrono::seconds(2))
            {
                log_line("[role] server_role_lock_skipped_pending_evidence epoch=" + std::to_string(m_session_epoch) +
                         " runtimeRole=" + m_runtime_role +
                         " bridgeRole=" + bridge_role_name(m_bridge_role) +
                         " authorityMode=" + m_authority_mode +
                         " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                         " logPath=" + (m_server_role_log_path.empty() ? "unknown" : m_server_role_log_path.string()) +
                         " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_start_offset)) +
                         " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_server_role_window_end_offset)) +
                         " signal=server_classification_pending_bridge_deferred");
                m_server_role_pending_last_log = now;
            }
        }
        else if (m_sidecar_authoritative && runtime_role_lower == "localclient")
        {
            // LocalClient (solo or hosted authority) is send-only for bridge traffic.
            // Do not bind/listen the relay UDP server socket from LocalClient.
            desired = BridgeRole::RemoteClient;
        }
        else if (runtime_role_lower == "remoteclient" || runtime_role_lower.find("remote") != std::string::npos)
        {
            desired = BridgeRole::RemoteClient;
        }

        if (m_role_lock_acquired && desired != m_bridge_role)
        {
            const auto desired_name = bridge_role_name(desired);
            const bool can_reassert_locked_role =
                desired != BridgeRole::Unknown &&
                m_bridge_role == BridgeRole::Unknown &&
                !m_role_lock_bridge_role.empty() &&
                lower_ascii(m_role_lock_bridge_role) == lower_ascii(desired_name);
            if (can_reassert_locked_role)
            {
                m_bridge_role = desired;
                NativeBridge::instance().set_role(desired);
                log_line("[role] bridge_role_reasserted reason=" + reason +
                         " role=" + desired_name +
                         " runtimeRole=" + m_runtime_role);
                return;
            }
            log_line("[role] lock_held skip_bridge_role_change reason=" + reason +
                     " lockedBridgeRole=" + bridge_role_name(m_bridge_role) +
                     " requestedBridgeRole=" + desired_name);
            return;
        }

        if (desired == m_bridge_role)
        {
            maybe_acquire_role_lock(now, "role_stable_" + reason);
            return;
        }

        if (!session_reset_reason &&
            m_role_lock_acquired &&
            m_bridge_role == BridgeRole::RemoteClient &&
            desired == BridgeRole::RemoteClient)
        {
            log_line("[bridge] bridge_role_change_preserve_route_state reason=" + reason +
                     " oldRole=" + bridge_role_name(m_bridge_role) +
                     " newRole=" + bridge_role_name(desired) +
                     " lock=true");
            m_bridge_role = desired;
            NativeBridge::instance().set_role(desired);
            maybe_acquire_role_lock(now, "role_preserve_" + reason);
            return;
        }

        const bool should_bind_server_socket =
            desired == BridgeRole::DedicatedServer || desired == BridgeRole::ListenHost;
        log_line("[bridge] bridge_bind_policy reason=" + reason +
                 " runtimeRole=" + m_runtime_role +
                 " desiredBridgeRole=" + bridge_role_name(desired) +
                 " shouldBindServer=" + std::string{should_bind_server_socket ? "true" : "false"} +
                 " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"});

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
        mark_phase7_status_dirty("bridge_role_changed");
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
        if (desired != BridgeRole::DedicatedServer)
        {
            m_bridge_bind_success_logged = false;
            m_bridge_bind_failed_logged = false;
        }
        m_bridge_next_snapshot_request = std::chrono::steady_clock::now();
        log_line("[bridge] role_set reason=" + reason +
                 " role=" + bridge_role_name(m_bridge_role) +
                 " runtimeRole=" + m_runtime_role +
                 " authorityMode=" + m_authority_mode +
                 " sidecarKind=" + m_sidecar_kind +
                 " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"});
        if (m_bridge_role == BridgeRole::DedicatedServer)
        {
            const auto exe_name = current_executable_path().filename().string();
            const auto pid = static_cast<unsigned long long>(GetCurrentProcessId());
            const auto bound_port = NativeBridge::instance().server_bound_port();
            const auto bind_ok = NativeBridge::instance().server_socket_open();
            if (bind_ok && !m_bridge_bind_success_logged)
            {
                m_bridge_bind_success_logged = true;
                m_bridge_bind_failed_logged = false;
                log_line("[bridge] bridge_bind_success process=" + (exe_name.empty() ? "unknown" : exe_name) +
                         " pid=" + std::to_string(pid) +
                         " port=" + std::to_string(static_cast<unsigned int>(bound_port == 0 ? m_bridge_udp_port : bound_port)) +
                         " role=" + bridge_role_name(m_bridge_role) +
                         " runtimeRole=" + m_runtime_role);
            }
            else if (!bind_ok && !m_bridge_bind_failed_logged)
            {
                m_bridge_bind_failed_logged = true;
                log_line("[bridge] bridge_bind_failed process=" + (exe_name.empty() ? "unknown" : exe_name) +
                         " pid=" + std::to_string(pid) +
                         " port=" + std::to_string(static_cast<unsigned int>(m_bridge_udp_port)) +
                         " error=" + std::to_string(NativeBridge::instance().server_last_bind_error()) +
                         " role=" + bridge_role_name(m_bridge_role) +
                         " runtimeRole=" + m_runtime_role);
            }
        }
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
        m_role_lock_start_signal.clear();
        m_bridge_route_lock_acquired = false;
        m_bridge_route_locked_host.clear();
        m_bridge_route_last_candidates.clear();
        m_bridge_route_last_discovered_host.clear();
        m_bridge_route_log_path.clear();
        m_bridge_route_next_check = {};
        m_bridge_route_loopback_same_machine_ok = false;
        m_bridge_route_rejected_candidates_logged.clear();
        m_bridge_route_fallback_candidates_logged.clear();
        m_bridge_route_bootstrap_pause_logged = false;
        m_bridge_route_wait_last_log = {};
        m_bridge_route_wait_last_reason.clear();
        m_bridge_route_force_non_loopback = false;
        m_bridge_route_recovery_logged = false;
        m_bridge_route_retry_consumed = false;
        m_bridge_bind_success_logged = false;
        m_bridge_bind_failed_logged = false;
        reset_route_probe_state("reset_role_route_locks");
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
        mark_phase7_status_dirty("role_route_locks_reset");
    }

    auto SignTextMod::is_remoteclient_ready_and_world_bound() const -> bool
    {
        return m_bridge_role == BridgeRole::RemoteClient &&
            !m_sidecar_authoritative &&
            m_session_ready_latched &&
            is_worldid_bound_for_current_epoch();
    }

    auto SignTextMod::should_ignore_remote_inbound_before_ready(
        const std::string& type,
        std::string* out_reason,
        std::string* out_authority_path) const -> bool
    {
        if (out_reason)
        {
            out_reason->clear();
        }
        if (out_authority_path)
        {
            const auto authority_mode_lower = lower_ascii(m_authority_mode);
            *out_authority_path = authority_mode_lower.find("hosted") != std::string::npos
                ? "hosted"
                : "dedicated";
        }
        if (m_bridge_role != BridgeRole::RemoteClient || m_sidecar_authoritative)
        {
            return false;
        }
        if (type != "upsert" && type != "clear" && type != "snapshot_end")
        {
            return false;
        }
        if (m_session_ready_latched && is_worldid_bound_for_current_epoch())
        {
            return false;
        }
        if (out_reason)
        {
            if (!m_session_ready_latched)
            {
                *out_reason = "session_ready_not_latched";
            }
            else if (!is_worldid_bound_for_current_epoch())
            {
                *out_reason = "world_not_bound";
            }
            else
            {
                *out_reason = "not_ready";
            }
        }
        return true;
    }

    auto SignTextMod::start_remote_post_ready_resync(const std::string& reason) -> void
    {
        if (m_bridge_role != BridgeRole::RemoteClient || m_sidecar_authoritative)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        m_remote_post_ready_resync_active = true;
        m_remote_post_ready_resync_in_flight = false;
        m_remote_post_ready_resync_attempts = 0;
        m_remote_post_ready_resync_started = now;
        m_remote_post_ready_resync_next_due = now;
        m_remote_post_ready_resync_last_send = {};
        m_remote_post_ready_resync_last_reason = reason;
        m_remote_post_ready_resync_bootstrap_done = true;
        m_bridge_snapshot_received = false;
        m_bridge_snapshot_world_id.clear();
        log_line("[bridge] resync_start epoch=" + std::to_string(m_session_epoch) +
                 " role=" + bridge_role_name(m_bridge_role) +
                 " authority=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                 " worldId=" + m_world_folder_id +
                 " reason=" + reason);
    }

    auto SignTextMod::stop_remote_post_ready_resync(const std::string& reason, const bool success) -> void
    {
        if (!m_remote_post_ready_resync_active)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (success)
        {
            const auto elapsed_ms = m_remote_post_ready_resync_started.time_since_epoch().count() == 0
                ? static_cast<long long>(0)
                : std::chrono::duration_cast<std::chrono::milliseconds>(now - m_remote_post_ready_resync_started).count();
            log_line("[bridge] resync_success epoch=" + std::to_string(m_session_epoch) +
                     " role=" + bridge_role_name(m_bridge_role) +
                     " worldId=" + m_world_folder_id +
                     " attempts=" + std::to_string(m_remote_post_ready_resync_attempts) +
                     " elapsedMs=" + std::to_string(elapsed_ms) +
                     " reason=" + reason);
        }
        else
        {
            log_line("[bridge] resync_cancel_on_session_exit epoch=" + std::to_string(m_session_epoch) +
                     " role=" + bridge_role_name(m_bridge_role) +
                     " worldId=" + m_world_folder_id +
                     " attempts=" + std::to_string(m_remote_post_ready_resync_attempts) +
                     " reason=" + reason);
        }
        m_remote_post_ready_resync_active = false;
        m_remote_post_ready_resync_in_flight = false;
        m_remote_post_ready_resync_attempts = 0;
        m_remote_post_ready_resync_started = {};
        m_remote_post_ready_resync_next_due = {};
        m_remote_post_ready_resync_last_send = {};
        m_remote_post_ready_resync_last_reason.clear();
    }

    auto SignTextMod::tick_remote_post_ready_resync(const std::chrono::steady_clock::time_point now) -> void
    {
        if (!m_remote_post_ready_resync_active)
        {
            return;
        }
        if (!is_remoteclient_ready_and_world_bound())
        {
            return;
        }
        const bool snapshot_current_world =
            m_bridge_snapshot_received &&
            (!is_hex_world_id(m_world_folder_id) || m_bridge_snapshot_world_id == m_world_folder_id);
        if (snapshot_current_world)
        {
            stop_remote_post_ready_resync("snapshot_applied_success", true);
            return;
        }
        if (m_remote_post_ready_resync_in_flight && now < m_remote_post_ready_resync_next_due)
        {
            return;
        }
        if (m_remote_post_ready_resync_in_flight && now >= m_remote_post_ready_resync_next_due)
        {
            m_remote_post_ready_resync_in_flight = false;
        }
        if (!has_viable_remote_route_for_snapshot())
        {
            m_remote_post_ready_resync_next_due = now + std::chrono::seconds(15);
            return;
        }

        const uint32_t next_attempt = m_remote_post_ready_resync_attempts + 1;
        std::string request_reason{"remote_post_ready_resync"};
        std::string retry_log_tag{"resync_retry_long"};
        auto retry_delay = std::chrono::seconds(15);
        if (next_attempt == 1)
        {
            request_reason = "remote_post_ready_resync_initial";
            retry_log_tag = "resync_start";
            retry_delay = std::chrono::seconds(1);
        }
        else if (next_attempt == 2 || next_attempt == 3)
        {
            request_reason = "remote_post_ready_resync_retry_short";
            retry_log_tag = "resync_retry_short";
            retry_delay = (next_attempt == 2) ? std::chrono::seconds(3) : std::chrono::seconds(15);
        }
        send_bridge_snapshot_request(request_reason);
        ++m_remote_post_ready_resync_attempts;
        m_remote_post_ready_resync_in_flight = true;
        m_remote_post_ready_resync_last_send = now;
        m_remote_post_ready_resync_next_due = now + retry_delay;
        log_line("[bridge] " + retry_log_tag +
                 " epoch=" + std::to_string(m_session_epoch) +
                 " role=" + bridge_role_name(m_bridge_role) +
                 " worldId=" + m_world_folder_id +
                 " attempt=" + std::to_string(next_attempt) +
                 " nextRetrySec=" + std::to_string(static_cast<long long>(retry_delay.count())));
    }

    auto SignTextMod::maybe_acquire_role_lock(
        const std::chrono::steady_clock::time_point now,
        const std::string& reason) -> void
    {
        (void)now;
        if (m_role_lock_acquired)
        {
            return;
        }
        if (!m_session_window_open || !m_definitive_session_start_seen)
        {
            return;
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const bool runtime_stable =
            runtime_role_lower == "localclient" ||
            runtime_role_lower == "remoteclient" ||
            runtime_role_lower == "dedicatedserver" ||
            runtime_role_lower == "hostedserver";
        const bool bridge_stable =
            m_bridge_role == BridgeRole::DedicatedServer ||
            m_bridge_role == BridgeRole::ListenHost ||
            m_bridge_role == BridgeRole::RemoteClient;
        if (!runtime_stable || !bridge_stable)
        {
            return;
        }

        m_role_lock_acquired = true;
        m_role_lock_runtime_role = m_runtime_role;
        m_role_lock_bridge_role = bridge_role_name(m_bridge_role);
        m_role_lock_world_id = m_world_folder_id;
        m_role_lock_start_signal = m_definitive_session_start_signal;
        log_line("[role] lock_acquired runtimeRole=" + m_role_lock_runtime_role +
                 " bridgeRole=" + m_role_lock_bridge_role +
                 " worldId=" + (m_role_lock_world_id.empty() ? "unknown" : m_role_lock_world_id) +
                 " reason=" + reason);
        mark_phase7_status_dirty("role_lock_acquired");
        trace_behavior_sm("role_lock_acquired",
                          "runtimeRole=" + m_role_lock_runtime_role +
                          " bridgeRole=" + m_role_lock_bridge_role +
                          " worldId=" + (m_role_lock_world_id.empty() ? "unknown" : m_role_lock_world_id) +
                          " reason=" + reason);
        schedule_localclient_role_lock_restore_passes("role_lock_acquired");
    }

    auto SignTextMod::update_bridge_health(const std::chrono::steady_clock::time_point now) -> void
    {
        const bool prior_unhealthy = m_bridge_health_unhealthy;
        if (m_bridge_role != BridgeRole::RemoteClient || m_sidecar_authoritative)
        {
            m_bridge_health_unhealthy = false;
            m_bridge_health_warning_logged = false;
            m_bridge_snapshot_retry_attempts = 0;
            m_bridge_sync_wait_started = now;
            if (prior_unhealthy != m_bridge_health_unhealthy)
            {
                mark_phase7_status_dirty("bridge_health_scope_clear");
            }
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
            if (prior_unhealthy != m_bridge_health_unhealthy)
            {
                mark_phase7_status_dirty("bridge_health_route_missing");
            }
            return;
        }

        if (snapshot_current_world)
        {
            m_bridge_health_unhealthy = false;
            m_bridge_health_warning_logged = false;
            m_bridge_snapshot_retry_attempts = 0;
            m_bridge_sync_wait_started = now;
            if (prior_unhealthy != m_bridge_health_unhealthy)
            {
                mark_phase7_status_dirty("bridge_health_snapshot_current_world");
            }
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
        if (prior_unhealthy != m_bridge_health_unhealthy)
        {
            mark_phase7_status_dirty("bridge_health_degraded");
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
        configure_bridge_role("tick");
        if (is_hosted_client_authority_context())
        {
            (void)consume_hosted_server_endpoint("tick");
        }
        tick_bridge_upnp();
        tick_bridge_route_discovery();
        const auto now = std::chrono::steady_clock::now();
        if (is_hosted_server_relay_context())
        {
            if (now >= m_hosted_server_next_hello)
            {
                (void)send_hosted_server_control_message("hosted_server_hello", "hosted_server_tick_hello");
                m_hosted_server_next_hello = now + (m_hosted_server_cache_initialized ? std::chrono::seconds(120) : std::chrono::seconds(30));
            }
            if (!m_hosted_server_cache_initialized &&
                m_hosted_server_resync_in_flight &&
                m_hosted_server_last_resync_request_sent.time_since_epoch().count() != 0 &&
                (now - m_hosted_server_last_resync_request_sent) >= std::chrono::seconds(45))
            {
                m_hosted_server_resync_in_flight = false;
                log_line("[bridge-hosted] hosted_server_resync_request_timeout epoch=" + std::to_string(m_session_epoch));
            }
            if (now >= m_hosted_server_next_resync_request &&
                !m_hosted_server_cache_initialized &&
                !m_hosted_server_resync_in_flight)
            {
                const bool sent = send_hosted_server_control_message("hosted_server_resync_request", "hosted_server_periodic_resync");
                if (sent)
                {
                    m_hosted_server_resync_in_flight = true;
                    m_hosted_server_last_resync_request_sent = now;
                    log_line("[bridge-hosted] hosted_resync_request epoch=" + std::to_string(m_session_epoch) +
                             " role=" + bridge_role_name(m_bridge_role) +
                             " authority=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                             " worldId=" + m_world_folder_id);
                }
                m_hosted_server_next_resync_request = now + std::chrono::seconds(45);
            }
        }
        const auto payloads = NativeBridge::instance().poll_incoming();
        for (const auto& payload : payloads)
        {
            handle_bridge_payload(payload);
        }

        update_bridge_health(now);
        if (m_bridge_role == BridgeRole::RemoteClient && !m_sidecar_authoritative)
        {
            if (is_remoteclient_ready_and_world_bound() && !m_remote_post_ready_resync_bootstrap_done)
            {
                start_remote_post_ready_resync("ready_world_bound");
            }
            tick_remote_post_ready_resync(now);
        }
        if (m_bridge_role == BridgeRole::RemoteClient &&
            !m_sidecar_authoritative &&
            !m_remote_post_ready_resync_active &&
            now >= m_bridge_next_snapshot_request)
        {
            if (m_remote_snapshot_no_cache_backoff_until.time_since_epoch().count() != 0 &&
                now < m_remote_snapshot_no_cache_backoff_until)
            {
                m_bridge_next_snapshot_request = m_remote_snapshot_no_cache_backoff_until;
                return;
            }
            if (!has_viable_remote_route_for_snapshot())
            {
                const bool should_log_blocked =
                    m_bridge_route_wait_last_reason != "no_valid_committed_route_for_snapshot_send" ||
                    m_bridge_route_wait_last_log.time_since_epoch().count() == 0 ||
                    (now - m_bridge_route_wait_last_log) >= std::chrono::seconds(5);
                if (should_log_blocked)
                {
                    log_line("[bridge-route] send_blocked type=snapshot_request reason=no_valid_committed_route" +
                             std::string(" role=") + bridge_role_name(m_bridge_role) +
                             " routeHost=" + (m_bridge_remote_server_host.empty() ? "none" : m_bridge_remote_server_host) +
                             " routeLocked=" + std::string{m_bridge_route_lock_acquired ? "true" : "false"} +
                             " nextRetrySec=5");
                    m_bridge_route_wait_last_reason = "no_valid_committed_route_for_snapshot_send";
                    m_bridge_route_wait_last_log = now;
                }
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

    auto SignTextMod::make_managed_row_storage_key(const std::string& storage_key, int row_index) const -> std::string
    {
        const int clamped_row = std::clamp(row_index, 0, 3);
        return storage_key + "|row=" + std::to_string(clamped_row);
    }

    auto SignTextMod::apply_autosize_defaults_for_font_profile(const bool has_override_pak, const std::string& reason) -> void
    {
        if (has_override_pak)
        {
            // Defaults when 0_WindroseTextSigns_RDFOverride_P.pak is installed.
            m_autosize_char_width_factor = 0.85f;
            m_row_gap_factor = 1.50f;
            m_row_gap_factor_2 = 1.50f;
            m_row_gap_factor_3 = 1.25f;
            m_row_gap_factor_4 = 1.00f;
            m_row_offsets_1 = {0.0f};
            m_row_offsets_2 = {8.0f, -5.0f};
            m_row_offsets_3 = {12.0f, 2.0f, -8.0f};
            m_row_offsets_4 = {17.0f, 7.0f, -3.0f, -13.0f};
        }
        else
        {
            // Defaults when 0_WindroseTextSigns_RDFOverride_P.pak is NOT installed.
            m_autosize_char_width_factor = 1.40f;
            m_row_gap_factor = 1.50f;
            m_row_gap_factor_2 = 1.50f;
            m_row_gap_factor_3 = 1.25f;
            m_row_gap_factor_4 = 1.00f;
            m_row_offsets_1 = {0.0f};
            m_row_offsets_2 = {6.0f, -6.0f};
            m_row_offsets_3 = {11.0f, 1.0f, -9.0f};
            m_row_offsets_4 = {16.0f, 6.0f, -4.0f, -14.0f};
        }
        m_autosize_profile_initialized = true;
        m_autosize_profile_has_override_pak = has_override_pak;
        log_line("[autosize] defaults_profile=" +
                 std::string{has_override_pak ? "custom_font_pak_installed" : "custom_font_pak_missing"} +
                 " reason=" + (reason.empty() ? "unknown" : reason));
    }

    auto SignTextMod::refresh_world_text_font_profile(const std::string& reason, const bool force_recheck) -> void
    {
        if (force_recheck)
        {
            // Detect again after session-ready/world load to avoid startup-order false negatives.
            m_world_text_font_override_pak_checked = false;
        }
        const bool has_override_pak = has_world_text_font_override_pak();
        const bool profile_changed =
            !m_autosize_profile_initialized ||
            m_autosize_profile_has_override_pak != has_override_pak;
        if (profile_changed)
        {
            apply_autosize_defaults_for_font_profile(has_override_pak, reason);
        }
        else
        {
            log_line("[autosize] defaults_profile_unchanged profile=" +
                     std::string{has_override_pak ? "custom_font_pak_installed" : "custom_font_pak_missing"} +
                     " reason=" + (reason.empty() ? "unknown" : reason));
        }
        if (force_recheck)
        {
            // Re-resolve packaged UFont once the world/session is ready.
            m_world_text_font_asset = nullptr;
            m_world_text_font_resolved = false;
            m_world_text_font_missing_logged = false;
            log_line("[phase4-font] resolve_cache_reset reason=" + (reason.empty() ? "unknown" : reason));
        }
    }

    auto SignTextMod::replay_cached_label_text_after_ready(const std::string& reason) -> std::pair<size_t, size_t>
    {
        std::string world_bound_reason{};
        if (!is_world_bound_operation_allowed("bridge_cached_replay_after_ready", &world_bound_reason))
        {
            log_line("[bridge] cached_replay_deferred reason=" + world_bound_reason +
                     " trigger=" + (reason.empty() ? "unknown" : reason));
            return {0, 0};
        }

        size_t candidates = 0;
        size_t rendered = 0;
        const auto active_world = m_world_folder_id;
        for (const auto& [key, rec] : m_labels)
        {
            if (!is_confirmed_label_text_kind(rec.kind))
            {
                continue;
            }
            if (is_hex_world_id(active_world) && rec.world_id != active_world)
            {
                continue;
            }
            ++candidates;
            bool applied_any = false;
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (!object || !object->IsA(AActor::StaticClass()))
                {
                    return LoopAction::Continue;
                }
                auto* actor = Cast<AActor>(object);
                if (actor && is_probable_label_actor(actor) && extract_stable_id(actor) == rec.stable_id)
                {
                    applied_any = apply_text_to_actor_component(actor, rec.text) || applied_any;
                }
                return LoopAction::Continue;
            });
            if (applied_any)
            {
                ++rendered;
            }
        }

        log_line("[bridge] cached_replay_after_ready trigger=" + (reason.empty() ? "unknown" : reason) +
                 " candidates=" + std::to_string(candidates) +
                 " rendered=" + std::to_string(rendered) +
                 " worldId=" + (m_world_folder_id.empty() ? "unknown" : m_world_folder_id));
        return {candidates, rendered};
    }

    auto SignTextMod::queue_first_authoritative_render_pass(const std::string& source, const std::string& world_id) -> void
    {
        const auto normalized_world_id =
            is_hex_world_id(world_id) ? world_id :
            (is_hex_world_id(m_world_folder_id) ? m_world_folder_id :
             (is_hex_world_id(m_worldid_latched_id) ? m_worldid_latched_id : std::string{}));
        if (!is_hex_world_id(normalized_world_id))
        {
            return;
        }
        if (m_first_authoritative_render_completed &&
            m_first_authoritative_render_epoch == m_session_epoch &&
            lower_ascii(m_first_authoritative_render_world_id) == lower_ascii(normalized_world_id))
        {
            return;
        }
        if (m_first_authoritative_render_pending &&
            m_first_authoritative_render_epoch == m_session_epoch &&
            lower_ascii(m_first_authoritative_render_world_id) == lower_ascii(normalized_world_id))
        {
            return;
        }

        m_first_authoritative_render_pending = true;
        m_first_authoritative_render_completed = false;
        m_first_authoritative_render_epoch = m_session_epoch;
        m_first_authoritative_render_world_id = normalized_world_id;
        m_first_authoritative_render_source = source.empty() ? "unknown" : source;
        m_first_authoritative_render_attempts = 0;
        m_first_authoritative_render_last_log = {};
        m_first_authoritative_render_last_reason.clear();
        log_line("[render-init] first_authoritative_data_seen source=" + m_first_authoritative_render_source +
                 " epoch=" + std::to_string(m_first_authoritative_render_epoch) +
                 " worldId=" + m_first_authoritative_render_world_id +
                 " records=" + std::to_string(m_labels.size()) +
                 " revision=" + std::to_string(m_revision));
    }

    auto SignTextMod::maybe_run_first_authoritative_render_pass(const std::string& trigger) -> void
    {
        if (!m_first_authoritative_render_pending)
        {
            return;
        }
        if (m_first_authoritative_render_epoch != m_session_epoch)
        {
            m_first_authoritative_render_pending = false;
            m_first_authoritative_render_completed = false;
            return;
        }

        std::string blocked_reason{};
        if (!m_session_ready_latched)
        {
            blocked_reason = "session_not_ready";
        }
        else if (!m_role_lock_acquired)
        {
            blocked_reason = "role_not_locked";
        }
        else
        {
            std::string world_bound_reason{};
            if (!is_world_bound_operation_allowed("first_authoritative_render_pass", &world_bound_reason))
            {
                blocked_reason = world_bound_reason.empty() ? "world_bound_blocked" : world_bound_reason;
            }
        }
        if (blocked_reason.empty() &&
            is_hex_world_id(m_first_authoritative_render_world_id) &&
                 is_hex_world_id(m_world_folder_id) &&
                 lower_ascii(m_first_authoritative_render_world_id) != lower_ascii(m_world_folder_id))
        {
            blocked_reason = "world_mismatch";
        }

        if (!blocked_reason.empty())
        {
            const auto now = std::chrono::steady_clock::now();
            if (m_first_authoritative_render_last_reason != blocked_reason ||
                m_first_authoritative_render_last_log.time_since_epoch().count() == 0 ||
                (now - m_first_authoritative_render_last_log) >= std::chrono::seconds(2))
            {
                log_line("[render-init] first_render_pass_deferred source=" + m_first_authoritative_render_source +
                         " trigger=" + (trigger.empty() ? "unknown" : trigger) +
                         " epoch=" + std::to_string(m_session_epoch) +
                         " worldId=" + (m_first_authoritative_render_world_id.empty() ? "unknown" : m_first_authoritative_render_world_id) +
                         " reason=" + blocked_reason);
                m_first_authoritative_render_last_reason = blocked_reason;
                m_first_authoritative_render_last_log = now;
            }
            return;
        }

        ++m_first_authoritative_render_attempts;
        const auto result = replay_cached_label_text_after_ready(
            "first_authoritative_render_pass:" + m_first_authoritative_render_source +
            ":" + (trigger.empty() ? "unknown" : trigger));

        // If authoritative records exist but no live label actor was render-applied yet,
        // keep the first-pass pending and retry. This handles spawn/stream timing where
        // data arrives before actors are visible in-world.
        if (result.first > 0 && result.second == 0)
        {
            const auto now = std::chrono::steady_clock::now();
            const std::string retry_reason = "awaiting_live_label_actors";
            if (m_first_authoritative_render_last_reason != retry_reason ||
                m_first_authoritative_render_last_log.time_since_epoch().count() == 0 ||
                (now - m_first_authoritative_render_last_log) >= std::chrono::seconds(2))
            {
                log_line("[render-init] first_render_pass_deferred source=" + m_first_authoritative_render_source +
                         " trigger=" + (trigger.empty() ? "unknown" : trigger) +
                         " epoch=" + std::to_string(m_session_epoch) +
                         " worldId=" + (m_first_authoritative_render_world_id.empty() ? "unknown" : m_first_authoritative_render_world_id) +
                         " reason=" + retry_reason +
                         " attempts=" + std::to_string(m_first_authoritative_render_attempts) +
                         " candidates=" + std::to_string(result.first) +
                         " rendered=" + std::to_string(result.second));
                m_first_authoritative_render_last_reason = retry_reason;
                m_first_authoritative_render_last_log = now;
            }
            return;
        }

        m_first_authoritative_render_pending = false;
        m_first_authoritative_render_completed = true;
        log_line("[render-init] first_render_pass_completed source=" + m_first_authoritative_render_source +
                 " trigger=" + (trigger.empty() ? "unknown" : trigger) +
                 " epoch=" + std::to_string(m_session_epoch) +
                 " worldId=" + (m_first_authoritative_render_world_id.empty() ? "unknown" : m_first_authoritative_render_world_id) +
                 " attempts=" + std::to_string(m_first_authoritative_render_attempts) +
                 " candidates=" + std::to_string(result.first) +
                 " rendered=" + std::to_string(result.second));
    }

    auto SignTextMod::has_world_text_font_override_pak() -> bool
    {
        if (m_world_text_font_override_pak_checked)
        {
            return m_world_text_font_override_pak_detected;
        }

        std::vector<std::filesystem::path> mods_dirs{};
        auto collect_mods_dirs_from_ancestors = [&](std::filesystem::path seed) -> void
        {
            if (seed.empty())
            {
                return;
            }

            std::error_code ec{};
            if (!std::filesystem::is_directory(seed, ec))
            {
                seed = seed.parent_path();
            }

            for (auto current = seed; !current.empty(); current = current.parent_path())
            {
                const auto mods_dir = current / "Content" / "Paks" / "~mods";
                if (std::filesystem::exists(mods_dir, ec) && std::filesystem::is_directory(mods_dir, ec))
                {
                    append_unique_path(mods_dirs, mods_dir);
                }
                if (current == current.parent_path())
                {
                    break;
                }
            }
        };

        collect_mods_dirs_from_ancestors(std::filesystem::current_path());
        collect_mods_dirs_from_ancestors(m_mod_root);
        if (const auto exe_path = current_executable_path(); !exe_path.empty())
        {
            collect_mods_dirs_from_ancestors(exe_path.parent_path());
        }

        m_world_text_font_override_pak_checked = true;
        m_world_text_font_override_pak_detected = false;

        const std::array<std::string, 3> required_suffixes = {
            ".pak",
            ".utoc",
            ".ucas"};

        std::filesystem::path detected_mods_dir{};
        size_t detected_hit_count = 0;
        for (const auto& mods_dir : mods_dirs)
        {
            std::error_code ec{};
            size_t hit_count = 0;
            for (const auto& suffix : required_suffixes)
            {
                const auto candidate = mods_dir / ("0_WindroseTextSigns_RDFOverride_P" + suffix);
                if (std::filesystem::exists(candidate, ec))
                {
                    ++hit_count;
                }
            }
            if (hit_count >= 2)
            {
                m_world_text_font_override_pak_detected = true;
                detected_mods_dir = mods_dir;
                detected_hit_count = hit_count;
                break;
            }
        }

        log_line("[phase4-font] override_pak_detected=" +
                 std::string{m_world_text_font_override_pak_detected ? "true" : "false"} +
                 " searchedModsDirs=" + std::to_string(mods_dirs.size()) +
                 " detectedModsDir=" + (detected_mods_dir.empty() ? std::string{"none"} : detected_mods_dir.string()) +
                 " detectedHitCount=" + std::to_string(detected_hit_count));
        return m_world_text_font_override_pak_detected;
    }

    auto SignTextMod::resolve_world_text_font_size_limits() -> std::pair<float, float>
    {
        const bool has_override_pak = m_autosize_profile_initialized && m_autosize_profile_has_override_pak;
        if (has_override_pak)
        {
            return {12.0f, 24.0f};
        }
        return {10.0f, 20.0f};
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

        const bool is_row_component_key = storage_key.find("|row=") != std::string::npos;
        if (is_row_component_key)
        {
            return nullptr;
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
            std::sort(
                fallback_components_to_manage.begin(),
                fallback_components_to_manage.end(),
                [this](UObject* a, UObject* b) {
                    return lower_ascii(narrow_ascii(a->GetFullName())) < lower_ascii(narrow_ascii(b->GetFullName()));
                });
            auto* fallback = fallback_components_to_manage.front();
            m_component_name_cache[storage_key] = narrow_ascii(fallback->GetFullName());
            if (fallback_components_to_manage.size() > 1)
            {
                log_line("[phase4] component_recovered key=" + storage_key +
                         " count=" + std::to_string(fallback_components_to_manage.size()) +
                         " keeping=" + narrow_ascii(fallback->GetFullName()) +
                         " action=preserve_components_no_blank");
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
        if (!m_autosize_profile_initialized || !m_autosize_profile_has_override_pak)
        {
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
        // Hide and blank immediately to avoid one-frame default text/size flashes.
        (void)invoke_set_hidden_in_game(created_component, true);
        (void)invoke_set_visibility(created_component, false);
        (void)invoke_set_text(created_component, "");
        (void)invoke_set_relative_location(created_component, relative_location);
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
        log_line("[phase4-font] create_component_font component=" + narrow_ascii(created_component->GetFullName()) +
                 " applied=" + std::string{font_applied ? "true" : "false"});
        return created_component;
    }

    auto SignTextMod::destroy_managed_text_component(AActor* actor, const std::string& storage_key) -> bool
    {
        std::vector<std::string> managed_keys{};
        managed_keys.reserve(5);
        managed_keys.push_back(storage_key);
        for (int row = 0; row < 4; ++row)
        {
            managed_keys.push_back(make_managed_row_storage_key(storage_key, row));
        }

        std::unordered_set<std::string> expected_names{};
        for (const auto& key : managed_keys)
        {
            expected_names.insert(lower_ascii(make_managed_component_name(key)));
        }

        if (!actor)
        {
            for (const auto& key : managed_keys)
            {
                m_component_name_cache.erase(key);
            }
            return false;
        }

        bool any_removed = false;
        bool matched_managed_component = false;
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
            bool is_managed_match = expected_names.find(component_name) != expected_names.end();
            if (!is_managed_match)
            {
                for (const auto& key : managed_keys)
                {
                    if (const auto found = m_component_name_cache.find(key);
                        found != m_component_name_cache.end() &&
                        lower_ascii(found->second) == component_full_name)
                    {
                        is_managed_match = true;
                        break;
                    }
                }
            }

            if (!is_managed_match)
            {
                continue;
            }

            matched_managed_component = true;

            const bool hidden_applied = invoke_set_hidden_in_game(component, true);
            const bool visibility_applied = invoke_set_visibility(component, false);
            const bool blanked = invoke_set_text(component, "");
            any_removed = any_removed || hidden_applied || visibility_applied || blanked;
        }

        // Backward compatibility: old builds may have one recovered unnamed component
        // for the base key. If no managed rows were matched, clear any text render
        // components on this actor once to avoid stale overlap.
        if (!matched_managed_component)
        {
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
        }

        for (const auto& key : managed_keys)
        {
            m_component_name_cache.erase(key);
        }
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
        if (m_session_ready_latched && !m_world_text_font_profile_ready_latched)
        {
            refresh_world_text_font_profile("apply_text_to_actor_component_ready_guard", true);
            m_world_text_font_profile_ready_latched = true;
        }

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

            desired_r = std::clamp(rec.color_r, 0.0f, 1.0f);
            desired_g = std::clamp(rec.color_g, 0.0f, 1.0f);
            desired_b = std::clamp(rec.color_b, 0.0f, 1.0f);
            desired_a = std::clamp(rec.color_a, 0.0f, 1.0f);
        }

        // Runtime font size is intentionally recomputed per-client from local
        // font profile/layout settings. Stored/synced fontSize remains legacy
        // compatibility data and is not used for rendering.
        const auto normalized_for_fit = strip_terminal_line_breaks(text_value);
        const auto [font_min, font_max] = resolve_world_text_font_size_limits();
        const auto runtime_fit = fit_text_for_plaque(
            normalized_for_fit,
            m_autosize_char_width_factor,
            font_min,
            font_max);
        desired_font_size = std::clamp(runtime_fit.font_size, font_min, font_max);

        auto rows = split_rows(text_value);
        if (rows.empty())
        {
            rows.push_back("");
        }
        if (rows.size() > 4)
        {
            rows.resize(4);
        }
        const int row_count = std::max(1, static_cast<int>(rows.size()));
        std::array<float, 4> layout_offsets{0.0f, 0.0f, 0.0f, 0.0f};
        switch (row_count)
        {
        case 1:
            layout_offsets[0] = m_row_offsets_1[0];
            break;
        case 2:
            layout_offsets[0] = m_row_offsets_2[0];
            layout_offsets[1] = m_row_offsets_2[1];
            break;
        case 3:
            layout_offsets[0] = m_row_offsets_3[0];
            layout_offsets[1] = m_row_offsets_3[1];
            layout_offsets[2] = m_row_offsets_3[2];
            break;
        default:
            layout_offsets[0] = m_row_offsets_4[0];
            layout_offsets[1] = m_row_offsets_4[1];
            layout_offsets[2] = m_row_offsets_4[2];
            layout_offsets[3] = m_row_offsets_4[3];
            break;
        }
        const float row_gap_scale =
            (row_count == 2) ? m_row_gap_factor_2 :
            (row_count == 3) ? m_row_gap_factor_3 :
            (row_count >= 4) ? m_row_gap_factor_4 :
            1.0f;
        if (row_count > 1)
        {
            for (int i = 0; i < row_count; ++i)
            {
                layout_offsets[static_cast<size_t>(i)] *= row_gap_scale;
            }
        }
        float min_offset = layout_offsets[0];
        float max_offset = layout_offsets[0];
        for (int i = 1; i < row_count; ++i)
        {
            min_offset = std::min(min_offset, layout_offsets[static_cast<size_t>(i)]);
            max_offset = std::max(max_offset, layout_offsets[static_cast<size_t>(i)]);
        }
        const float block_total_height = std::max(0.0f, max_offset - min_offset);
        float avg_step = 0.0f;
        if (row_count > 1)
        {
            for (int i = 1; i < row_count; ++i)
            {
                avg_step += std::fabs(layout_offsets[static_cast<size_t>(i - 1)] - layout_offsets[static_cast<size_t>(i)]);
            }
            avg_step /= static_cast<float>(row_count - 1);
        }

        std::vector<float> row_offsets{};
        row_offsets.reserve(static_cast<size_t>(row_count));
        const bool fonted = m_autosize_profile_initialized && m_autosize_profile_has_override_pak;
        bool moved = false;
        bool sized = true;
        bool vcentered = true;
        bool ufont_applied_all_rows = true;
        bool ufont_apply_attempted = false;
        bool colored = true;
        bool text_applied = true;
        bool any_reused_existing = false;
        std::vector<UObject*> row_components_to_show{};
        row_components_to_show.reserve(static_cast<size_t>(row_count));

        const auto schedule_create_retry = [&]() {
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
        };

        auto collect_text_components_sorted = [&]() {
            std::vector<UObject*> components_out{};
            auto actor_components = actor->GetComponentsByClass(UActorComponent::StaticClass());
            for (int32_t i = 0; i < actor_components.Num(); ++i)
            {
                auto* component = actor_components[i];
                if (!component)
                {
                    continue;
                }
                const auto component_class = lower_ascii(narrow_ascii(component->GetClassPrivate()->GetFullName()));
                if (component_class.find("textrendercomponent") == std::string::npos)
                {
                    continue;
                }
                components_out.push_back(component);
            }
            std::sort(
                components_out.begin(),
                components_out.end(),
                [this](UObject* a, UObject* b) {
                    return lower_ascii(narrow_ascii(a->GetFullName())) < lower_ascii(narrow_ascii(b->GetFullName()));
                });
            return components_out;
        };

        auto current_text_components = collect_text_components_sorted();

        const auto find_cached_component_only = [&](const std::string& storage_key) -> UObject* {
            const auto cached_it = m_component_name_cache.find(storage_key);
            if (cached_it == m_component_name_cache.end())
            {
                return nullptr;
            }
            const auto cached_full_name = lower_ascii(cached_it->second);
            for (auto* component : current_text_components)
            {
                if (!component)
                {
                    continue;
                }
                const auto component_full_name = lower_ascii(narrow_ascii(component->GetFullName()));
                if (component_full_name == cached_full_name)
                {
                    return component;
                }
            }
            m_component_name_cache.erase(storage_key);
            return nullptr;
        };

        const int previous_row_count =
            (m_last_row_count_by_key.find(key) != m_last_row_count_by_key.end())
                ? m_last_row_count_by_key[key]
                : row_count;
        const bool row_layout_changed = previous_row_count != row_count;
        if (row_layout_changed)
        {
            const auto clear_cached_slot = [&](const std::string& storage_key) {
                if (auto* stale = find_cached_component_only(storage_key); stale)
                {
                    (void)invoke_set_hidden_in_game(stale, true);
                    (void)invoke_set_visibility(stale, false);
                    (void)invoke_set_text(stale, "");
                }
                m_component_name_cache.erase(storage_key);
            };
            clear_cached_slot(key);
            for (int stale_row = 0; stale_row < 4; ++stale_row)
            {
                clear_cached_slot(make_managed_row_storage_key(key, stale_row));
            }
            current_text_components = collect_text_components_sorted();
            log_line("[phase4] row_layout_changed_reset key=" + key +
                     " previousRows=" + std::to_string(previous_row_count) +
                     " nextRows=" + std::to_string(row_count));
        }

        const auto reseed_row_component_cache = [&]() {
            bool reseeded = false;
            std::unordered_set<std::string> used_component_full_names{};
            used_component_full_names.reserve(4);

            for (int row_index = 0; row_index < 4; ++row_index)
            {
                const auto row_storage_key = make_managed_row_storage_key(key, row_index);
                if (auto* cached = find_cached_component_only(row_storage_key); cached)
                {
                    const auto cached_full_name = lower_ascii(narrow_ascii(cached->GetFullName()));
                    if (used_component_full_names.insert(cached_full_name).second)
                    {
                        continue;
                    }
                }

                if (row_index == 0)
                {
                    if (auto* base_cached = find_cached_component_only(key); base_cached)
                    {
                        const auto base_full_name = narrow_ascii(base_cached->GetFullName());
                        const auto base_full_name_lower = lower_ascii(base_full_name);
                        if (!used_component_full_names.contains(base_full_name_lower))
                        {
                            m_component_name_cache[row_storage_key] = base_full_name;
                            m_component_name_cache.erase(key);
                            used_component_full_names.insert(base_full_name_lower);
                            reseeded = true;
                            continue;
                        }
                    }
                }

                for (auto* component : current_text_components)
                {
                    if (!component)
                    {
                        continue;
                    }
                    const auto component_full_name = narrow_ascii(component->GetFullName());
                    const auto component_full_name_lower = lower_ascii(component_full_name);
                    if (used_component_full_names.contains(component_full_name_lower))
                    {
                        continue;
                    }
                    m_component_name_cache[row_storage_key] = component_full_name;
                    used_component_full_names.insert(component_full_name_lower);
                    reseeded = true;
                    break;
                }
            }

            if (reseeded)
            {
                log_line("[phase4] row_cache_reseeded key=" + key +
                         " componentCandidates=" + std::to_string(current_text_components.size()));
            }
        };

        reseed_row_component_cache();

        const auto clear_row_component = [&](const std::string& row_storage_key) {
            auto* stale_row_component = find_cached_component_only(row_storage_key);
            if (!stale_row_component)
            {
                return;
            }
            (void)invoke_set_hidden_in_game(stale_row_component, true);
            (void)invoke_set_visibility(stale_row_component, false);
            (void)invoke_set_text(stale_row_component, "");
        };

        for (int row_index = row_count; row_index < 4; ++row_index)
        {
            clear_row_component(make_managed_row_storage_key(key, row_index));
        }

        for (int row_index = 0; row_index < row_count; ++row_index)
        {
            const auto row_storage_key = make_managed_row_storage_key(key, row_index);
            if (auto* existing_component = find_cached_component_only(row_storage_key))
            {
                (void)invoke_set_hidden_in_game(existing_component, true);
                (void)invoke_set_visibility(existing_component, false);
                continue;
            }
            if (row_index == 0)
            {
                if (auto* base_component = find_cached_component_only(key))
                {
                    (void)invoke_set_hidden_in_game(base_component, true);
                    (void)invoke_set_visibility(base_component, false);
                }
            }
        }

        std::unordered_set<uintptr_t> assigned_row_component_ptrs{};
        assigned_row_component_ptrs.reserve(static_cast<size_t>(row_count));

        for (int row_index = 0; row_index < row_count; ++row_index)
        {
            const auto row_storage_key = make_managed_row_storage_key(key, row_index);
            const float row_offset = layout_offsets[static_cast<size_t>(row_index)];
            row_offsets.push_back(row_offset);

            FVector row_relative_location(
                relative_location.GetX(),
                relative_location.GetY(),
                relative_location.GetZ() + row_offset);

            auto* text_component = find_cached_component_only(row_storage_key);
            bool reused_existing = text_component != nullptr;
            if (!text_component && row_index == 0)
            {
                auto* base_component = find_cached_component_only(key);
                if (base_component)
                {
                    text_component = base_component;
                    reused_existing = true;
                    m_component_name_cache[row_storage_key] = narrow_ascii(base_component->GetFullName());
                    m_component_name_cache.erase(key);
                }
            }
            if (text_component)
            {
                const auto component_ptr = reinterpret_cast<uintptr_t>(text_component);
                if (assigned_row_component_ptrs.contains(component_ptr))
                {
                    text_component = nullptr;
                    reused_existing = false;
                }
            }
            if (!text_component)
            {
                text_component = create_managed_text_component(actor, row_storage_key, row_relative_location);
                if (text_component)
                {
                    const auto component_ptr = reinterpret_cast<uintptr_t>(text_component);
                    if (assigned_row_component_ptrs.contains(component_ptr))
                    {
                        // Defensive path: AddComponentByClass can occasionally hand back
                        // an existing component during churn. Never allow the same pointer
                        // to back multiple rows in a single apply pass.
                        log_line("[phase4] row_component_collision key=" + key +
                                 " row=" + std::to_string(row_index) +
                                 " component=" + narrow_ascii(text_component->GetFullName()) +
                                 " action=create_retry_once");
                        auto* second_try_component = create_managed_text_component(actor, row_storage_key, row_relative_location);
                        if (second_try_component && !assigned_row_component_ptrs.contains(reinterpret_cast<uintptr_t>(second_try_component)))
                        {
                            text_component = second_try_component;
                        }
                        else
                        {
                            text_component = nullptr;
                        }
                    }
                }
            }
            if (!text_component)
            {
                schedule_create_retry();
                log_line("[phase4] apply_failed reason=CreateTextComponent actor=" + narrow_ascii(actor->GetFullName()) +
                         " key=" + key +
                         " row=" + std::to_string(row_index));
                return false;
            }

            any_reused_existing = any_reused_existing || reused_existing;
            assigned_row_component_ptrs.insert(reinterpret_cast<uintptr_t>(text_component));
            (void)invoke_set_hidden_in_game(text_component, true);
            (void)invoke_set_visibility(text_component, false);
            bool row_moved = invoke_set_relative_location(text_component, row_relative_location);
            if (!row_moved && reused_existing)
            {
                auto* stale_component = text_component;
                auto* recreated_component = create_managed_text_component(actor, row_storage_key, row_relative_location);
                if (recreated_component)
                {
                    (void)invoke_set_hidden_in_game(stale_component, true);
                    (void)invoke_set_visibility(stale_component, false);
                    (void)invoke_set_text(stale_component, "");

                    assigned_row_component_ptrs.erase(reinterpret_cast<uintptr_t>(stale_component));
                    text_component = recreated_component;
                    reused_existing = false;
                    any_reused_existing = any_reused_existing || reused_existing;
                    assigned_row_component_ptrs.insert(reinterpret_cast<uintptr_t>(text_component));
                    (void)invoke_set_hidden_in_game(text_component, true);
                    (void)invoke_set_visibility(text_component, false);
                    row_moved = invoke_set_relative_location(text_component, row_relative_location);
                    log_line("[phase4] row_component_recreated key=" + key +
                             " row=" + std::to_string(row_index) +
                             " reason=move_failed_reused_component");
                }
            }
            moved = row_moved || moved;
            const bool row_sized = invoke_set_float_value(
                text_component,
                STR("SetWorldSize"),
                STR("/Script/Engine.TextRenderComponent:SetWorldSize"),
                desired_font_size);
            const bool row_hcentered = invoke_set_byte_value(
                text_component,
                STR("SetHorizontalAlignment"),
                STR("/Script/Engine.TextRenderComponent:SetHorizontalAlignment"),
                1);
            const bool row_vcentered = invoke_set_byte_value(
                text_component,
                STR("SetVerticalAlignment"),
                STR("/Script/Engine.TextRenderComponent:SetVerticalAlignment"),
                1);
            bool row_font_applied = true;
            if (fonted)
            {
                ufont_apply_attempted = true;
                row_font_applied = apply_world_text_font(text_component);
            }
            const bool row_colored = invoke_set_text_render_color(text_component, desired_r, desired_g, desired_b, desired_a);
            const bool row_text_applied = invoke_set_text(text_component, rows[static_cast<size_t>(row_index)]);
            if (row_text_applied)
            {
                row_components_to_show.push_back(text_component);
            }

            sized = sized && row_sized;
            vcentered = vcentered && row_hcentered && row_vcentered;
            ufont_applied_all_rows = ufont_applied_all_rows && row_font_applied;
            colored = colored && row_colored;
            text_applied = text_applied && row_text_applied;
        }

        if (!text_applied)
        {
            m_phase4_last_failure_reason[key] = "SetTextFailed";
            m_phase4_next_retry[key] = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            m_create_null_retry_states.erase(key);
            log_line("[phase4] apply_failed reason=SetTextFailed key=" + key +
                     " rows=" + std::to_string(row_count));
            return false;
        }

        for (auto* row_component : row_components_to_show)
        {
            if (!row_component)
            {
                continue;
            }
            (void)invoke_set_hidden_in_game(row_component, false);
            (void)invoke_set_visibility(row_component, true);
        }

        m_phase4_last_failure_reason.erase(key);
        m_phase4_next_retry.erase(key);
        m_create_null_retry_states.erase(key);
        m_last_row_count_by_key[key] = row_count;
        m_rendered_text_cache[key] = text_value;
        m_phase4_last_apply_success_at[key] = std::chrono::steady_clock::now();
        std::ostringstream loc{};
        loc << std::fixed << std::setprecision(2)
            << relative_location.GetX() << ","
            << relative_location.GetY() << ","
            << relative_location.GetZ();
        std::ostringstream offsets{};
        offsets << std::fixed << std::setprecision(2);
        for (size_t i = 0; i < row_offsets.size(); ++i)
        {
            if (i > 0)
            {
                offsets << ",";
            }
            offsets << row_offsets[i];
        }

        log_line("[phase4] apply_success key=" + key +
                 " rows=" + std::to_string(row_count) +
                 " reusedAny=" + std::string{any_reused_existing ? "true" : "false"} +
                 " moved=" + std::string{moved ? "true" : "false"} +
                 " sized=" + std::string{sized ? "true" : "false"} +
                 " vcentered=" + std::string{vcentered ? "true" : "false"} +
                 " fonted=" + std::string{fonted ? "true" : "false"} +
                 " ufontApplyAttempted=" + std::string{ufont_apply_attempted ? "true" : "false"} +
                 " ufontAppliedAllRows=" + std::string{ufont_applied_all_rows ? "true" : "false"} +
                 " colored=" + std::string{colored ? "true" : "false"} +
                 " relLoc=" + loc.str() +
                 " textChars=" + std::to_string(text_value.size()));
        log_line("[phase4] row_layout key=" + key +
                 " rows=" + std::to_string(row_count) +
                 " fontSize=" + std::to_string(desired_font_size) +
                 " rowGapScale=" + std::to_string(row_gap_scale) +
                 " rowStepAvg=" + std::to_string(avg_step) +
                 " blockHeight=" + std::to_string(block_total_height) +
                 " rowOffsets=" + offsets.str() +
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
        if (m_session_ready_latched && !m_world_text_font_profile_ready_latched)
        {
            refresh_world_text_font_profile("apply_text_to_selected_label_ready_guard", true);
            m_world_text_font_profile_ready_latched = true;
        }

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
        const auto [font_min, font_max] = resolve_world_text_font_size_limits();
        const auto fit = fit_text_for_plaque(
            normalized_text_value,
            m_autosize_char_width_factor,
            font_min,
            font_max);
        rec.text = fit.wrapped_text;
        rec.font_size = std::clamp(fit.font_size, font_min, font_max);
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
                 " widthFactor=" + std::to_string(m_autosize_char_width_factor) +
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
        if (!is_worldid_bound_for_current_epoch())
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

        UObject* controlled_pawn = get_controller_pawn_property_only(controller);
        if (!controlled_pawn)
        {
            set_reason("controlled_pawn_missing");
            return false;
        }

        set_reason("ready");
        return true;
    }

    auto SignTextMod::is_worldid_bound_for_current_epoch() const -> bool
    {
        return m_worldid_bound_for_epoch &&
            m_worldid_bound_epoch == m_session_epoch &&
            is_hex_world_id(m_worldid_latched_id);
    }

    auto SignTextMod::try_resolve_worldid_for_ready_bind(std::string* out_world_id, std::string* out_source) -> bool
    {
        const auto set_out = [&](const std::string& world_id, const std::string& source) {
            if (out_world_id)
            {
                *out_world_id = world_id;
            }
            if (out_source)
            {
                *out_source = source;
            }
        };

        if (is_hex_world_id(m_worldid_latched_id))
        {
            set_out(m_worldid_latched_id, "existing_latched");
            return true;
        }
        if (is_hex_world_id(m_session_ready_world_id))
        {
            set_out(m_session_ready_world_id, "session_ready_world_id");
            return true;
        }
        if (auto connected_island_id = try_latest_connected_island_id_from_local_log();
            connected_island_id.has_value() && is_hex_world_id(*connected_island_id))
        {
            set_out(*connected_island_id, "connected_island_id");
            return true;
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
        if (!profile_root.empty())
        {
            if (auto chosen = choose_world_id_for_profile(profile_root); chosen.has_value() && is_hex_world_id(*chosen))
            {
                set_out(*chosen, "profile_worlds_root");
                return true;
            }
        }
        if (is_hex_world_id(m_world_folder_id))
        {
            set_out(m_world_folder_id, "existing_world_folder");
            return true;
        }
        return false;
    }

    auto SignTextMod::bind_worldid_for_epoch_if_ready(const std::string& reason) -> bool
    {
        if (is_worldid_bound_for_current_epoch())
        {
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto maybe_log_defer = [&](const std::string& defer_reason) {
            const bool should_log =
                m_worldid_last_defer_reason != defer_reason ||
                m_worldid_last_defer_log.time_since_epoch().count() == 0 ||
                (now - m_worldid_last_defer_log) >= std::chrono::seconds(2);
            if (should_log)
            {
                log_line("[worldid] bind_deferred reason=" + defer_reason +
                         " epoch=" + std::to_string(m_session_epoch));
                m_worldid_last_defer_reason = defer_reason;
                m_worldid_last_defer_log = now;
            }
        };

        if (!m_role_lock_acquired)
        {
            maybe_log_defer("role_not_locked");
            return false;
        }
        if (!m_session_ready_latched)
        {
            maybe_log_defer("session_ready_not_latched");
            return false;
        }

        std::string bound_world_id{};
        std::string bound_source{};
        if (!try_resolve_worldid_for_ready_bind(&bound_world_id, &bound_source) || !is_hex_world_id(bound_world_id))
        {
            maybe_log_defer("no_definitive_worldid_candidate");
            return false;
        }

        if (is_hex_world_id(m_worldid_latched_id) && lower_ascii(m_worldid_latched_id) != lower_ascii(bound_world_id))
        {
            log_line("[worldid] change_ignored reason=immutable_after_ready_lock current=" + m_worldid_latched_id +
                     " incoming=" + bound_world_id +
                     " epoch=" + std::to_string(m_session_epoch));
            bound_world_id = m_worldid_latched_id;
            bound_source = "existing_latched_immutable";
        }

        m_worldid_bind_phase = WorldIdBindPhase::StableIdLatched;
        m_worldid_latched_id = bound_world_id;
        m_worldid_provisional_id = bound_world_id;
        m_worldid_stability_seen_count = 1;
        m_worldid_stability_last_observed = now;
        m_worldid_generation_in_progress = false;
        m_worldid_last_defer_reason.clear();
        m_worldid_last_defer_log = {};

        m_worldid_bound_for_epoch = true;
        m_worldid_bound_epoch = m_session_epoch;
        m_worldid_bound_source = bound_source;
        m_world_bound_resumed_ops.clear();

        log_line("[worldid] worldid_bound_once epoch=" + std::to_string(m_session_epoch) +
                 " worldId=" + bound_world_id +
                 " source=" + bound_source +
                 " reason=" + reason);
        return true;
    }

    auto SignTextMod::is_world_bound_operation_allowed(const std::string& op, std::string* out_reason) -> bool
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

        std::string defer_reason{};
        if (!m_role_lock_acquired)
        {
            defer_reason = "role_not_locked";
        }
        else if (!m_session_ready_latched)
        {
            defer_reason = "session_ready_not_latched";
        }
        else if (!is_worldid_bound_for_current_epoch())
        {
            defer_reason = "worldid_not_bound";
        }
        else if (!is_hex_world_id(m_worldid_latched_id))
        {
            defer_reason = "worldid_not_hex";
        }

        if (defer_reason.empty())
        {
            set_reason("allowed");
            return true;
        }

        set_reason(defer_reason);
        const auto now = std::chrono::steady_clock::now();
        auto& state = m_world_bound_defer_logs_by_op[op];
        const bool should_log =
            state.reason != defer_reason ||
            state.last_log.time_since_epoch().count() == 0 ||
            (now - state.last_log) >= std::chrono::seconds(5);
        if (should_log)
        {
            log_line("[session] world_bound_operation_deferred op=" + op +
                     " reason=" + defer_reason +
                     " epoch=" + std::to_string(m_session_epoch) +
                     " runtimeRole=" + m_runtime_role);
            state.reason = defer_reason;
            state.last_log = now;
        }
        return false;
    }

    auto SignTextMod::note_world_bound_operation_resumed(const std::string& op) -> void
    {
        if (!m_world_bound_resumed_ops.insert(op).second)
        {
            return;
        }
        log_line("[session] world_bound_operation_resumed op=" + op +
                 " epoch=" + std::to_string(m_session_epoch) +
                 " worldId=" + m_worldid_latched_id);
    }

    auto SignTextMod::is_world_id_latched_for_authoritative_localclient_bind(std::string* out_reason) -> bool
    {
        const bool ok = bind_worldid_for_epoch_if_ready("compat_bind_check");
        if (out_reason)
        {
            *out_reason = ok ? "stable_latched" : "not_latched";
        }
        return ok;
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
        const bool server_runtime_process = is_dedicated_runtime_process();

        std::error_code size_ec{};
        const auto size = std::filesystem::file_size(m_destroy_signal_log_path, size_ec);
        if (size_ec)
        {
            return;
        }

        if (!m_destroy_signal_log_initialized || m_destroy_signal_log_offset > size)
        {
            m_destroy_signal_log_initialized = true;
            const uintmax_t backfill_bytes = server_runtime_process ? std::min<uintmax_t>(size, 512 * 1024) : 0;
            m_destroy_signal_log_offset = size - backfill_bytes;
            log_line("[save] destroy_signal_r5log armed path=" + m_destroy_signal_log_path.string() +
                     " offset=" + std::to_string(static_cast<unsigned long long>(m_destroy_signal_log_offset)));
            if (server_runtime_process)
            {
                maybe_begin_server_role_classification(m_destroy_signal_log_path, m_destroy_signal_log_offset);
            }
            if (m_destroy_signal_log_offset == size)
            {
                maybe_commit_server_role_classification(now, "r5log_armed");
                return;
            }
        }

        if (size == m_destroy_signal_log_offset)
        {
            if (server_runtime_process)
            {
                maybe_commit_server_role_classification(now, "r5log_no_new_bytes");
            }
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
        static const std::regex slot_token_rx{R"((BuildingBlock)\|([A-Fa-f0-9]{16,32})\|([0-9]+))", std::regex::icase};
        static const std::regex guid_key_value_rx{R"((?:BuildingGuid|Guid|ActorGuid)\s*[:=]\s*([A-Fa-f0-9]{16,32}))", std::regex::icase};
        static const std::regex guid_any_rx{R"(\b([A-Fa-f0-9]{32})\b)"};
        static const std::regex index_key_value_rx{R"((?:Block(?:Index)?|Index|Slot)\s*[:=]\s*([0-9]+))", std::regex::icase};
        static const std::regex construct_block_rx{
            R"(Construct\s+BuildingBlock\s+([0-9]+)\s+from\s+building\s+([A-Fa-f0-9]{16,32}))",
            std::regex::icase};
        constexpr uintmax_t k_r5_read_chunk_bytes = 262144;
        constexpr int k_r5_max_chunks_per_tick = 8;
        bool backlog_logged_this_tick = false;
        int chunks_processed = 0;
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

        while (m_destroy_signal_log_offset < size && chunks_processed < k_r5_max_chunks_per_tick)
        {
            const auto chunk_start_offset = m_destroy_signal_log_offset;
            const auto remaining_bytes = size - chunk_start_offset;
            if (!backlog_logged_this_tick && remaining_bytes > k_r5_read_chunk_bytes)
            {
                const auto chunk_count_estimate =
                    static_cast<unsigned long long>((remaining_bytes + k_r5_read_chunk_bytes - 1) / k_r5_read_chunk_bytes);
                log_line("[save] r5log_backlog_detected bytes=" +
                         std::to_string(static_cast<unsigned long long>(remaining_bytes)) +
                         " chunkBytes=" + std::to_string(static_cast<unsigned long long>(k_r5_read_chunk_bytes)) +
                         " estimatedChunks=" + std::to_string(chunk_count_estimate) +
                         " maxChunksThisTick=" + std::to_string(k_r5_max_chunks_per_tick));
                backlog_logged_this_tick = true;
            }
            input.clear();
            input.seekg(static_cast<std::streamoff>(chunk_start_offset), std::ios::beg);
            const auto bytes_to_read = std::min<uintmax_t>(remaining_bytes, k_r5_read_chunk_bytes);
            std::string chunk{};
            chunk.resize(static_cast<size_t>(bytes_to_read));
            input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            chunk.resize(static_cast<size_t>(std::max<std::streamsize>(0, input.gcount())));
            if (chunk.empty())
            {
                break;
            }
            m_destroy_signal_log_offset = chunk_start_offset + static_cast<uintmax_t>(chunk.size());
            ++chunks_processed;
            if (server_runtime_process)
            {
                maybe_begin_server_role_classification(m_destroy_signal_log_path, chunk_start_offset);
            }

            std::istringstream lines{chunk};
            std::string line{};
            uintmax_t line_offset = chunk_start_offset;
            while (std::getline(lines, line))
            {
                const auto line_lower = lower_ascii(line);
                const auto line_start = line_offset;
                const auto line_end = line_start + static_cast<uintmax_t>(line.size()) + 1;
                line_offset = line_end;
                if (server_runtime_process)
                {
                    maybe_observe_server_role_signal(line_lower, line_start, line_end);
                }

                const auto commit_client_role_from_start_signal = [&](const std::string& start_signal) {
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

                    const std::string pending_world_id = "pending-world";
                    std::filesystem::path startup_root{};
                    if (!profile_root.empty())
                    {
                        startup_root = profile_root / "WindroseTextSigns" / "StartupCache";
                    }
                    else
                    {
                        startup_root = m_mod_root / "Cache" / "StartupCache";
                    }

                    if (start_signal == "client_readytoplay_to_verifyingcoopconnection")
                    {
                        set_sidecar_route(
                            startup_root / "RemoteClient",
                            "RemoteClient",
                            "RemoteClientStartupCache",
                            "ServerAuthoritativePendingBridge",
                            "startup-cache",
                            false,
                            profile_root,
                            pending_world_id,
                            "definitive_start_signal");
                    }
                    else
                    {
                        set_sidecar_route(
                            startup_root / "LocalClient",
                            "LocalClient",
                            "LocalClientAuthoritativeStartupCache",
                            "LocalClientSoloOrHostedAuthoritative",
                            "authoritative-startup-cache",
                            true,
                            profile_root,
                            pending_world_id,
                            "definitive_start_signal");
                    }
                    configure_bridge_role("definitive_start_signal");
                    maybe_acquire_role_lock(now, "definitive_start_signal");
                    log_line("[role] definitive_start_role_lock signal=" + start_signal +
                             " runtimeRole=" + m_runtime_role +
                             " bridgeRole=" + bridge_role_name(m_bridge_role) +
                             " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                             " worldId=" + pending_world_id);
                };

                std::string definitive_start_signal{};
                if (!server_runtime_process)
                {
                    if (line_lower.find("startcoopofflinegameonselectedworld") != std::string::npos)
                    {
                        definitive_start_signal = "startcoopofflinegameonselectedworld";
                    }
                    else if (line_lower.find("client. change state readytoplay => verifyingcoopconnection") != std::string::npos)
                    {
                        const bool hosted_client_already_locked =
                            m_role_lock_acquired &&
                            lower_ascii(m_runtime_role) == "localclient" &&
                            lower_ascii(m_definitive_session_start_signal) == "client_readytoplay_to_startcoophostserver";
                        if (!hosted_client_already_locked)
                        {
                            definitive_start_signal = "client_readytoplay_to_verifyingcoopconnection";
                        }
                    }
                    else if (line_lower.find("client. change state readytoplay => startcoophostserver") != std::string::npos)
                    {
                        definitive_start_signal = "client_readytoplay_to_startcoophostserver";
                    }
                }
                bool suppress_post_exit_start = false;
                if (!definitive_start_signal.empty() &&
                    !server_runtime_process &&
                    m_definitive_session_last_end_seen.time_since_epoch().count() != 0 &&
                    m_definitive_session_last_end_log_path == m_destroy_signal_log_path)
                {
                    // Hard stale-offset guard: never allow a new session start to reopen from
                    // log lines at/behind the last definitive lobby-exit offset.
                    // This prevents post-exit churn where backlog replay re-matches old
                    // "ReadyToPlay => VerifyingCoopConnection" lines.
                    // Truncation-safe: if file size is now below the remembered end offset,
                    // treat it as a new/rotated log and skip this stale-offset suppression.
                    if (size >= m_definitive_session_last_end_offset &&
                        line_start <= m_definitive_session_last_end_offset)
                    {
                        suppress_post_exit_start = true;
                        const auto offset_delta =
                            m_definitive_session_last_end_offset - line_start;
                        log_line("[session] definitive_start_suppressed reason=post_exit_stale_offset signal=" +
                                 definitive_start_signal +
                                 " lineOffset=" +
                                 std::to_string(static_cast<unsigned long long>(line_start)) +
                                 " lastExitOffset=" +
                                 std::to_string(static_cast<unsigned long long>(m_definitive_session_last_end_offset)) +
                                 " delta=" +
                                 std::to_string(static_cast<unsigned long long>(offset_delta)));
                    }

                    constexpr auto k_post_exit_start_guard = std::chrono::seconds(4);
                    constexpr uintmax_t k_post_exit_start_guard_offset_bytes = 16384;
                    const auto end_age = now - m_definitive_session_last_end_seen;
                    const auto offset_delta =
                        line_start >= m_definitive_session_last_end_offset
                            ? (line_start - m_definitive_session_last_end_offset)
                            : 0;
                    if (!suppress_post_exit_start &&
                        end_age < k_post_exit_start_guard &&
                        offset_delta <= k_post_exit_start_guard_offset_bytes)
                    {
                        suppress_post_exit_start = true;
                        const auto age_ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(end_age).count();
                        log_line("[session] definitive_start_suppressed reason=post_exit_guard signal=" +
                                 definitive_start_signal +
                                 " ageMs=" + std::to_string(age_ms) +
                                 " offsetDelta=" +
                                 std::to_string(static_cast<unsigned long long>(offset_delta)));
                    }
                }
                if (!definitive_start_signal.empty() &&
                    !suppress_post_exit_start &&
                    try_begin_definitive_reset("start", definitive_start_signal, "none"))
                {
                    log_line("[session] definitive_start_detected signal=" + definitive_start_signal + " worldHint=none");
                    reset_session_state("definitive_session_start");
                    m_definitive_session_last_end_seen = {};
                    m_definitive_session_last_end_log_path.clear();
                    m_definitive_session_last_end_offset = 0;
                    // Anchor at the definitive-start line itself so all immediately-following
                    // lines are guaranteed in-window for this epoch.
                    open_session_window(definitive_start_signal, m_destroy_signal_log_path, line_start);
                    log_line("[session] definitive_start_anchor epoch=" + std::to_string(m_session_epoch) +
                             " signal=" + definitive_start_signal +
                             " startOffset=" + std::to_string(static_cast<unsigned long long>(line_start)));
                    commit_client_role_from_start_signal(definitive_start_signal);
                    log_line("[session] reset_extended_clears applied=true");
                }

                const bool line_in_active_session_window =
                    m_session_window_open &&
                    line_start >= m_session_window_start_offset;
                if (line_in_active_session_window)
                {
                    m_session_window_end_offset = line_end;
                }

            if (!server_runtime_process)
            {
                if (line_in_active_session_window &&
                    line_lower.find("hide loading screen. currentreason loadingscreen.reason.datakeeper.readytoplay") != std::string::npos)
                {
                    m_def_ready_hide_loading_datakeeper_seen = true;
                }
                if (line_in_active_session_window &&
                    line_lower.find("hide loading screen. currentreason loadingscreen.reason.coopproxy.waitingforueconnection") != std::string::npos)
                {
                    m_def_ready_hide_loading_coopproxy_wait_seen = true;
                }
                if (line_in_active_session_window &&
                    line_lower.find("client. change state startcoophostserver => verifyingcoopconnection") != std::string::npos)
                {
                    m_def_ready_hosted_secondary_verifying_seen = true;
                }
                if (line_in_active_session_window &&
                    line_lower.find("client. change state verifyingcoopconnection => coopconnectionverified") != std::string::npos)
                {
                    if (!m_bridge_route_gate_open)
                    {
                        m_bridge_route_gate_open = true;
                        m_bridge_route_gate_pending_logged = false;
                        m_bridge_route_probe_active = false;
                        m_bridge_route_probe_waiting_ack = false;
                        m_bridge_route_probe_index = 0;
                        m_bridge_route_probe_token.clear();
                        m_bridge_route_probe_host.clear();
                        m_bridge_route_probe_source.clear();
                        log_line("[bridge-route] route_lock_gate_open signal=verifying_to_coopconnectionverified epoch=" +
                                 std::to_string(m_session_epoch));
                    }
                }
                if (line_in_active_session_window &&
                    line_lower.find("client. change state waitingforislandandlocalaccountid => readytoplay") != std::string::npos)
                {
                    m_def_ready_hosted_secondary_waiting_island_ready_seen = true;
                }

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

                const bool lobby_transition =
                    line_lower.find("start transition to lobby") != std::string::npos;
                if (lobby_transition)
                {
                    if (try_begin_definitive_reset("end", "start_transition_to_lobby", "none"))
                    {
                        close_session_window("start_transition_to_lobby", m_destroy_signal_log_path, line_end);
                        m_definitive_session_last_end_seen = now;
                        m_definitive_session_last_end_log_path = m_destroy_signal_log_path;
                        m_definitive_session_last_end_offset = line_end;
                        log_line("[session] definitive_end_detected signal=start_transition_to_lobby");
                        arm_phase7_definitive_teardown("start_transition_to_lobby");
                        reset_session_state("definitive_session_end_lobby_transition");
                        log_line("[session] reset_extended_clears applied=true");
                    }
                }
            }
            else
            {
                if (line_lower.find("serveraccount. change state waitingforclientisready => readytoplay") != std::string::npos)
                {
                    m_def_ready_server_waiting_client_ready_seen = true;
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
        if (m_destroy_signal_log_offset < size && chunks_processed >= k_r5_max_chunks_per_tick)
        {
            const auto remaining = size - m_destroy_signal_log_offset;
            log_line("[save] r5log_backlog_catchup_deferred remainingBytes=" +
                     std::to_string(static_cast<unsigned long long>(remaining)) +
                     " chunksProcessed=" + std::to_string(chunks_processed) +
                     " maxChunksThisTick=" + std::to_string(k_r5_max_chunks_per_tick));
        }
        if (server_runtime_process)
        {
            maybe_commit_server_role_classification(now, "r5log_parse_pass");
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
        if (is_dedicated_runtime_process())
        {
            return;
        }
        if (!m_role_lock_acquired)
        {
            return;
        }
        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        if (runtime_role_lower != "localclient")
        {
            return;
        }

        auto* controller = try_get_primary_player_controller();
        if (!controller || !is_uobject_reflection_safe(controller) || !controller->IsA(AActor::StaticClass()))
        {
            return;
        }
        auto* controller_actor = Cast<AActor>(controller);
        if (!controller_actor)
        {
            return;
        }
        const auto actor_world_id = build_world_id_for_actor(controller_actor);
        configure_sidecar_for_actor(controller_actor, actor_world_id);
    }

    auto SignTextMod::is_session_window_active_for_gameplay(std::string* out_reason) const -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };

        if (!m_session_window_open)
        {
            set_reason("session_window_closed");
            return false;
        }
        if (!m_definitive_session_start_seen)
        {
            set_reason("definitive_start_not_seen");
            return false;
        }
        set_reason("ok");
        return true;
    }

    auto SignTextMod::open_session_window(
        const std::string& signal,
        const std::filesystem::path& log_path,
        const uintmax_t offset) -> void
    {
        m_session_window_open = true;
        m_definitive_session_start_seen = true;
        m_definitive_session_start_signal = signal;
        m_definitive_session_exit_signal.clear();
        m_session_window_log_path = log_path;
        m_session_window_start_offset = offset;
        m_session_window_end_offset = offset;
        m_session_window_blocked_last_log = {};
        m_session_window_blocked_last_signature.clear();
        m_ready_latch_blocked_last_log = {};
        m_ready_latch_blocked_last_signature.clear();
        m_ready_latch_blocked_first_seen = {};
        m_ready_latch_blocked_count = 0;
        log_line("[session] session_window_opened epoch=" + std::to_string(m_session_epoch) +
                 " signal=" + signal +
                 " logPath=" + (m_session_window_log_path.empty() ? "unknown" : m_session_window_log_path.string()) +
                 " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_session_window_start_offset)) +
                 " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_session_window_end_offset)));
    }

    auto SignTextMod::close_session_window(
        const std::string& signal,
        const std::filesystem::path& log_path,
        const uintmax_t offset) -> void
    {
        m_session_window_open = false;
        m_definitive_session_start_seen = false;
        m_definitive_session_exit_signal = signal;
        m_session_window_log_path = log_path;
        m_session_window_end_offset = offset;
        m_session_window_blocked_last_log = {};
        m_session_window_blocked_last_signature.clear();
        m_ready_latch_blocked_last_log = {};
        m_ready_latch_blocked_last_signature.clear();
        m_ready_latch_blocked_first_seen = {};
        m_ready_latch_blocked_count = 0;
        log_line("[session] session_window_closed epoch=" + std::to_string(m_session_epoch) +
                 " signal=" + signal +
                 " logPath=" + (m_session_window_log_path.empty() ? "unknown" : m_session_window_log_path.string()) +
                 " windowStartOffset=" + std::to_string(static_cast<unsigned long long>(m_session_window_start_offset)) +
                 " windowEndOffset=" + std::to_string(static_cast<unsigned long long>(m_session_window_end_offset)));
    }

    auto SignTextMod::reset_session_state(const std::string& reason) -> void
    {
        const auto old_epoch = m_session_epoch;
        const bool had_ready = m_session_ready_latched;
        const bool had_locks = m_role_lock_acquired || m_bridge_route_lock_acquired;
        const bool force_reset = reason == "definitive_session_start";
        if (m_remote_post_ready_resync_active)
        {
            stop_remote_post_ready_resync("session_reset_" + reason, false);
        }
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
        m_worldid_bound_for_epoch = false;
        m_worldid_bound_epoch = 0;
        m_worldid_bound_source.clear();
        m_session_action_defer_connecting_bl = false;
        m_deferred_definitive_start_pending = false;
        m_deferred_definitive_start_signal.clear();
        m_deferred_definitive_start_offset = 0;
        m_deferred_definitive_end_pending = false;
        m_deferred_definitive_end_offset = 0;
        m_world_bound_defer_logs_by_op.clear();
        m_world_bound_resumed_ops.clear();
        m_definitive_session_start_candidate_active = false;
        m_definitive_session_start_candidate_signal.clear();
        m_definitive_session_start_candidate_world_hint.clear();
        m_session_window_open = false;
        m_definitive_session_start_seen = false;
        m_definitive_session_start_signal.clear();
        m_definitive_session_exit_signal.clear();
        m_session_window_log_path.clear();
        m_session_window_start_offset = 0;
        m_session_window_end_offset = 0;
        m_session_window_blocked_last_log = {};
        m_session_window_blocked_last_signature.clear();
        m_ready_latch_blocked_last_log = {};
        m_ready_latch_blocked_last_signature.clear();
        m_ready_latch_blocked_first_seen = {};
        m_ready_latch_blocked_count = 0;
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
        m_def_ready_hide_loading_datakeeper_seen = false;
        m_def_ready_hide_loading_coopproxy_wait_seen = false;
        m_def_ready_hosted_secondary_verifying_seen = false;
        m_def_ready_hosted_secondary_waiting_island_ready_seen = false;
        m_def_ready_server_waiting_client_ready_seen = false;
        m_hosted_post_ready_reconcile_done = false;
        m_remote_cached_replay_pending_after_ready = false;
        m_first_authoritative_render_pending = false;
        m_first_authoritative_render_completed = false;
        m_first_authoritative_render_epoch = 0;
        m_first_authoritative_render_world_id.clear();
        m_first_authoritative_render_source.clear();
        m_first_authoritative_render_attempts = 0;
        m_first_authoritative_render_last_log = {};
        m_first_authoritative_render_last_reason.clear();
        m_world_text_font_profile_ready_latched = false;
        m_world_text_font_override_pak_checked = false;
        m_world_text_font_override_pak_detected = false;
        m_autosize_profile_initialized = false;
        m_autosize_profile_has_override_pak = false;
        m_world_text_font_asset = nullptr;
        m_world_text_font_resolved = false;
        m_world_text_font_missing_logged = false;
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
        m_phase4_last_apply_success_at.clear();
        m_create_null_retry_states.clear();
        m_hosted_authority_local_apply_deferred_keys.clear();
        m_bridge_route_retry_consumed = false;
        m_localclient_controller_probe_cached = nullptr;
        m_localclient_controller_probe_cache_valid = false;
        m_localclient_controller_probe_last = {};
        m_localclient_stability_unstable_last = {};
        m_localclient_stability_unstable_reason.clear();
        const bool preserve_r5_tail_continuity =
            reason == "definitive_session_start";
        if (!preserve_r5_tail_continuity)
        {
            m_destroy_signal_log_path.clear();
            m_destroy_signal_log_offset = 0;
            m_destroy_signal_log_initialized = false;
        }

        // Hard reset sockets before any role/route re-init in the new epoch.
        {
            const auto pre_bound_port = NativeBridge::instance().server_bound_port();
            const auto pre_open = NativeBridge::instance().server_socket_open();
            const auto pre_err = NativeBridge::instance().server_last_bind_error();
            m_bridge_role = BridgeRole::Unknown;
            NativeBridge::instance().set_role(BridgeRole::Unknown);
            const auto post_bound_port = NativeBridge::instance().server_bound_port();
            const auto post_open = NativeBridge::instance().server_socket_open();
            log_line("[bridge] bridge_listener_closed reason=session_reset_" + reason +
                     " preOpen=" + std::string{pre_open ? "true" : "false"} +
                     " prePort=" + std::to_string(static_cast<unsigned int>(pre_bound_port)) +
                     " preLastBindError=" + std::to_string(pre_err) +
                     " postOpen=" + std::string{post_open ? "true" : "false"} +
                     " postPort=" + std::to_string(static_cast<unsigned int>(post_bound_port)));
        }

        reset_visual_verify_debug_state();
        reset_server_role_classification_state("session_reset_" + reason);
        reset_role_route_locks("session_reset_" + reason);
        reset_bridge_snapshot_state("session_reset_" + reason);
        if (had_ready || had_locks)
        {
            log_line("[session] reset reason=" + reason +
                     " oldEpoch=" + std::to_string(old_epoch) +
                     " newEpoch=" + std::to_string(m_session_epoch));
        }
    }

    auto SignTextMod::is_definitive_ready_signal_observed(
        std::string* out_signal,
        std::string* out_reason) const -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };
        const auto set_signal = [&](const std::string& signal) {
            if (out_signal)
            {
                *out_signal = signal;
            }
        };

        if (!m_session_window_open || !m_definitive_session_start_seen)
        {
            set_reason("window_closed_or_start_missing");
            return false;
        }

        const auto start_signal = lower_ascii(m_definitive_session_start_signal);
        if (start_signal == "startcoopofflinegameonselectedworld")
        {
            if (m_def_ready_hide_loading_datakeeper_seen)
            {
                set_signal("hide_loading_datakeeper_readytoplay");
                set_reason("definitive");
                return true;
            }
            set_reason("waiting_hide_loading_datakeeper_readytoplay");
            return false;
        }

        if (start_signal == "client_readytoplay_to_verifyingcoopconnection")
        {
            if (m_def_ready_hide_loading_coopproxy_wait_seen)
            {
                set_signal("hide_loading_coopproxy_waitingforueconnection");
                set_reason("definitive");
                return true;
            }
            set_reason("waiting_hide_loading_coopproxy_waitingforueconnection");
            return false;
        }

        if (start_signal == "client_readytoplay_to_startcoophostserver")
        {
            if (m_def_ready_hide_loading_coopproxy_wait_seen)
            {
                set_signal("hide_loading_coopproxy_waitingforueconnection");
                set_reason("definitive_primary");
                return true;
            }
            if (m_def_ready_hosted_secondary_verifying_seen &&
                m_def_ready_hosted_secondary_waiting_island_ready_seen)
            {
                set_signal("startcoophostserver_to_verifying_then_waitingforisland_to_readytoplay");
                set_reason("definitive_secondary_chain");
                return true;
            }
            set_reason("waiting_hosted_primary_or_secondary_chain");
            return false;
        }

        if (start_signal == "server_host_ready_for_owner" ||
            start_signal == "hosted_server_role_chain_complete" ||
            start_signal == "dedicated_server_role_chain_complete")
        {
            if (m_def_ready_server_waiting_client_ready_seen)
            {
                set_signal("serveraccount_waitingforclientisready_to_readytoplay");
                set_reason("definitive");
                return true;
            }
            set_reason("waiting_serveraccount_waitingforclientisready_to_readytoplay");
            return false;
        }

        set_reason("unknown_definitive_start_signal");
        return false;
    }

    auto SignTextMod::is_session_ready_for_role_resolution(std::string* out_reason) -> bool
    {
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };

        std::string window_reason{};
        if (!is_session_window_active_for_gameplay(&window_reason))
        {
            set_reason(window_reason);
            return false;
        }

        std::string definitive_signal{};
        std::string definitive_reason{};
        const bool definitive_ready = is_definitive_ready_signal_observed(&definitive_signal, &definitive_reason);
        if (!definitive_ready)
        {
            set_reason("definitive_signal_missing:" + definitive_reason);
            return false;
        }

        set_reason("ready_definitive:" + definitive_signal);
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
        const auto now = std::chrono::steady_clock::now();
        const auto set_reason = [&](const std::string& reason) {
            if (out_reason)
            {
                *out_reason = reason;
            }
        };
        const auto mark_unstable = [&](const std::string& reason) -> bool {
            set_reason(reason);
            m_localclient_stability_unstable_last = now;
            m_localclient_stability_unstable_reason = reason;
            return false;
        };

        constexpr auto k_unstable_probe_throttle = std::chrono::milliseconds(200);
        if (m_localclient_stability_unstable_last.time_since_epoch().count() != 0 &&
            (now - m_localclient_stability_unstable_last) < k_unstable_probe_throttle)
        {
            const auto reason = m_localclient_stability_unstable_reason.empty()
                                    ? std::string{"localclient_unstable_throttled"}
                                    : std::string{"localclient_unstable_throttled:"} + m_localclient_stability_unstable_reason;
            set_reason(reason);
            return false;
        }

        if (is_dedicated_runtime_process())
        {
            return mark_unstable("dedicated_runtime");
        }

        const auto runtime_role_lower = lower_ascii(m_runtime_role);
        const auto authority_mode_lower = lower_ascii(m_authority_mode);
        if (runtime_role_lower != "localclient")
        {
            return mark_unstable("runtime_role_not_localclient");
        }
        if (authority_mode_lower == "worldauthoritypending")
        {
            return mark_unstable("authority_pending");
        }

        auto* controller = try_get_primary_player_controller();
        if (!controller || !is_uobject_reflection_safe(controller) || !controller->IsA(AActor::StaticClass()))
        {
            return mark_unstable("no_valid_player_controller");
        }
        auto* controller_actor = Cast<AActor>(controller);
        auto* world = controller_actor ? controller_actor->GetWorld() : nullptr;
        if (!world)
        {
            return mark_unstable("controller_world_unavailable");
        }
        const auto world_name = lower_ascii(narrow_ascii(world->GetName()));
        if (world_name.find("transition") != std::string::npos ||
            world_name.find("lobby") != std::string::npos ||
            world_name.find("entrance") != std::string::npos)
        {
            return mark_unstable("controller_world_transition");
        }

        UObject* controlled_pawn = get_controller_pawn_property_only(controller);
        if (!controlled_pawn)
        {
            return mark_unstable("controlled_pawn_missing");
        }

        m_localclient_stability_unstable_last = {};
        m_localclient_stability_unstable_reason.clear();
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
        if (!controller || !is_uobject_reflection_safe(controller))
        {
            return;
        }
        auto* controller_actor = controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
        if (!controller_actor)
        {
            return;
        }

        UObject* controlled_pawn = get_controller_pawn_property_only(controller);
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
        const auto pass_started_at = std::chrono::steady_clock::now();
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
            bool post_apply_grace = false;
            long long post_apply_age_ms = -1;
            if (const auto applied = m_phase4_last_apply_success_at.find(key); applied != m_phase4_last_apply_success_at.end())
            {
                const auto elapsed = pass_started_at - applied->second;
                post_apply_age_ms = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
                post_apply_grace = elapsed <= std::chrono::seconds(6);
            }

            // Visual verify can observe render-state propagation before UE finishes
            // creating/advancing render state on newly applied text. Treat this short
            // window as WARN-only instead of FAIL to avoid false negatives.
            const bool hard_fail = !pass && (component_pending_kill || (repeated_no_render && !post_apply_grace));
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
                     " postApplyGrace=" + std::string{post_apply_grace ? "true" : "false"} +
                     " postApplyAgeMs=" + std::to_string(post_apply_age_ms) +
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
                controlled_pawn = get_controller_pawn_property_only(controller);
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
                controlled_pawn = get_controller_pawn_property_only(controller);
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

        tick_r5_readiness_markers();

        if (!is_dedicated_runtime_process())
        {
            maybe_run_phase7_bootstrap_sanitize();
            // Hotkey reliability fallback:
            // capture hardware edge directly in case callback registration misses events
            // while input focus/context changes.
            const bool hotkey_is_down = ((GetAsyncKeyState(m_hotkey_vk) & 0x8000) != 0);
            if (!hotkey_is_down && m_hotkey_require_release_before_next_press)
            {
                m_hotkey_require_release_before_next_press = false;
                log_line("[input] hotkey_rearmed_on_release key=" + m_hotkey_name);
            }
            if (hotkey_is_down && !m_hotkey_poll_was_down)
            {
                const bool accepted = request_hotkey_press("polled_edge");
                if (accepted)
                {
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
                tick_phase7_umg_open_pending();
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
        const auto now = std::chrono::steady_clock::now();
        if (!is_dedicated_runtime_process())
        {
            if (!m_session_ready_latched)
            {
                std::string definitive_signal{};
                std::string definitive_reason{};
                const bool definitive_ready = is_definitive_ready_signal_observed(&definitive_signal, &definitive_reason);
                const auto note_ready_latch_blocked =
                    [&](const std::string& sig, const std::string& message) {
                        constexpr auto k_unchanged_heartbeat_interval = std::chrono::seconds(20);
                        const bool changed = m_ready_latch_blocked_last_signature != sig;
                        if (changed)
                        {
                            m_ready_latch_blocked_last_signature = sig;
                            m_ready_latch_blocked_last_log = now;
                            m_ready_latch_blocked_first_seen = now;
                            m_ready_latch_blocked_count = 1;
                            log_line(message + " epoch=" + std::to_string(m_session_epoch) + " count=1");
                            return;
                        }

                        ++m_ready_latch_blocked_count;
                        const bool heartbeat_due =
                            m_ready_latch_blocked_last_log.time_since_epoch().count() == 0 ||
                            (now - m_ready_latch_blocked_last_log) >= k_unchanged_heartbeat_interval;
                        if (heartbeat_due)
                        {
                            const auto blocked_for_ms =
                                m_ready_latch_blocked_first_seen.time_since_epoch().count() == 0
                                ? static_cast<long long>(0)
                                : std::chrono::duration_cast<std::chrono::milliseconds>(
                                      now - m_ready_latch_blocked_first_seen)
                                      .count();
                            log_line(message +
                                     " epoch=" + std::to_string(m_session_epoch) +
                                     " count=" + std::to_string(m_ready_latch_blocked_count) +
                                     " blockedMs=" + std::to_string(blocked_for_ms) +
                                     " heartbeat=true");
                            m_ready_latch_blocked_last_log = now;
                        }
                    };
                if (!m_definitive_session_start_seen)
                {
                    const std::string sig = "awaiting_definitive_start|" + m_runtime_role;
                    note_ready_latch_blocked(
                        sig,
                        "[session] ready_latch_blocked reason=awaiting_definitive_start role=" + m_runtime_role);
                }
                else if (!m_role_lock_acquired)
                {
                    const std::string sig = "role_not_locked|" + m_runtime_role;
                    note_ready_latch_blocked(
                        sig,
                        "[session] ready_latch_blocked reason=role_not_locked role=" + m_runtime_role);
                }
                else if (!definitive_ready)
                {
                    const std::string sig = "no_definitive_signal|" + m_runtime_role + "|" + definitive_reason;
                    note_ready_latch_blocked(
                        sig,
                        "[session] ready_latch_blocked reason=no_definitive_signal role=" + m_runtime_role +
                        " detail=" + definitive_reason);
                }
                else
                {
                    std::string ready_reason{};
                    if (is_session_ready_for_role_resolution(&ready_reason))
                    {
                        if (m_ready_latch_blocked_count > 0)
                        {
                            const auto blocked_for_ms =
                                m_ready_latch_blocked_first_seen.time_since_epoch().count() == 0
                                ? static_cast<long long>(0)
                                : std::chrono::duration_cast<std::chrono::milliseconds>(
                                      now - m_ready_latch_blocked_first_seen)
                                      .count();
                            log_line("[session] ready_latch_unblocked summaryCount=" +
                                     std::to_string(m_ready_latch_blocked_count) +
                                     " blockedMs=" + std::to_string(blocked_for_ms) +
                                     " finalSignature=" +
                                     (m_ready_latch_blocked_last_signature.empty()
                                          ? std::string{"none"}
                                          : m_ready_latch_blocked_last_signature) +
                                     " epoch=" + std::to_string(m_session_epoch));
                        }
                        m_session_ready_latched = true;
                        m_ready_latch_blocked_last_signature.clear();
                        m_ready_latch_blocked_last_log = {};
                        m_ready_latch_blocked_first_seen = {};
                        m_ready_latch_blocked_count = 0;
                        refresh_world_text_font_profile("ready_latch", true);
                        m_world_text_font_profile_ready_latched = true;
                        m_phase7_teardown_skip_logged = false;
                        m_ready_baseline_live_keys.clear();
                        m_ready_baseline_capture_remaining_scans = 2;
                        auto* controller = try_get_primary_player_controller();
                        auto* controller_actor = controller && controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
                        m_session_ready_world_id = controller_actor ? build_world_id_for_actor(controller_actor) : std::string{};
                        log_line("[session] ready_latched epoch=" + std::to_string(m_session_epoch) +
                                 " world=" + (m_session_ready_world_id.empty() ? "unknown" : m_session_ready_world_id) +
                                 " role=" + m_runtime_role +
                                 " signal=" + definitive_signal +
                                 " reason=" + (ready_reason.empty() ? "unknown" : ready_reason));
                        trace_behavior_sm("session_ready_latched",
                                          "epoch=" + std::to_string(m_session_epoch) +
                                          " world=" + (m_session_ready_world_id.empty() ? "unknown" : m_session_ready_world_id));
                        if (bind_worldid_for_epoch_if_ready("ready_latch"))
                        {
                            if (controller_actor)
                            {
                                configure_sidecar_for_actor(controller_actor, m_session_ready_world_id);
                            }
                        }
                    }
                }
            }
            if (m_session_ready_latched)
            {
                if (bind_worldid_for_epoch_if_ready("ready_latch_followup"))
                {
                    auto* controller = try_get_primary_player_controller();
                    auto* controller_actor = controller && controller->IsA(AActor::StaticClass()) ? Cast<AActor>(controller) : nullptr;
                    if (controller_actor)
                    {
                        configure_sidecar_for_actor(controller_actor, m_session_ready_world_id);
                    }
                }
                if (m_remote_cached_replay_pending_after_ready)
                {
                    replay_cached_label_text_after_ready("ready_latch_followup");
                    m_remote_cached_replay_pending_after_ready = false;
                }
                maybe_run_first_authoritative_render_pass("ready_latch_followup");
                if (!m_hosted_authority_local_apply_deferred_keys.empty() && should_render_world_text_components())
                {
                    std::vector<std::string> flush_keys{};
                    flush_keys.reserve(m_hosted_authority_local_apply_deferred_keys.size());
                    for (const auto& key : m_hosted_authority_local_apply_deferred_keys)
                    {
                        flush_keys.push_back(key);
                    }
                    size_t flushed = 0;
                    size_t remaining = 0;
                    for (const auto& key : flush_keys)
                    {
                        const auto found = m_labels.find(key);
                        if (found == m_labels.end() || !is_confirmed_label_text_kind(found->second.kind))
                        {
                            m_hosted_authority_local_apply_deferred_keys.erase(key);
                            continue;
                        }
                        size_t actor_hits = 0;
                        bool rendered = false;
                        const auto stable_id = found->second.stable_id;
                        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                            if (!object || !object->IsA(AActor::StaticClass()))
                            {
                                return LoopAction::Continue;
                            }
                            auto* actor = Cast<AActor>(object);
                            if (actor && is_probable_label_actor(actor) && extract_stable_id(actor) == stable_id)
                            {
                                ++actor_hits;
                                rendered = apply_text_to_actor_component(actor, found->second.text) || rendered;
                            }
                            return LoopAction::Continue;
                        });
                        if (actor_hits > 0)
                        {
                            ++flushed;
                            m_hosted_authority_local_apply_deferred_keys.erase(key);
                            log_line("[bridge-hosted] hosted_authority_local_render_flushed key=" + key +
                                     " stableId=" + stable_id +
                                     " actorHits=" + std::to_string(actor_hits) +
                                     " rendered=" + std::string{rendered ? "true" : "false"});
                        }
                        else
                        {
                            ++remaining;
                        }
                    }
                    if (flushed > 0)
                    {
                        log_line("[bridge-hosted] hosted_authority_local_render_flush_summary flushed=" +
                                 std::to_string(flushed) +
                                 " remaining=" + std::to_string(remaining) +
                                 " epoch=" + std::to_string(m_session_epoch));
                    }
                }
                maybe_prewarm_phase7_umg_editor();
            }
        }
        else
        {
            if (!m_session_ready_latched)
            {
                std::string definitive_signal{};
                std::string definitive_reason{};
                const bool definitive_ready = is_definitive_ready_signal_observed(&definitive_signal, &definitive_reason);
                const std::string candidate_sig =
                    "server_candidate|" + m_runtime_role + "|" + (definitive_ready ? std::string{"definitive"} : definitive_reason);
                const bool candidate_changed = m_ready_latch_blocked_last_signature != candidate_sig;
                const bool candidate_heartbeat =
                    m_ready_latch_blocked_last_log.time_since_epoch().count() == 0 ||
                    (now - m_ready_latch_blocked_last_log) >= std::chrono::seconds(20);
                if (candidate_changed || candidate_heartbeat)
                {
                    m_ready_latch_blocked_last_signature = candidate_sig;
                    m_ready_latch_blocked_last_log = now;
                    log_line("[session] server_ready_latch_candidate epoch=" + std::to_string(m_session_epoch) +
                             " role=" + m_runtime_role +
                             " definitive=" + std::string{definitive_ready ? "true" : "false"} +
                             " reason=" + definitive_reason +
                             (candidate_heartbeat && !candidate_changed ? " heartbeat=true" : ""));
                }
                if (definitive_ready)
                {
                    std::string ready_reason{};
                    if (is_session_ready_for_role_resolution(&ready_reason))
                    {
                        m_session_ready_latched = true;
                        m_ready_latch_blocked_last_signature.clear();
                        m_ready_latch_blocked_last_log = {};
                        m_ready_latch_blocked_first_seen = {};
                        m_ready_latch_blocked_count = 0;
                        refresh_world_text_font_profile("server_ready_latch", true);
                        m_world_text_font_profile_ready_latched = true;
                        m_phase7_teardown_skip_logged = false;
                        m_ready_baseline_live_keys.clear();
                        m_ready_baseline_capture_remaining_scans = 2;
                        m_session_ready_world_id = is_hex_world_id(m_world_folder_id) ? m_world_folder_id : std::string{};
                        log_line("[session] ready_latched epoch=" + std::to_string(m_session_epoch) +
                                 " world=" + (m_session_ready_world_id.empty() ? "unknown" : m_session_ready_world_id) +
                                 " role=" + m_runtime_role +
                                 " signal=" + definitive_signal +
                                 " reason=" + (ready_reason.empty() ? "unknown" : ready_reason));

                        const bool world_bound = bind_worldid_for_epoch_if_ready("server_ready_latch");
                        if (world_bound)
                        {
                            const bool hosted_server = lower_ascii(m_runtime_role) == "hostedserver";
                            apply_server_role_classification(
                                hosted_server,
                                hosted_server ? "server_ready_worldid_bind_hosted" : "server_ready_worldid_bind_dedicated");
                            configure_bridge_role("server_ready_worldid_bind");
                            log_line("[session] server_worldid_bound epoch=" + std::to_string(m_session_epoch) +
                                     " role=" + m_runtime_role +
                                     " worldId=" + m_worldid_latched_id);
                            if (hosted_server)
                            {
                                m_hosted_server_next_hello = now;
                                m_hosted_server_next_resync_request = now;
                                log_line("[bridge-hosted] hosted_server_post_ready_relay_bootstrap epoch=" +
                                         std::to_string(m_session_epoch) +
                                         " worldId=" + m_world_folder_id);
                            }
                        }
                        else
                        {
                            log_line("[session] server_worldid_bind_pending epoch=" + std::to_string(m_session_epoch) +
                                     " role=" + m_runtime_role);
                        }
                    }
                }
            }
            else if (bind_worldid_for_epoch_if_ready("server_ready_latch_followup"))
            {
                const bool hosted_server = lower_ascii(m_runtime_role) == "hostedserver";
                if (!is_hex_world_id(m_world_folder_id) || lower_ascii(m_world_folder_id) != lower_ascii(m_worldid_latched_id))
                {
                    apply_server_role_classification(
                        hosted_server,
                        hosted_server ? "server_ready_worldid_followup_hosted" : "server_ready_worldid_followup_dedicated");
                    configure_bridge_role("server_ready_worldid_followup");
                    log_line("[session] server_worldid_bound_followup epoch=" + std::to_string(m_session_epoch) +
                             " role=" + m_runtime_role +
                             " worldId=" + m_worldid_latched_id);
                }
            }
        }
        tick_bridge();
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

        std::string session_window_reason{};
        if (!is_session_window_active_for_gameplay(&session_window_reason))
        {
            const std::string block_sig =
                session_window_reason + "|" + m_runtime_role + "|" + m_authority_mode +
                "|epoch=" + std::to_string(m_session_epoch);
            constexpr auto k_blocked_heartbeat_interval = std::chrono::seconds(20);
            const bool changed = m_session_window_blocked_last_signature != block_sig;
            const bool heartbeat_due =
                m_session_window_blocked_last_log.time_since_epoch().count() == 0 ||
                (now - m_session_window_blocked_last_log) >= k_blocked_heartbeat_interval;
            if (changed || heartbeat_due)
            {
                log_line("[session] gameplay_construction_blocked_until_definitive_start epoch=" +
                         std::to_string(m_session_epoch) +
                         " reason=" + session_window_reason +
                         " runtimeRole=" + m_runtime_role +
                         " authorityMode=" + m_authority_mode);
                m_session_window_blocked_last_log = now;
                m_session_window_blocked_last_signature = block_sig;
            }
            return;
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
                        log_line("[session] reset_ignored reason=non_definitive_world_inactive");
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
                    m_phase4_last_apply_success_at.clear();
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
            request_hotkey_press("imgui_button");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Sidecar"))
        {
            load_sidecar_json();
        }

        ImGui::Separator();
        ImGui::Text("Phase 7 UMG Debug (Temporary)");
        float status_scale_value = m_phase7_debug_status_render_scale;
        float hint_scale_value = m_phase7_debug_hint_render_scale;
        bool status_scale_changed = ImGui::DragFloat("Status Render Scale", &status_scale_value, 0.01f, 0.25f, 2.00f, "%.2f");
        bool hint_scale_changed = ImGui::DragFloat("Hint Render Scale", &hint_scale_value, 0.01f, 0.25f, 2.00f, "%.2f");
        if (status_scale_changed || hint_scale_changed)
        {
            m_phase7_debug_status_render_scale = std::clamp(status_scale_value, 0.25f, 2.00f);
            m_phase7_debug_hint_render_scale = std::clamp(hint_scale_value, 0.25f, 2.00f);
            apply_phase7_umg_debug_scales("imgui_debug_tune");
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
            float autosize_char_width_factor_value = std::clamp(m_autosize_char_width_factor, 0.20f, 2.00f);
            float row_gap_factor_2_value = std::clamp(m_row_gap_factor_2, 0.00f, 3.00f);
            float row_gap_factor_3_value = std::clamp(m_row_gap_factor_3, 0.00f, 3.00f);
            float row_gap_factor_4_value = std::clamp(m_row_gap_factor_4, 0.00f, 3.00f);
            float row_offsets_1_value[1] = {m_row_offsets_1[0]};
            float row_offsets_2_value[2] = {m_row_offsets_2[0], m_row_offsets_2[1]};
            float row_offsets_3_value[3] = {m_row_offsets_3[0], m_row_offsets_3[1], m_row_offsets_3[2]};
            float row_offsets_4_value[4] = {m_row_offsets_4[0], m_row_offsets_4[1], m_row_offsets_4[2], m_row_offsets_4[3]};
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
            if (ImGui::DragFloat("Autosize Char Width Factor", &autosize_char_width_factor_value, 0.01f, 0.20f, 2.00f, "%.2f"))
            {
                m_autosize_char_width_factor = std::clamp(autosize_char_width_factor_value, 0.20f, 2.00f);
                log_line("[autosize] char_width_factor_updated value=" + std::to_string(m_autosize_char_width_factor));
            }
            bool row_gap_factors_changed = false;
            row_gap_factors_changed = ImGui::DragFloat("Row Gap Factor (2 rows)", &row_gap_factor_2_value, 0.01f, 0.00f, 3.00f, "%.2f") || row_gap_factors_changed;
            row_gap_factors_changed = ImGui::DragFloat("Row Gap Factor (3 rows)", &row_gap_factor_3_value, 0.01f, 0.00f, 3.00f, "%.2f") || row_gap_factors_changed;
            row_gap_factors_changed = ImGui::DragFloat("Row Gap Factor (4 rows)", &row_gap_factor_4_value, 0.01f, 0.00f, 3.00f, "%.2f") || row_gap_factors_changed;
            if (row_gap_factors_changed)
            {
                m_row_gap_factor_2 = std::clamp(row_gap_factor_2_value, 0.00f, 3.00f);
                m_row_gap_factor_3 = std::clamp(row_gap_factor_3_value, 0.00f, 3.00f);
                m_row_gap_factor_4 = std::clamp(row_gap_factor_4_value, 0.00f, 3.00f);
                log_line("[autosize] row_gap_factors_updated row2=" + std::to_string(m_row_gap_factor_2) +
                         " row3=" + std::to_string(m_row_gap_factor_3) +
                         " row4=" + std::to_string(m_row_gap_factor_4));
                if (live_surface_tune)
                {
                    const bool rendered = apply_text_to_actor_component(m_selected->actor, found->second.text);
                    log_line("[phase4] row_gap_factors_tune_live key=" + key +
                             " rendered=" + std::string{rendered ? "true" : "false"});
                }
            }
            ImGui::Separator();
            ImGui::Text("Row Offset Tuning (Z offsets)");
            bool row_offsets_changed = false;
            row_offsets_changed = ImGui::DragFloat("Row1 Off0", &row_offsets_1_value[0], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row2 Off0", &row_offsets_2_value[0], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row2 Off1", &row_offsets_2_value[1], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row3 Off0", &row_offsets_3_value[0], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row3 Off1", &row_offsets_3_value[1], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row3 Off2", &row_offsets_3_value[2], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row4 Off0", &row_offsets_4_value[0], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row4 Off1", &row_offsets_4_value[1], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row4 Off2", &row_offsets_4_value[2], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            row_offsets_changed = ImGui::DragFloat("Row4 Off3", &row_offsets_4_value[3], 0.05f, -60.0f, 60.0f, "%.2f") || row_offsets_changed;
            if (row_offsets_changed)
            {
                m_row_offsets_1 = {row_offsets_1_value[0]};
                m_row_offsets_2 = {row_offsets_2_value[0], row_offsets_2_value[1]};
                m_row_offsets_3 = {row_offsets_3_value[0], row_offsets_3_value[1], row_offsets_3_value[2]};
                m_row_offsets_4 = {row_offsets_4_value[0], row_offsets_4_value[1], row_offsets_4_value[2], row_offsets_4_value[3]};
                log_line("[autosize] row_offsets_updated row1=" + std::to_string(m_row_offsets_1[0]) +
                         " row2=" + std::to_string(m_row_offsets_2[0]) + "," + std::to_string(m_row_offsets_2[1]) +
                         " row3=" + std::to_string(m_row_offsets_3[0]) + "," + std::to_string(m_row_offsets_3[1]) + "," + std::to_string(m_row_offsets_3[2]) +
                         " row4=" + std::to_string(m_row_offsets_4[0]) + "," + std::to_string(m_row_offsets_4[1]) + "," + std::to_string(m_row_offsets_4[2]) + "," + std::to_string(m_row_offsets_4[3]));
                if (live_surface_tune)
                {
                    const bool rendered = apply_text_to_actor_component(m_selected->actor, found->second.text);
                    log_line("[phase4] row_offsets_tune_live key=" + key +
                             " rendered=" + std::string{rendered ? "true" : "false"});
                }
            }
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
