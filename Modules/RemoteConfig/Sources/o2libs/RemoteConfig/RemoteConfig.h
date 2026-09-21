#pragma once

#include "o2/Utils/Coroutines/Coroutines.h"
#include "o2/Utils/Function/Function.h"
#include "o2/Utils/Serialization/DataValue.h"
#include "o2/Utils/Serialization/Serializable.h"
#include "o2/Utils/Singleton.h"
#include "o2/Utils/Types/Containers/Map.h"
#include "o2/Utils/Types/Containers/Vector.h"
#include "o2/Utils/Types/Ref.h"
#include "o2/Utils/Types/String.h"
#include "o2libs/Core/ServiceSettings.h"
#include "o2libs/Core/ServiceTransport.h"
#include "o2libs/Core/Storage.h"

#include <memory>

using namespace o2;

// Remote config access macros
#define o2RemoteConfig o2libs::RemoteConfig::Instance()

namespace o2libs
{
    // ---------------------------------------------------------------------------------------------
    // Remote configs of the game: configs by key, scheduled launches and A/B tests from the o2
    // live-ops service. Asks the service which configs this player gets - the answer is document ids
    // only, with the player's A/B groups and the switches scheduled for the next days - and downloads
    // the documents it lacks by id. A config is a chain of documents: the base one and the merge
    // patches of the player's groups over it. Documents are immutable, so everything is cached by id:
    // the next run starts from the last answer, an offline player gets a scheduled switch on time.
    // Scripts reach it as o2libs.RemoteConfig. The wire format is in Modules/RemoteConfig/PROTOCOL.md
    // ---------------------------------------------------------------------------------------------
    class RemoteConfig: public Singleton<RemoteConfig>
    {
    public:
        Function<void()> onChanged; // Called when the set of configs or their content has changed

        float exposureFlushPeriod = 15.0f; // Seconds between sending the collected exposures
        float retryPeriod = 30.0f;         // Seconds before asking again after a failed fetch

    public:
        // Constructor. Null arguments mean the defaults: o2Network, o2Storage, the system clock
        explicit RemoteConfig(RefCounter* refCounter,
                              const Ref<IServiceTransport>& transport = nullptr,
                              const Ref<IStorageBackend>& storage = nullptr,
                              const Function<double()>& clock = Function<double()>());

        // Destructor
        ~RemoteConfig();

        // Sets the service and brings up the last stored answer. Does not touch the network
        void Initialize(const ServiceSettings& settings);

        // Initializes from the asset LiveOps.json and starts a fetch. Returns false when the project has no such asset
        bool InitializeFromAssets();

        // Returns true after Initialize with valid settings
        bool IsInitialized() const;

        // Returns true when configs are in place, stored or fresh
        bool IsReady() const;

        // Asks the service and downloads what is missing; starts at once. Completes with false when the service
        // or the CDN is unreachable: the configs in place stay as they were. Calls made while a fetch runs share it
        Coroutine<bool> Fetch();

        // Returns true while a fetch is running
        bool IsFetching() const;

        // Updates timers: fetches when the answer went stale, applies the scheduled switches that came due,
        // sends the exposures. Driven by o2Tasks after Initialize; an application without tasks calls it each frame
        void Update(float dt);

        // Returns true when the player has this config now
        bool HasConfig(const String& key) const;

        // Returns the config document, a null value when there is none
        const DataValue& GetConfig(const String& key);

        // Returns the value inside the config by a dotted path ("rewards.0.coins"), a null value when absent
        const DataValue& GetValue(const String& key, const String& path);

        // Returns the typed value by the path, or the default one when it is absent or not a plain value
        template<typename _type>
        _type Get(const String& key, const String& path, const _type& defaultValue);

        // Reads the whole config into a serializable object. Returns false when there is no such config
        bool GetObject(const String& key, ISerializable& object);

        // Returns the keys of the configs in place
        Vector<String> GetConfigKeys() const;

        // Returns the player's group in the experiment, empty when the player is not in it. Counts as exposure
        String GetGroup(const String& experiment);

        // Returns the experiments the player is in: experiment id -> group id
        const Map<String, String>& GetExperiments() const;

        // Sets a text player attribute for targeting. Sent with the next fetch
        void SetAttribute(const String& name, const String& value);

        // Sets a numeric player attribute for targeting
        void SetAttribute(const String& name, double value);

        // Sets a flag player attribute for targeting
        void SetAttribute(const String& name, bool value);

