#include "o2/stdafx.h"
#include "RemoteConfig.h"

#include "o2/Utils/Debug/Debug.h"
#include "o2/Utils/Tasks/TaskManager.h"
#include "o2libs/Core/JsonMergePatch.h"
#include "o2libs/Core/PlayerIdentity.h"

#include <chrono>
#include <cstdlib>

namespace o2
{
    DECLARE_SINGLETON(o2libs::RemoteConfig);
}

namespace o2libs
{
    DataDocument RemoteConfig::mNullValue;

    namespace
    {
        const int maxPendingExposures = 200;

        double SystemClock()
        {
            using namespace std::chrono;
            return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
        }

        const char* PlatformName()
        {
#if defined(PLATFORM_WASM)
            return "web";
#elif defined(PLATFORM_ANDROID)
            return "android";
#elif defined(PLATFORM_IOS)
            return "ios";
#elif defined(PLATFORM_MAC)
            return "mac";
#elif defined(PLATFORM_WINDOWS)
            return "windows";
#else
            return "linux";
#endif
        }

        double NumberOf(const DataValue* value, double defaultValue = 0)
        {
            if (!value || !value->IsNumber())
                return defaultValue;

            double res = defaultValue;
            value->Get(res);
            return res;
        }

        String StringOf(const DataValue* value)
        {
            if (!value || !value->IsString())
                return String();

            return String(value->GetString());
        }

        void ReadChain(const DataValue& value, Vector<String>& chain)
        {
            chain.Clear();
            if (!value.IsArray())
                return;

            for (auto& id : value)
            {
                if (id.IsString())
                    chain.Add(String(id.GetString()));
            }
        }

        // Ids come from the network and become storage keys and URLs: hex only
        bool IsDocumentId(const String& id)
        {
            if (id.IsEmpty() || id.Length() > 128)
                return false;

            for (char c : id)
            {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    return false;
            }

            return true;
        }
    }

    RemoteConfig::RemoteConfig(RefCounter* refCounter,
                               const Ref<IServiceTransport>& transport /*= nullptr*/,
                               const Ref<IStorageBackend>& storage /*= nullptr*/,
                               const Function<double()>& clock /*= Function<double()>()*/):
        Singleton<RemoteConfig>(refCounter), mTransport(transport), mStorage(storage), mClock(clock),
        mAlive(std::make_shared<bool>(true))
    {
        if (!mTransport)
            mTransport = mmake<HttpServiceTransport>();

        mAttributes.SetObject();
        mPendingExposures.SetArray();
    }

    RemoteConfig::~RemoteConfig()
    {
        *mAlive = false;

        if (mInstance == this)
            mInstance = nullptr;
    }

    void RemoteConfig::Initialize(const ServiceSettings& settings)
    {
        mSettings = settings;
        while (!mSettings.url.IsEmpty() && mSettings.url.EndsWith("/"))
            mSettings.url = mSettings.url.SubStr(0, mSettings.url.Length() - 1);

        if (!mStorage)
            mStorage = o2Storage.GetBackend();

        // Several games can share one storage: every game of the portal lives on one web origin
        mStoragePrefix = "rc." + mSettings.key + ".";

        mHasManifest = false;
        mActiveChains.Clear();
        mConfigs.Clear();
        mExperiments.Clear();
        mExposureByConfig.Clear();
        mExposed.Clear();
        mNextSwitchAt = 0;
        mNextFetchAt = 0;

        if (!IsInitialized())
            return;

        String text;
        if (mStorage->Read(GetStorageKey("manifest"), text) && mManifest.LoadFromData(text) && mManifest.IsObject())
        {
            mHasManifest = true;
            Rebuild();
        }

        if (mStorage->Read(GetStorageKey("exposed"), text) && !text.IsEmpty())
            mExposed = text.Split("\n");

        mPendingExposures.SetArray();
        if (mStorage->Read(GetStorageKey("events"), text))
        {
            if (!mPendingExposures.LoadFromData(text) || !mPendingExposures.IsArray())
                mPendingExposures.SetArray();
        }

        StartUpdating();
        RegisterScriptApi();
    }

