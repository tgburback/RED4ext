// Wine/Proton-safe mutex type for RED4ext-internal state.
//
// Under Wine/Proton, the first `lock`/`unlock` of any `std::mutex` during
// DllMain (and, empirically, anywhere inside the Cyberpunk 2077 process)
// triggers EXCEPTION_ACCESS_VIOLATION in MSVCP140._Mtx_unlock. Root cause is
// a layout mismatch between MSVC STL's std::mutex (which accesses
// CRITICAL_SECTION internals directly) and Wine's CRITICAL_SECTION
// implementation. An SRWLOCK-based wrapper sidesteps the issue entirely by
// going through documented Win32 APIs only.
//
// RED4extInternal::wine_compat_mutex is a drop-in for std::mutex in places
// where we own the lock lifetime (members of our System classes). It
// satisfies BasicLockable/Lockable, so std::lock_guard/std::scoped_lock/etc
// work as expected.

#pragma once

#include <mutex>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>

namespace RED4extInternal
{
class srwlock_mutex
{
public:
    srwlock_mutex() = default;
    srwlock_mutex(const srwlock_mutex &) = delete;
    srwlock_mutex &operator=(const srwlock_mutex &) = delete;

    void lock() noexcept { AcquireSRWLockExclusive(&lock_); }
    void unlock() noexcept { ReleaseSRWLockExclusive(&lock_); }
    bool try_lock() noexcept { return TryAcquireSRWLockExclusive(&lock_) != 0; }

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
};

using wine_compat_mutex = srwlock_mutex;
} // namespace RED4extInternal

#else

namespace RED4extInternal
{
using wine_compat_mutex = std::mutex;
} // namespace RED4extInternal

#endif
