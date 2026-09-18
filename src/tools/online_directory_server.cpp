#include "core/online_directory.h"
#include "core/online_session.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {
std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

int main(int argc, char** argv) {
    jojo::NetworkEndpoint endpoint{{
        0u, 0u, 0u, 0u},
        jojo::kOnlineDirectoryDefaultPort};

    if (argc >= 2) {
        const auto parsed = jojo::parse_direct_endpoint(argv[1]);
        if (!parsed) {
            std::cerr << "invalid_bind_endpoint="
                      << parsed.detail << "\n";
            return 2;
        }
        endpoint = parsed.value;
    }

    auto server = jojo::OnlineDirectoryServer::bind(endpoint);
    if (!server) {
        std::cerr << "bind_error=" << server.detail << "\n";
        return 3;
    }

    std::cout << "JOJO online directory listening on "
              << jojo::format_direct_endpoint(
                     server.value.local_endpoint())
              << "\n";
    std::cout.flush();

    for (;;) {
        const auto polled = server.value.poll(now_ms());
        if (!polled) {
            std::cerr << "poll_error=" << polled.detail << "\n";
            return 4;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(5));
    }
}
