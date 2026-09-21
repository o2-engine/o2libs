#pragma once

#include "o2/Utils/Basic/IObject.h"
#include "o2/Utils/Types/Containers/Map.h"
#include "o2/Utils/Types/Containers/Vector.h"
#include "o2/Utils/Types/Ref.h"
#include "o2/Utils/Types/String.h"

using namespace o2;

namespace o2libs
{
    // ---------------------------------------------------------------------------------------
    // Where the storage keeps its values. Keys are short ascii names like "rc.manifest", values
    // are arbitrary strings
    // ---------------------------------------------------------------------------------------
    class IStorageBackend: public RefCounterable
    {
    public:
        // Reads the value; returns false when there is no such key
        virtual bool Read(const String& key, String& value) const = 0;

        // Writes the value
        virtual void Write(const String& key, const String& value) = 0;

        // Removes the key
        virtual void Remove(const String& key) = 0;

        // Returns the keys starting with the prefix
        virtual Vector<String> GetKeys(const String& prefix) const = 0;
    };

    // ------------------------------------------------------------
    // Keeps values in memory: tests, and platforms with no storage
    // ------------------------------------------------------------
    class MemoryStorageBackend: public IStorageBackend
    {
    public:
        explicit MemoryStorageBackend(RefCounter* refCounter);

        bool Read(const String& key, String& value) const override;
        void Write(const String& key, const String& value) override;
        void Remove(const String& key) override;
        Vector<String> GetKeys(const String& prefix) const override;

    private:
        Map<String, String> mValues;
    };

    // ------------------------------
    // A folder with one file per key
    // ------------------------------
    class FileStorageBackend: public IStorageBackend
    {
    public:
        FileStorageBackend(RefCounter* refCounter, const String& folder);

        bool Read(const String& key, String& value) const override;
        void Write(const String& key, const String& value) override;
        void Remove(const String& key) override;
        Vector<String> GetKeys(const String& prefix) const override;

        // Returns the folder the values are kept in
        const String& GetFolder() const;

    private:
        String mFolder;

    private:
        String PathOf(const String& key) const;
    };

#if defined(PLATFORM_WASM)
    // -----------------------------------------------------------------------
    // The browser's localStorage: the only thing in a web build that survives
    // a page reload
    // -----------------------------------------------------------------------
    class LocalStorageBackend: public IStorageBackend
    {
    public:
        LocalStorageBackend(RefCounter* refCounter, const String& prefix);

        bool Read(const String& key, String& value) const override;
        void Write(const String& key, const String& value) override;
        void Remove(const String& key) override;
        Vector<String> GetKeys(const String& prefix) const override;

    private:
        String mPrefix;
    };
#endif

    // -----------------------------------------------------------------------------------------
    // Small persistent key-value storage shared by the o2libs modules, o2libs.Storage in scripts.
    // Files in the user's application data folder; localStorage in the browser
    // -----------------------------------------------------------------------------------------
    class Storage: public IObject
    {
    public:
        // Returns the value, or the default one when there is no such key @SCRIPTABLE
        static String Get(const String& key, const String& defaultValue = "");

        // Stores the value @SCRIPTABLE
        static void Set(const String& key, const String& value);

        // Returns true when the key is stored @SCRIPTABLE
        static bool Has(const String& key);

        // Removes the key @SCRIPTABLE
        static void Remove(const String& key);

        // Returns the stored keys starting with the prefix @SCRIPTABLE
        static Vector<String> GetKeys(const String& prefix);

        // Replaces the backend; null restores the platform default
        static void SetBackend(const Ref<IStorageBackend>& backend);

        // Returns the current backend, creating the platform default on first use
        static const Ref<IStorageBackend>& GetBackend();

        // Sets the name of the data folder used by the default backend; call before first use.
        // Default is the executable's name
        static void SetApplicationName(const String& name);

        // Returns the folder of the default file backend for this platform
        static String GetDefaultFolder();

        IOBJECT(Storage);

    private:
        static Ref<IStorageBackend> mBackend;
        static String               mApplicationName;
    };
}
// --- META ---

CLASS_BASES_META(o2libs::Storage)
{
    BASE_CLASS(o2::IObject);
}
END_META;
CLASS_FIELDS_META(o2libs::Storage)
{
}
END_META;
CLASS_METHODS_META(o2libs::Storage)
{

    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(String, Get, const String&, const String&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(void, Set, const String&, const String&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(bool, Has, const String&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(void, Remove, const String&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(Vector<String>, GetKeys, const String&);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(void, SetBackend, const Ref<IStorageBackend>&);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(const Ref<IStorageBackend>&, GetBackend);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(void, SetApplicationName, const String&);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(String, GetDefaultFolder);
}
END_META;
// --- END META ---
