#include <shlobj.h>
#include <unordered_map>
#include <vector>

#include "xbyak/xbyak.h"
#include "f4se_common/SafeWrite.h"
#include "f4se_common/BranchTrampoline.h"
#include "f4se/PluginAPI.h"
#include "f4se/GameData.h"

#include "rva/RVA.h"
#include "Config.h"
#include "Globals.h"
#include "ExtraTypes.h"
using namespace Ex;

IDebugLog gLog;
PluginHandle g_pluginHandle = kPluginHandle_Invalid;
F4SEMessagingInterface *g_messaging = nullptr;

std::unordered_map<BGSKeyword*, std::vector<Ex::BGSConstructibleObject*>> g_cobjMap;

//--------------------
// Addresses [6]
//--------------------
using _GetKeywordByIndex          = BGSKeyword*(*)(UInt32 unk1, UInt16 index);
using _DoesCOBJSatisfyConstraints = bool(*)(Ex::BGSConstructibleObject*, BGSKeyword*);
using _TryAddLeafNode             = void(*)(void* unk1, Ex::BGSConstructibleObject*);

RVA <_GetKeywordByIndex>          GetKeywordByIndex                  ({{ RUNTIME_VERSION_1_10_114, 0x0568F50 }}, "8D 41 FF 83 F8 11 77 2C");
RVA <_DoesCOBJSatisfyConstraints> DoesCOBJSatisfyConstraints         ({{ RUNTIME_VERSION_1_10_114, 0x01F5990 }}, "E8 ? ? ? ? 84 C0 74 6E 48 8B 43 08 C6 00 01", 0, 1, 5);
RVA <_TryAddLeafNode>             TryAddLeafNode                     ({{ RUNTIME_VERSION_1_10_114, 0x0211B00 }}, "40 53 48 83 EC 50 48 8B 01 48 8B D9 48 8B 08");
RVA <uintptr_t>                   KeywordLeaf_HookTarget             ({{ RUNTIME_VERSION_1_10_114, 0x02119DF }}, "48 3B DF 74 1E 48 8B 13 48 85 D2 74 0D 48 8D 4D D8");
RVA <uintptr_t>                   FormListHasEligibleCOBJ_HookTarget ({{ RUNTIME_VERSION_1_10_114, 0x01F8A60 }}, "E8 ? ? ? ? 44 0F B6 44 24 ? 45 84 C0 75 09");
RVA <uintptr_t>                   IconLoadLag_Target                 ({{ RUNTIME_VERSION_1_10_114, 0x0BEF865 }}, "41 B4 01 41 8B DE 4C 8D BE 34 03 00 00");

//-----------------------
// F4SE Event Handling
//-----------------------
void OnF4SEMessage(F4SEMessagingInterface::Message* msg) {
    switch (msg->type) {
        case F4SEMessagingInterface::kMessage_GameDataReady:
            _MESSAGE("Clearing COBJ map.");
            g_cobjMap.clear();
            break;
    }
}

//-----------------------
// Utils
//-----------------------
void TryBuildMap() {
    if (g_cobjMap.empty()) {
        _MESSAGE("Building COBJ map...");

        for (int i = 0; i < (*G::dataHandler)->arrCOBJ.count; i++) {
            auto cobj = (Ex::BGSConstructibleObject*)(*G::dataHandler)->arrCOBJ[i];
            for (int j = 0; j < cobj->keywordCount; j++) {
                BGSKeyword* cobjKeyword = GetKeywordByIndex(9, cobj->keywords[j]);
                g_cobjMap[cobjKeyword].push_back(cobj);
            }
        }

        _MESSAGE("Map built.");
    }
}

//-----------------------
// Hooks
//-----------------------
void Handler_KeywordLeaf(void* unk1, BGSKeyword* keyword) {
    TryBuildMap();
    if (g_cobjMap.find(keyword) != g_cobjMap.end()) {
        for (auto cobj : g_cobjMap[keyword]) {
            TryAddLeafNode(unk1, cobj);
        }
    }
}

void Handler_DoesKeywordHaveCOBJSatisfyingConstraints(DataHandler* dataHandler, UInt32 formType, EvaluateCOBJConditionsStruct* searchStruct) {
    TryBuildMap();
    BGSKeyword* keyword = *(searchStruct->keyword);
    if (keyword) {
        if (g_cobjMap.find(keyword) != g_cobjMap.end()) {
            for (auto cobj : g_cobjMap[keyword]) {
                if (DoesCOBJSatisfyConstraints(cobj, keyword)) {
                    *(searchStruct->found) = 1;
                    return;
                }
            }
        }
    }
    else {
        _MESSAGE("WARNING: Keyword was null!");
    }
}

