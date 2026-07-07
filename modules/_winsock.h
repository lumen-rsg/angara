// =============================================================================
// _winsock.h — Winsock2 compatibility shims for Angara network modules
// =============================================================================
// Include this AFTER _wincompat.h in net modules for full Windows socket support.
// Converts POSIX/BSD socket APIs to Winsock2 equivalents.
// =============================================================================
#ifndef ANGARA_WINSOCK_H
#define ANGARA_WINSOCK_H

#ifdef _WIN32

// Already included by _wincompat.h: windows.h, io.h
#include <winsock2.h>
#include <ws2tcpip.h>

// ---------------------------------------------------------------------------
// Winsock initialization
// ---------------------------------------------------------------------------
static inline int winsock_init(void) {
    static int initialized = 0;
    if (initialized) return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    initialized = 1;
    return 0;
}

// ---------------------------------------------------------------------------
// fd close: close() -> closesocket() for socket fds
// NOTE: This macro affects ALL close() calls in the including translation unit.
// Net modules deal exclusively with sockets, so this is safe.
// ---------------------------------------------------------------------------
#ifdef close
#undef close
#endif
#define close(fd) closesocket(fd)

// ---------------------------------------------------------------------------
// Non-blocking: fcntl(F_SETFL, O_NONBLOCK) -> ioctlsocket(FIONBIO)
// ---------------------------------------------------------------------------
static inline int sock_set_nonblocking(SOCKET fd, int nonblock) {
    u_long mode = nonblock ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode);
}

// We also override the set_nonblocking helper used by net modules.
// Original uses fcntl(); we provide an alternative.
#define set_nonblocking(fd) sock_set_nonblocking(fd, 1)

// ---------------------------------------------------------------------------
// poll() -> WSAPoll() (Vista+)
// ---------------------------------------------------------------------------
#define poll(fds, nfds, timeout) WSAPoll((WSAPOLLFD*)(fds), (ULONG)(nfds), timeout)
#define pollfd WSAPOLLFD
#ifndef POLLIN
#define POLLIN  1
#endif
#ifndef POLLOUT
#define POLLOUT 4
#endif

// ---------------------------------------------------------------------------
// MSG_NOSIGNAL / MSG_DONTWAIT — Winsock does not have them
// ---------------------------------------------------------------------------
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

// ---------------------------------------------------------------------------
// errno mapping for socket errors
// ---------------------------------------------------------------------------
static inline int sock_errno(void) {
    int e = WSAGetLastError();
    switch (e) {
    case WSAEWOULDBLOCK:   return EAGAIN;
    case WSAEINPROGRESS:   return EINPROGRESS;
    case WSAECONNRESET:    return ECONNRESET;
    case WSAECONNABORTED:  return ECONNABORTED;
    case WSAETIMEDOUT:     return ETIMEDOUT;
    case WSAECONNREFUSED:  return ECONNREFUSED;
    case WSAEINTR:         return EINTR;
    default:               return e;
    }
}
// On Windows, errno is NOT set by Winsock calls. Code checking errno after
// socket operations should use sock_errno() instead.
// For convenience in MSG_DONTWAIT recv() patterns (which return -1 with
// EAGAIN on non-blocking), we remap errno reads:
#ifndef errno
#define errno sock_errno()
#endif

// ---------------------------------------------------------------------------
// EWOULDBLOCK — Winsock uses WSAEWOULDBLOCK
// ---------------------------------------------------------------------------
#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif

// ---------------------------------------------------------------------------
// inet_pton — not available on older Windows SDKs; Winsock2 provides InetPton
// ---------------------------------------------------------------------------
#if !defined(NTDDI_VERSION) || NTDDI_VERSION < NTDDI_VISTA
static inline int inet_pton(int af, const char* src, void* dst) {
    struct sockaddr_storage ss;
    int size = sizeof(ss);
    char src_copy[256];
    strncpy(src_copy, src, sizeof(src_copy) - 1);
    src_copy[sizeof(src_copy) - 1] = '\0';
    if (af == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)&ss;
        size = sizeof(*sin);
        sin->sin_family = AF_INET;
    } else if (af == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)&ss;
        size = sizeof(*sin6);
        sin6->sin6_family = AF_INET6;
    } else {
        return -1;
    }
    // Use WSAStringToAddressW for conversion
    wchar_t wsrc[256];
    MultiByteToWideChar(CP_UTF8, 0, src, -1, wsrc, 256);
    return WSAStringToAddressW(wsrc, af, NULL, (LPSOCKADDR)&ss, &size) == 0 ? 1 : 0;
}
#endif

#endif // _WIN32
#endif // ANGARA_WINSOCK_H