        // Puts a document in place of the config over whatever the service says, until cleared.
        // For tests of game code, debug menus and cheats; not stored, not reported as exposure
        void SetLocalOverride(const String& key, const DataValue& config);

        // Removes the local override of the config; an empty key removes all of them
        void ClearLocalOverride(const String& key = String());

        // Returns the revision of the configs: grows with every change, a cheap way to follow them
        int GetRevision() const;

        // Removes the stored answers and documents of this project
        void ClearCache();

        // Returns the service time in seconds since the epoch: the device clock corrected by the last answer
        double GetServerTime() const;

    protected:
        typedef Vector<String> Chain;

        // State shared with the coroutines of one fetch: they outlive neither the answer nor the object
        struct FetchState
        {
            Signal done;            // Released when the fetch has finished
            bool   success = false; // Result of the fetch
        };

    protected:
        Ref<IServiceTransport> mTransport; // Transport to the service and the CDN
        Ref<IStorageBackend>   mStorage;   // Where answers, documents and exposures are kept
        Function<double()>     mClock;     // Device clock, seconds since the epoch

        ServiceSettings mSettings;      // Service address and project key
        String          mStoragePrefix; // Prefix of this project's keys in the storage

        DataDocument mAttributes; // Player attributes sent with a fetch

        DataDocument mManifest;            // Last answer of the service
        bool         mHasManifest = false; // Is there an answer in place

        Map<String, Chain>          mActiveChains;     // Chains in effect now: config -> document ids
        Map<String, DataDocument>   mConfigs;          // Merged config documents
        Map<String, DataDocument>   mOverrides;        // Local overrides, beat mConfigs
        Map<String, String>         mExperiments;      // Experiment id -> group id of the player
        Map<String, Vector<String>> mExposureByConfig; // Config -> experiments that shaped it

        int mRevision = 0; // Grows with every change of the configs

        double mClockOffset = 0;  // Service time minus device time
        double mNextSwitchAt = 0; // Time of the next scheduled switch, 0 when none
        double mNextFetchAt = 0;  // Time when the answer goes stale, 0 when never

        std::shared_ptr<FetchState> mFetch; // Running fetch, null when none

        Vector<String> mExposed;                  // "experiment:group" already reported, kept in the storage
        DataDocument   mPendingExposures;         // Exposures not sent yet, kept in the storage
        float          mExposureTimer = 0;        // Time since the last exposures flush
        bool           mSendingExposures = false; // Is an exposures request in flight

        std::shared_ptr<bool> mAlive;          // False after destruction; coroutines check it after every await
        bool                  mUpdating = false; // Is the update task registered in o2Tasks

        static DataDocument mNullValue; // Returned for an absent config or value

    protected:
        // Returns the storage key of this project by name
        String GetStorageKey(const String& name) const;

        // Reads a stored document by id. Returns false when it is not stored
        bool ReadDocument(const String& id, String& text) const;

        // Runs one fetch: the request, the downloads, the switch to the new answer. A coroutine starts later than it is
        // made and may outlive the object: alive comes as an argument and is looked at before anything of the object is
        Coroutine<void> RunFetch(std::shared_ptr<FetchState> state, std::shared_ptr<bool> alive);

        // Downloads the documents of the answer that are not stored yet. Completes with false when any failed
        Coroutine<bool> DownloadMissing(DataDocument manifest, std::shared_ptr<bool> alive);

        // Puts the downloaded answer in place
        void ApplyManifest(const DataDocument& manifest);

        // Collects the document ids of the answer, scheduled ones included
        static void CollectDocumentIds(const DataValue& manifest, Vector<String>& ids);

        // Works out what is in effect at this moment and rebuilds the documents that changed
        void Rebuild();

        // Removes stored documents the answer does not use
        void Prune();

        // Bumps the revision and calls onChanged
        void OnConfigsChanged();

        // Remembers that the player met the experiment, once per experiment and group
        void NoteExposure(const String& experiment);

        // Sends the collected exposures
        Coroutine<void> FlushExposures(std::shared_ptr<bool> alive);

        // Registers the update task in o2Tasks, once
        void StartUpdating();

        // Registers o2libs.RemoteConfig for scripts
        void RegisterScriptApi();
    };

    template<typename _type>
    _type RemoteConfig::Get(const String& key, const String& path, const _type& defaultValue)
    {
        const DataValue& value = GetValue(key, path);
        if (value.IsNull() || value.IsObject() || value.IsArray())
            return defaultValue;

        _type res = defaultValue;
        value.Get(res);
        return res;
    }
}
