#pragma once

namespace MemfdLoad {
    // dlopen of the library in `fd` (a memfd) with `flags`, through /proc/self/fd/<n>. glibc returns an already
    // loaded library whose name matches instead of opening the file, and another plugin may have loaded its own
    // memfd under the same number before closing it: while the name is taken, the memfd moves to another number
    // first. Nothing is loaded unless the path leads to `fd`'s own file. Always closes `fd`. nullptr on failure,
    // with dlerror() set when dlopen itself failed.
    [[nodiscard]] void* loadLibrary(int fd, int flags) noexcept;
}
