#include "MemfdLoad.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>

namespace {
    // Enough numbers for every library another plugin could have left under our name form.
    constexpr int MAXIMUM_TRIES = 64;

    // The path must lead to this very memfd: a /proc of another pid namespace, or none, would name another file.
    bool pathIsFile(const char* path, int fd) noexcept {
        struct stat byPath {};
        struct stat byDescriptor {};
        return stat(path, &byPath) == 0 && fstat(fd, &byDescriptor) == 0 && byPath.st_dev == byDescriptor.st_dev && byPath.st_ino == byDescriptor.st_ino;
    }
}

void* MemfdLoad::loadLibrary(int fd, int flags) noexcept {
    char path[64];
    for (int attempt = 0; attempt < MAXIMUM_TRIES; ++attempt) {
        std::snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
        // RTLD_NOLOAD loads nothing: it answers whether a library is already known by this name.
        void* taken = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
        if (!taken) {
            if (!pathIsFile(path, fd)) {
                close(fd);
                dlerror(); // the probe's error, if any, is not why nothing was loaded
                return nullptr;
            }
            void* handle = dlopen(path, flags);
            close(fd);
            return handle;
        }
        dlclose(taken);
        const int next = fcntl(fd, F_DUPFD_CLOEXEC, fd + 1);
        close(fd);
        if (next < 0)
            return nullptr;
        fd = next;
    }
    close(fd);
    return nullptr;
}
