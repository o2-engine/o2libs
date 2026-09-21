#include "o2/stdafx.h"
#include "o2/Utils/FileSystem/FileSystem.h"
#include "o2libs/Core/JsonMergePatch.h"
#include "o2libs/Core/PlayerIdentity.h"
#include "o2/Utils/Coroutines/Coroutines.h"
#include "o2/Utils/Jobs/JobSystem.h"
#include "o2/Utils/Threading/Thread.h"
#include "o2libs/RemoteConfig/RemoteConfig.h"
#include <gtest/gtest.h>

using namespace o2;
using namespace o2libs;

namespace
{
    // One frame of the application, as far as coroutines go: resumptions may come from the workers
    void PumpCoroutines()
    {
        o2Jobs.ExecuteMainThreadJobs(-1.0f);
        o2Coroutines.OnNewFrame();
        Thread::SleepForMilliseconds(1);
    }

    // Answers from a table, on Flush - as the network does, later than the call
    class FakeTransport: public IServiceTransport
    {
    public:
        struct Call
        {
            String method, url, body;

            Signal                           done;
            std::shared_ptr<ServiceResponse> response = std::make_shared<ServiceResponse>();
        };

        Map<String, String> answers;   // url -> body, 200
        Map<String, int>    failures;  // url -> status (0: no answer)
        Vector<Call>        pending;
        Vector<Call>        log;

        explicit FakeTransport(RefCounter* refCounter) { SetRefCounter(refCounter); }

        Coroutine<ServiceResponse> Post(const String& url, const String& body) override { return Add("POST", url, body); }
        Coroutine<ServiceResponse> Get(const String& url) override { return Add("GET", url, String()); }

        Coroutine<ServiceResponse> Add(const String& method, const String& url, const String& body)
        {
            Call call;
            call.method = method;
            call.url = url;
            call.body = body;
            pending.Add(call);

            return [](Signal done, std::shared_ptr<ServiceResponse> response) -> Coroutine<ServiceResponse>
            {
                co_await done;
                co_return *response;
            }(call.done, call.response);
        }

        // Answers everything asked, and what is asked in return, until it has been quiet for a while
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
                    log.Add(call);

                    if (failures.ContainsKey(call.url))
                        call.response->status = failures[call.url];
                    else if (answers.ContainsKey(call.url))
                    {
                        call.response->ok = true;
                        call.response->status = 200;
                        call.response->body = answers[call.url];
                    }
                    else
                        call.response->status = 404;

