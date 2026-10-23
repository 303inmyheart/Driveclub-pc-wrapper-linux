// SPDX-FileCopyrightText: Copyright 2017 Discord, Inc.
// SPDX-License-Identifier: MIT
// Adapted from externals/discord-rpc/src/connection_unix.cpp; license retained there.
// Linux transport with native and Flatpak IPC discovery; no socket symlinks or
// runtime-directory overrides are required.
#include "connection.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

int GetProcessId()
{
    return ::getpid();
}

struct BaseConnectionUnix : public BaseConnection {
    int sock{-1};
};

static BaseConnectionUnix Connection;
static sockaddr_un PipeAddr{};
#ifdef MSG_NOSIGNAL
static int MsgFlags = MSG_NOSIGNAL;
#else
static int MsgFlags = 0;
#endif

static const char* GetTempPath()
{
    const char* temp = getenv("XDG_RUNTIME_DIR");
    temp = temp ? temp : getenv("TMPDIR");
    temp = temp ? temp : getenv("TMP");
    temp = temp ? temp : getenv("TEMP");
    temp = temp ? temp : "/tmp";
    return temp;
}

/*static*/ BaseConnection* BaseConnection::Create()
{
    PipeAddr.sun_family = AF_UNIX;
    return &Connection;
}

/*static*/ void BaseConnection::Destroy(BaseConnection*& c)
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(c);
    self->Close();
    c = nullptr;
}

bool BaseConnection::Open()
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);
    if (self->isOpen) return true;
    const char* root = GetTempPath();
    const char* directories[] = {"", "/app/com.discordapp.Discord", "/app/dev.vencord.Vesktop"};
    for (const char* suffix : directories) {
        for (int pipeNum = 0; pipeNum < 10; ++pipeNum) {
            const int length = snprintf(PipeAddr.sun_path, sizeof(PipeAddr.sun_path),
                                       "%s%s/discord-ipc-%d", root, suffix, pipeNum);
            if (length < 0 || static_cast<size_t>(length) >= sizeof(PipeAddr.sun_path)) continue;
            const int candidate = socket(AF_UNIX, SOCK_STREAM, 0);
            if (candidate < 0) continue;
            const int flags = fcntl(candidate, F_GETFL, 0);
            if (flags < 0 || fcntl(candidate, F_SETFL, flags | O_NONBLOCK) < 0 ||
                fcntl(candidate, F_SETFD, FD_CLOEXEC) < 0) {
                close(candidate); continue;
            }
            if (connect(candidate, reinterpret_cast<const sockaddr*>(&PipeAddr), sizeof(PipeAddr)) == 0) {
                self->sock = candidate;
                self->isOpen = true;
                return true;
            }
            close(candidate);
        }
    }
    return false;
}

bool BaseConnection::Close()
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);
    if (self->sock == -1) {
        return false;
    }
    close(self->sock);
    self->sock = -1;
    self->isOpen = false;
    return true;
}

bool BaseConnection::Write(const void* data, size_t length)
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);

    if (self->sock == -1) {
        return false;
    }

    ssize_t sentBytes = send(self->sock, data, length, MsgFlags);
    if (sentBytes < 0) {
        Close();
    }
    return sentBytes == (ssize_t)length;
}

bool BaseConnection::Read(void* data, size_t length)
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);

    if (self->sock == -1) {
        return false;
    }

    int res = (int)recv(self->sock, data, length, MsgFlags);
    if (res < 0) {
        if (errno == EAGAIN) {
            return false;
        }
        Close();
    }
    else if (res == 0) {
        Close();
    }
    return res == (int)length;
}
