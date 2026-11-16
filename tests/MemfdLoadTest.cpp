// MemfdLoad::loadLibrary against the collision glibc makes by name: a library already loaded under
// /proc/self/fd/<n> or /proc/<pid>/fd/<n>, its fd closed, then another memfd put on that same number.
// The second library must be the one loaded.
//   memfd-load-test LIBRARY_A LIBRARY_B   (each built from tests/MemfdTestLibrary.c, B with -DSECOND)
#include "../src/MemfdLoad.hpp"

#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

namespace {
    int failures = 0;

    void check(bool passed, std::string_view what) {
        std::printf("%s: %.*s\n", passed ? "ok" : "FAIL", static_cast<int>(what.size()), what.data());
        if (!passed)
            ++failures;
    }

    // A memfd holding the file at `path`.
    int memfdOf(const char* path) {
        std::ifstream file(path, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const int fd = memfd_create("memfd-load-test", MFD_CLOEXEC);
        if (!file || bytes.empty() || fd < 0 || write(fd, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) {
            std::fprintf(stderr, "cannot copy %s into a memfd\n", path);
            std::exit(2);
        }
        return fd;
    }

    // `first` loaded by plain dlopen under /proc/self/fd/<n> or /proc/<pid>/fd/<n>, its fd closed, then `second`
    // put on that very number and loaded by MemfdLoad::loadLibrary: true when `second`'s symbol is served.
    bool secondIsLoaded(const char* first, const char* second, bool selfForm) {
        const int   firstFd = memfdOf(first);
        const auto  path    = selfForm ? std::format("/proc/self/fd/{}", firstFd) : std::format("/proc/{}/fd/{}", getpid(), firstFd);
        if (!dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE)) {
            std::fprintf(stderr, "%s\n", dlerror());
            std::exit(2);
        }
        close(firstFd);
        int secondFd = memfdOf(second);
        if (secondFd != firstFd) {
            dup2(secondFd, firstFd);
            close(secondFd);
            secondFd = firstFd;
        }
        // A plain dlopen of the same name returns the first library: the collision is real.
        void* plain = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
        check(plain && !dlsym(plain, "memfd_test_second"), "the name alone gives the first library back");
        void* handle = MemfdLoad::loadLibrary(secondFd, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
        return handle && dlsym(handle, "memfd_test_second");
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s LIBRARY_A LIBRARY_B\n", argv[0]);
        return 2;
    }
    // Each case in its own process: a library stays loaded under its name for good (RTLD_NODELETE), so a case run
    // after another would find the names the first one left.
    for (const bool selfForm : {false, true}) {
        std::fflush(stdout);
        const pid_t child = fork();
        if (child == 0) {
            check(secondIsLoaded(argv[1], argv[2], selfForm),
                  selfForm ? "a library left under /proc/self/fd/<n>: the second library is loaded"
                           : "a library left under /proc/<pid>/fd/<n>: the second library is loaded");
            std::fflush(stdout);
            _exit(failures ? 1 : 0);
        }
        int status = 0;
        if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ++failures;
    }
    std::printf("memfd-load: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
