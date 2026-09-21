#include "o2/stdafx.h"
#include "Storage.h"

#include "o2/Utils/FileSystem/FileSystem.h"

#include <cstdlib>

#if defined(PLATFORM_WASM)
#include <emscripten.h>
#endif

#if defined(PLATFORM_ANDROID)
#include "o2/Application/Application.h"
#endif

namespace o2libs
{
    Ref<IStorageBackend> Storage::mBackend;
    String               Storage::mApplicationName = "Default";

    MemoryStorageBackend::MemoryStorageBackend(RefCounter* refCounter)
    {
        SetRefCounter(refCounter);
    }

    bool MemoryStorageBackend::Read(const String& key, String& value) const
    {
        return mValues.TryGetValue(key, value);
    }

    void MemoryStorageBackend::Write(const String& key, const String& value)
    {
        mValues[key] = value;
    }

    void MemoryStorageBackend::Remove(const String& key)
    {
        mValues.Remove(key);
    }

    Vector<String> MemoryStorageBackend::GetKeys(const String& prefix) const
    {
        Vector<String> res;
        for (auto& kv : mValues)
        {
            if (kv.first.StartsWith(prefix))
                res.Add(kv.first);
        }

        return res;
    }

    namespace
    {
        const char* hexDigits = "0123456789ABCDEF";

        bool IsPlainChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-';
        }

        // Keys become file names: anything outside [A-Za-z0-9._-] is written as %XX
        String EscapeKey(const String& key)
        {
            std::string res;
            for (char c : key)
            {
                if (IsPlainChar(c))
                    res += c;
                else
                {
                    res += '%';
                    res += hexDigits[((unsigned char)c) >> 4];
                    res += hexDigits[((unsigned char)c) & 15];
                }
            }

            return String(res.c_str());
        }

        int HexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        }

        String UnescapeKey(const String& name)
        {
            std::string res;
            int length = name.Length();
            for (int i = 0; i < length; i++)
            {
                if (name[i] == '%' && i + 2 < length && HexValue(name[i + 1]) >= 0 && HexValue(name[i + 2]) >= 0)
                {
                    res += (char)(HexValue(name[i + 1])*16 + HexValue(name[i + 2]));
                    i += 2;
                }
                else
                    res += name[i];
            }

            return String(res.c_str());
        }

        const char* valueExtension = ".val";
    }

    FileStorageBackend::FileStorageBackend(RefCounter* refCounter, const String& folder):
        mFolder(folder)
    {
        SetRefCounter(refCounter);
    }

    String FileStorageBackend::PathOf(const String& key) const
    {
        return mFolder + "/" + EscapeKey(key) + valueExtension;
    }

    bool FileStorageBackend::Read(const String& key, String& value) const
    {
        String path = PathOf(key);
        if (!o2FileSystem.IsFileExist(path))
            return false;

        value = FileSystem::ReadFile(path);
        return true;
    }

    void FileStorageBackend::Write(const String& key, const String& value)
    {
        if (!o2FileSystem.IsFolderExist(mFolder))
            o2FileSystem.FolderCreate(mFolder);

        // Through a temporary file: a crash in the middle leaves the previous value
        String path = PathOf(key);
        String temp = path + ".tmp";
        FileSystem::WriteFile(temp, value);

        if (o2FileSystem.IsFileExist(path))
            o2FileSystem.FileDelete(path);

        o2FileSystem.FileMove(temp, path);
    }

    void FileStorageBackend::Remove(const String& key)
    {
        String path = PathOf(key);
        if (o2FileSystem.IsFileExist(path))
            o2FileSystem.FileDelete(path);
    }

    Vector<String> FileStorageBackend::GetKeys(const String& prefix) const
    {
        Vector<String> res;
        if (!o2FileSystem.IsFolderExist(mFolder))
            return res;

        for (auto& file : o2FileSystem.GetFolderInfo(mFolder).files)
        {
            String name = FileSystem::GetPathWithoutDirectories(file.path);
            if (!name.EndsWith(valueExtension))
                continue;

            String key = UnescapeKey(name.SubStr(0, name.Length() - (int)strlen(valueExtension)));
            if (key.StartsWith(prefix))
                res.Add(key);
        }

        return res;
    }

    const String& FileStorageBackend::GetFolder() const
    {
        return mFolder;
    }