    bool RemoteConfig::InitializeFromAssets()
    {
        const ServiceSettings& settings = ServiceSettings::GetDefault();
        if (!settings.IsValid())
        {
            RegisterScriptApi();
            return false;
        }

        Initialize(settings);
        Fetch();
        return true;
    }

    bool RemoteConfig::IsInitialized() const
    {
        return mSettings.IsValid();
    }

    bool RemoteConfig::IsReady() const
    {
        return mHasManifest;
    }

    bool RemoteConfig::IsFetching() const
    {
        return mFetch != nullptr;
    }

    String RemoteConfig::GetStorageKey(const String& name) const
    {
        return mStoragePrefix + name;
    }

    bool RemoteConfig::ReadDocument(const String& id, String& text) const
    {
        return mStorage->Read(GetStorageKey("doc." + id), text);
    }

    double RemoteConfig::GetServerTime() const
    {
        return (mClock ? mClock() : SystemClock()) + mClockOffset;
    }

    Coroutine<bool> RemoteConfig::Fetch()
    {
        if (!IsInitialized())
        {
            o2Debug.LogWarning("RemoteConfig: fetch before Initialize, or LiveOps.json has no url and key");

            auto failed = []() -> Coroutine<bool> { co_return false; }();
            failed.Start(JobThread::Main);
            return failed;
        }

        if (!mFetch)
        {
            mFetch = std::make_shared<FetchState>();
            Async(RunFetch(mFetch, mAlive), JobThread::Main);
        }

        auto waiter = [](std::shared_ptr<FetchState> state) -> Coroutine<bool>
        {
            co_await state->done;
            co_return state->success;
        }(mFetch);

        waiter.Start(JobThread::Main);
        return waiter;
    }

    Coroutine<void> RemoteConfig::RunFetch(std::shared_ptr<FetchState> state, std::shared_ptr<bool> alive)
    {
        if (!*alive)
        {
            state->done.Synchronize();
            co_return;
        }

        DataDocument request;
        request.SetObject();
        request.AddMember("key") = mSettings.key;
        request.AddMember("player") = o2PlayerIdentity.GetId();
        request.AddMember("platform") = String(PlatformName());

        if (!mSettings.appVersion.IsEmpty())
            request.AddMember("app") = mSettings.appVersion;

        if (mSettings.build != 0)
            request.AddMember("build") = mSettings.build;

        request.AddMember("attrs") = (const DataValue&)mAttributes;

        if (mHasManifest)
        {
            String etag = StringOf(mManifest.FindMember("etag"));
            if (!etag.IsEmpty())
                request.AddMember("etag") = etag;
        }

        ServiceResponse response = co_await mTransport->Post(mSettings.url + "/v1/fetch", request.SaveAsString());
        if (!*alive)
        {
            state->done.Synchronize();
            co_return;
        }

        bool success = false;

        DataDocument manifest;
        if (response.ok && manifest.LoadFromData(response.body) && manifest.IsObject())
        {
            double serverTime = NumberOf(manifest.FindMember("time"));
            if (serverTime > 0)
                mClockOffset = serverTime - (mClock ? mClock() : SystemClock());

            bool unchanged = false;
            if (auto value = manifest.FindMember("unchanged"))
            {
                if (value->IsBoolean())
                    value->Get(unchanged);
            }

            if (unchanged && mHasManifest)
            {
                mNextFetchAt = GetServerTime() + NumberOf(manifest.FindMember("ttl"), 900);
                success = true;
            }
            else
            {
                // All or nothing: a half-downloaded answer would mix two published versions
                success = co_await DownloadMissing(manifest, alive);
                if (!*alive)
                {
                    state->done.Synchronize();
                    co_return;
                }

                if (success)
                    ApplyManifest(manifest);
            }
        }

        if (!success)
            mNextFetchAt = GetServerTime() + retryPeriod;

        mFetch = nullptr;
        state->success = success;
        state->done.Synchronize();
    }