                    call.done.Synchronize();
                }
            }
        }

        int Count(const String& method, const String& urlPart) const
        {
            int res = 0;
            for (auto& call : log)
            {
                if (call.method == method && call.url.Find(urlPart) >= 0)
                    res++;
            }

            return res;
        }

        String LastBody(const String& urlPart) const
        {
            for (int i = log.Count() - 1; i >= 0; i--)
            {
                if (log[i].url.Find(urlPart) >= 0)
                    return log[i].body;
            }

            return String();
        }
    };

    const char* service = "http://svc/liveops";
    const char* cdn = "http://cdn/c/p1";

    // Document ids are hex; these read well in a test
    const char* idBase = "ba5e";
    const char* idPatchA = "a0a0";
    const char* idPatchB = "b0b0";
    const char* idEvent = "e0e0";
    const char* idEconomy = "ec00";

    struct Fixture
    {
        Ref<FakeTransport>        transport = mmake<FakeTransport>();
        Ref<MemoryStorageBackend> storage = mmake<MemoryStorageBackend>();
        Ref<MemoryStorageBackend> identity = mmake<MemoryStorageBackend>();
        double                    now = 1000000;
        Ref<RemoteConfig>         client;
        int                       changes = 0;

        Fixture()
        {
            o2Storage.SetBackend(identity);
            o2PlayerIdentity.Reset();

            Document(idBase, R"({"levels": 30, "price": 4.99, "rewards": {"coins": 500, "skin": "neon"}, "list": [1, {"x": 7}]})");
            Document(idPatchA, R"({"price": 2.49})");
            Document(idPatchB, R"({"rewards": {"coins": 750, "skin": null}})");
            Document(idEvent, R"({"levels": 50, "event": true})");
            Document(idEconomy, R"({"multiplier": 2})");

            Start();
        }

        ~Fixture()
        {
            Drop();
            o2Storage.SetBackend(nullptr);
            o2PlayerIdentity.Reset();
        }

        // The object is a singleton: the previous one goes before the next one comes
        void Drop()
        {
            if (client)
                RemoteConfig::DestroySingleton(client);
        }

        // A new run of the game over the same storage
        void Start(const char* key = "o2c_test")
        {
            ServiceSettings settings;
            settings.url = service;
            settings.key = key;
            settings.appVersion = "1.2.0";
            settings.build = 12;

            Drop();
            client = mmake<RemoteConfig>(transport, storage, [this]() { return now; });
            client->onChanged = [this]() { changes++; };
            client->Initialize(settings);
        }

        void Document(const char* id, const char* json)
        {
            transport->answers[String(cdn) + "/" + id + ".json"] = json;
        }

        void Manifest(const String& json)
        {
            transport->answers[String(service) + "/v1/fetch"] = json;
        }

        bool Fetch()
        {
            auto fetch = client->Fetch();
            transport->Flush();
            return fetch.IsDone() && fetch.GetResult();
        }

        String TwoTestsManifest() const
        {
            return String(R"({"etag": "v1", "ttl": 600, "time": 1000000, "cdn": "http://cdn/c/p1",
                "configs": {"season_pass": ["ba5e", "a0a0", "b0b0"], "economy": ["ec00"]},
                "experiments": {"price_test": "cheap", "reward_test": "rich"},
                "exposure": {"season_pass": ["price_test", "reward_test"]}})");
        }
    };

    bool SameJson(const DataValue& a, const DataValue& b)
    {
        if (a.IsObject() != b.IsObject() || a.IsArray() != b.IsArray())
            return false;

        if (a.IsObject())
        {
            if (a.GetMembersCount() != b.GetMembersCount())
                return false;

            for (auto it = a.BeginMember(); it != a.EndMember(); ++it)
            {
                auto other = b.FindMember(it->name);
                if (!other || !SameJson(it->value, *other))
                    return false;
            }

            return true;
        }

        if (a.IsArray())
        {
            if (a.GetElementsCount() != b.GetElementsCount())
                return false;

            for (int i = 0; i < a.GetElementsCount(); i++)
            {
                if (!SameJson(a.GetElement(i), b.GetElement(i)))
                    return false;
            }

            return true;
        }

        return a == b;
    }
}

// The service runs the same vectors: o2portal/liveops/test/merge.test.ts
TEST(MergePatch, SharedVectors)
{
    DataDocument vectors;
    ASSERT_TRUE(vectors.LoadFromFile(String(O2LIBS_ROOT_DIR) + "/Modules/RemoteConfig/Tests/merge-vectors.json"));
    ASSERT_TRUE(vectors.IsArray());
    ASSERT_GT(vectors.GetElementsCount(), 10);

    for (auto& vector : vectors)
    {
        DataDocument target;
        (DataValue&)target = vector.GetMember("target");

        ApplyMergePatch(target, vector.GetMember("patch"));

        EXPECT_TRUE(SameJson(target, vector.GetMember("result")))
            << vector.GetMember("name").GetString() << ": got " << target.SaveAsString().Data();
    }
}

TEST(RemoteConfig, FetchDownloadsAndMergesTheChain)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());

    EXPECT_FALSE(f.client->IsReady());
    EXPECT_TRUE(f.Fetch());
    EXPECT_TRUE(f.client->IsReady());
    EXPECT_EQ(f.changes, 1);

    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 30);
    EXPECT_EQ(f.client->Get<int>("economy", "multiplier", 0), 2);
    EXPECT_EQ(f.client->GetConfigKeys().Count(), 2);
    EXPECT_FALSE(f.client->HasConfig("nope"));
    EXPECT_TRUE(f.client->GetConfig("nope").IsNull());
}