#if defined(PLATFORM_WASM)
    // Returns a malloc'ed string or 0. localStorage throws in private windows and when site data
    // is blocked, hence the try blocks
    EM_JS(char*, o2libs_storage_get, (const char* key), {
        try {
            var v = localStorage.getItem(UTF8ToString(key));
            if (v === null) return 0;
            return stringToNewUTF8(v);
        } catch (e) { return 0; }
    });

    EM_JS(void, o2libs_storage_set, (const char* key, const char* value), {
        try { localStorage.setItem(UTF8ToString(key), UTF8ToString(value)); } catch (e) {}
    });

    EM_JS(void, o2libs_storage_remove, (const char* key), {
        try { localStorage.removeItem(UTF8ToString(key)); } catch (e) {}
    });

    // Keys starting with the prefix, joined with '\n'
    EM_JS(char*, o2libs_storage_keys, (const char* prefix), {
        var res = [];
        try {
            var p = UTF8ToString(prefix);
            for (var i = 0; i < localStorage.length; i++) {
                var k = localStorage.key(i);
                if (k !== null && k.indexOf(p) === 0) res.push(k);
            }
        } catch (e) {}
        return stringToNewUTF8(res.join('\n'));
    });

    LocalStorageBackend::LocalStorageBackend(RefCounter* refCounter, const String& prefix):
        mPrefix(prefix)
    {
        SetRefCounter(refCounter);
    }

    bool LocalStorageBackend::Read(const String& key, String& value) const
    {
        char* data = o2libs_storage_get((mPrefix + key).Data());
        if (!data)
            return false;

        value = String(data);
        free(data);
        return true;
    }

    void LocalStorageBackend::Write(const String& key, const String& value)
    {
        o2libs_storage_set((mPrefix + key).Data(), value.Data());
    }

    void LocalStorageBackend::Remove(const String& key)
    {
        o2libs_storage_remove((mPrefix + key).Data());
    }

    Vector<String> LocalStorageBackend::GetKeys(const String& prefix) const
    {
        Vector<String> res;

        char* data = o2libs_storage_keys((mPrefix + prefix).Data());
        if (!data)
            return res;

        String joined(data);
        free(data);

        if (joined.IsEmpty())
            return res;

        for (auto& key : joined.Split("\n"))
            res.Add(key.SubStr(mPrefix.Length()));

        return res;
    }
#endif

    String Storage::Get(const String& key, const String& defaultValue /*= ""*/)
    {
        String value;
        return GetBackend()->Read(key, value) ? value : defaultValue;
    }

    void Storage::Set(const String& key, const String& value)
    {
        GetBackend()->Write(key, value);
    }

    bool Storage::Has(const String& key)
    {
        String value;
        return GetBackend()->Read(key, value);
    }

    void Storage::Remove(const String& key)
    {
        GetBackend()->Remove(key);
    }

    Vector<String> Storage::GetKeys(const String& prefix)
    {
        return GetBackend()->GetKeys(prefix);
    }

    void Storage::SetBackend(const Ref<IStorageBackend>& backend)
    {
        mBackend = backend;
    }

    const Ref<IStorageBackend>& Storage::GetBackend()
    {
        if (!mBackend)
        {
#if defined(PLATFORM_WASM)
            mBackend = mmake<LocalStorageBackend>("o2libs/");
#else
            String folder = GetDefaultFolder();
            if (folder.IsEmpty())
                mBackend = mmake<MemoryStorageBackend>();
            else
                mBackend = mmake<FileStorageBackend>(folder);
#endif
        }

        return mBackend;
    }

    void Storage::SetApplicationName(const String& name)
    {
        mApplicationName = name;
    }

    String Storage::GetDefaultFolder()
    {
        auto env = [](const char* name) { const char* v = std::getenv(name); return String(v ? v : ""); };

        String base;

#if defined(PLATFORM_WASM)
        return String();
#elif defined(PLATFORM_ANDROID)
        base = o2Application.GetDataPath();
#elif defined(PLATFORM_WINDOWS)
        base = env("APPDATA");
#elif defined(PLATFORM_MAC) || defined(PLATFORM_IOS)
        base = env("HOME");
        if (!base.IsEmpty())
            base += "/Library/Application Support";
#else
        base = env("XDG_DATA_HOME");
        if (base.IsEmpty() && !env("HOME").IsEmpty())
            base = env("HOME") + "/.local/share";
#endif

        if (base.IsEmpty())
            return String();

        return base + "/o2libs/" + mApplicationName;
    }
}
// --- META ---

DECLARE_CLASS(o2libs::Storage, o2libs__Storage);
// --- END META ---
