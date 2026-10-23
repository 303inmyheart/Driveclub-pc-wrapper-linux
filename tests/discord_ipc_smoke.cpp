// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include "connection.h"
int main(int argc, char** argv) {
    auto* client = BaseConnection::Create();
    if (argc > 1 && std::strcmp(argv[1], "missing") == 0) {
        const auto count = [] { return std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator{}); };
        const auto before = count();
        for (int i = 0; i < 100; ++i) assert(!client->Open() && !client->isOpen);
        assert(count() == before);
    } else {
        assert(client->Open() && client->isOpen);
        assert(client->Write("PING", 4));
        char reply[4]{}; bool received = false;
        for (int i = 0; i < 100 && !received; ++i) {
            received = client->Read(reply, 4);
            if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(received && std::memcmp(reply, "PONG", 4) == 0);
        assert(client->Close() && !client->isOpen);
    }
    BaseConnection::Destroy(client);
    assert(client == nullptr);
}
