#include <WindroseTextSigns/SignTextMod.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <ios>
#include <regex>
#include <sstream>
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
    extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
    extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* hModule, char* lpFilename, unsigned long nSize);
    constexpr int k_vk_f8 = 0x77;

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

    auto collect_moddata_roots(const std::filesystem::path& cwd, const std::filesystem::path& mod_root) -> std::vector<std::filesystem::path>;

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

        const auto candidates = collect_moddata_roots(cwd, mod_root);
        for (const auto& path : candidates)
        {
            if (std::filesystem::exists(path))
            {
                out.data_root = path;
                break;
            }
        }
        if (out.data_root.empty() && !candidates.empty())
        {
            out.data_root = candidates.front();
        }
        out.profile_root.clear();
        out.world_id = "unknown-world";
        out.data_mode = dedicated_server ? "ServerAuthoritativeFallbackModData" : "LocalClientStartupCacheFallbackModData";
        out.sidecar_kind = dedicated_server ? "authoritative-fallback" : "cache-fallback";
        out.authoritative = dedicated_server;
        if (!dedicated_server)
        {
            out.data_root /= "StartupCache";
        }
        return out;
    }

    auto collect_moddata_roots(const std::filesystem::path& cwd, const std::filesystem::path& mod_root) -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> roots{};
        if (!mod_root.empty())
        {
            auto mods_dir = mod_root.parent_path();
            if (!mods_dir.empty() && lower_ascii_path_token(mods_dir.filename().string()) == "mods")
            {
                auto ue4ss_dir = mods_dir.parent_path();
                if (!ue4ss_dir.empty())
                {
                    append_unique_path(roots, ue4ss_dir / "ModData" / "WindroseTextSigns");
                }
            }
        }

        append_unique_path(roots, cwd / "ue4ss" / "ModData" / "WindroseTextSigns");
        append_unique_path(roots, cwd / "ModData" / "WindroseTextSigns");
        return roots;
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

    auto try_extract_property_log_value(FProperty* prop, UObject* container) -> std::optional<std::string>
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

    auto is_interesting_widget_function_name(const std::string& lower_name) -> bool
    {
        if (lower_name.empty())
        {
            return false;
        }
        static const std::array<const char*, 16> tokens{
            "apply",
            "cancel",
            "confirm",
            "close",
            "accept",
            "commit",
            "submit",
            "click",
            "pressed",
            "focus",
            "text",
            "marker",
            "name",
            "open",
            "show",
            "hide"};
        for (const auto* token : tokens)
        {
            if (token && lower_name.find(token) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    auto is_marker_popup_related_context(const std::string& lower_context_full_name, const std::string& lower_context_class_name) -> bool
    {
        static const std::array<const char*, 8> tokens{
            "wbp_usermarker_customizationpopup",
            "usermarker_customizationpopup",
            "usercreatedmarkers",
            "etxt_markername",
            "uw_buttonconfirm",
            "uw_buttoncancel",
            "wbp_artbutton_accent_tiled",
            "wbp_artbutton_tiled"};
        for (const auto* token : tokens)
        {
            if (!token)
            {
                continue;
            }
            if (lower_context_full_name.find(token) != std::string::npos ||
                lower_context_class_name.find(token) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    auto is_interesting_marker_process_event_name(const std::string& lower_function_full_name) -> bool
    {
        if (lower_function_full_name.empty())
        {
            return false;
        }
        static const std::array<const char*, 8> tokens{
            "oneditabletextchangedevent",
            "oneditabletextcommittedevent",
            "onbuttonclickedevent",
            "onbuttonpressedevent",
            "uw_buttonconfirm",
            "uw_buttoncancel",
            "ia_closepopup",
            "onclose__delegatesignature"};
        for (const auto* token : tokens)
        {
            if (token && lower_function_full_name.find(token) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    auto collect_interesting_functions_for_owner(UStruct* owner, std::vector<std::string>& out_lines, uint32_t max_lines) -> void
    {
        if (!owner || max_lines == 0)
        {
            return;
        }

        std::unordered_set<std::string> seen_full_names{};
        auto* cursor = owner;
        uint32_t safety_hops = 0;
        while (cursor && safety_hops++ < 16 && out_lines.size() < max_lines)
        {
            UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
                if (!object || !object->IsA(UFunction::StaticClass()))
                {
                    return LoopAction::Continue;
                }

                auto* function = Cast<UFunction>(object);
                if (!function || function->GetOuterPrivate() != cursor)
                {
                    return LoopAction::Continue;
                }

                const auto fn_short_name = RC::to_string(function->GetName());
                const auto fn_short_lower = lower_copy_ascii(fn_short_name);
                if (!is_interesting_widget_function_name(fn_short_lower))
                {
                    return LoopAction::Continue;
                }

                const auto fn_full_name = RC::to_string(function->GetFullName());
                if (!seen_full_names.insert(fn_full_name).second)
                {
                    return LoopAction::Continue;
                }

                std::ostringstream row{};
                row << fn_full_name;
                row << " flags=0x" << std::hex << std::uppercase << function->GetFunctionFlags();
                out_lines.push_back(row.str());
                if (out_lines.size() >= max_lines)
                {
                    return LoopAction::Break;
                }

                return LoopAction::Continue;
            });
            cursor = cursor->GetSuperStruct();
        }
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

    auto find_function_by_chain_or_path(UObject* context, const TCHAR* in_chain_name, const TCHAR* path_name) -> UFunction*
    {
        if (context && in_chain_name)
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
        if (!context || !fn)
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
        if (!context || !fn)
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
            if ((prop_name_lower == "value" || prop_name_lower.find("size") != std::string::npos || prop_name_lower.find("scale") != std::string::npos)
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
}

namespace WindroseTextSigns
{
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
        if (m_log.is_open())
        {
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

    auto SignTextMod::is_static_construct_probe_enabled() const -> bool
    {
        if (std::filesystem::exists(m_mod_root / "Config" / "enable_static_construct_probe.flag"))
        {
            return true;
        }
        return config_bool_value("WTS_STATIC_CONSTRUCT_PROBE", false);
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
        m_log.open(m_log_path, std::ios::out | std::ios::app);
        log_line("[startup] WindroseTextSigns initialized");
    }

    auto SignTextMod::log_line(const std::string& line) -> void
    {
        const auto row = now_utc() + " " + line;
        if (m_log.is_open())
        {
            m_log << row << "\n";
            m_log.flush();
        }
        Output::send<LogLevel::Warning>(STR("[WindroseTextSigns] {}"), RC::to_wstring(row));
    }

    auto SignTextMod::on_unreal_init() -> void
    {
        m_mod_root = resolve_mod_root();
        m_static_construct_probe_enabled = is_static_construct_probe_enabled();
        configure_data_root();
        m_legacy_sidecar_path = m_mod_root / "SignTexts.json";
        m_sidecar_path = m_data_root / "SignTexts.json";
        m_backup_root = m_data_root / "Backups";
        m_legacy_sidecar_paths.clear();
        append_unique_path(m_legacy_sidecar_paths, m_legacy_sidecar_path);
        for (const auto& root : collect_moddata_roots(std::filesystem::current_path(), m_mod_root))
        {
            append_unique_path(m_legacy_sidecar_paths, root / "SignTexts.json");
        }

        open_log();
        log_line(std::string{"[build] version=0.1.2-prototype compiled="} + __DATE__ + " " + __TIME__ + " flags=F8,F9,F10,phase2-role-aware-sidecar,remote-cache-routing,staticconstruct-gated");
        log_line("[role] runtimeRole=" + m_runtime_role +
                 " dataMode=" + m_data_mode +
                 " authorityMode=" + m_authority_mode +
                 " sidecarKind=" + m_sidecar_kind +
                 " authoritative=" + std::string{m_sidecar_authoritative ? "true" : "false"} +
                 " profileRoot=" + (m_save_profile_root.empty() ? "none" : m_save_profile_root) +
                 " worldFolderId=" + m_world_folder_id);
        log_line("[save] data_root=" + m_data_root.string() +
                 " sidecar=" + m_sidecar_path.string() +
                 " backups=" + m_backup_root.string());
        log_line("[probe] StaticConstructObject post-probe config enabled=" +
                 std::string{m_static_construct_probe_enabled ? "true" : "false"});

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

        migrate_legacy_sidecar_if_needed();

        load_sidecar_json();
        register_input_hotkey();
        probe_phase7_native_ui_capabilities();
        install_process_event_probe();
        install_static_construct_probe();

        m_unreal_ready = true;
        m_last_restore_scan = std::chrono::steady_clock::now();
        m_last_probe_status = std::chrono::steady_clock::now();

        log_line("[phase] Phase 1 bootstrap active: hooks + hotkey + sidecar loaded");
    }

    auto SignTextMod::register_input_hotkey() -> void
    {
        register_keydown_event(Input::Key::F8, [this]() {
            m_hotkey_requested.store(true);
        });
        register_keydown_event(Input::Key::F10, [this]() {
            m_clear_hotkey_requested.store(true);
        });
        register_keydown_event(Input::Key::F9, [this]() {
            m_buildmenu_probe_requested.store(true);
        });
        log_line("[input] Registered hotkeys: F8=target/open_editor, F9=buildmenu_asset_probe, F10=clear_selected");
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
        auto* controller = try_get_primary_player_controller();
        if (!controller)
        {
            return false;
        }

        bool input_mode_applied = false;
        if (enable_ui_mode)
        {
            input_mode_applied = invoke_no_param(
                controller,
                STR("SetInputModeGameAndUI"),
                STR("/Script/Engine.PlayerController:SetInputModeGameAndUI"));
            if (!input_mode_applied)
            {
                input_mode_applied = invoke_no_param(
                    controller,
                    STR("SetInputModeUIOnly"),
                    STR("/Script/Engine.PlayerController:SetInputModeUIOnly"));
            }
        }
        else
        {
            input_mode_applied = invoke_no_param(
                controller,
                STR("SetInputModeGameOnly"),
                STR("/Script/Engine.PlayerController:SetInputModeGameOnly"));
        }

        const bool cursor_set = set_bool_property_if_present(controller, "bshowmousecursor", enable_ui_mode);
        return input_mode_applied || cursor_set;
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
        log_line("[phase7] open_native_editor success widget=" + narrow_ascii(widget->GetFullName()) +
                 " inputModeApplied=" + std::string{input_mode ? "true" : "false"});
        return true;
    }

    auto SignTextMod::close_phase7_native_editor(bool restore_game_input) -> void
    {
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

    auto SignTextMod::migrate_legacy_sidecar_if_needed() -> void
    {
        if (m_sidecar_path.empty())
        {
            return;
        }
        if (std::filesystem::exists(m_sidecar_path))
        {
            log_line("[save] using sidecar path=" + m_sidecar_path.string());
            return;
        }

        std::vector<std::filesystem::path> existing_legacy_paths{};
        for (const auto& legacy_path : m_legacy_sidecar_paths)
        {
            if (legacy_path.empty() || normalized_path_for_compare(legacy_path) == normalized_path_for_compare(m_sidecar_path))
            {
                continue;
            }
            if (std::filesystem::exists(legacy_path))
            {
                existing_legacy_paths.push_back(legacy_path);
            }
        }

        if (existing_legacy_paths.empty())
        {
            log_line("[save] no legacy sidecar to migrate newPath=" + m_sidecar_path.string());
            return;
        }

        std::sort(existing_legacy_paths.begin(), existing_legacy_paths.end(), [](const auto& lhs, const auto& rhs) {
            std::error_code lhs_ec{};
            std::error_code rhs_ec{};
            return std::filesystem::last_write_time(lhs, lhs_ec) > std::filesystem::last_write_time(rhs, rhs_ec);
        });

        const auto& source_path = existing_legacy_paths.front();
        std::error_code copy_ec{};
        std::filesystem::copy_file(source_path, m_sidecar_path, std::filesystem::copy_options::overwrite_existing, copy_ec);
        if (copy_ec)
        {
            log_line("[save] legacy sidecar migration failed legacyPath=" + source_path.string() +
                     " newPath=" + m_sidecar_path.string() + " error=" + copy_ec.message());
            return;
        }

        const auto marker_path = source_path.parent_path() / "MIGRATED_TO.txt";
        std::ofstream marker(marker_path, std::ios::out | std::ios::trunc);
        if (marker.is_open())
        {
            marker << "WindroseTextSigns sidecar migrated at " << now_utc() << "\n";
            marker << "from=" << source_path.string() << "\n";
            marker << "to=" << m_sidecar_path.string() << "\n";
            marker.close();
        }

        log_line("[save] migrated legacy sidecar legacyPath=" + source_path.string() +
                 " newPath=" + m_sidecar_path.string());
    }

    auto SignTextMod::install_process_event_probe() -> void
    {
        if (m_process_event_probe_id != Hook::ERROR_ID)
        {
            return;
        }

        m_process_event_probe_id = Hook::RegisterProcessEventPreCallback(
            [this](auto&, UObject* context, UFunction* function, void* params) {
                process_event_probe(context, function, params);
            },
            {false, true, STR("WindroseTextSigns"), STR("PlacementAndLabelProbe")});

        if (m_process_event_probe_id == Hook::ERROR_ID)
        {
            log_line("[probe] ProcessEvent pre-probe registration failed (hook unavailable). StaticConstructObject probe enabled=" +
                     std::string{m_static_construct_probe_enabled ? "true" : "false"});
            return;
        }
        log_line("[probe] ProcessEvent pre-probe installed id=" + std::to_string(m_process_event_probe_id));
    }

    auto SignTextMod::install_static_construct_probe() -> void
    {
        if (!m_static_construct_probe_enabled)
        {
            log_line("[probe] StaticConstructObject post-probe skipped reason=disabled");
            return;
        }
        if (m_static_construct_probe_id != Hook::ERROR_ID)
        {
            return;
        }

        m_static_construct_probe_id = Hook::RegisterStaticConstructObjectPostCallback(
            [this](auto& info, const FStaticConstructObjectParameters& params) {
                UObject* constructed = info.GetCurrentResolvedReturnValue();
                static_construct_probe(params, constructed);
            },
            {false, true, STR("WindroseTextSigns"), STR("StaticConstructLabelProbe")});

        if (m_static_construct_probe_id == Hook::ERROR_ID)
        {
            log_line("[probe] StaticConstructObject post-probe registration failed");
            return;
        }
        log_line("[probe] StaticConstructObject post-probe installed id=" + std::to_string(m_static_construct_probe_id));
    }

    auto SignTextMod::uninstall_process_event_probe() -> void
    {
    }

    auto SignTextMod::try_get_primary_player_controller() -> UObject*
    {
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
            const auto full_name = lower_ascii(narrow_ascii(object->GetFullName()));
            if (full_name.find("default__") != std::string::npos)
            {
                continue;
            }
            if (!first_non_default)
            {
                first_non_default = object;
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
            if (full_name.find("bp_r5playercontroller_c") != std::string::npos)
            {
                score += 25;
            }
            if (full_name.find("transitionmap") != std::string::npos || full_name.find("clientlobby") != std::string::npos)
            {
                score -= 15;
            }

            if (score > best_score)
            {
                best_score = score;
                best = object;
            }
        }

        return best ? best : first_non_default;
    }

    auto SignTextMod::is_probable_marker_ui_widget(UObject* object) const -> bool
    {
        if (!object || !object->GetClassPrivate())
        {
            return false;
        }

        const auto full_name = lower_ascii(narrow_ascii(object->GetFullName()));
        const auto class_name = lower_ascii(narrow_ascii(object->GetClassPrivate()->GetFullName()));
        if (full_name.find("default__") != std::string::npos)
        {
            return false;
        }
        return class_name.find("wbp_usermarker_customizationpopup_c") != std::string::npos;
    }

    auto SignTextMod::log_marker_widget_snapshot(UObject* widget) -> void
    {
        if (!widget || !widget->GetClassPrivate())
        {
            return;
        }

        log_line("[phase7-mapui] widget_open candidate=" + narrow_ascii(widget->GetFullName()) +
                 " class=" + narrow_ascii(widget->GetClassPrivate()->GetFullName()));

        {
            const auto widget_class_key = "widgetclass:" + lower_ascii(narrow_ascii(widget->GetClassPrivate()->GetFullName()));
            if (m_marker_widget_callable_logged.insert(widget_class_key).second)
            {
                std::vector<std::string> function_rows{};
                collect_interesting_functions_for_owner(widget->GetClassPrivate(), function_rows, 48);
                if (function_rows.empty())
                {
                    log_line("[phase7-mapui] callable_probe scope=popup class=" +
                             narrow_ascii(widget->GetClassPrivate()->GetFullName()) +
                             " interestingFns=0");
                }
                else
                {
                    log_line("[phase7-mapui] callable_probe scope=popup class=" +
                             narrow_ascii(widget->GetClassPrivate()->GetFullName()) +
                             " interestingFns=" + std::to_string(function_rows.size()));
                    for (const auto& row : function_rows)
                    {
                        log_line("[phase7-mapui] callable popup fn=" + row);
                    }
                }
            }
        }

        {
            static const std::array<const TCHAR*, 12> probe_names{
                STR("OnApply"),
                STR("Apply"),
                STR("Confirm"),
                STR("OnConfirm"),
                STR("Cancel"),
                STR("OnCancel"),
                STR("Close"),
                STR("OnClose"),
                STR("SetText"),
                STR("GetText"),
                STR("OnTextCommitted"),
                STR("OnTextChanged")};
            for (const auto* probe_name : probe_names)
            {
                if (!probe_name || !widget->GetClassPrivate())
                {
                    continue;
                }
                if (auto* probe_fn = widget->GetClassPrivate()->GetFunctionByNameInChain(probe_name))
                {
                    log_line("[phase7-mapui] callable_probe direct popup fn=" + narrow_ascii(probe_fn->GetFullName()));
                }
            }
        }

        uint32_t child_count = 0;
        constexpr uint32_t k_max_child_logs = 32;
        for_each_property_in_chain_compat(widget->GetClassPrivate(), [&](FProperty* prop) {
            if (!prop || child_count >= k_max_child_logs)
            {
                return;
            }
            if (prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
            {
                return;
            }

            auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(widget);
            if (!value_ptr || !*value_ptr || *value_ptr == widget || !(*value_ptr)->GetClassPrivate())
            {
                return;
            }

            const auto prop_name = lower_ascii(RC::to_string(prop->GetName()));
            const auto obj_name = lower_ascii(narrow_ascii((*value_ptr)->GetFullName()));
            const auto obj_class = lower_ascii(narrow_ascii((*value_ptr)->GetClassPrivate()->GetFullName()));

            const bool interesting =
                prop_name.find("text") != std::string::npos ||
                prop_name.find("button") != std::string::npos ||
                prop_name.find("apply") != std::string::npos ||
                prop_name.find("cancel") != std::string::npos ||
                prop_name.find("confirm") != std::string::npos ||
                prop_name.find("icon") != std::string::npos ||
                prop_name.find("markername") != std::string::npos ||
                obj_class.find("editabletext") != std::string::npos ||
                obj_class.find("button") != std::string::npos ||
                obj_class.find("widget") != std::string::npos;
            if (!interesting)
            {
                return;
            }

            ++child_count;
            log_line("[phase7-mapui] child prop=" + prop_name +
                     " obj=" + obj_name +
                     " class=" + obj_class);

            if ((*value_ptr)->GetClassPrivate())
            {
                const auto child_class_key = "childclass:" + lower_ascii(narrow_ascii((*value_ptr)->GetClassPrivate()->GetFullName()));
                if (m_marker_widget_callable_logged.insert(child_class_key).second)
                {
                    std::vector<std::string> child_function_rows{};
                    collect_interesting_functions_for_owner((*value_ptr)->GetClassPrivate(), child_function_rows, 24);
                    log_line("[phase7-mapui] callable_probe scope=child class=" +
                             narrow_ascii((*value_ptr)->GetClassPrivate()->GetFullName()) +
                             " viaProp=" + prop_name +
                             " interestingFns=" + std::to_string(child_function_rows.size()));
                    for (const auto& row : child_function_rows)
                    {
                        log_line("[phase7-mapui] callable child fn=" + row);
                    }

                    static const std::array<const TCHAR*, 10> child_probe_names{
                        STR("OnClicked"),
                        STR("OnPressed"),
                        STR("Click"),
                        STR("Press"),
                        STR("SetText"),
                        STR("GetText"),
                        STR("OnTextChanged"),
                        STR("OnTextCommitted"),
                        STR("SetKeyboardFocus"),
                        STR("SetUserFocus")};
                    for (const auto* probe_name : child_probe_names)
                    {
                        if (!probe_name)
                        {
                            continue;
                        }
                        if (auto* probe_fn = (*value_ptr)->GetClassPrivate()->GetFunctionByNameInChain(probe_name))
                        {
                            log_line("[phase7-mapui] callable_probe direct child prop=" + prop_name +
                                     " fn=" + narrow_ascii(probe_fn->GetFullName()));
                        }
                    }
                }
            }
        });

        bool bridge_has_text = false;
        bool bridge_has_confirm = false;
        bool bridge_has_cancel = false;
        bool bridge_text_get = false;
        bool bridge_text_set = false;
        bool bridge_text_focus = false;
        UObject* bridge_text_widget = nullptr;
        for_each_property_in_chain_compat(widget->GetClassPrivate(), [&](FProperty* prop) {
            if (!prop || prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
            {
                return;
            }
            auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(widget);
            if (!value_ptr || !*value_ptr || !(*value_ptr)->GetClassPrivate())
            {
                return;
            }

            const auto prop_name = lower_ascii(RC::to_string(prop->GetName()));
            const auto class_name = lower_ascii(narrow_ascii((*value_ptr)->GetClassPrivate()->GetFullName()));
            if (prop_name.find("markername") != std::string::npos || class_name.find("editabletext") != std::string::npos)
            {
                bridge_has_text = true;
                bridge_text_widget = *value_ptr;
                bridge_text_get = ((*value_ptr)->GetClassPrivate()->GetFunctionByNameInChain(STR("GetText")) != nullptr);
                bridge_text_set = ((*value_ptr)->GetClassPrivate()->GetFunctionByNameInChain(STR("SetText")) != nullptr);
                bridge_text_focus =
                    ((*value_ptr)->GetClassPrivate()->GetFunctionByNameInChain(STR("SetKeyboardFocus")) != nullptr) ||
                    ((*value_ptr)->GetClassPrivate()->GetFunctionByNameInChain(STR("SetUserFocus")) != nullptr);
            }
            if (prop_name.find("confirm") != std::string::npos || prop_name.find("buttonconfirm") != std::string::npos)
            {
                bridge_has_confirm = true;
            }
            if (prop_name.find("cancel") != std::string::npos || prop_name.find("buttoncancel") != std::string::npos)
            {
                bridge_has_cancel = true;
            }
        });

        bool bridge_text_read_ok = false;
        std::string bridge_text_preview{};
        if (bridge_text_widget)
        {
            bridge_text_read_ok = invoke_get_text_value(bridge_text_widget, bridge_text_preview);
            if (!bridge_text_read_ok)
            {
                bridge_text_read_ok = read_text_property_value_no_process_event(bridge_text_widget, bridge_text_preview);
            }
        }

        log_line("[phase7-mapui] bridge_probe popup=" + lower_ascii(narrow_ascii(widget->GetFullName())) +
                 " text=" + std::string{bridge_has_text ? "1" : "0"} +
                 " getText=" + std::string{bridge_text_get ? "1" : "0"} +
                 " setText=" + std::string{bridge_text_set ? "1" : "0"} +
                 " textFocus=" + std::string{bridge_text_focus ? "1" : "0"} +
                 " readText=" + std::string{bridge_text_read_ok ? "1" : "0"} +
                 " readTextLen=" + std::to_string(bridge_text_preview.size()) +
                 " confirm=" + std::string{bridge_has_confirm ? "1" : "0"} +
                 " cancel=" + std::string{bridge_has_cancel ? "1" : "0"});

        log_line("[phase7-mapui] widget_snapshot_done candidate=" + narrow_ascii(widget->GetFullName()) +
                 " childLogs=" + std::to_string(child_count));
    }

    auto SignTextMod::read_marker_popup_text(UObject* popup_widget) -> std::string
    {
        if (!popup_widget || !popup_widget->GetClassPrivate())
        {
            return {};
        }

        std::string text_value{};
        for_each_property_in_chain_compat(popup_widget->GetClassPrivate(), [&](FProperty* prop) {
            if (!prop || !text_value.empty())
            {
                return;
            }
            if (prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
            {
                return;
            }
            auto prop_name = lower_ascii(RC::to_string(prop->GetName()));
            if (prop_name.find("markername") == std::string::npos &&
                prop_name.find("text") == std::string::npos)
            {
                return;
            }

            auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(popup_widget);
            if (!value_ptr || !*value_ptr || !(*value_ptr)->GetClassPrivate())
            {
                return;
            }

            const auto child_class = lower_ascii(narrow_ascii((*value_ptr)->GetClassPrivate()->GetFullName()));
            if (child_class.find("editabletext") == std::string::npos)
            {
                return;
            }

            for_each_property_in_chain_compat((*value_ptr)->GetClassPrivate(), [&](FProperty* child_prop) {
                if (!child_prop || !text_value.empty())
                {
                    return;
                }
                if (child_prop->GetClass().HashObject() != FTextProperty::StaticClass().HashObject())
                {
                    return;
                }
                auto child_prop_name = lower_ascii(RC::to_string(child_prop->GetName()));
                if (child_prop_name.find("text") == std::string::npos)
                {
                    return;
                }
                auto* text_ptr = child_prop->ContainerPtrToValuePtr<FText>(*value_ptr);
                if (!text_ptr)
                {
                    return;
                }
                text_value = narrow_ascii(text_ptr->ToString());
            });
        });

        return text_value;
    }

    auto SignTextMod::count_live_user_marker_widgets() -> int32_t
    {
        int32_t count = 0;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->GetClassPrivate())
            {
                return LoopAction::Continue;
            }
            const auto full_name = lower_ascii(narrow_ascii(object->GetFullName()));
            if (full_name.find("default__") != std::string::npos)
            {
                return LoopAction::Continue;
            }
            const auto class_name = lower_ascii(narrow_ascii(object->GetClassPrivate()->GetFullName()));
            if (class_name.find("wbp_mapmarker_usercreated_c") != std::string::npos)
            {
                ++count;
            }
            return LoopAction::Continue;
        });
        return count;
    }

    auto SignTextMod::snapshot_live_user_marker_texts() -> std::vector<std::string>
    {
        std::vector<std::string> texts{};
        std::unordered_set<std::string> dedupe{};

        auto maybe_append = [&](std::string value) {
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), value.end());
            if (value.empty())
            {
                return;
            }
            if (dedupe.insert(value).second)
            {
                texts.push_back(std::move(value));
            }
        };

        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object || !object->GetClassPrivate())
            {
                return LoopAction::Continue;
            }

            const auto full_name = lower_ascii(narrow_ascii(object->GetFullName()));
            if (full_name.find("default__") != std::string::npos)
            {
                return LoopAction::Continue;
            }
            const auto class_name = lower_ascii(narrow_ascii(object->GetClassPrivate()->GetFullName()));
            if (class_name.find("wbp_mapmarker_usercreated_c") == std::string::npos)
            {
                return LoopAction::Continue;
            }

            std::string marker_text{};
            if (read_text_property_value_no_process_event(object, marker_text))
            {
                maybe_append(marker_text);
            }

            for_each_property_in_chain_compat(object->GetClassPrivate(), [&](FProperty* prop) {
                if (!prop)
                {
                    return;
                }
                if (prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
                {
                    return;
                }
                auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(object);
                if (!value_ptr || !*value_ptr)
                {
                    return;
                }
                std::string child_text{};
                if (read_text_property_value_no_process_event(*value_ptr, child_text))
                {
                    maybe_append(child_text);
                }
            });

            return LoopAction::Continue;
        });

        return texts;
    }

    auto read_popup_widget_visibility(UObject* popup_widget, bool& out_visible) -> bool
    {
        if (!popup_widget)
        {
            return false;
        }

        if (invoke_bool_return_no_param(
                popup_widget,
                STR("IsInViewport"),
                STR("/Script/UMG.UserWidget:IsInViewport"),
                out_visible))
        {
            return true;
        }
        if (invoke_bool_return_no_param(
                popup_widget,
                STR("IsVisible"),
                STR("/Script/UMG.Widget:IsVisible"),
                out_visible))
        {
            return true;
        }
        return false;
    }

    auto find_child_object_by_property_name(UObject* owner, const std::string& lower_prop_name) -> UObject*
    {
        if (!owner || !owner->GetClassPrivate() || lower_prop_name.empty())
        {
            return nullptr;
        }

        UObject* found = nullptr;
        for_each_property_in_chain_compat(owner->GetClassPrivate(), [&](FProperty* prop) {
            if (found || !prop)
            {
                return;
            }
            if (prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
            {
                return;
            }
            auto prop_name = RC::to_string(prop->GetName());
            std::transform(prop_name.begin(), prop_name.end(), prop_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (prop_name != lower_prop_name)
            {
                return;
            }

            auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(owner);
            if (!value_ptr || !*value_ptr)
            {
                return;
            }
            found = *value_ptr;
        });
        return found;
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

    auto find_marker_popup_owner(UObject* context, const std::function<bool(UObject*)>& is_popup) -> UObject*
    {
        if (!context || !is_popup)
        {
            return nullptr;
        }
        if (is_popup(context))
        {
            return context;
        }

        UObject* outer = context->GetOuterPrivate();
        uint32_t hop_count = 0;
        while (outer && hop_count++ < 16)
        {
            if (is_popup(outer))
            {
                return outer;
            }
            outer = outer->GetOuterPrivate();
        }
        return nullptr;
    }

    auto has_function_in_chain(UObject* context, const TCHAR* function_name) -> bool
    {
        if (!context || !context->GetClassPrivate() || !function_name)
        {
            return false;
        }
        return context->GetClassPrivate()->GetFunctionByNameInChain(function_name) != nullptr;
    }

    auto read_button_pressed_from_widget_like(UObject* widget_like, bool& out_pressed, std::string& out_path_used) -> bool
    {
        if (!widget_like)
        {
            return false;
        }

        if (invoke_bool_return_no_param(
                widget_like,
                STR("IsPressed"),
                STR("/Script/UMG.Button:IsPressed"),
                out_pressed))
        {
            out_path_used = "self";
            return true;
        }

        static const std::array<std::string, 6> nested_prop_candidates{
            "btn_root",
            "button",
            "rootbutton",
            "root_button",
            "btn",
            "uw_button"};

        for (const auto& nested_prop_name : nested_prop_candidates)
        {
            UObject* nested = find_child_object_by_property_name(widget_like, nested_prop_name);
            if (!nested || nested == widget_like)
            {
                continue;
            }

            if (invoke_bool_return_no_param(
                    nested,
                    STR("IsPressed"),
                    STR("/Script/UMG.Button:IsPressed"),
                    out_pressed))
            {
                out_path_used = nested_prop_name;
                return true;
            }
        }

        return false;
    }

    auto read_popup_button_pressed(UObject* popup_widget, const std::string& button_prop_name, bool& out_pressed, std::string& out_path_used) -> bool
    {
        if (!popup_widget || !popup_widget->GetClassPrivate() || button_prop_name.empty())
        {
            return false;
        }

        bool found_button = false;
        UObject* button_obj = nullptr;
        for_each_property_in_chain_compat(popup_widget->GetClassPrivate(), [&](FProperty* prop) {
            if (found_button || !prop)
            {
                return;
            }
            if (prop->GetClass().HashObject() != FObjectProperty::StaticClass().HashObject())
            {
                return;
            }
            auto prop_name = RC::to_string(prop->GetName());
            std::transform(prop_name.begin(), prop_name.end(), prop_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (prop_name != button_prop_name)
            {
                return;
            }
            auto* value_ptr = prop->ContainerPtrToValuePtr<UObject*>(popup_widget);
            if (!value_ptr || !*value_ptr)
            {
                return;
            }
            button_obj = *value_ptr;
            found_button = true;
        });

        if (!found_button || !button_obj)
        {
            return false;
        }

        return read_button_pressed_from_widget_like(button_obj, out_pressed, out_path_used);
    }

    auto SignTextMod::tick_marker_ui_state_probe() -> void
    {
        constexpr bool k_phase7_safe_discovery_mode = true;
        auto* controller = try_get_primary_player_controller();
        if (controller)
        {
            bool show_mouse_cursor = false;
            if (get_bool_property_if_present(controller, "bshowmousecursor", show_mouse_cursor))
            {
                if (!m_cursor_state_known || show_mouse_cursor != m_last_show_mouse_cursor)
                {
                    m_cursor_state_known = true;
                    m_last_show_mouse_cursor = show_mouse_cursor;
                    log_line("[phase7-mapui] cursor_state bShowMouseCursor=" +
                             std::string{show_mouse_cursor ? "true" : "false"} +
                             " controller=" + narrow_ascii(controller->GetFullName()));
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (m_last_marker_probe_scan.time_since_epoch().count() != 0 &&
            (now - m_last_marker_probe_scan) < std::chrono::seconds(1))
        {
            return;
        }
        m_last_marker_probe_scan = now;

        auto close_popup_session = [&](const std::string& close_reason) {
            if (!m_marker_popup_open)
            {
                return;
            }

            const int32_t user_markers_on_close = count_live_user_marker_widgets();
            const auto marker_texts_on_close = snapshot_live_user_marker_texts();
            const int32_t marker_delta = user_markers_on_close - m_marker_popup_user_markers_on_open;
            std::string outcome = "unknown_or_cancel";
            if (m_marker_popup_confirm_clicked && !m_marker_popup_cancel_clicked)
            {
                outcome = "likely_apply_confirm_clicked";
            }
            else if (m_marker_popup_cancel_clicked && !m_marker_popup_confirm_clicked)
            {
                outcome = "likely_cancel_clicked";
            }
            else if (marker_delta > 0)
            {
                outcome = "likely_apply_marker_created";
            }
            else if (marker_delta == 0 && m_marker_popup_last_text != m_marker_popup_text_on_open)
            {
                outcome = "text_changed_but_no_marker_delta";
            }

            std::unordered_set<std::string> open_text_set{};
            for (const auto& text : m_marker_popup_user_marker_texts_on_open)
            {
                open_text_set.insert(text);
            }
            std::vector<std::string> added_marker_texts{};
            for (const auto& text : marker_texts_on_close)
            {
                if (open_text_set.find(text) == open_text_set.end())
                {
                    added_marker_texts.push_back(text);
                }
            }
            if (!added_marker_texts.empty())
            {
                m_marker_popup_last_text = added_marker_texts.front();
            }

            std::string added_marker_texts_joined{};
            for (size_t i = 0; i < added_marker_texts.size(); ++i)
            {
                if (i > 0)
                {
                    added_marker_texts_joined += "|";
                }
                added_marker_texts_joined += added_marker_texts[i];
            }

            log_line("[phase7-mapui] popup_session_close popup=" + m_marker_popup_active_name +
                     " closeReason=" + close_reason +
                     " textOnOpen=\"" + m_marker_popup_text_on_open + "\"" +
                     " textOnClose=\"" + m_marker_popup_last_text + "\"" +
                     " userMarkersOnClose=" + std::to_string(user_markers_on_close) +
                     " markerDelta=" + std::to_string(marker_delta) +
                     " markerTextsOpen=" + std::to_string(m_marker_popup_user_marker_texts_on_open.size()) +
                     " markerTextsClose=" + std::to_string(marker_texts_on_close.size()) +
                     " markerTextsAdded=" + std::to_string(added_marker_texts.size()) +
                     " markerTextsAddedList=\"" + added_marker_texts_joined + "\"" +
                     " confirmClicked=" + std::string{m_marker_popup_confirm_clicked ? "true" : "false"} +
                     " cancelClicked=" + std::string{m_marker_popup_cancel_clicked ? "true" : "false"} +
                     " outcome=" + outcome);

            m_marker_popup_open = false;
            m_marker_popup_active_name.clear();
            m_marker_popup_text_on_open.clear();
            m_marker_popup_last_text.clear();
            m_marker_popup_user_markers_on_open = 0;
            m_marker_popup_user_marker_texts_on_open.clear();
            m_marker_popup_confirm_clicked = false;
            m_marker_popup_cancel_clicked = false;
            m_marker_popup_last_confirm_pressed = false;
            m_marker_popup_last_cancel_pressed = false;
            m_marker_popup_last_visibility = false;
            m_marker_popup_last_direct_text.clear();
            m_marker_popup_last_telemetry_log = {};
            m_marker_popup_gettext_fn_available = false;
            m_marker_popup_settext_fn_available = false;
            m_marker_popup_confirm_fn_available = false;
            m_marker_popup_cancel_fn_available = false;
        };

        UObject* first_popup = nullptr;
        std::string first_popup_name{};
        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!is_probable_marker_ui_widget(object))
            {
                return LoopAction::Continue;
            }

            const auto full_name = narrow_ascii(object->GetFullName());
            if (!first_popup)
            {
                first_popup = object;
                first_popup_name = full_name;
                return LoopAction::Break;
            }
            return LoopAction::Continue;
        });

        UObject* active_popup = first_popup;
        std::string active_popup_name = first_popup_name;

        if (active_popup)
        {
            if (m_marker_widget_logged.insert(active_popup_name).second)
            {
                log_marker_widget_snapshot(active_popup);
            }

            bool popup_visible = true;

            if (m_marker_popup_open && m_marker_popup_active_name != active_popup_name)
            {
                close_popup_session("popup_instance_swapped");
            }

            if (!m_marker_popup_open && popup_visible)
            {
                m_marker_popup_open = true;
                m_marker_popup_active_name = active_popup_name;
                m_marker_popup_text_on_open = "<discovery-safe>";
                m_marker_popup_last_text = "<discovery-safe>";
                m_marker_popup_last_direct_text.clear();
                m_marker_popup_user_markers_on_open = count_live_user_marker_widgets();
                m_marker_popup_user_marker_texts_on_open = snapshot_live_user_marker_texts();
                m_marker_popup_confirm_clicked = false;
                m_marker_popup_cancel_clicked = false;
                m_marker_popup_last_confirm_pressed = false;
                m_marker_popup_last_cancel_pressed = false;
                m_marker_popup_last_visibility = popup_visible;
                m_marker_popup_last_telemetry_log = now;
                UObject* marker_name_widget = find_child_object_by_property_name(active_popup, "etxt_markername");
                m_marker_popup_gettext_fn_available =
                    marker_name_widget && has_function_in_chain(marker_name_widget, STR("GetText"));
                m_marker_popup_settext_fn_available =
                    marker_name_widget && has_function_in_chain(marker_name_widget, STR("SetText"));
                if (UObject* confirm_widget = find_child_object_by_property_name(active_popup, "uw_buttonconfirm"))
                {
                    m_marker_popup_confirm_fn_available =
                        has_function_in_chain(confirm_widget, STR("OnClick__DelegateSignature")) ||
                        has_function_in_chain(confirm_widget, STR("IsPressed"));
                }
                if (UObject* cancel_widget = find_child_object_by_property_name(active_popup, "uw_buttoncancel"))
                {
                    m_marker_popup_cancel_fn_available =
                        has_function_in_chain(cancel_widget, STR("OnClick__DelegateSignature")) ||
                        has_function_in_chain(cancel_widget, STR("IsPressed"));
                }
                log_line("[phase7-mapui] popup_session_open popup=" + m_marker_popup_active_name +
                         " textOnOpen=\"" + m_marker_popup_text_on_open + "\"" +
                         " userMarkersOnOpen=" + std::to_string(m_marker_popup_user_markers_on_open) +
                         " markerTextsOnOpen=" + std::to_string(m_marker_popup_user_marker_texts_on_open.size()));
                log_line("[phase7-mapui] popup_fn_probe popup=" + m_marker_popup_active_name +
                         " markerGetText=" + std::string{m_marker_popup_gettext_fn_available ? "true" : "false"} +
                         " markerSetText=" + std::string{m_marker_popup_settext_fn_available ? "true" : "false"} +
                         " confirmClickable=" + std::string{m_marker_popup_confirm_fn_available ? "true" : "false"} +
                         " cancelClickable=" + std::string{m_marker_popup_cancel_fn_available ? "true" : "false"} +
                         " directTextRead=disabled_safe_mode");
                if (k_phase7_safe_discovery_mode)
                {
                    log_line("[phase7-mapui] safe_discovery_mode enabled: live textbox/button polling disabled");
                }
            }
            else if (m_marker_popup_open)
            {
                if ((m_marker_popup_last_telemetry_log.time_since_epoch().count() == 0 ||
                     (now - m_marker_popup_last_telemetry_log) >= std::chrono::seconds(2)))
                {
                    m_marker_popup_last_telemetry_log = now;
                    log_line("[phase7-mapui] popup_telemetry popup=" + m_marker_popup_active_name +
                             " visible=true mode=safe_discovery");
                }
                m_marker_popup_last_visibility = popup_visible;
            }

            m_marker_widget_active.clear();
            m_marker_widget_active.insert(active_popup_name);
            return;
        }

        close_popup_session("popup_not_found");

        if (!m_marker_widget_active.empty())
        {
            for (const auto& existing : m_marker_widget_active)
            {
                log_line("[phase7-mapui] widget_closed candidate=" + existing);
            }
            m_marker_widget_active.clear();
        }
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
            load_sidecar_json();
        }
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

        const bool local_authority = is_world_authoritative(world);
        const auto safe_world_id = sanitize_path_segment(world_id.empty() ? std::string{"unknown-world"} : world_id);
        if (local_authority)
        {
            auto world_folder_id = m_world_folder_id;
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
            const auto moddata_roots = collect_moddata_roots(std::filesystem::current_path(), m_mod_root);
            cache_base = moddata_roots.empty() ? (m_mod_root / "Cache") : moddata_roots.front();
        }

        set_sidecar_route(
            cache_base / "RemoteCache" / safe_world_id,
            "RemoteClient",
            "RemoteClientCache",
            "ServerAuthoritativePendingBridge",
            "cache",
            false,
            profile_root,
            safe_world_id,
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
            log_line("[target] F8 selection found no Wooden Label candidate candidateCount=0 worldId=" + controller_world_id +
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
            m_hotkey_retry_remaining = 8;
            m_hotkey_retry_next = now;
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
                log_line("[target] F8 selection retries_exhausted");
                m_ui_open = m_phase7_imgui_fallback_enabled;
            }
            return;
        }

        m_hotkey_retry_remaining = 0;
        m_selected = selected;
        m_selected->world_id = selected->world_id;
        const auto world_id = m_selected->world_id;
        const auto key = build_storage_key(world_id, selected->stable_id);
        if (const auto found = m_labels.find(key); found != m_labels.end())
        {
            std::snprintf(m_text_buffer.data(), m_text_buffer.size(), "%s", found->second.text.c_str());
        }
        else
        {
            m_text_buffer.fill('\0');
        }

        const bool native_opened = open_phase7_native_editor_for_selection();
        if (native_opened)
        {
            m_ui_open = false;
            return;
        }

        m_ui_open = m_phase7_imgui_fallback_enabled;
        log_line("[phase7] F8 fallback_to_imgui=" + std::string{m_phase7_imgui_fallback_enabled ? "true" : "false"} +
                 " nativeSupported=" + std::string{m_phase7_native_supported ? "true" : "false"});
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
        log_line("[input] " + action_name + " auto-selected stableId=" + m_selected->stable_id +
                 " actor=" + narrow_ascii(m_selected->actor->GetFullName()));
        return true;
    }

    auto SignTextMod::tick_pending_fallback_hotkeys() -> void
    {
        if (m_clear_hotkey_requested.exchange(false))
        {
            if (ensure_selected_label_for_action("F10 clear"))
            {
                clear_text_on_selected_label();
                m_text_buffer.fill('\0');
                if (m_phase7_native_editor_open)
                {
                    close_phase7_native_editor(true);
                }
            }
        }
        if (m_buildmenu_probe_requested.exchange(false))
        {
            run_buildmenu_asset_probe();
        }
    }

    auto SignTextMod::run_six_sign_targeting_test() -> void
    {
        struct SeenRow
        {
            std::string stable_id{};
            std::string key{};
            std::string actor_name{};
        };

        std::vector<SeenRow> rows{};
        std::unordered_set<std::string> unique_keys{};

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
            const auto world_id = build_world_id_for_actor(actor);
            const auto key = build_storage_key(world_id, stable_id);

            if (unique_keys.insert(key).second)
            {
                rows.push_back(SeenRow{
                    stable_id,
                    key,
                    narrow_ascii(actor->GetFullName())});
            }

            if (rows.size() >= 6)
            {
                return LoopAction::Break;
            }
            return LoopAction::Continue;
        });

        log_line("[test6] start uniqueKeysFound=" + std::to_string(rows.size()));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& row = rows[i];
            log_line("[test6] row=" + std::to_string(i + 1) +
                     " key=" + row.key +
                     " stableId=" + row.stable_id +
                     " actor=" + row.actor_name);
        }

        if (rows.size() >= 6)
        {
            log_line("[test6] PASS foundAtLeastSixUniqueSigns=true");
        }
        else
        {
            log_line("[test6] FAIL foundAtLeastSixUniqueSigns=false uniqueKeysFound=" + std::to_string(rows.size()));
        }
    }

    auto SignTextMod::run_buildmenu_asset_probe() -> void
    {
        log_line("[buildmenu-probe] start");

        std::vector<UObject*> wooden_items{};
        wooden_items.reserve(32);
        std::vector<std::string> widget_candidates{};
        widget_candidates.reserve(32);
        std::vector<std::string> function_candidates{};
        function_candidates.reserve(64);

        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!object)
            {
                return LoopAction::Continue;
            }

            const auto full_name = narrow_ascii(object->GetFullName());
            const auto lower_full_name = lower_ascii(full_name);

            if (lower_full_name.find("da_bi_utilities_lables_wooden_") != std::string::npos)
            {
                wooden_items.push_back(object);
            }

            if (contains_any_token(lower_full_name, {"wbp_", "widgetblueprintgeneratedclass"}) &&
                contains_any_token(lower_full_name, {"build", "menu", "craft", "storage", "beds", "lables", "plaque"}))
            {
                if (widget_candidates.size() < 64)
                {
                    widget_candidates.push_back(full_name);
                }
            }

            if (object->IsA(UFunction::StaticClass()))
            {
                if (contains_any_token(lower_full_name, {
                        "makeconstructcommand",
                        "finishconstruction",
                        "onbuildingaddedtoisland",
                        "buildmenu",
                        "buildingmenu",
                        "craft",
                        "recipe",
                        "lables",
                        "wallplaque"}))
                {
                    if (function_candidates.size() < 128)
                    {
                        function_candidates.push_back(full_name);
                    }
                }
            }

            return LoopAction::Continue;
        });

        log_line("[buildmenu-probe] wooden_items_found=" + std::to_string(wooden_items.size()));
        for (size_t i = 0; i < wooden_items.size() && i < 32; ++i)
        {
            auto* item = wooden_items[i];
            if (!item)
            {
                continue;
            }

            const auto item_full_name = narrow_ascii(item->GetFullName());
            const auto class_name = item->GetClassPrivate()
                ? narrow_ascii(item->GetClassPrivate()->GetFullName())
                : std::string{"unknown"};
            const auto outer_name = item->GetOuterPrivate()
                ? narrow_ascii(item->GetOuterPrivate()->GetFullName())
                : std::string{"none"};

            log_line("[buildmenu-probe] wooden_item index=" + std::to_string(i) +
                     " object=" + item_full_name +
                     " class=" + class_name +
                     " outer=" + outer_name);

            uint32_t logged_fields = 0;
            for_each_property_in_chain_compat(item->GetClassPrivate(), [&](FProperty* prop) {
                if (!prop || logged_fields >= 48)
                {
                    return;
                }
                auto value = try_extract_property_log_value(prop, item);
                if (!value.has_value() || value->empty())
                {
                    return;
                }
                ++logged_fields;
                log_line("[buildmenu-probe] wooden_item_field index=" + std::to_string(i) +
                         " prop=" + lower_ascii(RC::to_string(prop->GetName())) +
                         " value=" + *value);
            });

            log_line("[buildmenu-probe] wooden_item_field_count index=" + std::to_string(i) +
                     " count=" + std::to_string(logged_fields));
        }

        log_line("[buildmenu-probe] widget_candidates_found=" + std::to_string(widget_candidates.size()));
        for (size_t i = 0; i < widget_candidates.size() && i < 40; ++i)
        {
            log_line("[buildmenu-probe] widget_candidate index=" + std::to_string(i) +
                     " value=" + widget_candidates[i]);
        }

        log_line("[buildmenu-probe] function_candidates_found=" + std::to_string(function_candidates.size()));
        for (size_t i = 0; i < function_candidates.size() && i < 80; ++i)
        {
            log_line("[buildmenu-probe] function_candidate index=" + std::to_string(i) +
                     " value=" + function_candidates[i]);
        }

        log_line("[buildmenu-probe] complete");
    }

    auto SignTextMod::tick_file_triggers() -> void
    {
        const auto trigger_path = m_mod_root / "Config" / "run_test6.flag";
        if (std::filesystem::exists(trigger_path))
        {
            log_line("[test6] trigger file detected path=" + trigger_path.string());
            m_six_sign_test_requested.store(true);

            std::error_code remove_ec{};
            std::filesystem::remove(trigger_path, remove_ec);
            if (remove_ec)
            {
                log_line("[test6] trigger file remove failed path=" + trigger_path.string() + " error=" + remove_ec.message());
            }
        }

        const auto buildmenu_trigger_path = m_mod_root / "Config" / "run_buildmenu_probe.flag";
        if (std::filesystem::exists(buildmenu_trigger_path))
        {
            log_line("[buildmenu-probe] trigger file detected path=" + buildmenu_trigger_path.string());
            m_buildmenu_probe_requested.store(true);

            std::error_code remove_ec{};
            std::filesystem::remove(buildmenu_trigger_path, remove_ec);
            if (remove_ec)
            {
                log_line("[buildmenu-probe] trigger file remove failed path=" + buildmenu_trigger_path.string() +
                         " error=" + remove_ec.message());
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
            R"__RX__("([^"]+)"\s*:\s*\{\s*"text"\s*:\s*"((?:\\.|[^"\\])*)"\s*,\s*"asset"\s*:\s*"((?:\\.|[^"\\])*)"(?:\s*,\s*"surfaceAxis"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"surfaceSign"\s*:\s*(-?1))?(?:\s*,\s*"depthOffset"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"alignX"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"alignY"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"fontSize"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"colorR"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"colorG"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"colorB"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"colorA"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*,\s*"lastSeen"\s*:\s*"((?:\\.|[^"\\])*)")?\s*\})__RX__");

        struct ParsedCandidate
        {
            std::filesystem::path path{};
            std::unordered_map<std::string, LabelRecord> labels{};
            size_t parsed_rows{0};
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
                    rec.surface_axis = ((*it)[4].matched) ? std::clamp(safe_stof((*it)[4].str(), 0.0f), 0.0f, 1.0f) : 0.0f;
                    rec.surface_sign = ((*it)[5].matched && safe_stoi((*it)[5].str(), 1) < 0) ? -1 : 1;
                    rec.depth_offset = ((*it)[6].matched) ? safe_stof((*it)[6].str(), 12.0f) : 12.0f;
                    rec.align_x = ((*it)[7].matched) ? safe_stof((*it)[7].str(), 0.0f) : 0.0f;
                    rec.align_y = ((*it)[8].matched) ? safe_stof((*it)[8].str(), 1.5f) : 1.5f;
                    rec.font_size = ((*it)[9].matched) ? std::max(1.0f, safe_stof((*it)[9].str(), 18.0f)) : 18.0f;
                    rec.color_r = ((*it)[10].matched) ? std::clamp(safe_stof((*it)[10].str(), 0.393822f), 0.0f, 1.0f) : 0.393822f;
                    rec.color_g = ((*it)[11].matched) ? std::clamp(safe_stof((*it)[11].str(), 0.393822f), 0.0f, 1.0f) : 0.393822f;
                    rec.color_b = ((*it)[12].matched) ? std::clamp(safe_stof((*it)[12].str(), 0.393822f), 0.0f, 1.0f) : 0.393822f;
                    rec.color_a = ((*it)[13].matched) ? std::clamp(safe_stof((*it)[13].str(), 1.0f), 0.0f, 1.0f) : 1.0f;
                    rec.last_seen_utc = ((*it)[14].matched) ? unescape_json((*it)[14].str()) : std::string{};
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
        for (const auto& [k, rec] : m_labels)
        {
            log_line("[save] load_record key=" + k + " stableId=" + rec.stable_id +
                     " worldId=" + rec.world_id + " path=" + chosen->path.string());
        }
        log_line("[save] load_done records=" + std::to_string(m_labels.size()) +
                 " parsedRows=" + std::to_string(chosen->parsed_rows) +
                 " path=" + chosen->path.string());

        if (restored_from_backup)
        {
            log_line("[save] auto_restore_from_backup source=" + chosen->path.string() +
                     " target=" + m_sidecar_path.string() +
                     " restoredRecords=" + std::to_string(m_labels.size()));
            save_sidecar_json("auto_restore_from_backup", "auto-restore", "auto-restore", "auto-restore");
        }
    }

    auto SignTextMod::save_sidecar_json(const std::string& reason, const std::string& key, const std::string& stable_id, const std::string& world_id) -> void
    {
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
        payload << "  \"version\": 1,\n";
        payload << "  \"runtimeRole\": \"" << escape_json(m_runtime_role) << "\",\n";
        payload << "  \"dataMode\": \"" << escape_json(m_data_mode) << "\",\n";
        payload << "  \"authorityMode\": \"" << escape_json(m_authority_mode) << "\",\n";
        payload << "  \"sidecarKind\": \"" << escape_json(m_sidecar_kind) << "\",\n";
        payload << "  \"authoritative\": " << (m_sidecar_authoritative ? "true" : "false") << ",\n";
        payload << "  \"profileRoot\": \"" << escape_json(m_save_profile_root) << "\",\n";
        payload << "  \"worldFolderId\": \"" << escape_json(m_world_folder_id) << "\",\n";
        payload << "  \"lastWriteUtc\": \"" << escape_json(now_utc()) << "\",\n";
        payload << "  \"labels\": {\n";
        bool first = true;
        for (const auto& [key, rec] : m_labels)
        {
            if (!first) { payload << ",\n"; }
            first = false;
            std::ostringstream axis_value{};
            axis_value << std::fixed << std::setprecision(2) << std::clamp(rec.surface_axis, 0.0f, 1.0f);

            payload << "    \"" << escape_json(key) << "\": { \"text\": \"" << escape_json(rec.text)
                << "\", \"asset\": \"" << escape_json(rec.asset)
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

        // Fallback: if there is exactly one TextRenderComponent on this label actor,
        // treat it as managed to keep runtime update/clear resilient.
        UObject* fallback = nullptr;
        int32_t fallback_count = 0;
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
            fallback = component;
            ++fallback_count;
        }
        if (fallback_count == 1 && fallback)
        {
            m_component_name_cache[storage_key] = narrow_ascii(fallback->GetFullName());
            return fallback;
        }

        return nullptr;
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
            bool destroyed = invoke_no_param(component, STR("K2_DestroyComponent"), STR("/Script/Engine.ActorComponent:K2_DestroyComponent"));
            if (!destroyed)
            {
                destroyed = invoke_no_param(component, STR("DestroyComponent"), STR("/Script/Engine.ActorComponent:DestroyComponent"));
            }
            if (!destroyed)
            {
                destroyed = invoke_no_param(component, STR("UnregisterComponent"), STR("/Script/Engine.ActorComponent:UnregisterComponent"));
            }
            if (!destroyed)
            {
                any_removed = any_removed || hidden_applied || visibility_applied || blanked;
            }
            any_removed = any_removed || destroyed;
        }

        m_component_name_cache.erase(storage_key);
        return any_removed;
    }

    auto SignTextMod::apply_text_to_actor_component(AActor* actor, const std::string& text_value) -> bool
    {
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
            m_phase4_next_retry.erase(key);
            log_line("[phase4] apply_empty_text_clear key=" + key + " removed=" + std::string{removed ? "true" : "false"});
            return removed;
        }

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

        const bool removed_existing = destroy_managed_text_component(actor, key);
        auto* text_component = create_managed_text_component(actor, key, relative_location);
        if (!text_component)
        {
            m_phase4_next_retry[key] = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            log_line("[phase4] apply_failed reason=CreateTextComponent actor=" + narrow_ascii(actor->GetFullName()) +
                     " key=" + key + " removedExisting=" + std::string{removed_existing ? "true" : "false"});
            return false;
        }
        log_line("[phase4] component_created key=" + key +
                 " removedExisting=" + std::string{removed_existing ? "true" : "false"} +
                 " component=" + narrow_ascii(text_component->GetFullName()));

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
        bool colored = invoke_set_text_render_color(text_component, desired_r, desired_g, desired_b, desired_a);

        bool text_applied = invoke_set_text(text_component, text_value);

        // Runtime fallback: if final properties/text fail, rebuild component once.
        if ((!sized || !vcentered || !colored || !text_applied) && text_component)
        {
            log_line("[phase4] update_partial_failure key=" + key +
                     " moved=" + std::string{moved ? "true" : "false"} +
                     " sized=" + std::string{sized ? "true" : "false"} +
                     " vcentered=" + std::string{vcentered ? "true" : "false"} +
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
                colored = invoke_set_text_render_color(text_component, desired_r, desired_g, desired_b, desired_a);
                text_applied = invoke_set_text(text_component, text_value);
            }
        }
        if (!text_applied)
        {
            m_phase4_next_retry[key] = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            log_line("[phase4] apply_failed reason=SetTextFailed key=" + key +
                     " component=" + narrow_ascii(text_component->GetFullName()));
            return false;
        }

        m_phase4_next_retry.erase(key);
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
        const auto has_non_whitespace = std::any_of(text_value.begin(), text_value.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        });
        if (!has_non_whitespace)
        {
            clear_text_on_selected_label();
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
        const auto fit = fit_text_for_plaque(text_value);
        rec.text = fit.wrapped_text;
        rec.font_size = std::clamp(fit.font_size, 10.0f, 20.0f);
        rec.asset = m_selected->asset;
        rec.last_seen_utc = now_utc();

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
        save_sidecar_json("apply", key, rec.stable_id, rec.world_id);
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
        m_component_name_cache.erase(key);
        m_phase4_next_retry.erase(key);
        m_seen_live_label_keys.erase(key);
        m_missing_label_scan_counts.erase(key);
        save_sidecar_json("clear", key, m_selected->stable_id, world_id);
        const bool removed = destroy_managed_text_component(m_selected->actor, key);
        log_line("[phase4] clear_component key=" + key + " removed=" + std::string{removed ? "true" : "false"});
        log_line("[apply] clear_done key=" + key + " stableId=" + m_selected->stable_id +
                 " worldId=" + world_id + " path=" + m_sidecar_path.string());
    }

    auto SignTextMod::restore_known_text_if_any(AActor* actor, const std::string& stable_id) -> void
    {
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
        if (const auto retry = m_phase4_next_retry.find(key); retry != m_phase4_next_retry.end())
        {
            if (std::chrono::steady_clock::now() < retry->second)
            {
                return;
            }
        }
        const auto rendered = m_rendered_text_cache.find(key);
        if (rendered != m_rendered_text_cache.end() && rendered->second == found->second.text)
        {
            if (find_managed_text_component(actor, key))
            {
                return;
            }
        }
        (void)apply_text_to_actor_component(actor, found->second.text);
    }

    auto SignTextMod::process_event_probe(UObject* context, UFunction* function, void* params) -> void
    {
        if (!context || !function)
        {
            return;
        }
        if (m_in_process_event_probe)
        {
            return;
        }
        m_in_process_event_probe = true;
        struct ProbeScopeReset
        {
            bool& flag;
            ~ProbeScopeReset()
            {
                flag = false;
            }
        } reset_scope{m_in_process_event_probe};

        const auto fn = lower_ascii(narrow_ascii(function->GetFullName()));
        // Stability hardening:
        // ProcessEvent UI telemetry for marker popup is disabled because it caused
        // client crashes during text-box interaction in this title.
        const bool construct_interesting = fn.find("makeconstructcommand") != std::string::npos ||
            fn.find("finishconstruction") != std::string::npos ||
            fn.find("onbuildingaddedtoisland") != std::string::npos ||
            fn.find("construct") != std::string::npos;
        if (!construct_interesting)
        {
            return;
        }

        ++m_probe_event_count;
        {
            const auto context_full_name = narrow_ascii(context->GetFullName());
            const auto context_class_name = context->GetClassPrivate()
                ? narrow_ascii(context->GetClassPrivate()->GetFullName())
                : std::string{"unknown"};

            std::ostringstream row{};
            row << "[probe] fn=" << fn;
            row << " context=" << context_full_name;
            row << " class=" << context_class_name;
            if (auto id = try_extract_guid_from_params(function, params); id.has_value())
            {
                row << " guidFromParams=" << *id;
            }
            if (context->IsA(AActor::StaticClass()))
            {
                auto* actor = Cast<AActor>(context);
                if (actor && is_probable_label_actor(actor))
                {
                    ++m_probe_label_hit_count;
                    row << " labelActor=1";
                    row << " stableId=" << extract_stable_id(actor);
                    row << " asset=" << detect_label_asset(actor);
                }
            }
            log_line(row.str());
        }
    }

    auto SignTextMod::static_construct_probe(const FStaticConstructObjectParameters& params, UObject* constructed_object) -> void
    {
        ++m_construct_event_count;
        if (!constructed_object || !constructed_object->IsA(AActor::StaticClass()))
        {
            return;
        }

        auto* actor = Cast<AActor>(constructed_object);
        if (!actor || !is_probable_label_actor(actor))
        {
            return;
        }

        const auto stable_id = extract_stable_id(actor);
        const auto actor_world_id = build_world_id_for_actor(actor);
        configure_sidecar_for_actor(actor, actor_world_id);
        const auto world_id = active_storage_world_id(actor_world_id);
        const auto key = build_storage_key(world_id, stable_id);
        const auto now = std::chrono::steady_clock::now();
        if (const auto seen = m_construct_probe_last_seen.find(key); seen != m_construct_probe_last_seen.end())
        {
            if ((now - seen->second) < std::chrono::milliseconds(500))
            {
                return;
            }
        }
        m_construct_probe_last_seen[key] = now;

        // If this key was missing in recent scans and now re-appears, treat it as
        // a rebuilt/reused instance key and drop stale persisted text before restore.
        if (const auto miss_it = m_missing_label_scan_counts.find(key);
            miss_it != m_missing_label_scan_counts.end() && miss_it->second > 0)
        {
            if (const auto found = m_labels.find(key); found != m_labels.end())
            {
                log_line("[save] prune_on_reuse key=" + key +
                         " stableId=" + found->second.stable_id +
                         " worldId=" + found->second.world_id +
                         " missCount=" + std::to_string(miss_it->second));
                m_labels.erase(found);
                m_rendered_text_cache.erase(key);
                m_component_name_cache.erase(key);
                m_phase4_next_retry.erase(key);
                m_seen_live_label_keys.erase(key);
                m_missing_label_scan_counts.erase(key);
                save_sidecar_json("prune_on_reuse", key, stable_id, world_id);
            }
        }

        ++m_construct_label_hit_count;
        std::string class_name{"unknown"};
        std::string outer_name{"unknown"};
        std::string req_name{"unknown"};

        if (params.Class)
        {
            class_name = narrow_ascii(params.Class->GetFullName());
        }
        if (params.Outer)
        {
            outer_name = narrow_ascii(params.Outer->GetFullName());
        }
        if (params.Name.ToString().size() > 0)
        {
            req_name = narrow_ascii(params.Name.ToString());
        }

        log_line("[probe-sco] class=" + class_name +
                 " outer=" + outer_name +
                 " requestedName=" + req_name +
                 " actor=" + narrow_ascii(actor->GetFullName()) +
                 " stableId=" + stable_id +
                 " asset=" + detect_label_asset(actor));
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

        const auto world_id = lower_ascii(build_world_id_for_actor(controller_actor));
        return world_id.find("genlandia") != std::string::npos;
    }

    auto SignTextMod::on_update() -> void
    {
        if (!m_unreal_ready)
        {
            return;
        }

        // F8 reliability fallback:
        // capture hardware edge directly in case callback registration misses events
        // while input focus/context changes.
        const bool f8_is_down = ((GetAsyncKeyState(k_vk_f8) & 0x8000) != 0);
        if (f8_is_down && !m_f8_poll_was_down)
        {
            m_hotkey_requested.store(true);
            log_line("[input] F8 polled_edge");
        }
        m_f8_poll_was_down = f8_is_down;

        tick_pending_hotkey();
        tick_pending_fallback_hotkeys();
        tick_file_triggers();
        tick_marker_ui_state_probe();
        if (m_six_sign_test_requested.exchange(false))
        {
            run_six_sign_targeting_test();
        }
        const auto now = std::chrono::steady_clock::now();

        if (now - m_last_restore_scan > std::chrono::seconds(2))
        {
            m_last_restore_scan = now;
            if (!is_restore_scan_world_active())
            {
                m_consecutive_empty_label_scans = 0;
                if (!m_restore_scan_wait_logged)
                {
                    log_line("[save] restore_scan waiting reason=no_active_world");
                    m_restore_scan_wait_logged = true;
                }
                return;
            }
            if (m_restore_scan_wait_logged)
            {
                log_line("[save] restore_scan active");
                m_restore_scan_wait_logged = false;
            }

            std::unordered_set<std::string> present_label_keys{};
            std::unordered_map<std::string, uint32_t> present_world_counts{};
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
                present_label_keys.insert(key);
                ++present_world_counts[world_id];
                m_seen_live_label_keys.insert(key);
                m_missing_label_scan_counts.erase(key);
                restore_known_text_if_any(actor, stable_id);
                return LoopAction::Continue;
            });

            if (!present_label_keys.empty())
            {
                m_consecutive_empty_label_scans = 0;
            }
            else
            {
                ++m_consecutive_empty_label_scans;
            }

            // Destroy cleanup:
            // - only prune while at least one live label is visible in the current world scan.
            // - this avoids deleting all persisted records during disconnect/map travel when
            //   no labels are visible temporarily.
            const bool allow_prune = !present_label_keys.empty();
            if (allow_prune)
            {
                constexpr uint32_t k_prune_missing_scan_threshold = 4;
                constexpr uint32_t k_min_live_labels_in_world_for_prune = 2;
                std::unordered_set<std::string> keys_to_prune{};
                for (const auto& [key, rec] : m_labels)
                {
                    if (present_label_keys.find(key) != present_label_keys.end())
                    {
                        continue;
                    }
                    const auto world_it = present_world_counts.find(rec.world_id);
                    if (world_it == present_world_counts.end() || world_it->second < k_min_live_labels_in_world_for_prune)
                    {
                        continue;
                    }
                    if (m_seen_live_label_keys.find(key) == m_seen_live_label_keys.end())
                    {
                        continue;
                    }
                    uint32_t& miss_count = m_missing_label_scan_counts[key];
                    ++miss_count;
                    if (miss_count >= k_prune_missing_scan_threshold)
                    {
                        keys_to_prune.insert(key);
                    }
                }

                if (!m_labels.empty() && keys_to_prune.size() == m_labels.size())
                {
                    log_line("[save] prune_destroyed_label skipped reason=mass_prune_guard candidateCount=" +
                             std::to_string(keys_to_prune.size()) +
                             " presentWorlds=" + std::to_string(present_world_counts.size()));
                    keys_to_prune.clear();
                }

                uint32_t pruned_count = 0;
                for (const auto& key : keys_to_prune)
                {
                    auto found = m_labels.find(key);
                    if (found == m_labels.end())
                    {
                        continue;
                    }
                    log_line("[save] prune_destroyed_label key=" + key +
                             " stableId=" + found->second.stable_id +
                             " worldId=" + found->second.world_id);
                    m_labels.erase(found);
                    m_rendered_text_cache.erase(key);
                    m_component_name_cache.erase(key);
                    m_phase4_next_retry.erase(key);
                    m_seen_live_label_keys.erase(key);
                    m_missing_label_scan_counts.erase(key);
                    ++pruned_count;
                }

                if (pruned_count > 0)
                {
                    save_sidecar_json(
                        "prune_destroyed_label_batch",
                        "batch:" + std::to_string(pruned_count),
                        "batch",
                        "batch");
                }
            }
            else
            {
                log_line("[save] prune_destroyed_label deferred reason=no_live_labels_visible count=" +
                         std::to_string(m_consecutive_empty_label_scans));
            }
        }

        if (now - m_last_probe_status > std::chrono::seconds(20))
        {
            m_last_probe_status = now;
            log_line("[probe-status] events=" + std::to_string(m_probe_event_count) +
                     " labelHits=" + std::to_string(m_probe_label_hit_count) +
                     " scoEvents=" + std::to_string(m_construct_event_count) +
                     " scoLabelHits=" + std::to_string(m_construct_label_hit_count) +
                     " records=" + std::to_string(m_labels.size()));
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
        ImGui::Text("Hotkey: F8");
        ImGui::Text("Build-menu probe hotkey: F9");
        ImGui::Text("Clear hotkey: F10");
        ImGui::Text("Tests: Config/run_test6.flag, Config/run_buildmenu_probe.flag");
        ImGui::Text("Probe events: %llu", static_cast<unsigned long long>(m_probe_event_count));
        ImGui::Text("Label hits: %llu", static_cast<unsigned long long>(m_probe_label_hit_count));
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

        if (ImGui::Button("Target Label From Camera (F8 logic)"))
        {
            m_hotkey_requested.store(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Sidecar"))
        {
            load_sidecar_json();
        }
        ImGui::SameLine();
        if (ImGui::Button("Run Test (6 Signs)"))
        {
            m_six_sign_test_requested.store(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Run BuildMenu Asset Probe (F9)"))
        {
            m_buildmenu_probe_requested.store(true);
        }

        if (!m_selected.has_value())
        {
            ImGui::TextDisabled("Look at Wooden Label then press F8.");
            return;
        }
        if (!ensure_selected_actor_valid("render_ui"))
        {
            ImGui::TextDisabled("Selected label no longer exists. Look at a Wooden Label and press F8.");
            return;
        }

        if (!m_phase7_imgui_fallback_enabled)
        {
            ImGui::Separator();
            ImGui::TextDisabled("ImGui text editor is disabled (Phase 7 native-first mode).");
            ImGui::TextDisabled("Use F8 to invoke native path, or enable fallback above for diagnostics.");
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
