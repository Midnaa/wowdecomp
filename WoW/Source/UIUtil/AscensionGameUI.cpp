#include "AscensionGameUI.h"
#include "AscensionAuraAmounts.h"
#include "AscensionDBC.h"
#include "AscensionSpellCharges.h"
#include "AscensionJSON.h"
#include <algorithm>
#include <cctype>
#include <set>
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameXML/FrameXML.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/Source/MD5.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/DBClient/DBCacheInstances.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/DB/Rec/SkillLineRec.h"
#include "WoW/Source/DB/Rec/SkillRaceClassInfoRec.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/Magic/MagicClient/SpellTable.h"
#include "WoW/Source/Object/ItemStats.h"
#include "WoW/Source/Object/Unit.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Item_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/PartyFrame.h"
#include "WoW/Source/Ui/RaidInfo.h"
#include "WoW/Source/Ui/ScriptEvents.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"

namespace {
std::set<std::string> s_libraries;
AscensionAuraAmounts s_auraAmounts;
AscensionDBC s_chargeLinks, s_chargeCategories;
AscensionSpellCharges s_charges;

bool LibraryName(const char* name, size_t length) {
    if (!length || length > 120) { return false; }
    for (size_t i = 0; i < length; ++i) {
        if (!std::isalnum(static_cast<unsigned char>(name[i])) && name[i] != '-' && name[i] != '_' && name[i] != '.') { return false; }
    }
    return std::strstr(name, "..") == nullptr;
}

int IsLibraryLoaded(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, s_libraries.count(name) != 0);
    return 1;
}

int LoadLibrary(lua_State* L) {
    size_t length;
    const char* name = luaL_checklstring(L, 1, &length);
    if (!LibraryName(name, length)) { return luaL_error(L, "Invalid UI library name"); }
    bool loaded = s_libraries.count(name) != 0;
    if (!loaded) {
        std::string path = "Interface\\LibraryXML\\" + std::string(name) + ".toc";
        if (SFile::FileExistsEx(path.c_str(), 1)) {
            // A library may request itself through LibStub while its TOC loads.
            s_libraries.insert(name);
            MD5_CTX digest;
            MD5Init(&digest);
            CStatus status;
            int top = lua_gettop(L);
            const char* savedTaint = lua_tainted;
            bool resetTaint = lua_taintexpected && !lua_taintedclosure;
            if (resetTaint) { lua_tainted = nullptr; }
            lua_newtable(L);
            loaded = FrameXML_CreateFrames(path.c_str(), name, &digest, &status) != 0;
            lua_settop(L, top);
            if (resetTaint) { lua_tainted = savedTaint; }
            if (!loaded) { s_libraries.erase(name); }
        }
    }
    lua_pushboolean(L, loaded);
    return 1;
}

int CanPerformAction(lua_State* L) {
    lua_Integer action = luaL_checkinteger(L, 1);
    lua_pushboolean(L, action >= 0 && action <= 26 && CGGameUI::CanPerformAction(static_cast<uint32_t>(action)));
    return 1;
}

int UnitCastingSpellID(lua_State* L) {
    auto* unit = GetUnitFromName(luaL_checkstring(L, 1));
    if (!unit) { return 0; }
    lua_pushnumber(L, unit->m_castingSpellId ? unit->m_castingSpellId : unit->m_channelSpellId);
    return 1;
}

int UnitIsLevelScaling(lua_State* L) {
    auto* unit = GetUnitFromName(luaL_checkstring(L, 1));
    if (!unit) { lua_pushnil(L); }
    else { lua_pushboolean(L, (unit->m_unit->m_unitDynamicFlags & 0x100) != 0); }
    return 1;
}

int UnitGroupRolesAssignedKey(lua_State* L) {
    WOWGUID guid = GetGuidByKeyword(luaL_checkstring(L, 1));
    const auto* member = CGRaidInfo::FindMemberByGuid(guid);
    uint32_t roles = member ? member->m_groupRole : CGPartyInfo::GetGroupRole(guid);
    lua_pushstring(L, roles & 2 ? "TANK" : roles & 4 ? "HEALER" : roles & 8 ? "DAMAGER" : "NONE");
    return 1;
}

