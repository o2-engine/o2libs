#include "o2/stdafx.h"
#include "o2/Network/NetworkSystem.h"
#include "o2/Utils/Jobs/JobSystem.h"
#include "o2libs/Core/PlayerIdentity.h"
#include "o2libs/Saves/PlayerSaves.h"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

using namespace o2;
using namespace o2libs;

// Against a running live-ops service, over the engine's real HTTP stack. Skipped unless O2LIBS_LIVE_URL and
// O2LIBS_LIVE_KEY are set; o2portal/liveops/tools/live-check.ts runs it and checks what the service got
namespace
{
    class LiveProgress: public SaveSection
    {
    public:
        int level = 1;

        void Serialize(DataValue& data) const override { data.SetObject(); data.AddMember("level") = level; }
        void Deserialize(const DataValue& data) override { if (auto v = data.FindMember("level")) v->Get(level); }
    };

    bool PumpUntil(const Function<bool()>& done, float seconds = 10)
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

    Ref<PlayerSaves> MakeSaves(const char* player)
    {
        o2Storage.SetBackend(mmake<MemoryStorageBackend>());
        o2PlayerIdentity.Reset();
        o2PlayerIdentity.SetId(player);

        ServiceSettings settings;
        settings.url = std::getenv("O2LIBS_LIVE_URL");
        settings.key = std::getenv("O2LIBS_LIVE_KEY");
        settings.appVersion = "1.2.0";

        auto saves = mmake<PlayerSaves>(nullptr, mmake<MemoryStorageBackend>());
        saves->Initialize(settings);
        return saves;
    }
}

TEST(PlayerSavesLive, SaveGoesUpAndAnEditedOneComesDown)
{
    if (!std::getenv("O2LIBS_LIVE_URL") || !std::getenv("O2LIBS_LIVE_KEY"))
        GTEST_SKIP() << "O2LIBS_LIVE_URL / O2LIBS_LIVE_KEY are not set";

    // This player's progress goes up
    auto saves = MakeSaves("live-check-saver");
    saves->GetSection<LiveProgress>("progress")->level = 5;
    saves->MarkChanged();

    auto up = saves->Sync();
    ASSERT_TRUE(PumpUntil([&]() { return up.IsDone(); }));
    EXPECT_TRUE(up.GetResult());
    EXPECT_EQ(saves->GetRevision(), 1);
    PlayerSaves::DestroySingleton(saves);

    // This one's save was made on the portal: the game takes it
    saves = MakeSaves("live-check-edited");
    auto progress = saves->GetSection<LiveProgress>("progress");
    progress->level = 2;
    saves->MarkChanged();

    auto down = saves->Sync();
    ASSERT_TRUE(PumpUntil([&]() { return down.IsDone(); }));
    EXPECT_TRUE(down.GetResult());
    EXPECT_EQ(progress->level, 99);
    PlayerSaves::DestroySingleton(saves);

    o2Storage.SetBackend(nullptr);
    o2PlayerIdentity.Reset();
}
