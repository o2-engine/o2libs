#include "o2/stdafx.h"
#include "JsonMergePatch.h"

namespace o2libs
{
    void ApplyMergePatch(DataValue& target, const DataValue& patch)
    {
        if (!patch.IsObject())
        {
            target = patch;
            return;
        }

        if (!target.IsObject())
            target.SetObject();

        for (auto it = patch.BeginMember(); it != patch.EndMember(); ++it)
        {
            if (it->value.IsNull())
            {
                if (target.FindMember(it->name))
                    target.RemoveMember(it->name);

                continue;
            }

            DataValue* member = target.FindMember(it->name);
            if (!member)
            {
                DataValue name(it->name, target.GetDocument());
                member = &target.AddMember(name);
            }

            ApplyMergePatch(*member, it->value);
        }
    }
}
