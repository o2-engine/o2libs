#pragma once

#include "o2/Utils/Function/Function.h"
#include "o2/Utils/Serialization/DataValue.h"
#include "o2/Utils/Serialization/Serializable.h"
#include "o2/Utils/Types/Containers/Map.h"
#include "o2/Utils/Types/Containers/Vector.h"
#include "o2/Utils/Types/Ref.h"
#include "o2/Utils/Types/String.h"
#include "o2libs/Core/ServiceSettings.h"
#include "o2libs/Core/Storage.h"
#include "o2libs/RemoteConfig/RemoteConfigTransport.h"

#include <memory>

using namespace o2;

namespace o2libs
{
    // -------------------------------------------------------------------------------------------
    // The remote config client. Asks the live-ops service which configs this player gets - the
    // answer is small: ids only, plus the player's A/B groups and the switches scheduled for the
    // next days - then downloads the documents it does not have yet from the CDN by id. A config
    // is a chain of ids: the base document and the merge patches of the player's A/B groups, laid
    // over it in order. Documents are immutable, so everything is cached by id and a player who is
    // offline runs on the last answer, scheduled switches included.
    //
    // The protocol is described in Modules/RemoteConfig/PROTOCOL.md
    // -------------------------------------------------------------------------------------------
    class RemoteConfigClient: public RefCounterable
    {
    public:
        Function<void()> onChanged; // The set of configs or their content has changed

        float exposureFlushPeriod = 15.0f; // Seconds between sending the collected exposures
        float retryPeriod = 30.0f;         // Seconds before asking again after a failure

    public:
        // Constructor. Null arguments mean the defaults: o2Network, o2libs::Storage, system clock
        RemoteConfigClient(RefCounter* refCounter,
                           const Ref<IRemoteConfigTransport>& transport = nullptr,
                           const Ref<IStorageBackend>& storage = nullptr,
                           const Function<double()>& clock = Function<double()>());

        // Destructor
        ~RemoteConfigClient();

        // Sets the service and brings the last stored answer up, so the configs of the previous
        // run are there at once. Does not touch the network
        void Init(const ServiceSettings& settings);

        // Returns true after Init with valid settings
        bool IsInitialized() const;

        // Returns true when configs are in place - stored or fresh
        bool IsReady() const;

        // Asks the service and downloads what is missing. The callback gets false when the
        // service or the CDN could not be reached; the configs in place stay as they were
        void Fetch(const Function<void(bool)>& onCompleted = Function<void(bool)>());

        // Returns true while a fetch is running
        bool IsFetching() const;

        // Fetches when the answer's time to live has passed, applies the scheduled switches that
        // came due, sends the exposures. Call every frame
        void Update(float dt);

        // Returns true when the player has this config now
        bool HasConfig(const String& key) const;

        // Returns the config document, a null value when there is none
        const DataValue& GetConfig(const String& key);

        // Returns the value inside the config by a dotted path ("rewards.0.coins"), null when absent
        const DataValue& GetValue(const String& key, const String& path);

        // Returns the typed value by the path, or the default one
        template<typename _type>
        _type Get(const String& key, const String& path, const _type& defaultValue);

        // Reads the whole config into a serializable object; false when there is no such config
        bool GetObject(const String& key, ISerializable& object);

        // Returns the keys of the configs in place
        Vector<String> GetConfigKeys() const;

        // Returns the player's group in the experiment, empty when the player is not in it
        String GetGroup(const String& experiment);

        // Returns the experiments the player is in: experiment -> group
        const Map<String, String>& GetExperiments() const;

        // Sets a player attribute for targeting (level, payer, country...). Sent with the next fetch
        void SetAttribute(const String& name, const String& value);
        void SetAttribute(const String& name, double value);
        void SetAttribute(const String& name, bool value);

        // Removes all the stored answers and documents of this project
        void ClearCache();

        // Returns the service time, seconds since the epoch: the device clock corrected by the last answer
        double GetServerTime() const;

    private:
        using Chain = Vector<String>;

        Ref<IRemoteConfigTransport> mTransport;
        Ref<IStorageBackend>        mStorage;
        Function<double()>          mClock;

        ServiceSettings mSettings;
        String          mStoragePrefix;

        DataDocument mAttributes;

        DataDocument        mManifest;        // The last answer of the service
        bool                mHasManifest = false;
        Map<String, Chain>  mActiveChains;    // What is in effect now: config -> document ids
        Map<String, DataDocument> mConfigs;   // The merged documents
        Map<String, String> mExperiments;
        Map<String, Vector<String>> mExposureByConfig;

        double mClockOffset = 0;   // Service time minus device time
        double mNextSwitchAt = 0;  // The next scheduled switch, 0 when none
        double mNextFetchAt = 0;   // When the answer goes stale

        bool mFetching = false;
        Vector<Function<void(bool)>> mFetchCallbacks;

        int  mPendingDocuments = 0;
        bool mDocumentsFailed = false;
        DataDocument mIncomingManifest;

        Vector<String> mExposed;          // "experiment:group" already reported, kept in the storage
        DataDocument   mPendingExposures; // Not sent yet, kept in the storage
        float          mExposureTimer = 0;
        bool           mSendingExposures = false;

        std::shared_ptr<bool> mAlive; // Callbacks of requests in flight look at it

        static DataDocument mNullValue;

    private:
        String StorageKey(const String& name) const;
        bool   ReadDocument(const String& id, String& text) const;

        void OnManifestAnswer(bool ok, const String& body);
        void DownloadMissing();
        void OnDocumentsReady();
        void FinishFetch(bool success);

        static void CollectIds(const DataValue& manifest, Vector<String>& ids);

        // Works out what is in effect at this moment and rebuilds the documents that changed
        void Rebuild();
        void Prune();

        void NoteExposure(const String& experiment);
        void FlushExposures();
    };

    template<typename _type>
    _type RemoteConfigClient::Get(const String& key, const String& path, const _type& defaultValue)
    {
        const DataValue& value = GetValue(key, path);
        if (value.IsNull() || value.IsObject() || value.IsArray())
            return defaultValue;

        _type res = defaultValue;
        value.Get(res);
        return res;
    }
}
