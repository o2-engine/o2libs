#include "o2/stdafx.h"
#include "PlayerIdentity.h"

#include "o2/Utils/Types/UID.h"
#include "o2libs/Core/Storage.h"

namespace o2
{
    DECLARE_SINGLETON(o2libs::PlayerIdentity);
}

namespace o2libs
{
    namespace
    {
        const char* storageKey = "core.playerId";
    }

    PlayerIdentity::PlayerIdentity(RefCounter* refCounter):
        Singleton<PlayerIdentity>(refCounter)
    {}

    PlayerIdentity::~PlayerIdentity()
    {
        if (mInstance == this)
            mInstance = nullptr;
    }

    const String& PlayerIdentity::GetId()
    {
        if (!mId.IsEmpty())
            return mId;

        mId = o2Storage.Get(storageKey);
        if (mId.IsEmpty())
        {
            UID uid;
            uid.Randomize();

            mId = (String)uid.ToString();
            mIsNew = true;

            o2Storage.Set(storageKey, mId);
        }

        return mId;
    }

    void PlayerIdentity::SetId(const String& id)
    {
        mId = id;
        o2Storage.Set(storageKey, mId);
    }

    bool PlayerIdentity::IsNew()
    {
        GetId();
        return mIsNew;
    }

    void PlayerIdentity::Reset()
    {
        mId = String();
        mIsNew = false;
    }
}