int UnitAbsorbs(lua_State* L) {
    auto* unit = GetUnitFromName(luaL_checkstring(L, 1));
    int64_t total = 0;
    bool healing = lua_toboolean(L, lua_upvalueindex(1));
    if (unit) {
        auto found = s_auraAmounts.units.find(unit->m_obj->m_guid.m_guid);
        if (found != s_auraAmounts.units.end()) {
            for (const auto& entry : found->second) {
                SpellRec spell;
                if (!g_spellDB.GetRecord(entry.second.spell, &spell)) { continue; }
                for (int i = 0; i < 3; ++i) {
                    bool absorb = healing ? spell.m_effectAura[i] == 301
                        : spell.m_effectAura[i] == 69 || spell.m_effectAura[i] == 97;
                    if (absorb) { total += std::max(entry.second.amounts[i], 0); }
                }
            }
        }
    }
    lua_pushnumber(L, static_cast<lua_Number>(total));
    return 1;
}

int RegisterInterfaceEvent(lua_State* L) {
    size_t length;
    const char* name = luaL_checklstring(L, 1, &length);
    if (!length || length > 128) { return luaL_error(L, "Invalid interface event name"); }
    for (size_t i = 0; i < length; ++i) {
        if (!std::isalnum(static_cast<unsigned char>(name[i])) && name[i] != '_') { return luaL_error(L, "Invalid interface event name"); }
    }
    lua_pushboolean(L, FrameScript_RegisterInterfaceEvent(name) != UINT32_MAX);
    return 1;
}

int SignalEvent(lua_State* L) {
    RegisterInterfaceEvent(L);
    lua_pop(L, 1);
    uint32_t event = FrameScript_RegisterInterfaceEvent(lua_tostring(L, 1));
    if (event != UINT32_MAX) { FrameScript_SignalEvent(event, L, lua_gettop(L) - 1); }
    return 0;
}

int InviteRequest(lua_State* L) {
    size_t length;
    const char* name = luaL_checklstring(L, 1, &length);
    if (!length || length > 127 || std::memchr(name, 0, length)) { return luaL_error(L, "Invalid player name"); }
    if (!ClientServices::Connection()->m_authenticated) { lua_pushboolean(L, false); return 1; }
    CDataStore message;
    message.Put(static_cast<uint32_t>(lua_tointeger(L, lua_upvalueindex(1))));
    message.PutString(name);
    message.Finalize();
    ClientServices::Send2(&message);
    lua_pushboolean(L, true);
    return 1;
}

int SetCameraFoV(lua_State* L) {
    double fov = luaL_checknumber(L, 1);
    if (!std::isfinite(fov) || fov < 0.1 || fov > 3.1) { return luaL_error(L, "Invalid camera field of view"); }
    if (auto* camera = CGWorldFrame::GetActiveCamera()) { camera->m_fov = static_cast<float>(fov); }
    return 0;
}

int SetNamePlateCastBarMode(lua_State* L) {
    lua_Integer mode = luaL_checkinteger(L, 1);
    if (mode < 0 || mode > 2) { return luaL_error(L, "Invalid nameplate cast-bar mode"); }
    if (g_showVKeyCastbarCVar) { g_showVKeyCastbarCVar->Set(mode != 0, true, false, false, true); }
    return 0;
}

int GetItemQuality(lua_State* L) {
    int32_t id = static_cast<int32_t>(luaL_checkinteger(L, 1));
    auto* item = g_itemDBCache.GetRecord(id, WOWGUID(), nullptr, nullptr, false);
    if (!item) { return 0; }
    lua_pushnumber(L, item->m_quality);
    return 1;
}

int GetItemName(lua_State* L) {
    int32_t id = static_cast<int32_t>(luaL_checkinteger(L, 1));
    if (!g_itemDBCache.GetRecord(id, WOWGUID(), nullptr, nullptr, false)) { return 0; }
    char name[1024] = {};
    CGItem_C::BuildItemName(name, sizeof(name), id, 0);
    lua_pushstring(L, name);
    return 1;
}

int GetSkillInfo(lua_State* L) {
    int32_t id = static_cast<int32_t>(luaL_checkinteger(L, 1));
    const auto* skill = g_skillLineDB.GetRecord(id);
    if (!skill) { return 0; }
    auto* player = ClntObjMgrGetActivePlayerObj();
    int32_t index = player ? player->GetSkillIndex(id) : -1;
    const auto* requirements = player ? SpellTableLookupInfo(player->m_unit->m_unitFieldBytes0[0],
        player->m_unit->m_unitFieldBytes0[1], id) : nullptr;
    lua_pushstring(L, skill->m_displayName);
    lua_pushnumber(L, index >= 0 ? player->GetSkillValue(index) : 0);
    lua_pushnumber(L, index >= 0 ? player->GetSkillModifier(index) : 0);
    lua_pushnumber(L, index >= 0 ? player->GetSkillMaxRank(index) : 0);
    lua_pushboolean(L, index >= 0 && requirements && (requirements->m_flags & 0x20) && player->GetSkillStep(index));
    lua_pushstring(L, skill->m_description);
    return 6;
}

