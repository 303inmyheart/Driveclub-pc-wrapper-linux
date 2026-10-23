// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <stop_token>

namespace DriveclubPC::DeferredRecording {

// One recorder waits, producers publish work before Notify. The generation prevents a
// notification between the caller's work check and Wait from being lost.
class RecorderWake {
public:
    std::uint32_t Snapshot() const {
        return generation.load(std::memory_order_seq_cst);
    }

    void Notify() {
        generation.fetch_add(1, std::memory_order_seq_cst);
        generation.notify_one();
    }

    void Wait(std::uint32_t snapshot, std::stop_token stop) {
        // A stopped recorder must also leave an indefinite atomic wait during shutdown.
        std::stop_callback wake_on_stop{stop, [this] { Notify(); }};
        if (!stop.stop_requested()) {
            generation.wait(snapshot, std::memory_order_seq_cst);
        }
    }

private:
    std::atomic<std::uint32_t> generation{};
};

} // namespace DriveclubPC::DeferredRecording