TEST(RemoteConfig, PlayerIsInSeveralExperimentsAtOnce)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    // Both groups' patches are on the same config, each on its own paths
    EXPECT_FLOAT_EQ(f.client->Get<float>("season_pass", "price", 0), 2.49f);
    EXPECT_EQ(f.client->Get<int>("season_pass", "rewards.coins", 0), 750);
    EXPECT_TRUE(f.client->GetValue("season_pass", "rewards.skin").IsNull());
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 30);

    EXPECT_EQ(f.client->GetExperiments().Count(), 2);
    EXPECT_EQ(f.client->GetGroup("price_test"), String("cheap"));
    EXPECT_EQ(f.client->GetGroup("reward_test"), String("rich"));
    EXPECT_EQ(f.client->GetGroup("other_test"), String());
}

TEST(RemoteConfig, PathsReachIntoArraysAndMissIsDefault)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    EXPECT_EQ(f.client->Get<int>("season_pass", "list.0", -1), 1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "list.1.x", -1), 7);
    EXPECT_EQ(f.client->Get<int>("season_pass", "list.5", -1), -1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "list.x", -1), -1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "rewards", -1), -1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels.deeper", -1), -1);
    EXPECT_EQ(f.client->Get<String>("season_pass", "rewards.nothing", "dflt"), String("dflt"));
    EXPECT_TRUE(f.client->GetValue("season_pass", "").IsObject());
}

TEST(RemoteConfig, RequestCarriesPlayerVersionAndAttributes)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    f.client->SetAttribute("level", 12.0);
    f.client->SetAttribute("country", String("GE"));
    f.client->SetAttribute("payer", true);
    ASSERT_TRUE(f.Fetch());

    DataDocument request;
    ASSERT_TRUE(request.LoadFromData(f.transport->LastBody("/v1/fetch")));

    EXPECT_EQ(String(request.GetMember("key").GetString()), String("o2c_test"));
    EXPECT_EQ(String(request.GetMember("player").GetString()), o2PlayerIdentity.GetId());
    EXPECT_EQ(String(request.GetMember("app").GetString()), String("1.2.0"));
    EXPECT_EQ((int)request.GetMember("build"), 12);
    EXPECT_FALSE(String(request.GetMember("platform").GetString()).IsEmpty());
    EXPECT_EQ((int)request.GetMember("attrs").GetMember("level"), 12);
    EXPECT_EQ(String(request.GetMember("attrs").GetMember("country").GetString()), String("GE"));
    EXPECT_TRUE((bool)request.GetMember("attrs").GetMember("payer"));
    EXPECT_EQ(request.FindMember("etag"), nullptr);
}

TEST(RemoteConfig, UnchangedAnswerCostsNoDownloads)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());
    int downloads = f.transport->Count("GET", ".json");
    EXPECT_EQ(downloads, 4);

    f.Manifest(R"({"unchanged": true, "etag": "v1", "ttl": 600, "time": 1000100})");
    EXPECT_TRUE(f.Fetch());

    DataDocument request;
    ASSERT_TRUE(request.LoadFromData(f.transport->LastBody("/v1/fetch")));
    EXPECT_EQ(String(request.GetMember("etag").GetString()), String("v1"));

    EXPECT_EQ(f.transport->Count("GET", ".json"), downloads);
    EXPECT_EQ(f.changes, 1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "rewards.coins", 0), 750);
}