    Coroutine<bool> RemoteConfig::DownloadMissing(DataDocument manifest, std::shared_ptr<bool> alive)
    {
        if (!*alive)
            co_return false;

        Vector<String> ids;
        CollectDocumentIds(manifest, ids);

        String cdn = StringOf(manifest.FindMember("cdn"));
        while (!cdn.IsEmpty() && cdn.EndsWith("/"))
            cdn = cdn.SubStr(0, cdn.Length() - 1);

        Vector<String> missing;
        for (auto& id : ids)
        {
            if (!IsDocumentId(id))
            {
                o2Debug.LogWarning("RemoteConfig: the service answered with a malformed document id");
                co_return false;
            }

            String text;
            if (!ReadDocument(id, text))
                missing.Add(id);
        }

        if (missing.IsEmpty())
            co_return true;

        Vector<Coroutine<ServiceResponse>> downloads;
        for (auto& id : missing)
            downloads.Add(Async(mTransport->Get(cdn + "/" + id + ".json"), JobThread::Main));

        co_await WaitAll(downloads);
        if (!*alive)
            co_return false;

        bool complete = true;
        for (int i = 0; i < missing.Count(); i++)
        {
            ServiceResponse response = downloads[i].GetResult();

            DataDocument check;
            if (response.ok && check.LoadFromData(response.body))
                mStorage->Write(GetStorageKey("doc." + missing[i]), response.body);
            else
                complete = false;
        }

        co_return complete;
    }

    void RemoteConfig::ApplyManifest(const DataDocument& manifest)
    {
        mManifest = manifest;
        mHasManifest = true;
        mStorage->Write(GetStorageKey("manifest"), mManifest.SaveAsString());

        mNextFetchAt = GetServerTime() + NumberOf(mManifest.FindMember("ttl"), 900);

        Rebuild();
        Prune();
    }

    void RemoteConfig::CollectDocumentIds(const DataValue& manifest, Vector<String>& ids)
    {
        auto collect = [&](const DataValue* configs) {
            if (!configs || !configs->IsObject())
                return;

            for (auto it = configs->BeginMember(); it != configs->EndMember(); ++it)
            {
                Chain chain;
                ReadChain(it->value, chain);
                for (auto& id : chain)
                {
                    if (!ids.Contains(id))
                        ids.Add(id);
                }
            }
        };

        collect(manifest.FindMember("configs"));

        if (auto timeline = manifest.FindMember("timeline"))
        {
            if (timeline->IsArray())
            {
                for (auto& entry : *timeline)
                {
                    if (entry.IsObject())
                        collect(entry.FindMember("configs"));
                }
            }
        }
    }

    void RemoteConfig::Rebuild()
    {
        double now = GetServerTime();

        Map<String, Chain> chains;
        if (auto configs = mManifest.FindMember("configs"))
        {
            if (configs->IsObject())
            {
                for (auto it = configs->BeginMember(); it != configs->EndMember(); ++it)
                    ReadChain(it->value, chains[String(it->name.GetString())]);
            }
        }

        // The answer describes "now" of the moment it was made; the timeline carries every switch
        // after it, in time order. Those already due are laid over
        mNextSwitchAt = 0;
        if (auto timeline = mManifest.FindMember("timeline"))
        {
            if (timeline->IsArray())
            {
                for (auto& entry : *timeline)
                {
                    if (!entry.IsObject())
                        continue;

                    double at = NumberOf(entry.FindMember("at"));
                    if (at > now)
                    {
                        if (mNextSwitchAt == 0 || at < mNextSwitchAt)
                            mNextSwitchAt = at;

                        continue;
                    }

                    auto configs = entry.FindMember("configs");
                    if (!configs || !configs->IsObject())
                        continue;

                    for (auto it = configs->BeginMember(); it != configs->EndMember(); ++it)
                    {
                        Chain chain;
                        ReadChain(it->value, chain);

                        String key(it->name.GetString());
                        if (chain.IsEmpty())
                            chains.Remove(key);
                        else
                            chains[key] = chain;
                    }
                }
            }
        }

        mExperiments.Clear();
        if (auto experiments = mManifest.FindMember("experiments"))
        {
            if (experiments->IsObject())
            {
                for (auto it = experiments->BeginMember(); it != experiments->EndMember(); ++it)
                    mExperiments[String(it->name.GetString())] = StringOf(&it->value);
            }
        }

        mExposureByConfig.Clear();
        if (auto exposure = mManifest.FindMember("exposure"))
        {
            if (exposure->IsObject())
            {
                for (auto it = exposure->BeginMember(); it != exposure->EndMember(); ++it)
                    ReadChain(it->value, mExposureByConfig[String(it->name.GetString())]);
            }
        }

        bool changed = false;

        Vector<String> gone;
        for (auto& kv : mActiveChains)
        {
            if (!chains.ContainsKey(kv.first))
                gone.Add(kv.first);
        }

        for (auto& key : gone)
        {
            mActiveChains.Remove(key);
            mConfigs.Remove(key);
            changed = true;
        }

        for (auto& kv : chains)
        {
            if (mActiveChains.ContainsKey(kv.first) && mActiveChains[kv.first] == kv.second)
                continue;

            DataDocument merged;
            bool complete = true;
            bool first = true;
            for (auto& id : kv.second)
            {
                String text;
                DataDocument part;
                if (!ReadDocument(id, text) || !part.LoadFromData(text))
                {
                    complete = false;
                    break;
                }

                if (first)
                    merged = part;
                else
                    ApplyMergePatch(merged, part);

                first = false;
            }

            // A document lost from the storage: keep what is in place, the next fetch brings it back
            if (!complete)
            {
                mNextFetchAt = 0;
                continue;
            }

            mConfigs[kv.first] = merged;
            mActiveChains[kv.first] = kv.second;
            changed = true;
        }

        if (changed)
            OnConfigsChanged();
    }

