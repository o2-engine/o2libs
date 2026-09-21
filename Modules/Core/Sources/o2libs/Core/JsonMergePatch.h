#pragma once

#include "o2/Utils/Serialization/DataValue.h"

using namespace o2;

namespace o2libs
{
    // Applies a JSON merge patch (RFC 7386) to the target: objects merge member by member, a null
    // member removes, anything else - arrays included - replaces. The same algorithm runs in the
    // live-ops service; the shared test vectors are in Modules/RemoteConfig/Tests/merge-vectors.json
    void ApplyMergePatch(DataValue& target, const DataValue& patch);
}