TEST(RemoteConfig, OnlyMissingDocumentsAreDownloaded)
{
    Fixture f;
    f.Manifest(R"({"etag": "v1", "ttl": 600, "cdn": "http://cdn/c/p1", "configs": {"season_pass": ["ba5e"]}})");
    ASSERT_TRUE(f.Fetch());
    EXPECT_EQ(f.transport->Count("GET", ".json"), 1);

    f.Manifest(R"({"etag": "v2", "ttl": 600, "cdn": "http://cdn/c/p1", "configs": {"season_pass": ["ba5e", "a0a0"]}})");
    ASSERT_TRUE(f.Fetch());

    EXPECT_EQ(f.transport->Count("GET", "ba5e"), 1);
    EXPECT_EQ(f.transport->Count("GET", "a0a0"), 1);
    EXPECT_EQ(f.changes, 2);
    EXPECT_FLOAT_EQ(f.client->Get<float>("season_pass", "price", 0), 2.49f);
}

TEST(RemoteConfig, NextRunStartsFromTheStoredAnswerWithNoNetwork)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    f.transport->failures[String(service) + "/v1/fetch"] = 0;
    int calls = f.transport->log.Count();

    f.Start();
    EXPECT_TRUE(f.client->IsReady());
    EXPECT_EQ(f.transport->log.Count(), calls);
    EXPECT_EQ(f.client->Get<int>("season_pass", "rewards.coins", 0), 750);
    EXPECT_EQ(f.client->GetGroup("price_test"), String("cheap"));

    EXPECT_FALSE(f.Fetch());
    EXPECT_EQ(f.client->Get<int>("season_pass", "rewards.coins", 0), 750);
}

TEST(RemoteConfig, FailedDownloadKeepsThePreviousVersionWhole)
{
    Fixture f;
    f.Manifest(R"({"etag": "v1", "ttl": 600, "cdn": "http://cdn/c/p1", "configs": {"season_pass": ["ba5e"], "economy": ["ec00"]}})");
    ASSERT_TRUE(f.Fetch());

    // v2 changes both configs, one of its documents does not arrive
    f.transport->failures[String(cdn) + "/a0a0.json"] = 0;
    f.Manifest(R"({"etag": "v2", "ttl": 600, "cdn": "http://cdn/c/p1", "configs": {"season_pass": ["ba5e", "a0a0"], "economy": ["e0e0"]}})");
    EXPECT_FALSE(f.Fetch());

    EXPECT_EQ(f.changes, 1);
    EXPECT_FLOAT_EQ(f.client->Get<float>("season_pass", "price", 0), 4.99f);
    EXPECT_EQ(f.client->Get<int>("economy", "multiplier", 0), 2);

    // The next request still says v1, and the next attempt completes
    f.transport->failures.Clear();
    EXPECT_TRUE(f.Fetch());

    DataDocument request;
    ASSERT_TRUE(request.LoadFromData(f.transport->LastBody("/v1/fetch")));
    EXPECT_EQ(String(request.GetMember("etag").GetString()), String("v1"));
    EXPECT_FLOAT_EQ(f.client->Get<float>("season_pass", "price", 0), 2.49f);
    EXPECT_TRUE(f.client->Get<bool>("economy", "event", false));
}

TEST(RemoteConfig, BrokenAnswersAreRefused)
{
    Fixture f;

    f.Manifest("not json");
    EXPECT_FALSE(f.Fetch());

    f.Manifest(R"({"etag": "v1", "cdn": "http://cdn/c/p1", "configs": {"x": ["../../etc/passwd"]}})");
    EXPECT_FALSE(f.Fetch());
    EXPECT_EQ(f.transport->Count("GET", "passwd"), 0);

    f.Document("dead", "<html>502</html>");
    f.Manifest(R"({"etag": "v1", "cdn": "http://cdn/c/p1", "configs": {"x": ["dead"]}})");
    EXPECT_FALSE(f.Fetch());

    EXPECT_FALSE(f.client->IsReady());
    EXPECT_EQ(f.changes, 0);
}

TEST(RemoteConfig, ScheduledSwitchHappensOnTimeEvenOffline)
{
    Fixture f;
    f.Manifest(R"({"etag": "v1", "ttl": 100000, "time": 1000000, "cdn": "http://cdn/c/p1",
        "configs": {"season_pass": ["ba5e"], "economy": ["ec00"]},
        "timeline": [
            {"at": 1003600, "configs": {"season_pass": ["e0e0"]}},
            {"at": 1007200, "configs": {"season_pass": ["ba5e"], "economy": []}}
        ]})");
    ASSERT_TRUE(f.Fetch());

    // The documents of the future came with the first fetch
    EXPECT_EQ(f.transport->Count("GET", "e0e0"), 1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 30);

    f.transport->failures[String(service) + "/v1/fetch"] = 0;
    int calls = f.transport->log.Count();

    f.now = 1003599;
    f.client->Update(1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 30);
    EXPECT_EQ(f.changes, 1);

    f.now = 1003600;
    f.client->Update(1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 50);
    EXPECT_TRUE(f.client->HasConfig("economy"));
    EXPECT_EQ(f.changes, 2);

    // The game was closed over the second switch
    f.now = 1009000;
    f.Start();
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 30);
    EXPECT_FALSE(f.client->HasConfig("economy"));
    EXPECT_EQ(f.transport->log.Count(), calls);
}

TEST(RemoteConfig, DeviceClockIsCorrectedByTheService)
{
    Fixture f;
    f.now = 500; // A device with a wrong date
    f.Manifest(R"({"etag": "v1", "ttl": 100000, "time": 1000000, "cdn": "http://cdn/c/p1",
        "configs": {"season_pass": ["ba5e"]},
        "timeline": [{"at": 1000060, "configs": {"season_pass": ["e0e0"]}}]})");
    ASSERT_TRUE(f.Fetch());

    EXPECT_NEAR(f.client->GetServerTime(), 1000000, 0.001);

    f.now = 559;
    f.client->Update(1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 30);

    f.now = 560;
    f.client->Update(1);
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 50);
}

TEST(RemoteConfig, StaleAnswerIsRefetchedAndFailureRetried)
{
    Fixture f;
    f.Manifest(R"({"etag": "v1", "ttl": 600, "time": 1000000, "cdn": "http://cdn/c/p1", "configs": {"season_pass": ["ba5e"]}})");
    ASSERT_TRUE(f.Fetch());
    EXPECT_EQ(f.transport->Count("POST", "/v1/fetch"), 1);

    f.now += 599;
    f.client->Update(1);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/fetch"), 1);

    f.transport->failures[String(service) + "/v1/fetch"] = 503;
    f.now += 1;
    f.client->Update(1);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/fetch"), 2);

    // Not every frame: after the retry period
    f.client->Update(1);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/fetch"), 2);

    f.now += f.client->retryPeriod;
    f.client->Update(1);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/fetch"), 3);
}

TEST(RemoteConfig, ConcurrentFetchesShareOneRequest)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());

    auto first = f.client->Fetch();
    auto second = f.client->Fetch();
    EXPECT_TRUE(f.client->IsFetching());
    f.transport->Flush();

    ASSERT_TRUE(first.IsDone() && second.IsDone());
    EXPECT_TRUE(first.GetResult() && second.GetResult());
    EXPECT_EQ(f.transport->Count("POST", "/v1/fetch"), 1);
    EXPECT_FALSE(f.client->IsFetching());
}