    void RemoteConfig::OnConfigsChanged()
    {
        mRevision++;

        if (onChanged)
            onChanged();
    }

    int RemoteConfig::GetRevision() const
    {
        return mRevision;
    }

    void RemoteConfig::SetLocalOverride(const String& key, const DataValue& config)
    {
        DataDocument doc;
        (DataValue&)doc = config;
        mOverrides[key] = doc;

        OnConfigsChanged();
    }

    void RemoteConfig::ClearLocalOverride(const String& key /*= String()*/)
    {
        if (key.IsEmpty())
            mOverrides.Clear();
        else
            mOverrides.Remove(key);

        OnConfigsChanged();
    }

    void RemoteConfig::Prune()
    {
        Vector<String> used;
        CollectDocumentIds(mManifest, used);

        String prefix = GetStorageKey("doc.");
        for (auto& key : mStorage->GetKeys(prefix))
        {
            if (!used.Contains(key.SubStr(prefix.Length())))
                mStorage->Remove(key);
        }
    }

    void RemoteConfig::Update(float dt)
    {
        if (!IsInitialized())
            return;

        double now = GetServerTime();

        if (mHasManifest && mNextSwitchAt > 0 && now >= mNextSwitchAt)
            Rebuild();

        if (!mFetch && mNextFetchAt > 0 && now >= mNextFetchAt)
            Fetch();

        mExposureTimer += dt;
        if (mExposureTimer >= exposureFlushPeriod)
        {
            mExposureTimer = 0;

            if (!mSendingExposures && mPendingExposures.GetElementsCount() > 0)
                Async(FlushExposures(mAlive), JobThread::Main);
        }
    }

    void RemoteConfig::StartUpdating()
    {
        if (mUpdating || !TaskManager::IsSingletonInitialzed())
            return;

        mUpdating = true;

        auto alive = mAlive;
        o2Tasks.Run([this, alive](float dt) { if (*alive) Update(dt); }, [alive]() { return !*alive; });
    }

    bool RemoteConfig::HasConfig(const String& key) const
    {
        return mOverrides.ContainsKey(key) || mConfigs.ContainsKey(key);
    }

    const DataValue& RemoteConfig::GetConfig(const String& key)
    {
        if (mOverrides.ContainsKey(key))
            return mOverrides[key];

        if (!mConfigs.ContainsKey(key))
            return mNullValue;

        // Reading a config is the moment the player meets the experiments that shaped it
        if (mExposureByConfig.ContainsKey(key))
        {
            for (auto& experiment : mExposureByConfig[key])
                NoteExposure(experiment);
        }

        return mConfigs[key];
    }