//-----------------------
// Plugin Init
//-----------------------
bool InitPlugin(UInt32 runtimeVersion = 0) {
    _MESSAGE("%s load", PLUGIN_NAME_SHORT);
    _MESSAGE("runtime version: %08X", runtimeVersion);
    G::Init();
    RVAManager::UpdateAddresses(runtimeVersion);

    if (!g_localTrampoline.Create(1024 * 64, nullptr)) {
        _ERROR("couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
        return false;
    }

    if (!g_branchTrampoline.Create(1024 * 64)) {
        _ERROR("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
        return false;
    }

    _MESSAGE("Base address: %p", reinterpret_cast<uintptr_t>(GetModuleHandle(NULL)));

    // 1: Hook at COBJ loop for leaf nodes.
    {
        // On entry: BGSKeyword** at rbp-0x28, Current COBJ* in rbx, last COBJ* in rdi.
        struct Code : Xbyak::CodeGenerator {
            Code(void * buf) : Xbyak::CodeGenerator(4096, buf)
            {
                Xbyak::Label continueLabel, handler;
                lea(rcx, ptr[rbp - 0x28]); // load the address of the argument we need to pass on into rcx
                mov(rdx, ptr[rbp - 0x28]); // dereference #1: BGSKeyword** in rdx
                mov(rdx, ptr[rdx]);        // dereference #2: BGSKeyword* in rdx
                call(ptr[rip + handler]);
                jmp(ptr[rip + continueLabel]); // jump to end of loop

                L(continueLabel);
                dq(KeywordLeaf_HookTarget.GetUIntPtr() + 0x23); // end of loop

                L(handler);
                dq((uintptr_t)&Handler_KeywordLeaf);
            }
        };

        void* codeBuf = g_localTrampoline.StartAlloc();
        Code code(codeBuf);
        g_localTrampoline.EndAlloc(code.getCurr());
        g_branchTrampoline.Write5Branch(KeywordLeaf_HookTarget.GetUIntPtr(), (uintptr_t)codeBuf);
        _MESSAGE("Wrote branch %p", KeywordLeaf_HookTarget.GetUIntPtr());
    }

    // 2: Hook at FormList loop for non-leaf nodes.
    g_branchTrampoline.Write5Call(FormListHasEligibleCOBJ_HookTarget.GetUIntPtr(), (uintptr_t)Handler_DoesKeywordHaveCOBJSatisfyingConstraints);
    _MESSAGE("Wrote call %p", FormListHasEligibleCOBJ_HookTarget.GetUIntPtr());

    // 3: Fix icon load delay by ensuring vertical animation does not count towards preventing icon load
    unsigned char data[] = { 0x90, 0x90, 0x90 };
    SafeWriteBuf(IconLoadLag_Target.GetUIntPtr(), data, sizeof(data));
    _MESSAGE("Patched %p", IconLoadLag_Target.GetUIntPtr());

    return true;
}

extern "C"
{
//-----------------------
// F4SE Init
//-----------------------
void InitLog() {
    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "\\My Games\\Fallout4\\F4SE\\%s.log", PLUGIN_NAME_SHORT);
    gLog.OpenRelative(CSIDL_MYDOCUMENTS, logPath);
}

bool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info)
{
    InitLog();
    _MESSAGE("%s v%s", PLUGIN_NAME_SHORT, PLUGIN_VERSION_STRING);
    _MESSAGE("%s query", PLUGIN_NAME_SHORT);

    // populate info structure
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name    = PLUGIN_NAME_SHORT;
    info->version = PLUGIN_VERSION;

    // store plugin handle so we can identify ourselves later
    g_pluginHandle = f4se->GetPluginHandle();

    // Check game version
	if (!COMPATIBLE(f4se->runtimeVersion)) {
		char str[512];
		sprintf_s(str, sizeof(str), "Your game version: v%d.%d.%d.%d\nExpected version: v%d.%d.%d.%d\n%s will be disabled.",
			GET_EXE_VERSION_MAJOR(f4se->runtimeVersion),
			GET_EXE_VERSION_MINOR(f4se->runtimeVersion),
			GET_EXE_VERSION_BUILD(f4se->runtimeVersion),
			GET_EXE_VERSION_SUB(f4se->runtimeVersion),
			GET_EXE_VERSION_MAJOR(SUPPORTED_RUNTIME_VERSION),
			GET_EXE_VERSION_MINOR(SUPPORTED_RUNTIME_VERSION),
			GET_EXE_VERSION_BUILD(SUPPORTED_RUNTIME_VERSION),
			GET_EXE_VERSION_SUB(SUPPORTED_RUNTIME_VERSION),
			PLUGIN_NAME_LONG
		);

		MessageBox(NULL, str, PLUGIN_NAME_LONG, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

    if (f4se->runtimeVersion > SUPPORTED_RUNTIME_VERSION) {
        _MESSAGE("INFO: Newer game version (%08X) than target (%08X).", f4se->runtimeVersion, SUPPORTED_RUNTIME_VERSION);
    }

    // Get the messaging interface
    g_messaging = (F4SEMessagingInterface *)f4se->QueryInterface(kInterface_Messaging);
    if (!g_messaging) {
        _MESSAGE("couldn't get messaging interface");
        return false;
    }

	return true;
}

bool F4SEPlugin_Load(const F4SEInterface *f4se)
{
    g_messaging->RegisterListener(g_pluginHandle, "F4SE", OnF4SEMessage);
    return InitPlugin(f4se->runtimeVersion);
}

};
