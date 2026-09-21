#include "o2/stdafx.h"
#include "o2/Network/NetworkSystem.h"
#include "o2libs/Core/PlayerIdentity.h"
#include "o2libs/RemoteConfig/RemoteConfigClient.h"
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

    Storage::SetBackend(mmake<MemoryStorageBackend>());
    PlayerIdentity::Reset();
    if (const char* player = std::getenv("O2LIBS_LIVE_PLAYER"))
        PlayerIdentity::SetId(player);

    ServiceSettings settings;
    settings.url = url;
    settings.key = key;
    settings.appVersion = "1.2.0";

    auto storage = mmake<MemoryStorageBackend>();
    auto client = mmake<RemoteConfigClient>(nullptr, storage);
    client->exposureFlushPeriod = 0.1f;
    client->SetAttribute("level", 7.0);
    client->Init(settings);

    int result = -1;
    client->Fetch([&](bool ok) { result = ok ? 1 : 0; });
    ASSERT_TRUE(Pump([&]() { return result >= 0; }));
    ASSERT_EQ(result, 1);

    // The project of live-check.ts: two experiments at 100% with one non-control group each
    EXPECT_EQ(client->Get<int>("season_pass", "levels", 0), 30);
    EXPECT_FLOAT_EQ(client->Get<float>("season_pass", "price", 0), 2.49f);
    EXPECT_EQ(client->Get<int>("season_pass", "rewards.coins", 0), 750);
    EXPECT_EQ(client->GetGroup("price"), String("cheap"));
    EXPECT_EQ(client->GetGroup("rewards"), String("rich"));
    EXPECT_EQ(client->Get<int>("economy", "multiplier", 0), 2) << "the rule for level >= 5";

    // The second fetch is answered "unchanged"
    result = -1;
    client->Fetch([&](bool ok) { result = ok ? 1 : 0; });
    ASSERT_TRUE(Pump([&]() { return result >= 0; }));
    EXPECT_EQ(result, 1);

    // Exposures reach the service
    client->Update(1.0f);
    Pump([&]() { String events; return storage->Read(String("rc.") + key + ".events", events) && events == "[]"; }, 5);

    String events;
    storage->Read(String("rc.") + key + ".events", events);
    EXPECT_EQ(events, String("[]"));

    // The next run needs no network
    auto offline = mmake<RemoteConfigClient>(nullptr, storage);
    offline->Init(settings);
    EXPECT_TRUE(offline->IsReady());
    EXPECT_EQ(offline->Get<int>("season_pass", "rewards.coins", 0), 750);

    Storage::SetBackend(nullptr);
    PlayerIdentity::Reset();
}
