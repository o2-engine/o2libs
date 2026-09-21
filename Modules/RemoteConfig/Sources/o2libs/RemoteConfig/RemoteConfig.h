#pragma once

#include "o2/Utils/Basic/IObject.h"
#include "o2libs/RemoteConfig/RemoteConfigClient.h"

#if IS_SCRIPTING_SUPPORTED
#include "o2/Scripts/ScriptValue.h"
#endif

using namespace o2;

// The game's remote config client
#define o2RemoteConfig (*o2libs::RemoteConfig::GetClient())

namespace o2libs
{
    // -------------------------------------------------------------------------------------------
    // The game's remote configs, o2libs.RemoteConfig in scripts. Starts by itself from the asset
    // LiveOps.json (o2libs::Initialize), shows the configs of the previous run at once and asks
    // the service for fresh ones:
    //
    //     var pass = o2libs.RemoteConfig.Get("season_pass");
    //     var price = o2libs.RemoteConfig.GetNumber("offers", "starter.price", 4.99);
    //     o2libs.RemoteConfig.OnChanged(function() { ... });
    // -------------------------------------------------------------------------------------------
    class RemoteConfig: public IObject
    {
    public:
        // Sets the service by hand instead of LiveOps.json, brings up the stored configs @SCRIPTABLE
        static void Init(const String& url, const String& key);

        // Reads LiveOps.json from the assets, brings up the stored configs and starts a fetch.
        // Returns false when the project has no LiveOps.json
        static bool InitFromAssets();

        // Asks the service for the fresh configs; the callback gets false when it is unreachable @SCRIPTABLE
        static void Fetch(const Function<void(bool)>& onCompleted);

        // Returns true when configs are in place - stored or fresh @SCRIPTABLE
        static bool IsReady();

        // Returns true when the player has this config now @SCRIPTABLE
        static bool Has(const String& key);

#if IS_SCRIPTING_SUPPORTED
        // Returns the config as an object, undefined when there is none @SCRIPTABLE
        static ScriptValue Get(const String& key);
#endif

        // Returns the string inside the config by a dotted path, or the default one @SCRIPTABLE
        static String GetString(const String& key, const String& path, const String& defaultValue);

        // Returns the number inside the config by a dotted path, or the default one @SCRIPTABLE
        static float GetNumber(const String& key, const String& path, float defaultValue);

        // Returns the flag inside the config by a dotted path, or the default one @SCRIPTABLE
        static bool GetBool(const String& key, const String& path, bool defaultValue);

        // Returns the player's group in the experiment, empty when the player is not in it @SCRIPTABLE
        static String GetGroup(const String& experiment);

        // Sets a player attribute for targeting; sent with the next fetch @SCRIPTABLE
        static void SetAttribute(const String& name, const String& value);

        // Sets a numeric player attribute for targeting @SCRIPTABLE
        static void SetNumberAttribute(const String& name, float value);

        // Calls the function every time the configs change @SCRIPTABLE
        static void OnChanged(const Function<void()>& listener);

        // Timers of the client. Runs by itself as an o2Tasks task; an application without tasks calls it
        static void Update(float dt);

        // Returns the game's client, creating it on first use
        static const Ref<RemoteConfigClient>& GetClient();

        // Replaces the game's client; null drops it together with the listeners
        static void SetClient(const Ref<RemoteConfigClient>& client);

        IOBJECT(RemoteConfig);

    private:
        static Ref<RemoteConfigClient>  mClient;
        static Vector<Function<void()>> mListeners;
        static bool                     mPumping;
    };
}
// --- META ---

CLASS_BASES_META(o2libs::RemoteConfig)
{
    BASE_CLASS(o2::IObject);
}
END_META;
CLASS_FIELDS_META(o2libs::RemoteConfig)
{
}
END_META;
CLASS_METHODS_META(o2libs::RemoteConfig)
{

    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(void, Init, const String&, const String&);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(bool, InitFromAssets);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(void, Fetch, const Function<void(bool)>&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(bool, IsReady);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(bool, Has, const String&);
#if  IS_SCRIPTING_SUPPORTED
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(ScriptValue, Get, const String&);
#endif
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(String, GetString, const String&, const String&, const String&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(float, GetNumber, const String&, const String&, float);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(bool, GetBool, const String&, const String&, bool);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(String, GetGroup, const String&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(void, SetAttribute, const String&, const String&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(void, SetNumberAttribute, const String&, float);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(void, OnChanged, const Function<void()>&);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(void, Update, float);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(const Ref<RemoteConfigClient>&, GetClient);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(void, SetClient, const Ref<RemoteConfigClient>&);
}
END_META;
// --- END META ---
