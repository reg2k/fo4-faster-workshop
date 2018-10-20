#pragma once
#include "f4se/GameForms.h"
#include "f4se/NiTypes.h"
#include "f4se/GameTypes.h"
#include "f4se/GameInput.h"
#include "f4se/GameCamera.h"

namespace Ex {
    // 88
    // Modified from existing definition
    class BGSConstructibleObject : public TESForm
    {
    public:
        enum { kTypeID = kFormType_COBJ };

        BGSPickupPutdownSounds	pickupPutdownSounds;	// 20
        TESDescription			description;			// 38

        struct Component
        {
            BGSComponent	* component;	// 00
            UInt32			count;			// 08
        };

        tArray<Component>	* components;		// 50
        Condition			* conditions;		// 58
        TESForm             * createdObject;	// 60
        BGSKeyword			* workbenchKeyword;	// 68
        UInt16				createdCount;		// 70
        UInt16				priority;			// 72
        UInt32				unk74;				// 74
        UInt16				* keywords;		    // 78 - NEW
        UInt32				keywordCount;       // 80 - NEW
        UInt32				unk80;				// 80
    };
    STATIC_ASSERT(offsetof(BGSConstructibleObject, createdObject) == 0x60);
    STATIC_ASSERT(offsetof(BGSConstructibleObject, workbenchKeyword) == 0x68);
    STATIC_ASSERT(sizeof(BGSConstructibleObject) == 0x88);

    struct EvaluateCOBJConditionsStruct {
        BGSKeyword** keyword; // 00
        bool*        found;   // 08
        void*        unk10;   // 10 - WorkshopMenuItem?
        void*        unk18;   // 18
    };
    STATIC_ASSERT(offsetof(EvaluateCOBJConditionsStruct, found) == 0x8);
}