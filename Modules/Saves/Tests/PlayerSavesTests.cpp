#include "o2/stdafx.h"
#include "o2/Utils/Coroutines/Coroutines.h"
#include "o2/Utils/Jobs/JobSystem.h"
#include "o2/Utils/Threading/Thread.h"
#include "o2libs/Core/PlayerIdentity.h"
#include "o2libs/Saves/PlayerSaves.h"
#include <gtest/gtest.h>

using namespace o2;
using namespace o2libs;

namespace
{
    class ProgressSave: public SaveSection
    {
    public:
        int            level = 1;      // @SERIALIZABLE
        int            coins = 100;    // @SERIALIZABLE
        Vector<String> unlocked;       // @SERIALIZABLE
        int            loadedTimes = 0;

        void OnLoaded() override { loadedTimes++; }
        void ResetToDefaults() override { level = 1; coins = 100; unlocked.Clear(); }

        void Serialize(DataValue& data) const override
        {
            data.SetObject();
            data.AddMember("level") = level;
            data.AddMember("coins") = coins;
            data.AddMember("unlocked") = unlocked;
        }

        void Deserialize(const DataValue& data) override
        {
            if (auto v = data.FindMember("level")) v->Get(level);
            if (auto v = data.FindMember("coins")) v->Get(coins);
            if (auto v = data.FindMember("unlocked")) v->Get(unlocked);
        }
    };

    // A later layout of the same section: "coins" became "wallet.soft"
    class ProgressSaveV2: public SaveSection
    {
    public:
        int soft = 0;

        int GetVersion() const override { return 2; }

        void OnMigrate(int fromVersion, DataValue& data) override
        {
            int coins = 0;
            if (auto v = data.FindMember("coins")) v->Get(coins);
            data.AddMember("wallet").SetObject();
            data["wallet"].AddMember("soft") = coins;
        }

        void Serialize(DataValue& data) const override
        {
            data.SetObject();
            data.AddMember("wallet").SetObject();
            data["wallet"].AddMember("soft") = soft;
        }

        void Deserialize(const DataValue& data) override
        {
            if (auto w = data.FindMember("wallet"))
                if (auto v = w->FindMember("soft")) v->Get(soft);
        }
    };

    void PumpCoroutines()
    {
        o2Jobs.ExecuteMainThreadJobs(-1.0f);
        o2Coroutines.OnNewFrame();
        Thread::SleepForMilliseconds(1);
    }

    // The service, as far as a save goes: keeps one save per player, answers the way o2liveops does
    class FakeService: public IServiceTransport
    {
    public:
        struct Call
        {
            String body;

            Signal                           done;
            std::shared_ptr<ServiceResponse> response = std::make_shared<ServiceResponse>();
        };

        bool         online = true;
        int          revision = 0;
        bool         forced = false;
        DataDocument stored;
        Vector<Call> pending;
        int          requests = 0;

        explicit FakeService(RefCounter* refCounter) { SetRefCounter(refCounter); }

        Coroutine<ServiceResponse> Get(const String& url) override { return Post(url, String()); }

        Coroutine<ServiceResponse> Post(const String& url, const String& body) override
        {
            Call call;
            call.body = body;
            pending.Add(call);

            return [](Signal done, std::shared_ptr<ServiceResponse> response) -> Coroutine<ServiceResponse>
            {
                co_await done;
                co_return *response;
            }(call.done, call.response);
        }

        // Somebody edits the save on the portal
        void Edit(const char* json)
        {
            stored.LoadFromData(json);
            revision++;
            forced = true;
        }

        void Answer(Call& call)
        {
            requests++;
            if (!online)
                return;

            DataDocument request;
            request.LoadFromData(call.body);

            int clientRevision = (int)request.GetMember("rev");
            bool changed = (bool)request.GetMember("changed");

            DataDocument answer;
            answer.SetObject();

            bool take = clientRevision != revision && (forced || !changed);
            if (take)
                answer.AddMember("save") = (const DataValue&)stored;
            else if (changed)
            {
                (DataValue&)stored = request.GetMember("save");
                revision++;
            }

            forced = false;
            answer.AddMember("rev") = revision;

            call.response->ok = true;
            call.response->status = 200;
            call.response->body = answer.SaveAsString();
        }

