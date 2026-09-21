#include "o2/stdafx.h"
#include "SaveSection.h"

namespace o2libs
{
    SaveSection::SaveSection()
    {}

    int SaveSection::GetVersion() const
    {
        return 1;
    }

    void SaveSection::OnMigrate(int fromVersion, DataValue& data)
    {}

    void SaveSection::ResetToDefaults()
    {
        Ref<SaveSection> blank = DynamicCast<SaveSection>(GetType().CreateSampleRef());
        if (!blank)
            return;

        DataDocument defaults;
        blank->Serialize(defaults);
        Deserialize(defaults);
    }

    void SaveSection::OnLoaded()
    {}
}
// --- META ---

DECLARE_CLASS(o2libs::SaveSection, o2libs__SaveSection);
// --- END META ---
