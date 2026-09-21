#include "o2/stdafx.h"
#include "PlayerIdentity.h"

#include "o2/Utils/Types/UID.h"
#include "o2libs/Core/Storage.h"

namespace o2libs
{
    String PlayerIdentity::mId;
    bool   PlayerIdentity::mIsNew = false;

    namespace
    {
        const char* storageKey = "core.playerId";
    }

    String PlayerIdentity::GetId()
    {
        if (!mId.IsEmpty())
            return mId;

        mId = Storage::Get(storageKey);
        if (mId.IsEmpty())
        {
            UID uid;
            uid.Randomize();
            mId = (String)uid.ToString();
            mIsNew = true;

            Storage::Set(storageKey, mId);
        }

        return mId;
    }

    void PlayerIdentity::SetId(const String& id)
    {
        mId = id;
        Storage::Set(storageKey, mId);
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
// --- META ---

DECLARE_CLASS(o2libs::PlayerIdentity, o2libs__PlayerIdentity);
// --- END META ---