    const DataValue& RemoteConfig::GetValue(const String& key, const String& path)
    {
        const DataValue* value = &GetConfig(key);
        if (path.IsEmpty())
            return *value;

        for (auto& part : path.Split("."))
        {
            if (value->IsObject())
            {
                value = value->FindMember(part.Data());
                if (!value)
                    return mNullValue;
            }
            else if (value->IsArray())
            {
                char* end = nullptr;
                long idx = std::strtol(part.Data(), &end, 10);
                if (end == part.Data() || *end != '\0' || idx < 0 || idx >= value->GetElementsCount())
                    return mNullValue;

                value = &value->GetElement((int)idx);
            }
            else
                return mNullValue;
        }

        return *value;
    }

    bool RemoteConfig::GetObject(const String& key, ISerializable& object)
    {
        const DataValue& value = GetConfig(key);
        if (!value.IsObject())
            return false;

        object.Deserialize(value);
        return true;
    }

    Vector<String> RemoteConfig::GetConfigKeys() const
    {
        Vector<String> res;
        for (auto& kv : mConfigs)
            res.Add(kv.first);

        return res;
    }

    String RemoteConfig::GetGroup(const String& experiment)
    {
        String group;
        if (!mExperiments.TryGetValue(experiment, group))
            return String();

        NoteExposure(experiment);
        return group;
    }

    const Map<String, String>& RemoteConfig::GetExperiments() const
    {
        return mExperiments;
    }

    void RemoteConfig::SetAttribute(const String& name, const String& value)
    {
        mAttributes[name.Data()] = value;
    }

    void RemoteConfig::SetAttribute(const String& name, double value)
    {
        mAttributes[name.Data()] = value;
    }

    void RemoteConfig::SetAttribute(const String& name, bool value)
    {
        mAttributes[name.Data()] = value;
    }

    void RemoteConfig::ClearCache()
    {
        if (!mStorage)
            return;

        for (auto& key : mStorage->GetKeys(mStoragePrefix))
            mStorage->Remove(key);

        mHasManifest = false;
        mManifest.Clear();
        mNextSwitchAt = 0;

        bool had = !mConfigs.IsEmpty();
        mActiveChains.Clear();
        mConfigs.Clear();
        mExperiments.Clear();
        mExposureByConfig.Clear();
        mExposed.Clear();
        mPendingExposures.SetArray();

        if (had)
            OnConfigsChanged();
    }

    void RemoteConfig::NoteExposure(const String& experiment)
    {
        String group;
        if (!mExperiments.TryGetValue(experiment, group) || group.IsEmpty())
            return;

        String mark = experiment + ":" + group;
        if (mExposed.Contains(mark))
            return;

        mExposed.Add(mark);

        String joined;
        for (auto& item : mExposed)
            joined += (joined.IsEmpty() ? "" : "\n") + item;

        mStorage->Write(GetStorageKey("exposed"), joined);

        if (mPendingExposures.GetElementsCount() >= maxPendingExposures)
            return;

        DataValue& event = mPendingExposures.AddElement();
        event.SetObject();
        event.AddMember("type") = String("exposure");
        event.AddMember("experiment") = experiment;
        event.AddMember("group") = group;
        event.AddMember("at") = GetServerTime();

        mStorage->Write(GetStorageKey("events"), mPendingExposures.SaveAsString());
    }

    Coroutine<void> RemoteConfig::FlushExposures(std::shared_ptr<bool> alive)
    {
        if (!*alive)
            co_return;

        mSendingExposures = true;
        int sent = mPendingExposures.GetElementsCount();

        DataDocument request;
        request.SetObject();
        request.AddMember("key") = mSettings.key;
        request.AddMember("player") = o2PlayerIdentity.GetId();
        request.AddMember("events") = (const DataValue&)mPendingExposures;

        ServiceResponse response = co_await mTransport->Post(mSettings.url + "/v1/events", request.SaveAsString());
        if (!*alive)
            co_return;

        mSendingExposures = false;

        // 4xx: the service will never take these, they are not resent
        if (!response.ok && !(response.status >= 400 && response.status < 500))
            co_return;

        DataDocument rest;
        rest.SetArray();
        for (int i = sent; i < mPendingExposures.GetElementsCount(); i++)
            rest.AddElement() = mPendingExposures.GetElement(i);

        mPendingExposures = rest;
        mStorage->Write(GetStorageKey("events"), mPendingExposures.SaveAsString());
    }
}
