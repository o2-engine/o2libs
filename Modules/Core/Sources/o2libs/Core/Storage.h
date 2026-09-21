#pragma once

#include "o2/Utils/Singleton.h"
#include "o2/Utils/Types/Containers/Map.h"
#include "o2/Utils/Types/Containers/Vector.h"
#include "o2/Utils/Types/Ref.h"
#include "o2/Utils/Types/String.h"

using namespace o2;

// Persistent storage access macros
#define o2Storage o2libs::Storage::Instance()

namespace o2libs
{
    // ----------------------------------------------------------------------------------------
    // Storage backend interface. Keeps string values by short ascii keys like "rc.manifest"
    // ----------------------------------------------------------------------------------------
    class IStorageBackend: public RefCounterable
    {
    public:
        // Reads the value by key. Returns false when there is no such key
        virtual bool Read(const String& key, String& value) const = 0;

        // Writes the value by key
        virtual void Write(const String& key, const String& value) = 0;

        // Removes the key
        virtual void Remove(const String& key) = 0;

        // Returns the keys that start with the prefix
        virtual Vector<String> GetKeys(const String& prefix) const = 0;
    };

    // ------------------------------------------------------------------
    // Storage backend in memory. Used in tests and where nothing persists
    // ------------------------------------------------------------------
    class MemoryStorageBackend: public IStorageBackend
    {
    public:
        // Default constructor
        explicit MemoryStorageBackend(RefCounter* refCounter);

        // Reads the value by key. Returns false when there is no such key
        bool Read(const String& key, String& value) const override;

        // Writes the value by key
        void Write(const String& key, const String& value) override;

        // Removes the key
        void Remove(const String& key) override;

        // Returns the keys that start with the prefix
        Vector<String> GetKeys(const String& prefix) const override;

    protected:
        Map<String, String> mValues; // Stored values
    };

    // ------------------------------------------------------------------------------------
    // Storage backend in a folder: one file per key, written through a temporary file, so
    // that an interrupted write leaves the previous value
    // ------------------------------------------------------------------------------------
    class FileStorageBackend: public IStorageBackend
    {
    public:
        // Constructor with the folder to keep the values in
        FileStorageBackend(RefCounter* refCounter, const String& folder);

        // Reads the value by key. Returns false when there is no such key
        bool Read(const String& key, String& value) const override;

        // Writes the value by key
        void Write(const String& key, const String& value) override;

        // Removes the key
        void Remove(const String& key) override;

        // Returns the keys that start with the prefix
        Vector<String> GetKeys(const String& prefix) const override;

        // Returns the folder the values are kept in
        const String& GetFolder() const;

    protected:
        String mFolder; // Folder with value files

    protected:
        // Returns the file path of the key
        String GetKeyPath(const String& key) const;
    };

#if defined(PLATFORM_WASM)
    // ---------------------------------------------------------------------------------------
    // Storage backend in the browser's localStorage: the only thing in a web build that stays
    // after the page is reloaded
    // ---------------------------------------------------------------------------------------
    class LocalStorageBackend: public IStorageBackend
    {
    public:
        // Constructor with the prefix of the keys in localStorage
        LocalStorageBackend(RefCounter* refCounter, const String& prefix);

        // Reads the value by key. Returns false when there is no such key
        bool Read(const String& key, String& value) const override;

        // Writes the value by key
        void Write(const String& key, const String& value) override;

        // Removes the key
        void Remove(const String& key) override;

        // Returns the keys that start with the prefix
        Vector<String> GetKeys(const String& prefix) const override;

    protected:
        String mPrefix; // Prefix of the keys in localStorage
    };
#endif

    // -------------------------------------------------------------------------------------------
    // Persistent key-value storage of the o2libs modules. Keeps values in the writable folder of
    // the application on every platform, and in localStorage in the browser
    // -------------------------------------------------------------------------------------------
    class Storage: public Singleton<Storage>
    {
    public:
        // Default constructor. The platform backend is created on first use
        explicit Storage(RefCounter* refCounter);

        // Destructor
        ~Storage();

        // Returns the value by key, or the default one when there is no such key
        String Get(const String& key, const String& defaultValue = "") const;

        // Stores the value by key
        void Set(const String& key, const String& value);

        // Returns true when the key is stored
        bool Has(const String& key) const;

        // Removes the key
        void Remove(const String& key);

        // Returns the stored keys that start with the prefix
        Vector<String> GetKeys(const String& prefix) const;

        // Replaces the backend. Null restores the platform one
        void SetBackend(const Ref<IStorageBackend>& backend);

        // Returns the backend; creates the platform one on first use
        const Ref<IStorageBackend>& GetBackend() const;

        // Sets the application name: a part of the platform folder path. Call before first use
        void SetApplicationName(const String& name);

        // Returns the writable folder of the application on this platform, empty when there is none
        String GetWritableFolder() const;

    protected:
        mutable Ref<IStorageBackend> mBackend; // Current backend

        String mApplicationName = "Default"; // Application name in the platform folder path
    };
}