TEST(RemoteConfig, UnusedDocumentsLeaveTheStorage)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());
    EXPECT_EQ(f.storage->GetKeys("rc.o2c_test.doc.").Count(), 4);

    f.Manifest(R"({"etag": "v2", "ttl": 600, "cdn": "http://cdn/c/p1", "configs": {"season_pass": ["ba5e"]},
        "timeline": [{"at": 2000000, "configs": {"season_pass": ["e0e0"]}}]})");
    ASSERT_TRUE(f.Fetch());

    auto keys = f.storage->GetKeys("rc.o2c_test.doc.");
    EXPECT_EQ(keys.Count(), 2);
    EXPECT_TRUE(keys.Contains("rc.o2c_test.doc.ba5e"));
    EXPECT_TRUE(keys.Contains("rc.o2c_test.doc.e0e0"));
}

TEST(RemoteConfig, ProjectsDoNotShareTheCache)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    f.Start("o2c_other");
    EXPECT_FALSE(f.client->IsReady());
    EXPECT_FALSE(f.client->HasConfig("season_pass"));

    f.client->ClearCache();
    f.Start("o2c_test");
    EXPECT_TRUE(f.client->IsReady());
}

TEST(RemoteConfig, ExposureIsReportedOncePerExperimentAndGroup)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    f.transport->answers[String(service) + "/v1/events"] = "{}";
    ASSERT_TRUE(f.Fetch());

    // Nothing is reported until the game reads what the experiment changed
    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/events"), 0);

    f.client->GetConfig("economy");
    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/events"), 0);

    f.client->GetConfig("season_pass");
    f.client->GetConfig("season_pass");
    f.client->GetGroup("price_test");
    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    ASSERT_EQ(f.transport->Count("POST", "/v1/events"), 1);

    DataDocument request;
    ASSERT_TRUE(request.LoadFromData(f.transport->LastBody("/v1/events")));
    EXPECT_EQ(String(request.GetMember("player").GetString()), o2PlayerIdentity.GetId());
    ASSERT_EQ(request.GetMember("events").GetElementsCount(), 2);

    auto& event = request.GetMember("events").GetElement(0);
    EXPECT_EQ(String(event.GetMember("type").GetString()), String("exposure"));
    EXPECT_EQ(String(event.GetMember("experiment").GetString()), String("price_test"));
    EXPECT_EQ(String(event.GetMember("group").GetString()), String("cheap"));

    // Not again: neither in this run nor in the next
    f.client->GetConfig("season_pass");
    f.Start();
    f.client->GetConfig("season_pass");
    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/events"), 1);
}

