#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <winhttp.h>

#include <WindroseTextSigns/RelayHttp.hpp>

#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    auto widen_utf8(const std::string& value) -> std::wstring
    {
        if (value.empty())
        {
            return {};
        }
        const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (needed <= 0)
        {
            return {};
        }
        std::wstring out(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed);
        return out;
    }

    auto narrow_utf8(const std::wstring& value) -> std::string
    {
        if (value.empty())
        {
            return {};
        }
        const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return {};
        }
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed, nullptr, nullptr);
        return out;
    }

    auto last_error_string(const char* prefix) -> std::string
    {
        return std::string{prefix} + " winerr=" + std::to_string(GetLastError());
    }

    auto request(
        const wchar_t* method,
        const std::string& url,
        const std::string& bearer_token,
        const std::string& body,
        const unsigned long timeout_ms) -> WindroseTextSigns::RelayHttpResponse
    {
        WindroseTextSigns::RelayHttpResponse out{};
        const auto wide_url = widen_utf8(url);
        if (wide_url.empty())
        {
            out.error = "invalid_url";
            return out;
        }

        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &parts))
        {
            out.error = last_error_string("WinHttpCrackUrl");
            return out;
        }

        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength > 0)
        {
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        }

        HINTERNET session = WinHttpOpen(
            L"WindroseTextSigns/0.1",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!session)
        {
            out.error = last_error_string("WinHttpOpen");
            return out;
        }

        const int timeout = static_cast<int>(std::clamp<unsigned long>(timeout_ms, 500ul, 30000ul));
        WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

        HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
        if (!connect)
        {
            out.error = last_error_string("WinHttpConnect");
            WinHttpCloseHandle(session);
            return out;
        }

        const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET req = WinHttpOpenRequest(
            connect,
            method,
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags);
        if (!req)
        {
            out.error = last_error_string("WinHttpOpenRequest");
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return out;
        }

        std::wstring headers = L"Accept: application/json\r\n";
        if (!bearer_token.empty())
        {
            headers += L"Authorization: Bearer " + widen_utf8(bearer_token) + L"\r\n";
        }
        if (!body.empty())
        {
            headers += L"Content-Type: application/json; charset=utf-8\r\n";
        }

        void* request_data = body.empty() ? nullptr : static_cast<void*>(const_cast<char*>(body.data()));
        const BOOL sent = WinHttpSendRequest(
            req,
            headers.c_str(),
            static_cast<DWORD>(headers.size()),
            request_data,
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0);
        if (!sent)
        {
            out.error = last_error_string("WinHttpSendRequest");
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return out;
        }

        if (!WinHttpReceiveResponse(req, nullptr))
        {
            out.error = last_error_string("WinHttpReceiveResponse");
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return out;
        }

        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &status_size, nullptr))
        {
            out.status_code = status;
        }

        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(req, &available) || available == 0)
            {
                break;
            }
            std::vector<char> buffer(static_cast<size_t>(available));
            DWORD read = 0;
            if (!WinHttpReadData(req, buffer.data(), available, &read) || read == 0)
            {
                break;
            }
            out.body.append(buffer.data(), static_cast<size_t>(read));
        }

        out.ok = out.status_code >= 200 && out.status_code < 300;
        if (!out.ok && out.error.empty())
        {
            out.error = "http_status_" + std::to_string(out.status_code);
        }

        WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
}

namespace WindroseTextSigns
{
    auto RelayHttp::get(
        const std::string& url,
        const std::string& bearer_token,
        const unsigned long timeout_ms) -> RelayHttpResponse
    {
        return request(L"GET", url, bearer_token, {}, timeout_ms);
    }

    auto RelayHttp::post_json(
        const std::string& url,
        const std::string& bearer_token,
        const std::string& body,
        const unsigned long timeout_ms) -> RelayHttpResponse
    {
        return request(L"POST", url, bearer_token, body, timeout_ms);
    }
}
