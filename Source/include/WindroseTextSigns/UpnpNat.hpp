#pragma once

#include <cstdint>
#include <string>

namespace WindroseTextSigns
{
    struct UpnpNatResult
    {
        bool attempted{false};
        bool ok{false};
        bool com_available{false};
        bool collection_available{false};
        std::string local_ip{};
        uint16_t internal_port{0};
        uint16_t external_port{0};
        std::string protocol{"UDP"};
        std::string message{};
    };

    class UpnpNat
    {
      public:
        static auto map_udp_port(uint16_t internal_port, uint16_t external_port, const std::string& description) -> UpnpNatResult;
    };
}