TEST(RemoteConfig, ExposuresWaitForTheNetwork)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    f.transport->failures[String(service) + "/v1/events"] = 0;
    f.client->GetGroup("reward_test");
    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/events"), 1);

    // The game is restarted, the network is back
    f.Start();
    f.transport->failures.Clear();
    f.transport->answers[String(service) + "/v1/events"] = "{}";
    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    ASSERT_EQ(f.transport->Count("POST", "/v1/events"), 2);

    DataDocument request;
    ASSERT_TRUE(request.LoadFromData(f.transport->LastBody("/v1/events")));
    EXPECT_EQ(request.GetMember("events").GetElementsCount(), 1);

    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/events"), 2);
}

TEST(RemoteConfig, RefusedExposuresAreNotResentForever)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    f.transport->failures[String(service) + "/v1/events"] = 400;
    f.client->GetGroup("reward_test");
    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();

    EXPECT_EQ(f.transport->Count("POST", "/v1/events"), 1);
}

TEST(RemoteConfig, ClientDestroyedWithRequestInFlight)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());

    auto fetch = f.client->Fetch();
    f.Drop();
    ASSERT_EQ(f.client, nullptr);
    f.transport->Flush();

    EXPECT_TRUE(fetch.IsDone());
    EXPECT_FALSE(fetch.GetResult());

    SUCCEED();
}

TEST(RemoteConfig, FetchBeforeInitFails)
{
    Fixture f;
    f.Drop();
    f.client = mmake<RemoteConfig>(f.transport, f.storage);

    auto fetch = f.client->Fetch();
    f.transport->Flush();

    ASSERT_TRUE(fetch.IsDone());
    EXPECT_FALSE(fetch.GetResult());
    EXPECT_EQ(f.transport->log.Count(), 0);
}

