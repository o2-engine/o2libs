#include "o2/stdafx.h"
#include "RemoteConfig.h"

#include "o2/Utils/Tasks/TaskManager.h"

namespace o2libs
{
    Ref<RemoteConfigClient>  RemoteConfig::mClient;
    Vector<Function<void()>> RemoteConfig::mListeners;
    bool                     RemoteConfig::mPumping = false;

    void RemoteConfig::Init(const String& url, const String& key)
    {
        ServiceSettings settings = ServiceSettings::GetDefault();
        settings.url = url;
        settings.key = key;

        GetClient()->Init(settings);
    }

    bool RemoteConfig::InitFromAssets()
    {
        const ServiceSettings& settings = ServiceSettings::GetDefault();
        if (!settings.IsValid())
            return false;

        GetClient()->Init(settings);
        GetClient()->Fetch();
        return true;
    }

    void RemoteConfig::Fetch(const Function<void(bool)>& onCompleted)
    {
        GetClient()->Fetch(onCompleted);
    }

    bool RemoteConfig::IsReady()
    {
        return GetClient()->IsReady();
    }

    bool RemoteConfig::Has(const String& key)
    {
        return GetClient()->HasConfig(key);
    }

#if IS_SCRIPTING_SUPPORTED
    ScriptValue RemoteConfig::Get(const String& key)
    {
        const DataValue& value = GetClient()->GetConfig(key);
        if (value.IsNull())
            return ScriptValue();

        ScriptValue res;
        value.Get(res);
        return res;
    }
#endif

    String RemoteConfig::GetString(const String& key, const String& path, const String& defaultValue)
    {
        const DataValue& value = GetClient()->GetValue(key, path);
        return value.IsString() ? String(value.GetString()) : defaultValue;
    }

    float RemoteConfig::GetNumber(const String& key, const String& path, float defaultValue)
    {
        const DataValue& value = GetClient()->GetValue(key, path);
        if (!value.IsNumber())
            return defaultValue;

        double res = defaultValue;
        value.Get(res);
        return (float)res;
    }

    bool RemoteConfig::GetBool(const String& key, const String& path, bool defaultValue)
    {
        const DataValue& value = GetClient()->GetValue(key, path);
        if (!value.IsBoolean())
            return defaultValue;

        bool res = defaultValue;
        value.Get(res);
        return res;
    }

    String RemoteConfig::GetGroup(const String& experiment)
    {
        return GetClient()->GetGroup(experiment);
    }

    void RemoteConfig::SetAttribute(const String& name, const String& value)
    {
        GetClient()->SetAttribute(name, value);
    }

    void RemoteConfig::SetNumberAttribute(const String& name, float value)
    {
        GetClient()->SetAttribute(name, (double)value);
    }

    int RemoteConfig::GetRevision()
    {
        return GetClient()->GetRevision();
    }

    void RemoteConfig::OnChanged(const Function<void()>& listener)
    {
        mListeners.Add(listener);
    }

    void RemoteConfig::Update(float dt)
    {
        if (mClient)
            mClient->Update(dt);
    }

    const Ref<RemoteConfigClient>& RemoteConfig::GetClient()
    {
        if (!mClient)
            SetClient(mmake<RemoteConfigClient>());

        // The client's timers ride the engine's tasks: no call from the game's update is needed
        if (!mPumping && TaskManager::IsSingletonInitialzed())
        {
            mPumping = true;
            o2Tasks.Run([](float dt) { RemoteConfig::Update(dt); }, []() { return false; });
        }

        return mClient;
    }

    void RemoteConfig::SetClient(const Ref<RemoteConfigClient>& client)
    {
        mClient = client;

        if (!mClient)
        {
            mListeners.Clear();
            return;
        }

        mClient->onChanged = []() {
            auto listeners = mListeners;
            for (auto& listener : listeners)
                listener();
        };
    }
}
// --- META ---

DECLARE_CLASS(o2libs::RemoteConfig, o2libs__RemoteConfig);
// --- END META ---
