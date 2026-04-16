#include "App.hpp"
#include "Image.hpp"
#include "Utils.hpp"

// MC/DC probe helpers. Kept in their own functions so __try/__except can be
// used without running into C2712 (MSVC forbids __try in functions that also
// need C++ object unwinding). Each probe writes one debug-string tag so the
// runtime Wine/Proton log tells us which primitive works and which crashes.
// Removed after diagnosis is complete.
static void RED4ext_McdcProbe_HeapMutex() noexcept
{
    std::mutex* m = new (std::nothrow) std::mutex();
    if (!m)
    {
        OutputDebugStringA("RED4EXT_MCDC: heap std::mutex ALLOC FAILED\n");
        return;
    }
    __try
    {
        m->lock();
        m->unlock();
        OutputDebugStringA("RED4EXT_MCDC: heap std::mutex OK\n");
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        OutputDebugStringA("RED4EXT_MCDC: heap std::mutex CRASHED\n");
    }
}

static void RED4ext_McdcProbe_StaticMutex() noexcept
{
    __try
    {
        static std::mutex staticMutex;
        staticMutex.lock();
        staticMutex.unlock();
        OutputDebugStringA("RED4EXT_MCDC: static std::mutex OK\n");
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        OutputDebugStringA("RED4EXT_MCDC: static std::mutex CRASHED\n");
    }
}

static void RED4ext_McdcProbe_Srwlock() noexcept
{
    __try
    {
        SRWLOCK lock = SRWLOCK_INIT;
        AcquireSRWLockExclusive(&lock);
        ReleaseSRWLockExclusive(&lock);
        OutputDebugStringA("RED4EXT_MCDC: SRWLOCK OK\n");
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        OutputDebugStringA("RED4EXT_MCDC: SRWLOCK CRASHED\n");
    }
}

BOOL APIENTRY DllMain(HMODULE aModule, DWORD aReason, LPVOID aReserved)
{
    RED4EXT_UNUSED_PARAMETER(aReserved);

    switch (aReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(aModule);

        // MC/DC probes — narrow down which synchronization primitive fails
        // under Wine/Proton's DllMain context. Results appear via
        // OutputDebugStringA before any potentially crashing App::Construct
        // call, so the Wine log shows them even if the next step faults.
        OutputDebugStringA("RED4EXT_MCDC: probe run beginning\n");
        RED4ext_McdcProbe_HeapMutex();
        RED4ext_McdcProbe_StaticMutex();
        RED4ext_McdcProbe_Srwlock();
        OutputDebugStringA("RED4EXT_MCDC: probe run complete\n");

        try
        {
            const auto image = Image::Get();
            if (!image->IsCyberpunk())
            {
                break;
            }

            App::Construct();
        }
        catch (const std::exception& e)
        {
            SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An exception occured while loading RED4ext.\n\n{}",
                                                Utils::Widen(e.what()));
        }
        catch (...)
        {
            SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An unknown exception occured while loading RED4ext.");
        }

        break;
    }
    case DLL_PROCESS_DETACH:
    {
        try
        {
            const auto image = Image::Get();
            if (!image->IsCyberpunk())
            {
                break;
            }

            App::Destruct();
        }
        catch (const std::exception& e)
        {
            SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An exception occured while unloading RED4ext.\n\n{}",
                                                Utils::Widen(e.what()));
        }
        catch (...)
        {
            SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An unknown exception occured while unloading RED4ext.");
        }

        break;
    }
    }

    return TRUE;
}
