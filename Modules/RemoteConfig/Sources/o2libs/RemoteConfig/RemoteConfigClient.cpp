#include "o2/stdafx.h"
#include "RemoteConfigClient.h"

#include "o2/Utils/Debug/Debug.h"
#include "o2libs/Core/JsonMergePatch.h"
#include "o2libs/Core/PlayerIdentity.h"

#include <chrono>
#include <cstdlib>

namespace o2libs
{
    DataDocument RemoteConfigClient::mNullValue;

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

    RemoteConfigClient::RemoteConfigClient(RefCounter* refCounter,
                                           const Ref<IRemoteConfigTransport>& transport /*= nullptr*/,
                                           const Ref<IStorageBackend>& storage /*= nullptr*/,
                                           const Function<double()>& clock /*= Function<double()>()*/):
        mTransport(transport), mStorage(storage), mClock(clock), mAlive(std::make_shared<bool>(true))
    {
        SetRefCounter(refCounter);

        if (!mTransport)
            mTransport = mmake<HttpRemoteConfigTransport>();

        mAttributes.SetObject();
        mPendingExposures.SetArray();
    }

    RemoteConfigClient::~RemoteConfigClient()
    {
        *mAlive = false;
    }

    void RemoteConfigClient::Init(const ServiceSettings& settings)
    {
        mSettings = settings;
        while (mSettings.url.EndsWith("/"))
            mSettings.url = mSettings.url.SubStr(0, mSettings.url.Length() - 1);

        if (!mStorage)
            mStorage = Storage::GetBackend();

        // Several games can share one storage (every portal game lives on one web origin)
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
        if (mStorage->Read(StorageKey("manifest"), text) && mManifest.LoadFromData(text) && mManifest.IsObject())
        {
            mHasManifest = true;
            Rebuild();
        }

        if (mStorage->Read(StorageKey("exposed"), text) && !text.IsEmpty())
            mExposed = text.Split("\n");

        mPendingExposures.SetArray();
        if (mStorage->Read(StorageKey("events"), text))
        {
            if (!mPendingExposures.LoadFromData(text) || !mPendingExposures.IsArray())
                mPendingExposures.SetArray();
        }
    }

    bool RemoteConfigClient::IsInitialized() const
    {
        return mSettings.IsValid();
    }

    bool RemoteConfigClient::IsReady() const
    {
        return mHasManifest;
    }

    bool RemoteConfigClient::IsFetching() const
    {
        return mFetching;
    }

    String RemoteConfigClient::StorageKey(const String& name) const
    {
        return mStoragePrefix + name;
    }

    bool RemoteConfigClient::ReadDocument(const String& id, String& text) const
    {
        return mStorage->Read(StorageKey("doc." + id), text);
    }

    double RemoteConfigClient::GetServerTime() const
    {
        return (mClock ? mClock() : SystemClock()) + mClockOffset;
    }

    void RemoteConfigClient::Fetch(const Function<void(bool)>& onCompleted)
    {
        if (!IsInitialized())
        {
            o2Debug.LogWarning("RemoteConfig: fetch before Init, or LiveOps.json has no url and key");
            if (onCompleted)
                onCompleted(false);

            return;
        }

        if (onCompleted)
            mFetchCallbacks.Add(onCompleted);

        if (mFetching)
            return;

        mFetching = true;

        DataDocument request;
        request.SetObject();
        request.AddMember("key") = mSettings.key;
        request.AddMember("player") = PlayerIdentity::GetId();
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

        std::weak_ptr<bool> alive = mAlive;
        mTransport->Post(mSettings.url + "/v1/fetch", request.SaveAsString(),
                         [this, alive](bool ok, int status, const String& body) {
                             auto guard = alive.lock();
                             if (guard && *guard)
                                 OnManifestAnswer(ok, body);
                         });
    }

    void RemoteConfigClient::OnManifestAnswer(bool ok, const String& body)
    {
        if (!ok || !mIncomingManifest.LoadFromData(body) || !mIncomingManifest.IsObject())
        {
            FinishFetch(false);
            return;
        }

        double serverTime = NumberOf(mIncomingManifest.FindMember("time"));
        if (serverTime > 0)
            mClockOffset = serverTime - (mClock ? mClock() : SystemClock());

        double ttl = NumberOf(mIncomingManifest.FindMember("ttl"), 900);

        bool unchanged = false;
        if (auto value = mIncomingManifest.FindMember("unchanged"))
        {
            if (value->IsBoolean())
                value->Get(unchanged);
        }

        if (unchanged && mHasManifest)
        {
            mNextFetchAt = GetServerTime() + ttl;
            FinishFetch(true);
            return;
        }

        DownloadMissing();
    }

    void RemoteConfigClient::CollectIds(const DataValue& manifest, Vector<String>& ids)
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

    void RemoteConfigClient::DownloadMissing()
    {
        Vector<String> ids;
        CollectIds(mIncomingManifest, ids);

        String cdn = StringOf(mIncomingManifest.FindMember("cdn"));
        while (cdn.EndsWith("/"))
            cdn = cdn.SubStr(0, cdn.Length() - 1);

        Vector<String> missing;
        for (auto& id : ids)
        {
            if (!IsDocumentId(id))
            {
                o2Debug.LogWarning("RemoteConfig: the service answered with a malformed document id");
                FinishFetch(false);
                return;
            }

            String text;
            if (!ReadDocument(id, text))
                missing.Add(id);
        }

        if (missing.IsEmpty())
        {
            OnDocumentsReady();
            return;
        }

        mPendingDocuments = missing.Count();
        mDocumentsFailed = false;

        std::weak_ptr<bool> alive = mAlive;
        for (auto& id : missing)
        {
            mTransport->Get(cdn + "/" + id + ".json", [this, alive, id](bool ok, int status, const String& body) {
                auto guard = alive.lock();
                if (!guard || !*guard)
                    return;

                DataDocument check;
                if (ok && check.LoadFromData(body))
                    mStorage->Write(StorageKey("doc." + id), body);
                else
                    mDocumentsFailed = true;

                if (--mPendingDocuments > 0)
                    return;

                // All or nothing: a half-downloaded answer would mix two published versions
                if (mDocumentsFailed)
                    FinishFetch(false);
                else
                    OnDocumentsReady();
            });
        }
    }

