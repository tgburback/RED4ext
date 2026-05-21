# RED4ext — Wine/Proton compatibility fixes

This branch (`wine-proton-spdlog-fix`) is a fork of [WopsS/RED4ext](https://github.com/WopsS/RED4ext) that adds runtime compatibility with **Wine / Proton**, allowing RED4ext to load and initialize cleanly inside a Cyberpunk 2077 install running under Steam Play on Linux.

It is not a rewrite or a Linux-native port. The DLL still targets Windows and is still built with MSVC. The patches address a small number of binary-compatibility issues that prevent the unmodified upstream build from initializing under Wine, even though it works on native Windows.

---

## TL;DR

Upstream RED4ext crashes during `DllMain` under Wine/Proton with `EXCEPTION_ACCESS_VIOLATION` inside `MSVCP140._Mtx_unlock`. Root cause: MSVC's STL implementation of `std::mutex` reaches into `CRITICAL_SECTION` internals whose layout it assumes matches Windows' own, but Wine's `CRITICAL_SECTION` layout differs. The fix replaces RED4ext's internal `std::mutex` use with an `SRWLOCK`-based wrapper that only touches documented Win32 APIs, and applies the same change inside the bundled spdlog. A defensive SEH guard around logger init catches any residual fault deeper in the sink/filesystem path.

The patch is surgical: seven commits, ~120 lines of substantive change, one new header.

Verified working against Cyberpunk 2077 patch 2.3.1 under recent Proton.

## Why this is needed

Upstream RED4ext has had Linux/Proton compatibility reports for years, none of which are resolved at time of writing:

- [#27 — Installation on Proton (Linux)](https://github.com/WopsS/RED4ext/issues/27) (Aug 2023)
- [#61 — Doesn't work on Linux](https://github.com/WopsS/RED4ext/issues/61) (Jun 2024)
- [#76 — 2.13 / GE Proton-9.16: "No access to memory location"](https://github.com/WopsS/RED4ext/issues/76) (Nov 2024)
- [#109 — Inconsistent hook on Linux/Proton](https://github.com/WopsS/RED4ext/issues/109) (Sep 2025)
- [#115 — Does it work on Linux?](https://github.com/WopsS/RED4ext/issues/115) (Jan 2026)

The "No access to memory location" symptom in #76, with a zero-byte log file, is consistent with the bug class described below: the logger crashed during initialization before it could write anything.

## The bug

When RED4ext is loaded into the Cyberpunk 2077 process under Wine/Proton, the very first `lock()` / `unlock()` of any `std::mutex` — whether in RED4ext's own code, in spdlog's internal registry, or in any other static-storage `std::mutex` brought in by a transitive dependency — triggers an access violation:

```
EXCEPTION_ACCESS_VIOLATION at MSVCP140._Mtx_unlock
```

The crash is reproducible during `DllMain` and, empirically, anywhere else in the process.

### Root cause

MSVC's implementation of `std::mutex` (in `MSVCP140.dll`) is built directly on top of `CRITICAL_SECTION`. Specifically, `_Mtx_lock` / `_Mtx_unlock` do not call the public Win32 functions (`EnterCriticalSection` / `LeaveCriticalSection`); they reach into the `CRITICAL_SECTION` struct's fields directly for performance reasons. This is fine on Windows because MSVC and Windows ship together and the layout is, in practice, stable.

Wine reimplements `CRITICAL_SECTION` and the surrounding Win32 synchronization primitives. The fields are not laid out identically to Windows' implementation. When `MSVCP140` (which Proton ships as a real Microsoft DLL, not a Wine reimplementation) tries to read those fields out of a Wine-initialized `CRITICAL_SECTION`, it reads garbage or unmapped memory.

The result: any DLL that links MSVC's `std::mutex` and is loaded into a Wine process will fault the moment the mutex is used.

This is not a Wine bug in the narrow sense — Wine provides correct behavior through the documented `CRITICAL_SECTION` APIs. It is a binary-compatibility fault line caused by MSVC's STL bypassing those APIs in favor of layout-dependent direct access.

### Why diagnosis took some doing

The crash signature points at `MSVCP140`, which makes it look like an STL bug. The faulting RIP is inside a Microsoft DLL, so a quick read suggests the mutex itself is the problem rather than the `CRITICAL_SECTION` it's built on. Localizing the fault took inserting MC/DC-style probe points around the candidate `std::mutex` uses in `DllMain` to narrow down which mutex was failing and when. (Those probes are in commit history but stripped from the final branch — see `751a146` and `fe7cd84`.)

Once it was clear the fault was in any first use of any `std::mutex`, the layout-mismatch hypothesis followed.

## The fix

Three layers, ordered from most-targeted to most-defensive:

### 1. `WineMutex.hpp` — SRWLOCK-backed mutex

A drop-in replacement for `std::mutex` that satisfies the C++ `Lockable` named requirement (so `std::lock_guard`, `std::scoped_lock`, `std::unique_lock` all work unchanged), but is implemented on top of `SRWLOCK`:

```cpp
class srwlock_mutex {
public:
    void lock() noexcept { AcquireSRWLockExclusive(&lock_); }
    void unlock() noexcept { ReleaseSRWLockExclusive(&lock_); }
    bool try_lock() noexcept { return TryAcquireSRWLockExclusive(&lock_) != 0; }
private:
    SRWLOCK lock_ = SRWLOCK_INIT;
};
```

`SRWLOCK` is opaque from the caller's perspective — there is no layout to mismatch. `Acquire/Release/TryAcquireSRWLockExclusive` are documented Win32 functions that go through Wine's normal Win32 dispatch path.

On non-`_WIN32` builds the alias collapses back to `std::mutex` so cross-platform consumers are unaffected.

The replacement is applied where RED4ext owns the lock's lifetime: `HookingSystem`, `LoggerSystem`, `ScriptCompilationSystem`.

### 2. Patched spdlog

spdlog's internal `logger_registry` and async factory also use `std::mutex`, and they were faulting for the same reason. The bundled spdlog submodule is repointed at [`tgburback/spdlog` branch `wine-srwlock`](https://github.com/tgburback/spdlog/tree/wine-srwlock), which contains two commits applying the same SRWLOCK substitution at the registry level and a related fix to the async factory.

This is the smallest of the changes by line count but the most important for actually getting logging up.

### 3. SEH safety net in `Utils::CreateLogger`

Even with the mutex substitutions, occasional `EXCEPTION_ACCESS_VIOLATION` was observed deeper in the spdlog sink / `std::filesystem` path when the log directory or rotation parameters tripped specific Wine code paths. To prevent these from killing RED4ext init entirely, `Utils.cpp` is compiled with `/EHa` and the logger-construction call is wrapped in `try { ... } catch (...) { fallback to null-sink }`.

This is defensive, not load-bearing — it exists so that a failure deeper in logging cannot prevent the rest of RED4ext from coming up. The rest of the codebase still uses standard C++ exception semantics.

## What this is not

- **Not a Linux build.** The output is still a Windows DLL built with MSVC; it is loaded into a Windows process inside Wine. No Wine-specific code paths exist outside the conditional `_WIN32` block in `WineMutex.hpp`.
- **Not a fork in the social sense.** No new features, no opinion changes, no API extensions. The fork exists to apply binary-compatibility patches that upstream has not adopted.
- **Not a full audit.** Other `std::mutex` uses may exist in transitive dependencies that haven't been hit on the tested code paths. If you encounter a `MSVCP140._Mtx_unlock` crash from a code path not covered here, the same substitution will apply.

---

## For users — getting RED4ext working on Linux/Proton

If you're here because Cyberpunk 2077 + RED4ext doesn't load on your Linux machine:

1. Build the DLL from this branch (`wine-proton-spdlog-fix`) on a Windows machine or in a Windows VM, using the standard upstream build instructions in [BUILDING.md](BUILDING.md). Make sure to initialize submodules recursively — the patched spdlog is required.
2. Install the resulting `winmm.dll` and supporting files into your Cyberpunk install the same way as the upstream binary release.
3. Launch the game through Steam with Proton. No special launch options are required for RED4ext itself.

Tested combinations are intentionally not listed here because they go stale quickly. If it loads and the log file in `red4ext/logs/` is non-empty and contains plugin-init lines, it's working.

This fork is not a substitute for following upstream releases. When upstream addresses Wine compatibility — or breaks it — this fork will lag.

## Building

See [BUILDING.md](BUILDING.md) from upstream. The build process is unchanged. Ensure submodules are initialized recursively to pick up the patched spdlog.

## License

This fork inherits RED4ext's upstream license — see [LICENSE.md](LICENSE.md). The added `WineMutex.hpp` and the modifications to `Utils.cpp`, system headers, and CMake config carry the same license.

## Acknowledgements

- [WopsS](https://github.com/WopsS) and the RED4ext contributors for the original project.
- The [spdlog](https://github.com/gabime/spdlog) maintainers for a clean enough internal structure that the SRWLOCK substitution was straightforward.
- The Proton and Wine projects, whose `CRITICAL_SECTION` implementation makes this fork necessary and whose `SRWLOCK` implementation makes the fix possible.

