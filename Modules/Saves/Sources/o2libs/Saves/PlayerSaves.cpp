#include "o2/stdafx.h"
#include "PlayerSaves.h"

#include "o2/Utils/Debug/Debug.h"
#include "o2/Utils/Tasks/TaskManager.h"
#include "o2libs/Core/PlayerIdentity.h"

#include <chrono>

namespace o2
{
    DECLARE_SINGLETON(o2libs::PlayerSaves);
}

namespace o2libs
{
    DataDocument PlayerSaves::mNullValue;

    namespace
    {
        const int saveFormat = 1;

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
    }

    PlayerSaves::PlayerSaves(RefCounter* refCounter,
                             const Ref<IServiceTransport>& transport /*= nullptr*/,
                             const Ref<IStorageBackend>& storage /*= nullptr*/,
                             const Function<double()>& clock /*= Function<double()>()*/):
        Singleton<PlayerSaves>(refCounter), mTransport(transport), mStorage(storage), mClock(clock),
        mAlive(std::make_shared<bool>(true))
    {
        if (!mTransport)
            mTransport = mmake<HttpServiceTransport>();
    }

    PlayerSaves::~PlayerSaves()
    {
        if (mInitialized && mUnsaved)
            Save();

        *mAlive = false;

        if (mInstance == this)
            mInstance = nullptr;
    }

    void PlayerSaves::Initialize(const ServiceSettings& settings)
    {
        mSettings = settings;
        while (!mSettings.url.IsEmpty() && mSettings.url.EndsWith("/"))
            mSettings.url = mSettings.url.SubStr(0, mSettings.url.Length() - 1);

        if (!mStorage)
            mStorage = o2Storage.GetBackend();

        // Several games can share one storage: every game of the portal lives on one web origin
        mStorageKey = "save." + (mSettings.key.IsEmpty() ? String("local") : mSettings.key);

        mSections.Clear();
        mRevision = 0;
        mUpdatedAt = 0;
        mUnsaved = false;
        mUnsynced = false;

        String text;
        DataDocument save;
        if (mStorage->Read(mStorageKey, text) && save.LoadFromData(text) && save.IsObject())
            ReadSave(save);

        mInitialized = true;

        StartUpdating();
        RegisterScriptApi();
    }

    void PlayerSaves::InitializeFromAssets()
    {
        Initialize(ServiceSettings::GetDefault());

        if (IsCloudEnabled())
            Sync();
    }

    bool PlayerSaves::IsInitialized() const
    {
        return mInitialized;
    }

    bool PlayerSaves::IsCloudEnabled() const
    {
        return mInitialized && mSettings.IsValid();
    }

    void PlayerSaves::ReadSave(const DataValue& save)
    {
        mRevision = (int)NumberOf(save.FindMember("rev"));
        mUpdatedAt = NumberOf(save.FindMember("updatedAt"));

        if (auto unsynced = save.FindMember("unsynced"))
        {
            if (unsynced->IsBoolean())
                unsynced->Get(mUnsynced);
        }

        auto sections = save.FindMember("sections");
        if (!sections || !sections->IsObject())
            return;

        for (auto it = sections->BeginMember(); it != sections->EndMember(); ++it)
        {
            if (!it->value.IsObject())
                continue;

            Section& section = mSections[String(it->name.GetString())];
            section.version = (int)NumberOf(it->value.FindMember("v"), 1);

            if (auto data = it->value.FindMember("data"))
                (DataValue&)section.data = *data;
        }
    }

    void PlayerSaves::WriteSave(DataValue& save, bool withState)
    {
        save.SetObject();
        save.AddMember("format") = saveFormat;
        save.AddMember("player") = o2PlayerIdentity.GetId();
        save.AddMember("updatedAt") = mUpdatedAt;

        // What only this device needs to know; the service keeps its own
        if (withState)
        {
            save.AddMember("rev") = mRevision;
            save.AddMember("unsynced") = mUnsynced;
        }

        DataValue& sections = save.AddMember("sections");
        sections.SetObject();

        for (auto& kv : mSections)
        {
            Section& section = kv.second;
            if (section.object)
            {
                section.data.Clear();
                section.object->Serialize(section.data);
                section.version = section.object->GetVersion();
            }

            if (section.data.IsNull())
                continue;

            DataValue& stored = sections.AddMember(kv.first.Data());
            stored.SetObject();
            stored.AddMember("v") = section.version;
            stored.AddMember("data") = (const DataValue&)section.data;
        }
    }

    void PlayerSaves::LoadSectionObject(Section& section)
    {
        if (section.data.IsNull())
        {
            section.object->OnLoaded();
            return;
        }

        int version = section.object->GetVersion();
        if (section.version < version)
        {
            section.object->OnMigrate(section.version, section.data);
            section.version = version;
        }

        section.object->Deserialize(section.data);
        section.object->OnLoaded();
    }

    bool PlayerSaves::HasSection(const String& name) const
    {
        return mSections.ContainsKey(name);
    }

