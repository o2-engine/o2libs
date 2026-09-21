#include "o2/stdafx.h"
#include "PlayerSaves.h"

#include "o2libs/Core/PlayerIdentity.h"

#if IS_SCRIPTING_SUPPORTED
#include "o2/Scripts/ScriptEngine.h"
#endif

namespace o2libs
{
    void PlayerSaves::RegisterScriptApi()
    {
#if IS_SCRIPTING_SUPPORTED
        if (!ScriptEngine::IsSingletonInitialzed())
            return;

        auto global = o2Scripts.GetGlobal();
        if (global.GetProperty("o2libs").GetValueType() != ScriptValue::ValueType::Object)
            global.SetProperty("o2libs", ScriptValue::EmptyObject());

        // One object over the singleton. A script's section is a plain object: Get gives a copy, Set puts it back
        auto api = ScriptValue::EmptyObject();

        api.SetProperty("GetPlayerId", Function<String()>([]() { return o2PlayerIdentity.GetId(); }));
        api.SetProperty("Has", Function<bool(const String&)>([](const String& name) { return o2Saves.HasSection(name); }));

        api.SetProperty("Get", Function<ScriptValue(const String&)>([](const String& name)
        {
            const DataValue& data = o2Saves.GetSectionData(name);
            if (data.IsNull())
                return ScriptValue();

            ScriptValue res;
            data.Get(res);
            return res;
        }));

        api.SetProperty("Set", Function<void(const String&, const ScriptValue&)>([](const String& name, const ScriptValue& value)
        {
            DataDocument data;
            data.Set(value);
            o2Saves.SetSectionData(name, data);
        }));

        api.SetProperty("Remove", Function<void(const String&)>([](const String& name) { o2Saves.RemoveSection(name); }));
        api.SetProperty("Save", Function<void()>([]() { o2Saves.Save(); }));
        api.SetProperty("Reset", Function<void()>([]() { o2Saves.Reset(); }));

        api.SetProperty("Sync", Function<void(const Function<void(bool)>&)>([](const Function<void(bool)>& onCompleted)
        {
            Async([](Function<void(bool)> onCompleted) -> Coroutine<void>
            {
                bool success = co_await o2Saves.Sync();
                if (onCompleted)
                    onCompleted(success);
            }(onCompleted), JobThread::Main);
        }));

        api.SetProperty("OnReplaced", Function<void(const Function<void()>&)>([](const Function<void()>& listener)
        {
            auto previous = o2Saves.onReplaced;
            o2Saves.onReplaced = [previous, listener]()
            {
                if (previous)
                    previous();

                listener();
            };
        }));

        global.GetProperty("o2libs").SetProperty("Saves", api);
#endif
    }
}