int HasRuneUI(lua_State* L) {
    auto* player = ClntObjMgrGetActivePlayerObj();
    lua_pushboolean(L, player && player->m_unit->m_unitFieldBytes0[1] == 6);
    return 1;
}

void PushJSON(lua_State* L, const AscensionJSONValue& value) {
    switch (value.type) {
        case AscensionJSONValue::Null: lua_pushnil(L); break;
        case AscensionJSONValue::Boolean: lua_pushboolean(L, value.number != 0); break;
        case AscensionJSONValue::Number: lua_pushnumber(L, value.number); break;
        case AscensionJSONValue::String: lua_pushlstring(L, value.string.data(), value.string.size()); break;
        case AscensionJSONValue::Array: {
            lua_newtable(L);
            int index = 1;
            for (const auto& child : value.array) { PushJSON(L, child); lua_rawseti(L, -2, index++); }
            break;
        }
        case AscensionJSONValue::Object:
            lua_newtable(L);
            for (const auto& child : value.object) {
                lua_pushlstring(L, child.first.data(), child.first.size());
                PushJSON(L, child.second);
                lua_rawset(L, -3);
            }
            break;
    }
}

int DecodeJSON(lua_State* L) {
    size_t length;
    const char* text = luaL_checklstring(L, 1, &length);
    if (!lua_checkstack(L, 256)) { return luaL_error(L, "Not enough Lua stack to decode JSON"); }
    AscensionJSON parser;
    AscensionJSONValue value;
    if (!parser.Parse(text, length, value)) {
        lua_pushnil(L);
        lua_pushstring(L, "Invalid JSON");
        return 2;
    }
    PushJSON(L, value);
    return 1;
}

int IsHelpfulSpell(lua_State* L) {
    lua_Integer id = luaL_checkinteger(L, lua_istable(L, 1) ? 2 : 1);
    SpellRec spell;
    lua_pushboolean(L, id > 0 && id <= INT32_MAX && g_spellDB.GetRecord(static_cast<int32_t>(id), &spell)
        && SpellRec::IsHelpfulOrHarmful(&spell) == 1);
    return 1;
}

int GetSpellCharges(lua_State* L) {
    uint32_t spell = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t category = 0;
    for (uint32_t row = 0; row < s_chargeLinks.Count(); ++row) {
        if (s_chargeLinks.Value(row, 0) == spell) { category = s_chargeLinks.Value(row, 1); break; }
    }
    for (uint32_t row = 0; category && row < s_chargeCategories.Count(); ++row) {
        if (s_chargeCategories.Value(row, 0) != category) { continue; }
        uint32_t maximum = s_chargeCategories.Value(row, 1), recovery = s_chargeCategories.Value(row, 2);
        if (!maximum || maximum > 20 || !recovery) { return 0; }
        uint32_t missing = 0;
        double start = 0;
        auto found = s_charges.entries.find(category);
        if (found != s_charges.entries.end()) {
            const auto& state = found->second;
            uint32_t restored = state.Restored(OsGetAsyncTimeMs(), recovery);
            missing = state.missing > restored ? state.missing - restored : 0;
            if (missing) {
                uint32_t next = state.remaining + restored * recovery;
                start = (static_cast<double>(state.received) + next - recovery) / 1000.0;
            }
        }
        lua_pushnumber(L, maximum - std::min(maximum, missing));
        lua_pushnumber(L, maximum);
        lua_pushnumber(L, start);
        lua_pushnumber(L, recovery / 1000.0);
        return 4;
    }
    return 0;
}

void LoadTable(AscensionDBC& table, const char* path, uint32_t fields) {
    void* bytes = nullptr;
    uint32_t size;
    table = {};
    if (SFile::ReadFile(nullptr, path, &bytes, &size, 0, 1, nullptr)) {
        table.Assign(bytes, size, fields);
        SFile::Unload(bytes);
    }
}
}