    const DataValue& PlayerSaves::GetSectionData(const String& name)
    {
        if (!mSections.ContainsKey(name))
            return mNullValue;

        Section& section = mSections[name];
        if (section.object)
        {
            section.data.Clear();
            section.object->Serialize(section.data);
        }

        return section.data;
    }

    void PlayerSaves::SetSectionData(const String& name, const DataValue& data)
    {
        Section& section = mSections[name];
        (DataValue&)section.data = data;

        if (section.object)
        {
            section.version = section.object->GetVersion();
            LoadSectionObject(section);
        }

        MarkChanged();
    }

    void PlayerSaves::RemoveSection(const String& name)
    {
        if (!mSections.ContainsKey(name))
            return;

        mSections.Remove(name);
        MarkChanged();
    }

    void PlayerSaves::MarkChanged()
    {
        mUpdatedAt = mClock ? mClock() : SystemClock();
        mUnsaved = true;
        mUnsynced = true;
    }

    void PlayerSaves::Save()
    {
        if (!mInitialized)
            return;

        DataDocument save;
        WriteSave(save, true);
        mStorage->Write(mStorageKey, save.SaveAsString());

        mUnsaved = false;
        mSaveTimer = 0;
    }

    void PlayerSaves::Reset()
    {
        for (auto& kv : mSections)
        {
            kv.second.data.Clear();

            if (kv.second.object)
            {
                // The game keeps its reference: the object is emptied, not replaced
                kv.second.object->ResetToDefaults();
                kv.second.object->OnLoaded();
            }
        }

        MarkChanged();
        Save();
    }

    String PlayerSaves::GetSaveJson()
    {
        DataDocument save;
        WriteSave(save, false);
        return save.SaveAsString();
    }

    int PlayerSaves::GetRevision() const
    {
        return mRevision;
    }

    bool PlayerSaves::HasUnsyncedChanges() const
    {
        return mUnsynced;
    }

    Coroutine<bool> PlayerSaves::Sync()
    {
        auto sync = RunSync(mAlive);
        sync.Start(JobThread::Main);
        return sync;
    }

    Coroutine<bool> PlayerSaves::RunSync(std::shared_ptr<bool> alive)
    {
        if (!*alive || !IsCloudEnabled() || mSyncing)
            co_return false;

        mSyncing = true;
        mSyncTimer = 0;

        double sentAt = mUpdatedAt;

        DataDocument request;
        request.SetObject();
        request.AddMember("key") = mSettings.key;
        request.AddMember("player") = o2PlayerIdentity.GetId();
        request.AddMember("platform") = String(PlatformName());
        request.AddMember("rev") = mRevision;
        request.AddMember("changed") = mUnsynced;
        request.AddMember("updatedAt") = mUpdatedAt;

        if (!mSettings.appVersion.IsEmpty())
            request.AddMember("app") = mSettings.appVersion;

        if (mUnsynced)
            WriteSave(request.AddMember("save"), false);

        ServiceResponse response = co_await mTransport->Post(mSettings.url + "/v1/saves/sync", request.SaveAsString());
        if (!*alive)
            co_return false;

        mSyncing = false;

        DataDocument answer;
        if (!response.ok || !answer.LoadFromData(response.body) || !answer.IsObject())
            co_return false;

        int revision = (int)NumberOf(answer.FindMember("rev"), mRevision);
        auto taken = answer.FindMember("save");

        if (taken && taken->IsObject())
            TakeServerSave(*taken, revision);
        else
        {
            mRevision = revision;

            // Changed again while the request was away: that change is still to be sent
            if (mUpdatedAt == sentAt)
                mUnsynced = false;
        }

        Save();
        co_return true;
    }

    void PlayerSaves::TakeServerSave(const DataValue& data, int revision)
    {
        // Objects the game holds stay the same objects: their content is replaced
        Map<String, Ref<SaveSection>> objects;
        for (auto& kv : mSections)
        {
            if (kv.second.object)
                objects[kv.first] = kv.second.object;
        }

        mSections.Clear();
        ReadSave(data);

        mRevision = revision;
        mUnsynced = false;

        for (auto& kv : objects)
        {
            Section& section = mSections[kv.first];
            section.object = kv.second;

            if (section.data.IsNull())
            {
                kv.second->ResetToDefaults();
                kv.second->OnLoaded();
            }
            else
                LoadSectionObject(section);
        }

        if (onReplaced)
            onReplaced();
    }

    void PlayerSaves::Update(float dt)
    {
        if (!mInitialized)
            return;

        if (mUnsaved)
        {
            mSaveTimer += dt;
            if (mSaveTimer >= saveDelay)
                Save();
        }

        if (mUnsynced && IsCloudEnabled() && !mSyncing)
        {
            mSyncTimer += dt;
            if (mSyncTimer >= syncPeriod)
                Sync();
        }
    }

    void PlayerSaves::StartUpdating()
    {
        if (mUpdating || !TaskManager::IsSingletonInitialzed())
            return;

        mUpdating = true;

        auto alive = mAlive;
        o2Tasks.Run([this, alive](float dt) { if (*alive) Update(dt); }, [alive]() { return !*alive; });
    }
}