        void Flush()
        {
            for (int quiet = 0; quiet < 25; quiet++)
            {
                PumpCoroutines();
                if (pending.IsEmpty())
                    continue;

                quiet = 0;
                auto calls = pending;
                pending.Clear();

                for (auto& call : calls)
                {
                    Answer(call);
                    call.done.Synchronize();
                }
            }
        }
    };

    struct Fixture
    {
        Ref<FakeService>          service = mmake<FakeService>();
        Ref<MemoryStorageBackend> storage = mmake<MemoryStorageBackend>();
        Ref<MemoryStorageBackend> identity = mmake<MemoryStorageBackend>();
        double                    now = 1000;
        Ref<PlayerSaves>          saves;
        int                       replaced = 0;

        Fixture(bool cloud = true)
        {
            o2Storage.SetBackend(identity);
            o2PlayerIdentity.Reset();
            Start(cloud);
        }

        ~Fixture()
        {
            Drop();
            o2Storage.SetBackend(nullptr);
            o2PlayerIdentity.Reset();
        }

        void Drop()
        {
            if (saves)
                PlayerSaves::DestroySingleton(saves);
        }

        // A new run of the game over the same storage
        void Start(bool cloud = true)
        {
            Drop();

            ServiceSettings settings;
            if (cloud)
            {
                settings.url = "http://svc/liveops";
                settings.key = "o2c_test";
            }

            saves = mmake<PlayerSaves>(service, storage, [this]() { return now; });
            saves->onReplaced = [this]() { replaced++; };
            saves->Initialize(settings);
        }

        bool Sync()
        {
            auto sync = saves->Sync();
            service->Flush();
            return sync.IsDone() && sync.GetResult();
        }
    };
}

TEST(PlayerSaves, SectionsAreTypedObjectsOfTheGame)
{
    Fixture f(false);

    auto progress = f.saves->GetSection<ProgressSave>("progress");
    EXPECT_EQ(progress->level, 1);
    EXPECT_EQ(progress->loadedTimes, 1);
    EXPECT_EQ(f.saves->GetSection<ProgressSave>("progress"), progress);

    progress->level = 7;
    progress->unlocked.Add("fox");
    f.saves->MarkChanged();
    f.saves->Save();

    f.Start(false);
    auto again = f.saves->GetSection<ProgressSave>("progress");
    EXPECT_EQ(again->level, 7);
    EXPECT_EQ(again->coins, 100);
    ASSERT_EQ(again->unlocked.Count(), 1);
    EXPECT_EQ(again->unlocked[0], String("fox"));
}

TEST(PlayerSaves, SaveIsOneJsonDocumentWithThePlayerAndItsSections)
{
    Fixture f(false);
    f.saves->GetSection<ProgressSave>("progress")->level = 3;

    DataDocument settings;
    settings.LoadFromData(R"({"sound": false})");
    f.saves->SetSectionData("settings", settings);

    DataDocument save;
    ASSERT_TRUE(save.LoadFromData(f.saves->GetSaveJson()));
    EXPECT_EQ(String(save.GetMember("player").GetString()), o2PlayerIdentity.GetId());
    EXPECT_EQ((int)save.GetMember("sections").GetMember("progress").GetMember("data").GetMember("level"), 3);
    EXPECT_FALSE((bool)save.GetMember("sections").GetMember("settings").GetMember("data").GetMember("sound"));
    EXPECT_EQ(save.FindMember("rev"), nullptr) << "what only the device needs stays on the device";
}

TEST(PlayerSaves, ASectionThisBuildDoesNotKnowIsKept)
{
    Fixture f(false);

    DataDocument other;
    other.LoadFromData(R"({"season": 4, "claimed": [1, 2]})");
    f.saves->SetSectionData("season_pass", other);
    f.saves->Save();

    // A build without that module of the game
    f.Start(false);
    f.saves->GetSection<ProgressSave>("progress")->level = 2;
    f.saves->MarkChanged();
    f.saves->Save();

    f.Start(false);
    EXPECT_EQ((int)f.saves->GetSectionData("season_pass").GetMember("season"), 4);
    EXPECT_EQ(f.saves->GetSection<ProgressSave>("progress")->level, 2);
}