void AscensionGameUI_Register() {
    s_libraries.clear();
    LoadTable(s_chargeLinks, "DBFilesClient\\SpellCharges.dbc", 2);
    LoadTable(s_chargeCategories, "DBFilesClient\\SpellChargesCategory.dbc", 3);
    const luaL_Reg functions[] = {
        { "IsLibraryLoaded", IsLibraryLoaded }, { "LoadLibrary", LoadLibrary },
        { "CanPerformAction", CanPerformAction }, { "UnitCastingSpellID", UnitCastingSpellID },
        { "UnitIsLevelScaling", UnitIsLevelScaling }, { "UnitGroupRolesAssignedKey", UnitGroupRolesAssignedKey },
        { "RegisterInterfaceEvent", RegisterInterfaceEvent }, { "SignalEvent", SignalEvent },
        { "SetCameraFoV", SetCameraFoV }, { "GetItemQuality", GetItemQuality },
        { "SetNamePlateCastBarMode", SetNamePlateCastBarMode },
        { "GetItemName", GetItemName }, { "GetSkillInfo", GetSkillInfo },
        { "GetSpellCharges", GetSpellCharges }, { nullptr, nullptr }
    };
    for (const auto* entry = functions; entry->name; ++entry) { FrameScript_RegisterFunction(entry->name, entry->func); }
    FrameScript_RegisterFunction("DecodeJSON", DecodeJSON);
    lua_State* L = FrameScript_GetContext();
    for (const auto& entry : { std::make_pair("UnitGetTotalAbsorbs", false), std::make_pair("UnitGetTotalHealAbsorbs", true) }) {
        lua_pushboolean(L, entry.second);
        lua_pushcclosure(L, UnitAbsorbs, 1);
        lua_setglobal(L, entry.first);
    }
    for (const auto& entry : { std::make_pair("SuggestInvite", 0x0584), std::make_pair("RequestInvite", 0x0586) }) {
        lua_pushinteger(L, entry.second);
        lua_pushcclosure(L, InviteRequest, 1);
        lua_setglobal(L, entry.first);
    }
    const luaL_Reg spells[] = { { "IsHelpfulSpell", IsHelpfulSpell }, { "HasRuneUI", HasRuneUI }, { nullptr, nullptr } };
    luaL_register(L, "C_Spell", spells);
    lua_pop(L, 1);
    if (!CVar::Lookup("useUiScale")) {
        CVar::Register("useUiScale", "Whether the in-game UI uses its configured scale", 1, "0", nullptr, CATEGORY_DEFAULT, false, nullptr, false);
    }
    // Persist these original UI preferences; renderer profiles and alternate
    // selection textures still need their separate engine implementations.
    for (const auto& preference : { std::make_pair("ObjectSelectionCircleTexture", "1"),
        std::make_pair("lodObjectCull", "1"), std::make_pair("useRaidVideoSettings", "0"),
        std::make_pair("autoAcceptTrades", "0") }) {
        if (!CVar::Lookup(preference.first)) {
            CVar::Register(preference.first, "Ascension UI preference", 1, preference.second,
                nullptr, CATEGORY_DEFAULT, false, nullptr, false);
        }
    }
}

void AscensionGameUI_ResetRealm() {
    s_auraAmounts.units.clear();
    s_charges.entries.clear();
}

bool AscensionGameUI_ProcessMessage(uint16_t opcode, const void* data, size_t size) {
    if (opcode == 0x0673) {
        uint64_t target;
        if (s_auraAmounts.Read(data, size, target) && !CGlueMgr::s_scriptFunctionsLoaded) {
            for (const char* name : { "UNIT_ABSORB_AMOUNT_CHANGED", "UNIT_HEAL_ABSORB_AMOUNT_CHANGED" }) {
                uint32_t event = FrameScript_RegisterInterfaceEvent(name);
                if (event != UINT32_MAX) { Script_SendUnitSignal(WOWGUID{target}, event); }
            }
        }
        return true;
    }
    if (opcode != 0x09C2 && opcode != 0x09C4 && opcode != 0x09C5) { return false; }
    if (s_charges.Read(opcode, data, size, OsGetAsyncTimeMs()) && !CGlueMgr::s_scriptFunctionsLoaded) {
        uint32_t event = FrameScript_RegisterInterfaceEvent("SPELL_UPDATE_CHARGES");
        if (event != UINT32_MAX) { FrameScript_SignalEvent(event, nullptr); }
    }
    return true;
}