    void RemoteConfigClient::OnDocumentsReady()
    {
        mManifest = mIncomingManifest;
        mHasManifest = true;
        mStorage->Write(StorageKey("manifest"), mManifest.SaveAsString());

        mNextFetchAt = GetServerTime() + NumberOf(mManifest.FindMember("ttl"), 900);

        Rebuild();
        Prune();
        FinishFetch(true);
    }

    void RemoteConfigClient::FinishFetch(bool success)
    {
        mFetching = false;

        if (!success)
            mNextFetchAt = GetServerTime() + retryPeriod;

        auto callbacks = mFetchCallbacks;
        mFetchCallbacks.Clear();

        for (auto& callback : callbacks)
            callback(success);
    }

    void RemoteConfigClient::Rebuild()
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

        if (changed && onChanged)
            onChanged();
    }

    void RemoteConfigClient::Prune()
    {
        Vector<String> used;
        CollectIds(mManifest, used);

        String prefix = StorageKey("doc.");
        for (auto& key : mStorage->GetKeys(prefix))
        {
            if (!used.Contains(key.SubStr(prefix.Length())))
                mStorage->Remove(key);
        }
    }

    void RemoteConfigClient::Update(float dt)
    {
        if (!IsInitialized())
            return;

        double now = GetServerTime();

        if (mHasManifest && mNextSwitchAt > 0 && now >= mNextSwitchAt)
            Rebuild();

        if (!mFetching && mNextFetchAt > 0 && now >= mNextFetchAt)
            Fetch();

        mExposureTimer += dt;
        if (mExposureTimer >= exposureFlushPeriod)
        {
            mExposureTimer = 0;
            FlushExposures();
        }
    }

    bool RemoteConfigClient::HasConfig(const String& key) const
    {
        return mConfigs.ContainsKey(key);
    }

    const DataValue& RemoteConfigClient::GetConfig(const String& key)
    {
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

    const DataValue& RemoteConfigClient::GetValue(const String& key, const String& path)
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

    bool RemoteConfigClient::GetObject(const String& key, ISerializable& object)
    {
        const DataValue& value = GetConfig(key);
        if (!value.IsObject())
            return false;

        object.Deserialize(value);
        return true;
    }

    Vector<String> RemoteConfigClient::GetConfigKeys() const
    {
        Vector<String> res;
        for (auto& kv : mConfigs)
            res.Add(kv.first);

        return res;
    }

    String RemoteConfigClient::GetGroup(const String& experiment)
    {
        String group;
        if (!mExperiments.TryGetValue(experiment, group))
            return String();

        NoteExposure(experiment);
        return group;
    }

    const Map<String, String>& RemoteConfigClient::GetExperiments() const
    {
        return mExperiments;
    }

    void RemoteConfigClient::SetAttribute(const String& name, const String& value)
    {
        mAttributes[name.Data()] = value;
    }

    void RemoteConfigClient::SetAttribute(const String& name, double value)
    {
        mAttributes[name.Data()] = value;
    }

    void RemoteConfigClient::SetAttribute(const String& name, bool value)
    {
        mAttributes[name.Data()] = value;
    }

    void RemoteConfigClient::ClearCache()
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

        if (had && onChanged)
            onChanged();
    }

    void RemoteConfigClient::NoteExposure(const String& experiment)
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

        mStorage->Write(StorageKey("exposed"), joined);

        if (mPendingExposures.GetElementsCount() >= maxPendingExposures)
            return;

        DataValue& event = mPendingExposures.AddElement();
        event.SetObject();
        event.AddMember("type") = String("exposure");
        event.AddMember("experiment") = experiment;
        event.AddMember("group") = group;
        event.AddMember("at") = GetServerTime();

        mStorage->Write(StorageKey("events"), mPendingExposures.SaveAsString());
    }

    void RemoteConfigClient::FlushExposures()
    {
        if (mSendingExposures || mPendingExposures.GetElementsCount() == 0)
            return;

        mSendingExposures = true;
        int sent = mPendingExposures.GetElementsCount();

        DataDocument request;
        request.SetObject();
        request.AddMember("key") = mSettings.key;
        request.AddMember("player") = PlayerIdentity::GetId();
        request.AddMember("events") = (const DataValue&)mPendingExposures;

        std::weak_ptr<bool> alive = mAlive;
        mTransport->Post(mSettings.url + "/v1/events", request.SaveAsString(),
                         [this, alive, sent](bool ok, int status, const String& body) {
                             auto guard = alive.lock();
                             if (!guard || !*guard)
                                 return;

                             mSendingExposures = false;

                             // 4xx: the service will never take these, do not keep resending them
                             if (!ok && !(status >= 400 && status < 500))
                                 return;

                             DataDocument rest;
                             rest.SetArray();
                             for (int i = sent; i < mPendingExposures.GetElementsCount(); i++)
                                 rest.AddElement() = mPendingExposures.GetElement(i);

                             mPendingExposures = rest;
                             mStorage->Write(StorageKey("events"), mPendingExposures.SaveAsString());
                         });
    }
}