TEST(PlayerSaves, ChangesAreWrittenAfterTheDelayNotOnEveryChange)
{
    Fixture f(false);
    auto progress = f.saves->GetSection<ProgressSave>("progress");

    progress->coins = 250;
    f.saves->MarkChanged();
    f.saves->Update(f.saves->saveDelay * 0.5f);
    EXPECT_TRUE(f.storage->GetKeys("save.").IsEmpty());

    f.saves->Update(f.saves->saveDelay);
    EXPECT_EQ(f.storage->GetKeys("save.").Count(), 1);
}

TEST(PlayerSaves, UnsavedChangesAreWrittenWhenTheGameCloses)
{
    Fixture f(false);
    f.saves->GetSection<ProgressSave>("progress")->coins = 999;
    f.saves->MarkChanged();

    f.Start(false);
    EXPECT_EQ(f.saves->GetSection<ProgressSave>("progress")->coins, 999);
}

TEST(PlayerSaves, OlderLayoutIsMigratedBeforeItIsRead)
{
    Fixture f(false);
    f.saves->GetSection<ProgressSave>("progress")->coins = 480;
    f.saves->MarkChanged();
    f.saves->Save();

    f.Start(false);
    auto progress = f.saves->GetSection<ProgressSaveV2>("progress");
    EXPECT_EQ(progress->soft, 480);

    f.saves->MarkChanged();
    f.saves->Save();

    DataDocument save;
    save.LoadFromData(f.saves->GetSaveJson());
    EXPECT_EQ((int)save.GetMember("sections").GetMember("progress").GetMember("v"), 2);
}

TEST(PlayerSaves, LocalOnlyWithoutAService)
{
    Fixture f(false);
    EXPECT_FALSE(f.saves->IsCloudEnabled());
    f.saves->MarkChanged();

    EXPECT_FALSE(f.Sync());
    f.saves->Update(f.saves->syncPeriod * 2);
    f.service->Flush();
    EXPECT_EQ(f.service->requests, 0);
}

TEST(PlayerSaves, ChangesReachTheServiceAndOnlyChanges)
{
    Fixture f;
    auto progress = f.saves->GetSection<ProgressSave>("progress");
    progress->level = 5;
    f.saves->MarkChanged();
    EXPECT_TRUE(f.saves->HasUnsyncedChanges());

    ASSERT_TRUE(f.Sync());
    EXPECT_EQ(f.saves->GetRevision(), 1);
    EXPECT_FALSE(f.saves->HasUnsyncedChanges());
    EXPECT_EQ((int)f.service->stored.GetMember("sections").GetMember("progress").GetMember("data").GetMember("level"), 5);
    EXPECT_EQ(String(f.service->stored.GetMember("player").GetString()), o2PlayerIdentity.GetId());

    // Nothing changed: the save itself is not sent again
    ASSERT_TRUE(f.Sync());
    EXPECT_EQ(f.saves->GetRevision(), 1);
    EXPECT_EQ(f.service->revision, 1);
}

TEST(PlayerSaves, SyncHappensByItselfSomeTimeAfterAChange)
{
    Fixture f;
    f.saves->GetSection<ProgressSave>("progress")->level = 2;
    f.saves->MarkChanged();

    f.saves->Update(f.saves->syncPeriod * 0.5f);
    f.service->Flush();
    EXPECT_EQ(f.service->requests, 0);

    f.saves->Update(f.saves->syncPeriod);
    f.service->Flush();
    EXPECT_EQ(f.service->requests, 1);
    EXPECT_FALSE(f.saves->HasUnsyncedChanges());
}

