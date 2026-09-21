#pragma once

#include "o2/Utils/Serialization/Serializable.h"
#include "o2/Utils/Types/String.h"

using namespace o2;

namespace o2libs
{
    // -------------------------------------------------------------------------------------------
    // How the game reaches the project's services: the asset LiveOps.json, written by the portal's
    // Live-ops tab. Read at run time, so a script-only game on a stock runtime is configured too
    // -------------------------------------------------------------------------------------------
    class ServiceSettings: public ISerializable
    {
    public:
        String url;        // The service address, e.g. https://host/liveops @SERIALIZABLE
        String key;        // The project's client key, o2c_... @SERIALIZABLE
        String appVersion; // The game version sent for targeting, e.g. 1.2.0 @SERIALIZABLE
        int    build = 0;  // The game build number @SERIALIZABLE

    public:
        // Returns true when there is an address and a key
        bool IsValid() const;

        // Loads the settings from the built assets; returns false when there is no such asset
        bool Load(const String& assetPath = defaultAssetPath);

        // Returns the settings loaded from the default asset once
        static const ServiceSettings& GetDefault();

        static const char* defaultAssetPath;

        SERIALIZABLE(ServiceSettings);
    };
}
// --- META ---

CLASS_BASES_META(o2libs::ServiceSettings)
{
    BASE_CLASS(o2::ISerializable);
}
END_META;
CLASS_FIELDS_META(o2libs::ServiceSettings)
{
    FIELD().PUBLIC().SERIALIZABLE_ATTRIBUTE().NAME(url);
    FIELD().PUBLIC().SERIALIZABLE_ATTRIBUTE().NAME(key);
    FIELD().PUBLIC().SERIALIZABLE_ATTRIBUTE().NAME(appVersion);
    FIELD().PUBLIC().SERIALIZABLE_ATTRIBUTE().DEFAULT_VALUE(0).NAME(build);
}
END_META;
CLASS_METHODS_META(o2libs::ServiceSettings)
{

    FUNCTION().PUBLIC().SIGNATURE(bool, IsValid);
    FUNCTION().PUBLIC().SIGNATURE(bool, Load, const String&);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(const ServiceSettings&, GetDefault);
}
END_META;
// --- END META ---
