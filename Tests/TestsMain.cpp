#include "o2/stdafx.h"
#include "o2/O2.h"
#include "o2/Application/Application.h"
#include "o2libs/o2libs.h"
#include <gtest/gtest.h>

extern void InitializeTypeso2TestsSupport();

using namespace o2;

// Headless runner for the tests of the enabled o2libs modules
int main(int argc, char** argv)
{
    Application::SetHeadless(true);

    InitializeTypeso2TestsSupport();
    O2LIBS_INITIALIZE_TYPES;
    INITIALIZE_O2;

    ::testing::InitGoogleTest(&argc, argv);

    bool listOnly = ::testing::GTEST_FLAG(list_tests);

    Ref<Application> app;
    if (!listOnly)
    {
        app = mmake<Application>();
        app->Initialize();

        o2libs::Storage::InitializeSingleton();
        o2libs::PlayerIdentity::InitializeSingleton();
    }

    int result = RUN_ALL_TESTS();

    if (app)
        app->Deinitialize();

    return result;
}
