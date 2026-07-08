#include "sdnotify.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void sdNotifyReady()
{
    const char *socketPath = std::getenv("NOTIFY_SOCKET");
    if (!socketPath || !*socketPath)
        return; // not running under systemd Type=notify — nothing to signal

    const size_t pathLen = std::strlen(socketPath);
    if (pathLen == 0 || pathLen >= sizeof(sockaddr_un::sun_path))
        return; // NOTIFY_SOCKET is always short in practice; refuse to send a truncated path

    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socketPath, pathLen);
    // A leading '@' denotes the abstract namespace on the wire as a leading
    // NUL, not a literal '@' — the rest of the name (and its byte length)
    // is unchanged, so the length computation below is the same either way.
    if (addr.sun_path[0] == '@')
        addr.sun_path[0] = '\0';

    const socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + pathLen);

    static constexpr char kMsg[] = "READY=1";
    sendto(fd, kMsg, sizeof(kMsg) - 1, 0, reinterpret_cast<sockaddr *>(&addr), len);
    close(fd);
}
