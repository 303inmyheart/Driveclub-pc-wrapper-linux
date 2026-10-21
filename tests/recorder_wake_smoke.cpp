// SPDX-License-Identifier: GPL-2.0-or-later
#include <atomic>
#include <cassert>
#include <thread>
#include "driveclub/recorder_wake.h"

using DriveclubPC::DeferredRecording::RecorderWake;

int main() {
    RecorderWake wake;
    std::stop_source stop;
    // Publication after the caller's last work check, but before it actually parks.
    const auto old = wake.Snapshot();
    wake.Notify();
    wake.Wait(old, stop.get_token());
    // Stop requested before snapshot/Wait must never park waiting for another producer.
    stop.request_stop();
    wake.Wait(wake.Snapshot(), stop.get_token());

    // Producer/recorder handoff with both pre-park and post-park publications. Assert that
    // the last payload is seen and every acknowledged generation can make progress.
    std::atomic<unsigned> payload{}, ack{};
    std::jthread reader([&](std::stop_token token) {
        for (unsigned n = 1; n <= 10000; ++n) {
            while (payload.load(std::memory_order_acquire) < n) {
                const auto word = wake.Snapshot();
                if (payload.load(std::memory_order_acquire) < n)
                    wake.Wait(word, token);
            }
            ack.store(n, std::memory_order_release);
            ack.notify_one();
        }
    });
    for (unsigned n = 1; n <= 10000; ++n) {
        payload.store(n, std::memory_order_release);
        wake.Notify();
        unsigned seen;
        while ((seen = ack.load(std::memory_order_acquire)) < n)
            ack.wait(seen, std::memory_order_relaxed);
    }
    reader.join();
    assert(ack == 10000);
    // Race a shutdown against an idle recorder entering its wait. jthread destruction
    // requests stop and must join without needing a timeout or another producer.
    for (unsigned n = 0; n < 200; ++n) {
        RecorderWake idle;
        std::jthread thread([&](std::stop_token token) {
            idle.Wait(idle.Snapshot(), token);
        });
        if (n % 2) std::this_thread::yield();
    }
}
