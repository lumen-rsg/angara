// =============================================================================
// _wincompat.h — General Windows compatibility shims for Angara native modules
// =============================================================================
// This header provides POSIX API replacements that are missing or different on
// Windows (MSVC / MinGW).  It does NOT include Winsock2 socket shims — use
// _winsock.h for those.
// =============================================================================
#ifndef ANGARA_WINCOMPAT_H
#define ANGARA_WINCOMPAT_H

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600  // Vista+
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// dirent.h — use FindFirstFile/FindNextFile
// ---------------------------------------------------------------------------
#ifndef _DIRENT_DEFINED
#define _DIRENT_DEFINED
struct dirent {
    char d_name[MAX_PATH];
    int d_type;   // 0 = unknown, 4 = dir, 8 = file
};
typedef struct {
    HANDLE handle;
    WIN32_FIND_DATAW ffd;
    struct dirent cur;
    int first;
} DIR;

static inline DIR* opendir(const char* path) {
    if (!path) return NULL;
    wchar_t wpattern[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpattern, MAX_PATH);
    size_t len = wcslen(wpattern);
    if (len + 2 < MAX_PATH) {
        wpattern[len] = L'\\';
        wpattern[len + 1] = L'*';
        wpattern[len + 2] = L'\0';
    }
    DIR* d = (DIR*)calloc(1, sizeof(DIR));
    if (!d) return NULL;
    d->handle = FindFirstFileW(wpattern, &d->ffd);
    if (d->handle == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->first = 1;
    return d;
}

static inline struct dirent* readdir(DIR* d) {
    if (!d) return NULL;
    if (d->first) {
        d->first = 0;
    } else {
        if (!FindNextFileW(d->handle, &d->ffd)) return NULL;
    }
    WideCharToMultiByte(CP_UTF8, 0, d->ffd.cFileName, -1, d->cur.d_name, MAX_PATH, NULL, NULL);
    d->cur.d_type = (d->ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 4 : 8;
    return &d->cur;
}

static inline int closedir(DIR* d) {
    if (!d) return -1;
    FindClose(d->handle);
    free(d);
    return 0;
}
#endif // _DIRENT_DEFINED

// ---------------------------------------------------------------------------
// stat — use _stat64
// ---------------------------------------------------------------------------
#define stat _stat64
#ifndef S_ISREG
#define S_ISREG(m) ((m) & _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) ((m) & _S_IFDIR)
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (0)
#endif

// ---------------------------------------------------------------------------
// mkdir — Windows uses _mkdir
// ---------------------------------------------------------------------------
#define mkdir(path, mode) _mkdir(path)

// ---------------------------------------------------------------------------
// realpath — Windows uses _fullpath
// ---------------------------------------------------------------------------
#define realpath(path, resolved) _fullpath(resolved, path, MAX_PATH)

// ---------------------------------------------------------------------------
// getline — not in MSVCRT
// ---------------------------------------------------------------------------
#ifndef getline
static inline ssize_t wincompat_getline(char** lineptr, size_t* n, FILE* stream) {
    if (!lineptr || !n || !stream) return -1;
    size_t cap = *n;
    if (!*lineptr || cap == 0) {
        cap = 128;
        *lineptr = (char*)malloc(cap);
        if (!*lineptr) return -1;
        *n = cap;
    }
    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= cap) {
            cap *= 2;
            char* tmp = (char*)realloc(*lineptr, cap);
            if (!tmp) return -1;
            *lineptr = tmp;
            *n = cap;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n') break;
    }
    if (pos == 0 && c == EOF) return -1;
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}
#define getline wincompat_getline
#endif

// ---------------------------------------------------------------------------
// usleep — use Sleep
// ---------------------------------------------------------------------------
#define usleep(usec) Sleep(((usec) + 999) / 1000)

// ---------------------------------------------------------------------------
// gettimeofday — not on Windows
// ---------------------------------------------------------------------------
#ifndef gettimeofday
static inline int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    if (!tv) return -1;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    tv->tv_sec = (long)((li.QuadPart - 116444736000000000ULL) / 10000000ULL);
    tv->tv_usec = (long)((li.QuadPart / 10ULL) % 1000000ULL);
    return 0;
}
#endif

// ---------------------------------------------------------------------------
// ioctl(TIOCGWINSZ) — Windows Console API
// ---------------------------------------------------------------------------
#ifndef TIOCGWINSZ
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#define TIOCGWINSZ 0x5413

static inline int ioctl(int fd, unsigned long request, struct winsize* ws) {
    (void)fd;
    if (request == TIOCGWINSZ && ws) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi)) {
            ws->ws_col = (unsigned short)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
            ws->ws_row = (unsigned short)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
    }
    return -1;
}
#endif // TIOCGWINSZ

// ---------------------------------------------------------------------------
// rand_r — MSVC does not provide it
// ---------------------------------------------------------------------------
#ifndef rand_r
static inline int wincompat_rand_r(unsigned int* seed) {
    *seed = *seed * 1103515245 + 12345;
    return (int)((*seed >> 16) & 0x7fff);
}
#define rand_r(seed) wincompat_rand_r(seed)
#endif

#endif // _WIN32
#endif // ANGARA_WINCOMPAT_H
