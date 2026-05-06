#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <WindroseTextSigns/UpnpNat.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <natupnp.h>

#include <array>
#include <sstream>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ws2_32.lib")

namespace
{
    class BStrHolder
    {
      public:
        explicit BStrHolder(const wchar_t* value) : m_value(SysAllocString(value)) {}
        explicit BStrHolder(const std::wstring& value) : m_value(SysAllocStringLen(value.data(), static_cast<UINT>(value.size()))) {}
        ~BStrHolder()
        {
            if (m_value)
            {
                SysFreeString(m_value);
            }
        }
        BStrHolder(const BStrHolder&) = delete;
        auto operator=(const BStrHolder&) -> BStrHolder& = delete;
        [[nodiscard]] auto get() const -> BSTR { return m_value; }

      private:
        BSTR m_value{};
    };

    auto widen_ascii(const std::string& value) -> std::wstring
    {
        std::wstring out{};
        out.reserve(value.size());
        for (const auto c : value)
        {
            out.push_back(static_cast<unsigned char>(c));
        }
        return out;
    }

    auto hresult_hex(const HRESULT hr) -> std::string
    {
        std::ostringstream out{};
        out << "HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
        return out.str();
    }

    auto query_local_ipv4() -> std::string
    {
        WSADATA winsock_data{};
        const bool started = WSAStartup(MAKEWORD(2, 2), &winsock_data) == 0;
        if (!started)
        {
            return {};
        }

        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET)
        {
            WSACleanup();
            return {};
        }

        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &dest.sin_addr);
        (void)connect(sock, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

        sockaddr_in local{};
        int local_size = sizeof(local);
        std::string out{};
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &local_size) == 0)
        {
            std::array<char, INET_ADDRSTRLEN> buffer{};
            if (inet_ntop(AF_INET, &local.sin_addr, buffer.data(), static_cast<DWORD>(buffer.size())))
            {
                out = buffer.data();
            }
        }

        closesocket(sock);
        WSACleanup();
        return out;
    }
}

namespace WindroseTextSigns
{
    auto UpnpNat::map_udp_port(
        const uint16_t internal_port,
        const uint16_t external_port,
        const std::string& description) -> UpnpNatResult
    {
        UpnpNatResult out{};
        out.attempted = true;
        out.internal_port = internal_port;
        out.external_port = external_port == 0 ? internal_port : external_port;
        out.local_ip = query_local_ipv4();

        if (out.local_ip.empty())
        {
            out.message = "local_ipv4_unavailable";
            return out;
        }

        const HRESULT coinit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool did_init = SUCCEEDED(coinit);
        if (FAILED(coinit) && coinit != RPC_E_CHANGED_MODE)
        {
            out.message = "coinitialize_failed " + hresult_hex(coinit);
            return out;
        }

        IUPnPNAT* nat = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_UPnPNAT, nullptr, CLSCTX_INPROC_SERVER, IID_IUPnPNAT, reinterpret_cast<void**>(&nat));
        if (FAILED(hr) || !nat)
        {
            out.message = "upnpnat_unavailable " + hresult_hex(hr);
            if (did_init)
            {
                CoUninitialize();
            }
            return out;
        }
        out.com_available = true;

        IStaticPortMappingCollection* mappings = nullptr;
        hr = nat->get_StaticPortMappingCollection(&mappings);
        if (FAILED(hr) || !mappings)
        {
            out.message = "mapping_collection_unavailable " + hresult_hex(hr);
            nat->Release();
            if (did_init)
            {
                CoUninitialize();
            }
            return out;
        }
        out.collection_available = true;

        BStrHolder protocol(L"UDP");
        BStrHolder client(widen_ascii(out.local_ip));
        BStrHolder desc(widen_ascii(description.empty() ? "WindroseTextSigns bridge" : description));

        IStaticPortMapping* mapping = nullptr;
        hr = mappings->Add(
            static_cast<long>(out.external_port),
            protocol.get(),
            static_cast<long>(out.internal_port),
            client.get(),
            VARIANT_TRUE,
            desc.get(),
            &mapping);

        if (SUCCEEDED(hr))
        {
            out.ok = true;
            out.message = "mapped";
        }
        else
        {
            out.message = "add_mapping_failed " + hresult_hex(hr);
        }

        if (mapping)
        {
            mapping->Release();
        }
        mappings->Release();
        nat->Release();
        if (did_init)
        {
            CoUninitialize();
        }
        return out;
    }
}
