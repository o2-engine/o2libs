#include "o2/stdafx.h"
#include "o2/Utils/FileSystem/FileSystem.h"
#include "o2libs/Core/PlayerIdentity.h"
#include "o2libs/Core/Storage.h"
#include <gtest/gtest.h>

using namespace o2;
using namespace o2libs;

namespace
{
    String TempFolder(const char* name)
    {
        String folder = String("o2libs-test-") + name;
        if (o2FileSystem.IsFolderExist(folder))
            o2FileSystem.FolderRemove(folder);

        return folder;
    }

    struct BackendGuard
    {
        BackendGuard(const Ref<IStorageBackend>& backend) { o2Storage.SetBackend(backend); o2PlayerIdentity.Reset(); }
        ~BackendGuard() { o2Storage.SetBackend(nullptr); o2PlayerIdentity.Reset(); }
    };
}

TEST(Storage, MemoryBackendKeepsValues)
{
    BackendGuard guard(mmake<MemoryStorageBackend>());

    EXPECT_FALSE(o2Storage.Has("a"));
    EXPECT_EQ(o2Storage.Get("a", "fallback"), String("fallback"));

    o2Storage.Set("a", "1");
    o2Storage.Set("b.x", "2");
    o2Storage.Set("b.y", "3");

    EXPECT_TRUE(o2Storage.Has("a"));
    EXPECT_EQ(o2Storage.Get("a"), String("1"));
    EXPECT_EQ(o2Storage.GetKeys("b.").Count(), 2);

    o2Storage.Remove("b.x");
    EXPECT_EQ(o2Storage.GetKeys("b.").Count(), 1);
    EXPECT_FALSE(o2Storage.Has("b.x"));
}

TEST(Storage, FileBackendSurvivesReopen)
{
    String folder = TempFolder("reopen");

    {
        auto backend = mmake<FileStorageBackend>(folder);
        backend->Write("rc.manifest", "{\"a\":1}");
        backend->Write("rc.manifest", "{\"a\":2}");
    }

    auto backend = mmake<FileStorageBackend>(folder);
    String value;
    EXPECT_TRUE(backend->Read("rc.manifest", value));
    EXPECT_EQ(value, String("{\"a\":2}"));
    EXPECT_FALSE(backend->Read("missing", value));

    o2FileSystem.FolderRemove(folder);
}

TEST(Storage, FileBackendEscapesKeys)
{
    String folder = TempFolder("escape");
    auto backend = mmake<FileStorageBackend>(folder);

    String odd = "rc.o2c_key/../x:y";
    backend->Write(odd, "v");
    backend->Write("rc.plain", "w");

    String value;
    EXPECT_TRUE(backend->Read(odd, value));
    EXPECT_EQ(value, String("v"));

    auto keys = backend->GetKeys("rc.");
    EXPECT_EQ(keys.Count(), 2);
    EXPECT_TRUE(keys.Contains(odd));

    // Nothing escaped the folder
    EXPECT_EQ(o2FileSystem.GetFolderInfo(folder).files.Count(), 2);
    EXPECT_EQ(o2FileSystem.GetFolderInfo(folder).folders.Count(), 0);

    backend->Remove(odd);
    EXPECT_FALSE(backend->Read(odd, value));

    o2FileSystem.FolderRemove(folder);
}

TEST(PlayerIdentity, IsCreatedOnceAndKept)
{
    auto backend = mmake<MemoryStorageBackend>();
    BackendGuard guard(backend);

    String id = o2PlayerIdentity.GetId();
    EXPECT_FALSE(id.IsEmpty());
    EXPECT_TRUE(o2PlayerIdentity.IsNew());
    EXPECT_EQ(o2PlayerIdentity.GetId(), id);

    // The next run
    o2PlayerIdentity.Reset();
    EXPECT_EQ(o2PlayerIdentity.GetId(), id);
    EXPECT_FALSE(o2PlayerIdentity.IsNew());
}

TEST(PlayerIdentity, GameAccountReplacesIt)
{
    BackendGuard guard(mmake<MemoryStorageBackend>());

    o2PlayerIdentity.GetId();
    o2PlayerIdentity.SetId("account-42");

    o2PlayerIdentity.Reset();
    EXPECT_EQ(o2PlayerIdentity.GetId(), String("account-42"));
}

TEST(PlayerIdentity, TwoInstallsDiffer)
{
    String first, second;
    {
        BackendGuard guard(mmake<MemoryStorageBackend>());
        first = o2PlayerIdentity.GetId();
    }
    {
        BackendGuard guard(mmake<MemoryStorageBackend>());
        second = o2PlayerIdentity.GetId();
    }

    EXPECT_NE(first, second);
}
