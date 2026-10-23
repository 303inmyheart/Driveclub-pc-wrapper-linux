// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace DriveclubPC::StartupDiagnostics {
#ifdef __linux__
inline int fd = -1;
inline void Write(const char* data, size_t size) {
    if (fd < 0) return;
    while (size) {
        const auto count = ::write(fd, data, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        data += count;
        size -= count;
    }
}
inline void Mark(std::string_view stage) {
    Write(stage.data(), stage.size());
    Write("\n", 1);
}
inline void Module(std::string_view name, std::string_view state) {
    if (fd < 0) return;
    Write("HLE module: ", 12);
    Write(name.data(), name.size());
    Write(" | ", 3);
    Mark(state);
}
inline void SnapshotMaps();
// Only stack-local formatting and async-signal-safe writes in the fatal path.
// No unwinding, allocator, normal logger or guest memory access here.
inline void Fatal(int sig, siginfo_t* info, void* context) {
    if (fd < 0) return;
    char text[192];
    size_t used = 0;
    const auto append = [&](const char* s) { while (*s) text[used++] = *s++; };
    const auto hex = [&](uintptr_t value) {
        append("0x");
        for (int shift = sizeof(value) * 8 - 4; shift >= 0; shift -= 4)
            text[used++] = "0123456789abcdef"[(value >> shift) & 15];
    };
    append("FATAL signal="); hex(sig);
    append(" tid="); hex(::syscall(SYS_gettid));
    append(" address="); hex(info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0);
#if defined(__x86_64__)
    auto* uc = static_cast<ucontext_t*>(context);
    append(" rip="); hex(uc ? uc->uc_mcontext.gregs[REG_RIP] : 0);
    append(" rsp="); hex(uc ? uc->uc_mcontext.gregs[REG_RSP] : 0);
#endif
    text[used++] = '\n';
    Write(text, used);
    // open/read/write/close are signal-safe; capture modules loaded since the last checkpoint.
    SnapshotMaps();
}
inline void EarlySignal(int sig, siginfo_t* info, void* context) {
    Fatal(sig, info, context);
    // Preserve termination/core-dump semantics instead of swallowing the fault.
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(sig, &action, nullptr);
    raise(sig);
}
inline void SnapshotMaps() {
    if (fd < 0) return;
    Mark("--- process mappings ---");
    int input = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (input >= 0) {
        char bytes[4096];
        ssize_t n;
        while ((n = read(input, bytes, sizeof(bytes))) > 0) Write(bytes, n);
        close(input);
    }
    Mark("--- end mappings ---");
}
inline void Initialize(const char* directory) {
    const char* enabled = std::getenv("DCPC_STARTUP_DIAGNOSTICS");
    if (!enabled || std::strcmp(enabled, "1") != 0) return;
    char path[4096];
    const int n = std::snprintf(path, sizeof(path), "%s/startup-%ld.log", directory, long(getpid()));
    if (n < 0 || size_t(n) >= sizeof(path)) return;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return;
    dprintf(STDERR_FILENO, "Driveclub startup diagnostics: %s\n", path);
    Mark("Driveclub startup diagnostics v1; normal exits have no FATAL record.");
    struct sigaction action{};
    action.sa_sigaction = EarlySignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &action, nullptr);
    SnapshotMaps();
    std::atexit([] { Mark("process: normal exit (atexit)"); });
}
#else
inline void Mark(std::string_view) {}
inline void Module(std::string_view, std::string_view) {}
inline void SnapshotMaps() {}
inline void Initialize(const char*) {}
template <typename... Args> inline void Fatal(Args...) {}
#endif
} // namespace DriveclubPC::StartupDiagnostics
