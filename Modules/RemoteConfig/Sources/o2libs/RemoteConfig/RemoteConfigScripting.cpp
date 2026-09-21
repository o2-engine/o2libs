#include "o2/stdafx.h"
#include "RemoteConfig.h"

#if IS_SCRIPTING_SUPPORTED
#include "o2/Scripts/ScriptEngine.h"
#endif

namespace o2libs
{
    void RemoteConfig::RegisterScriptApi()
    {
#if IS_SCRIPTING_SUPPORTED
        if (!ScriptEngine::IsSingletonInitialzed())
            return;

        auto global = o2Scripts.GetGlobal();
        if (global.GetProperty("o2libs").GetValueType() != ScriptValue::ValueType::Object)
            global.SetProperty("o2libs", ScriptValue::EmptyObject());

        // One object over the singleton: scripts have no coroutines, so Fetch takes a function here
        auto api = ScriptValue::EmptyObject();

        api.SetProperty("IsReady", Function<bool()>([]() { return o2RemoteConfig.IsReady(); }));
        api.SetProperty("Has", Function<bool(const String&)>([](const String& key) { return o2RemoteConfig.HasConfig(key); }));

        api.SetProperty("Get", Function<ScriptValue(const String&)>([](const String& key)
        {
            const DataValue& value = o2RemoteConfig.GetConfig(key);
            if (value.IsNull())
                return ScriptValue();

            ScriptValue res;
            value.Get(res);
            return res;
        }));

        api.SetProperty("GetNumber", Function<float(const String&, const String&, float)>([](const String& key, const String& path, float defaultValue)
        {
            const DataValue& value = o2RemoteConfig.GetValue(key, path);
            return value.IsNumber() ? (float)(double)value : defaultValue;
        }));

        api.SetProperty("GetString", Function<String(const String&, const String&, const String&)>([](const String& key, const String& path, const String& defaultValue)
        {
            const DataValue& value = o2RemoteConfig.GetValue(key, path);
            return value.IsString() ? String(value.GetString()) : defaultValue;
        }));

        api.SetProperty("GetBool", Function<bool(const String&, const String&, bool)>([](const String& key, const String& path, bool defaultValue)
        {
            const DataValue& value = o2RemoteConfig.GetValue(key, path);
            return value.IsBoolean() ? (bool)value : defaultValue;
        }));

        api.SetProperty("GetGroup", Function<String(const String&)>([](const String& experiment) { return o2RemoteConfig.GetGroup(experiment); }));
        api.SetProperty("GetRevision", Function<int()>([]() { return o2RemoteConfig.GetRevision(); }));

        api.SetProperty("SetAttribute", Function<void(const String&, const String&)>([](const String& name, const String& value) { o2RemoteConfig.SetAttribute(name, value); }));
        api.SetProperty("SetNumberAttribute", Function<void(const String&, float)>([](const String& name, float value) { o2RemoteConfig.SetAttribute(name, (double)value); }));

        api.SetProperty("Init", Function<void(const String&, const String&)>([](const String& url, const String& key)
        {
            ServiceSettings settings = ServiceSettings::GetDefault();
            settings.url = url;
            settings.key = key;
            o2RemoteConfig.Initialize(settings);
        }));

        api.SetProperty("Fetch", Function<void(const Function<void(bool)>&)>([](const Function<void(bool)>& onCompleted)
        {
            Async([](Function<void(bool)> onCompleted) -> Coroutine<void>
            {
                bool success = co_await o2RemoteConfig.Fetch();
                if (onCompleted)
                    onCompleted(success);
            }(onCompleted), JobThread::Main);
        }));

        api.SetProperty("OnChanged", Function<void(const Function<void()>&)>([](const Function<void()>& listener)
        {
            auto previous = o2RemoteConfig.onChanged;
            o2RemoteConfig.onChanged = [previous, listener]()
            {
                if (previous)
                    previous();

                listener();
            };
        }));

        global.GetProperty("o2libs").SetProperty("RemoteConfig", api);
#endif
    }
}
