#pragma once

#include <optional>
#include <string>

namespace WindroseTextSigns
{
    struct RelayHttpResponse
    {
        bool ok{false};
        unsigned long status_code{0};
        std::string body{};
        std::string error{};
    };

    class RelayHttp
    {
      public:
        static auto get(
            const std::string& url,
            const std::string& bearer_token,
            unsigned long timeout_ms) -> RelayHttpResponse;

        static auto post_json(
            const std::string& url,
            const std::string& bearer_token,
            const std::string& body,
            unsigned long timeout_ms) -> RelayHttpResponse;
    };
}
