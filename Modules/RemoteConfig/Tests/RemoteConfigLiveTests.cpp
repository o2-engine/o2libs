#include "o2/stdafx.h"
#include "o2/Network/NetworkSystem.h"
#include "o2libs/Core/PlayerIdentity.h"
#include "o2/Utils/Jobs/JobSystem.h"
#include "o2libs/RemoteConfig/RemoteConfig.h"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

using namespace o2;
using namespace o2libs;

// Against a running live-ops service, over the engine's real HTTP stack. Skipped unless
// O2LIBS_LIVE_URL and O2LIBS_LIVE_KEY are set; o2portal/liveops/tools/live-check.ts sets a project
// up, runs this and checks what the service saw
namespace
{
    bool Pump(const Function<bool()>& done, float seconds = 10)
    {
        for (int i = 0; i < seconds*100 && !done(); i++)
        {
            o2Network.Update(0.01f);
            o2Jobs.ExecuteMainThreadJobs(-1.0f);
            o2Coroutines.OnNewFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return done();
    }
}

TEST(RemoteConfigLive, FetchesFromARunningService)
{
    const char* url = std::getenv("O2LIBS_LIVE_URL");
    const char* key = std::getenv("O2LIBS_LIVE_KEY");
    if (!url || !key)
        GTEST_SKIP() << "O2LIBS_LIVE_URL / O2LIBS_LIVE_KEY are not set";

    o2Storage.SetBackend(mmake<MemoryStorageBackend>());
    o2PlayerIdentity.Reset();
    if (const char* player = std::getenv("O2LIBS_LIVE_PLAYER"))
        o2PlayerIdentity.SetId(player);

    ServiceSettings settings;
    settings.url = url;
    settings.key = key;
    settings.appVersion = "1.2.0";

    auto storage = mmake<MemoryStorageBackend>();
    auto client = mmake<RemoteConfig>(nullptr, storage);
    client->exposureFlushPeriod = 0.1f;
    client->SetAttribute("level", 7.0);
    client->Initialize(settings);

    auto fetch = client->Fetch();
    ASSERT_TRUE(Pump([&]() { return fetch.IsDone(); }));
    ASSERT_TRUE(fetch.GetResult());

    // The project of live-check.ts: two experiments at 100% with one non-control group each
    EXPECT_EQ(client->Get<int>("season_pass", "levels", 0), 30);
    EXPECT_FLOAT_EQ(client->Get<float>("season_pass", "price", 0), 2.49f);
    EXPECT_EQ(client->Get<int>("season_pass", "rewards.coins", 0), 750);
    EXPECT_EQ(client->GetGroup("price"), String("cheap"));
    EXPECT_EQ(client->GetGroup("rewards"), String("rich"));
    EXPECT_EQ(client->Get<int>("economy", "multiplier", 0), 2) << "the rule for level >= 5";

    // The second fetch is answered "unchanged"
    auto again = client->Fetch();
    ASSERT_TRUE(Pump([&]() { return again.IsDone(); }));
    EXPECT_TRUE(again.GetResult());

    // Exposures reach the service
    client->Update(1.0f);
    Pump([&]() { String events; return storage->Read(String("rc.") + key + ".events", events) && events == "[]"; }, 5);

    String events;
    storage->Read(String("rc.") + key + ".events", events);
    EXPECT_EQ(events, String("[]"));

    // The next run needs no network
    RemoteConfig::DestroySingleton(client);
    auto offline = mmake<RemoteConfig>(nullptr, storage);
    offline->Initialize(settings);
    EXPECT_TRUE(offline->IsReady());
    EXPECT_EQ(offline->Get<int>("season_pass", "rewards.coins", 0), 750);

    RemoteConfig::DestroySingleton(offline);
    o2Storage.SetBackend(nullptr);
    o2PlayerIdentity.Reset();
}
