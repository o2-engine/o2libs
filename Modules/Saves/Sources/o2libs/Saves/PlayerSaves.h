#pragma once

#include "o2/Utils/Coroutines/Coroutines.h"
#include "o2/Utils/Function/Function.h"
#include "o2/Utils/Serialization/DataValue.h"
#include "o2/Utils/Singleton.h"
#include "o2/Utils/Types/Containers/Map.h"
#include "o2/Utils/Types/Ref.h"
#include "o2/Utils/Types/String.h"
#include "o2libs/Core/ServiceSettings.h"
#include "o2libs/Core/ServiceTransport.h"
#include "o2libs/Core/Storage.h"
#include "o2libs/Saves/SaveSection.h"

#include <memory>

using namespace o2;

// Player saves access macros
#define o2Saves o2libs::PlayerSaves::Instance()

namespace o2libs
{
    // ---------------------------------------------------------------------------------------------
    // Save of the player. Made of named sections, each a serializable object of the game, stored as
    // one JSON document in the writable folder of the application on every platform (localStorage in
    // the browser) under the player's identity. When the project is connected to the o2 live-ops
    // service the save is also kept there: the portal's Saves tab finds it by the player id, shows
    // and edits it, and an edited save replaces the local one on the next sync. Scripts reach it as
    // o2libs.Saves
    // ---------------------------------------------------------------------------------------------
    class PlayerSaves: public Singleton<PlayerSaves>
    {
    public:
        Function<void()> onReplaced; // Called when the save was replaced by the copy of the service; sections are re-read by then

        float saveDelay = 2.0f;   // Seconds between a change and writing the save locally
        float syncPeriod = 30.0f; // Seconds between a change and sending the save to the service

    public:
        // Constructor. Null arguments mean the defaults: o2Network, o2Storage, the system clock
        explicit PlayerSaves(RefCounter* refCounter,
                             const Ref<IServiceTransport>& transport = nullptr,
                             const Ref<IStorageBackend>& storage = nullptr,
                             const Function<double()>& clock = Function<double()>());

        // Destructor. Writes unsaved changes locally
        ~PlayerSaves();

        // Loads the local save. With valid settings the save is also synchronized with the service
        void Initialize(const ServiceSettings& settings);

        // Initializes from the asset LiveOps.json; without it the save is local only. Starts the first sync
        void InitializeFromAssets();

        // Returns true after Initialize
        bool IsInitialized() const;

        // Returns true when the save is synchronized with the service
        bool IsCloudEnabled() const;

        // Returns the section by name, typed; creates it on first use and reads the stored data into it
        template<typename _type>
        Ref<_type> GetSection(const String& name);

        // Returns true when the save has the section, read or not
        bool HasSection(const String& name) const;

        // Returns the stored data of the section, a null value when there is none. For scripts and tools
        const DataValue& GetSectionData(const String& name);

        // Replaces the data of the section; a typed section of that name is re-read
        void SetSectionData(const String& name, const DataValue& data);

        // Removes the section
        void RemoveSection(const String& name);

        // Marks the save as changed: it is written locally after saveDelay and sent after syncPeriod
        void MarkChanged();

        // Writes the save locally now
        void Save();

        // Sends the save to the service and takes the service's copy when that one wins. Completes with false
        // when the service is unreachable or the save is local only
        Coroutine<bool> Sync();

        // Removes the player's progress, locally; the next sync sends the empty save
        void Reset();

        // Updates timers. Driven by o2Tasks after Initialize; an application without tasks calls it each frame
        void Update(float dt);

        // Returns the whole save as JSON
        String GetSaveJson();

        // Returns the revision the service gave to the last synchronized save, 0 when never synchronized
        int GetRevision() const;

        // Returns true when there are changes the service has not got yet
        bool HasUnsyncedChanges() const;

    protected:
        // One section: the stored data and, once the game asked for it, the object that owns it
        struct Section
        {
            DataDocument     data;        // Stored data of the section
            int              version = 1; // Version of the layout the data was stored with
            Ref<SaveSection> object;      // Typed object, null until GetSection
        };

    protected:
        Ref<IServiceTransport> mTransport; // Transport to the service
        Ref<IStorageBackend>   mStorage;   // Where the save is kept locally
        Function<double()>     mClock;     // Device clock, seconds since the epoch

        ServiceSettings mSettings;            // Service address and project key
        String          mStorageKey;          // Key of the save in the storage
        bool            mInitialized = false; // Is the local save loaded

        Map<String, Section> mSections; // Sections by name

        int    mRevision = 0;     // Revision of the last synchronized save at the service
        double mUpdatedAt = 0;    // Time of the last change
        bool   mUnsaved = false;  // Are there changes not written locally
        bool   mUnsynced = false; // Are there changes not sent to the service

        float mSaveTimer = 0;   // Time since the first unsaved change
        float mSyncTimer = 0;   // Time since the first unsynced change
        bool  mSyncing = false; // Is a sync request in flight

        std::shared_ptr<bool> mAlive;            // False after destruction; coroutines check it after every await
        bool                  mUpdating = false; // Is the update task registered in o2Tasks

        static DataDocument mNullValue; // Returned for an absent section

    protected:
        // Reads the save document: sections, revision, time
        void ReadSave(const DataValue& save);

        // Writes the save document; typed sections are serialized first
        void WriteSave(DataValue& save, bool withState);

        // Reads the stored data into the typed object of the section, migrating it when needed
        void LoadSectionObject(Section& section);

        // Replaces the local save with the copy of the service
        void TakeServerSave(const DataValue& data, int revision);

        // Runs one sync. A coroutine may outlive the object: alive is looked at before anything of the object is
        Coroutine<bool> RunSync(std::shared_ptr<bool> alive);

        // Registers the update task in o2Tasks, once
        void StartUpdating();

        // Registers o2libs.Saves for scripts
        void RegisterScriptApi();
    };

    template<typename _type>
    Ref<_type> PlayerSaves::GetSection(const String& name)
    {
        Section& section = mSections[name];
        if (!section.object)
        {
            section.object = mmake<_type>();
            LoadSectionObject(section);
        }

        return DynamicCast<_type>(section.object);
    }
}