namespace
{
    struct SeasonPass: public ISerializable
    {
        int   levels = 0; // @SERIALIZABLE
        float price = 0;  // @SERIALIZABLE

        void Serialize(DataValue& data) const override {}
        void Deserialize(const DataValue& data) override
        {
            if (auto v = data.FindMember("levels")) v->Get(levels);
            if (auto v = data.FindMember("price")) v->Get(price);
        }
    };
}

TEST(RemoteConfig, ConfigReadsIntoAnObject)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    SeasonPass pass;
    EXPECT_TRUE(f.client->GetObject("season_pass", pass));
    EXPECT_EQ(pass.levels, 30);
    EXPECT_FLOAT_EQ(pass.price, 2.49f);
    EXPECT_FALSE(f.client->GetObject("nope", pass));
}

TEST(RemoteConfig, LocalOverrideBeatsTheServiceAndIsNotAnExposure)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    f.transport->answers[String(service) + "/v1/events"] = "{}";
    ASSERT_TRUE(f.Fetch());
    int revision = f.client->GetRevision();

    DataDocument mine;
    ASSERT_TRUE(mine.LoadFromData(R"({"levels": 99})"));
    f.client->SetLocalOverride("season_pass", mine);
    f.client->SetLocalOverride("only_local", mine);

    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 99);
    EXPECT_TRUE(f.client->HasConfig("only_local"));
    EXPECT_EQ(f.client->GetRevision(), revision + 2);
    EXPECT_EQ(f.changes, 3);

    f.client->Update(f.client->exposureFlushPeriod);
    f.transport->Flush();
    EXPECT_EQ(f.transport->Count("POST", "/v1/events"), 0);

    f.client->ClearLocalOverride("season_pass");
    EXPECT_EQ(f.client->Get<int>("season_pass", "levels", 0), 30);

    f.client->ClearLocalOverride();
    EXPECT_FALSE(f.client->HasConfig("only_local"));
}

TEST(RemoteConfig, TheLastCreatedObjectIsTheSingleton)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    EXPECT_EQ(&o2RemoteConfig, f.client.Get());
    EXPECT_FLOAT_EQ(o2RemoteConfig.Get<float>("season_pass", "price", 0), 2.49f);

    f.Drop();
    EXPECT_FALSE(RemoteConfig::IsSingletonInitialzed());
}

#if IS_SCRIPTING_SUPPORTED
TEST(RemoteConfig, ScriptsReachItAsOneObject)
{
    Fixture f;
    f.Manifest(f.TwoTestsManifest());
    ASSERT_TRUE(f.Fetch());

    auto eval = [](const char* code) { return o2Scripts.Eval(code); };

    EXPECT_TRUE(eval("o2libs.RemoteConfig.IsReady() && o2libs.RemoteConfig.Has('season_pass')").ToBool());
    EXPECT_NEAR(eval("o2libs.RemoteConfig.GetNumber('season_pass', 'price', 0)").ToNumber(), 2.49, 0.001);
    EXPECT_EQ(eval("o2libs.RemoteConfig.Get('season_pass').rewards.coins").ToNumber(), 750);
    EXPECT_EQ(eval("o2libs.RemoteConfig.GetString('season_pass', 'price', 'not a string')").ToString(), String("not a string"));
    EXPECT_EQ(eval("o2libs.RemoteConfig.GetGroup('price_test')").ToString(), String("cheap"));
    EXPECT_TRUE(eval("o2libs.RemoteConfig.Get('nope') === undefined").ToBool());

    eval("var __rcChanges = 0; o2libs.RemoteConfig.OnChanged(function() { __rcChanges++; });");
    f.Manifest(R"({"etag": "v2", "ttl": 600, "cdn": "http://cdn/c/p1", "configs": {"season_pass": ["ba5e"]}})");
    ASSERT_TRUE(f.Fetch());
    EXPECT_EQ(eval("__rcChanges").ToNumber(), 1);
    EXPECT_EQ(f.changes, 2);
}
#endif
