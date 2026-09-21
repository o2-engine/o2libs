#include "o2/stdafx.h"
#include "ServiceSettings.h"

#include "o2/Assets/Assets.h"
#include "o2/Utils/FileSystem/FileSystem.h"

namespace o2libs
{
    const char* ServiceSettings::defaultAssetPath = "LiveOps.json";

    bool ServiceSettings::IsValid() const
    {
        return !url.IsEmpty() && !key.IsEmpty();
    }

    bool ServiceSettings::Load(const String& assetPath /*= defaultAssetPath*/)
    {
        String path = o2Assets.GetBuiltAssetsPath() + assetPath;
        if (!o2FileSystem.IsFileExist(path))
            return false;

        DataDocument doc;
        if (!doc.LoadFromFile(path))
            return false;

        Deserialize(doc);

        while (!url.IsEmpty() && url.EndsWith("/"))
            url = url.SubStr(0, url.Length() - 1);

        return true;
    }

    const ServiceSettings& ServiceSettings::GetDefault()
    {
        static ServiceSettings settings;
        static bool loaded = false;

        if (!loaded)
        {
            loaded = true;
            settings.Load();
        }

        return settings;
    }
}
// --- META ---

DECLARE_CLASS(o2libs::ServiceSettings, o2libs__ServiceSettings);
// --- END META ---
