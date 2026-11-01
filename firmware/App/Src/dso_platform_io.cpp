// Copyright 2026 Martin Dodek
// SPDX-License-Identifier: MIT
//
// This instrument has no file or console backend. Supply the Newlib ABI
// explicitly so unsupported I/O reports a failure rather than pulling in
// toolchain placeholder implementations. These functions are normally removed
// by section garbage collection because the application never calls stdio.
#include <cerrno>
#include <cstddef>
#include <sys/types.h>

extern "C" int _close(int)
{
    errno = EBADF;
    return -1;
}

extern "C" off_t _lseek(int, off_t, int)
{
    errno = EBADF;
    return static_cast<off_t>(-1);
}

extern "C" ssize_t _read(int, void*, std::size_t)
{
    errno = EBADF;
    return static_cast<ssize_t>(-1);
}

extern "C" ssize_t _write(int, const void*, std::size_t)
{
    errno = EBADF;
    return static_cast<ssize_t>(-1);
}
