#pragma once

// The one header a project's entry points and application include. It knows the modules only by
// the O2LIBS_<MODULE> definitions their targets publish, so a build without a module - or a
// project that links no o2libs at all - compiles the same lines to nothing:
//
//     #if __has_include("o2libs/o2libs.h")
//     #include "o2libs/o2libs.h"
//     #endif
//     ...
//     O2LIBS_INITIALIZE_TYPES;      next to InitializeTypesGameLib()
//     app->Initialize();
//     O2LIBS_START;                 the engine is up: assets, network, tasks
//     app->Launch();
//
// Both live in the entry points, not in the game's application class: a script-only game on a stock
// runtime gets its modules started without a line of C++. Modules are singletons (o2RemoteConfig, o2Storage, ...) created here and pump themselves through o2Tasks.

#if defined(O2LIBS_CORE)
#include "o2libs/Core/PlayerIdentity.h"
#include "o2libs/Core/Storage.h"
extern void InitializeTypeso2libsCore();
#endif

#if defined(O2LIBS_REMOTE_CONFIG)
#include "o2libs/RemoteConfig/RemoteConfig.h"
extern void InitializeTypeso2libsRemoteConfig();
#endif

#if defined(O2LIBS_SAVES)
#include "o2libs/Saves/PlayerSaves.h"
extern void InitializeTypeso2libsSaves();
#endif

namespace o2libs
{
    inline bool& TypesInitialized()
    {
        static bool done = false;
        return done;
    }

    // Registers the reflected types of the modules in this build; before the application is created
    inline void InitializeTypes()
    {
        if (TypesInitialized())
            return;

        TypesInitialized() = true;

#if defined(O2LIBS_CORE)
        InitializeTypeso2libsCore();
#endif
#if defined(O2LIBS_REMOTE_CONFIG)
        InitializeTypeso2libsRemoteConfig();
#endif
#if defined(O2LIBS_SAVES)
        InitializeTypeso2libsSaves();
#endif
    }

    // Starts the modules that start by themselves; after the application is initialized. Does it
    // once: where main() has no such moment (iOS) the application calls it from OnStarted as well
    inline void Start()
    {
        static bool started = false;
        if (started)
            return;

        started = true;

#if defined(O2LIBS_CORE)
        Storage::InitializeSingleton();
        PlayerIdentity::InitializeSingleton();
#endif
#if defined(O2LIBS_REMOTE_CONFIG)
        RemoteConfig::InitializeSingleton();
        o2RemoteConfig.InitializeFromAssets();
#endif
#if defined(O2LIBS_SAVES)
        PlayerSaves::InitializeSingleton();
        o2Saves.InitializeFromAssets();
#endif
    }

}

#define O2LIBS_INITIALIZE_TYPES o2libs::InitializeTypes()
#define O2LIBS_START o2libs::Start()