TEST(PlayerSaves, SaveEditedOnThePortalReplacesTheLocalOneAndTheGameKeepsItsObjects)
{
    Fixture f;
    auto progress = f.saves->GetSection<ProgressSave>("progress");
    progress->level = 5;
    f.saves->MarkChanged();
    ASSERT_TRUE(f.Sync());

    f.service->Edit(R"({"format": 1, "sections": {"progress": {"v": 1, "data": {"level": 40, "coins": 100000, "unlocked": ["fox", "owl"]}}}})");

    // The player went on playing meanwhile: the edit still wins, that is what it was made for
    progress->coins = 120;
    f.saves->MarkChanged();
    ASSERT_TRUE(f.Sync());

    EXPECT_EQ(f.replaced, 1);
    EXPECT_EQ(progress->level, 40);
    EXPECT_EQ(progress->coins, 100000);
    EXPECT_EQ(progress->unlocked.Count(), 2);
    EXPECT_EQ(f.saves->GetRevision(), 2);
    EXPECT_FALSE(f.saves->HasUnsyncedChanges());

    // It is what the next run starts from
    f.Start();
    EXPECT_EQ(f.saves->GetSection<ProgressSave>("progress")->level, 40);
}

TEST(PlayerSaves, OfflineChangesWaitAndAreNotLost)
{
    Fixture f;
    f.service->online = false;

    f.saves->GetSection<ProgressSave>("progress")->level = 9;
    f.saves->MarkChanged();
    EXPECT_FALSE(f.Sync());
    EXPECT_TRUE(f.saves->HasUnsyncedChanges());

    f.Start();
    EXPECT_TRUE(f.saves->HasUnsyncedChanges()) << "the next run still knows it owes the service a save";

    f.service->online = true;
    ASSERT_TRUE(f.Sync());
    EXPECT_EQ((int)f.service->stored.GetMember("sections").GetMember("progress").GetMember("data").GetMember("level"), 9);
}

TEST(PlayerSaves, NewDeviceOfTheSamePlayerTakesTheSaveFromTheService)
{
    Fixture f;
    f.saves->GetSection<ProgressSave>("progress")->level = 12;
    f.saves->MarkChanged();
    ASSERT_TRUE(f.Sync());

    // The same account on a device with nothing stored
    f.storage = mmake<MemoryStorageBackend>();
    f.Start();
    auto progress = f.saves->GetSection<ProgressSave>("progress");
    EXPECT_EQ(progress->level, 1);

    ASSERT_TRUE(f.Sync());
    EXPECT_EQ(progress->level, 12);
}

TEST(PlayerSaves, ResetWipesTheProgressAndKeepsTheObjects)
{
    Fixture f;
    auto progress = f.saves->GetSection<ProgressSave>("progress");
    progress->level = 30;
    progress->unlocked.Add("fox");
    f.saves->MarkChanged();
    ASSERT_TRUE(f.Sync());

    f.saves->Reset();
    EXPECT_EQ(progress->level, 1);
    EXPECT_TRUE(progress->unlocked.IsEmpty());
    EXPECT_EQ(f.saves->GetSection<ProgressSave>("progress"), progress);
    EXPECT_TRUE(f.saves->HasUnsyncedChanges());

    ASSERT_TRUE(f.Sync());
    EXPECT_EQ((int)f.service->stored.GetMember("sections").GetMember("progress").GetMember("data").GetMember("level"), 1);

    f.Start();
    EXPECT_EQ(f.saves->GetSection<ProgressSave>("progress")->level, 1);
}

TEST(PlayerSaves, GamesOnOneStorageDoNotShareASave)
{
    Fixture f;
    f.saves->GetSection<ProgressSave>("progress")->level = 4;
    f.saves->MarkChanged();
    f.saves->Save();

    EXPECT_EQ(f.storage->GetKeys("save.o2c_test").Count(), 1);
    EXPECT_EQ(f.storage->GetKeys("save.local").Count(), 0);
}

#if IS_SCRIPTING_SUPPORTED
TEST(PlayerSaves, ScriptsReachItAsOneObject)
{
    Fixture f(false);

    o2Scripts.Eval("o2libs.Saves.Set('progress', { level: 6, items: ['a', 'b'] });");
    EXPECT_TRUE(f.saves->HasSection("progress"));
    EXPECT_EQ((int)f.saves->GetSectionData("progress").GetMember("level"), 6);

    EXPECT_EQ(o2Scripts.Eval("o2libs.Saves.Get('progress').items.length").ToNumber(), 2);
    EXPECT_TRUE(o2Scripts.Eval("o2libs.Saves.Get('nope') === undefined").ToBool());
    EXPECT_EQ(o2Scripts.Eval("o2libs.Saves.GetPlayerId()").ToString(), o2PlayerIdentity.GetId());
}
#endif
