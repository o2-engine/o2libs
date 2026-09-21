#pragma once

#include "o2/Utils/Basic/IObject.h"
#include "o2/Utils/Types/String.h"

using namespace o2;

namespace o2libs
{
    // -------------------------------------------------------------------------------------------
    // Who is playing, as far as the services are concerned: a random id made on the first run and
    // kept in the storage. A game with its own accounts replaces it. o2libs.PlayerIdentity in scripts
    // -------------------------------------------------------------------------------------------
    class PlayerIdentity: public IObject
    {
    public:
        // Returns the player id, creating and storing one on the first call @SCRIPTABLE
        static String GetId();

        // Replaces the player id, e.g. with the game's own account id @SCRIPTABLE
        static void SetId(const String& id);

        // Returns true when the id was created in this run: a new install @SCRIPTABLE
        static bool IsNew();

        // Forgets the cached id so that the next GetId reads the storage again
        static void Reset();

        IOBJECT(PlayerIdentity);

    private:
        static String mId;
        static bool   mIsNew;
    };
}
// --- META ---

CLASS_BASES_META(o2libs::PlayerIdentity)
{
    BASE_CLASS(o2::IObject);
}
END_META;
CLASS_FIELDS_META(o2libs::PlayerIdentity)
{
}
END_META;
CLASS_METHODS_META(o2libs::PlayerIdentity)
{

    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(String, GetId);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(void, SetId, const String&);
    FUNCTION().PUBLIC().SCRIPTABLE_ATTRIBUTE().SIGNATURE_STATIC(bool, IsNew);
    FUNCTION().PUBLIC().SIGNATURE_STATIC(void, Reset);
}
END_META;
// --- END META ---
