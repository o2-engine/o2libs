#pragma once

#include "o2/Utils/Serialization/Serializable.h"
#include "o2/Utils/Types/Ref.h"

using namespace o2;

namespace o2libs
{
    // -------------------------------------------------------------------------------------------
    // Section of the player's save: one module of the game's progress - the wallet, the campaign,
    // the settings. A game derives its sections from it, marks the fields @SERIALIZABLE and gets
    // them by name from o2Saves. The save keeps sections apart, so a module of the game that is
    // not in this build leaves its section untouched
    // -------------------------------------------------------------------------------------------
    class SaveSection: public ISerializable, public RefCounterable
    {
    public:
        // Default constructor
        SaveSection();

        // Returns the version of the section's layout. Grows when stored data needs OnMigrate to be read
        virtual int GetVersion() const;

        // Called before reading data stored by an older version of the section: brings it to the current layout
        virtual void OnMigrate(int fromVersion, DataValue& data);

        // Returns the section to the state of a new player. By default copies a freshly created object of the same type
        virtual void ResetToDefaults();

        // Called after the section was read: from the local save, or from the copy the service sent instead of it
        virtual void OnLoaded();

        SERIALIZABLE(SaveSection);
    };
}
// --- META ---

CLASS_BASES_META(o2libs::SaveSection)
{
    BASE_CLASS(o2::ISerializable);
    BASE_CLASS(o2::RefCounterable);
}
END_META;
CLASS_FIELDS_META(o2libs::SaveSection)
{
}
END_META;
CLASS_METHODS_META(o2libs::SaveSection)
{

    FUNCTION().PUBLIC().CONSTRUCTOR();
    FUNCTION().PUBLIC().SIGNATURE(int, GetVersion);
    FUNCTION().PUBLIC().SIGNATURE(void, OnMigrate, int, DataValue&);
    FUNCTION().PUBLIC().SIGNATURE(void, ResetToDefaults);
    FUNCTION().PUBLIC().SIGNATURE(void, OnLoaded);
}
END_META;
// --- END META ---
