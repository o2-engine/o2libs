#pragma once

#include "o2/Utils/Singleton.h"
#include "o2/Utils/Types/String.h"

using namespace o2;

// Player identity access macros
#define o2PlayerIdentity o2libs::PlayerIdentity::Instance()

namespace o2libs
{
    // -----------------------------------------------------------------------------------------
    // Player identity: who is playing, as far as the services are concerned. A random id made on
    // the first run and kept in the storage; a game with its own accounts replaces it
    // -----------------------------------------------------------------------------------------
    class PlayerIdentity: public Singleton<PlayerIdentity>
    {
    public:
        // Default constructor
        explicit PlayerIdentity(RefCounter* refCounter);

        // Destructor
        ~PlayerIdentity();

        // Returns the player id; creates and stores one on the first call
        const String& GetId();

        // Replaces the player id, e.g. with the id of the game's own account
        void SetId(const String& id);

        // Returns true when the id was created in this run, i.e. this is a new install
        bool IsNew();

        // Forgets the cached id, the next GetId reads the storage again
        void Reset();

    protected:
        String mId;            // Cached player id
        bool   mIsNew = false; // Is the id created in this run
    };
}
