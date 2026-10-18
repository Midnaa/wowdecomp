#include "AscensionProgressUI.h"
#include "AscensionAdvancementState.h"
#include "AscensionDBC.h"
#include <algorithm>
#include <cmath>
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"

namespace {
AscensionDBC s_entries, s_classes, s_tabs, s_essence, s_specs;
AscensionAdvancementState s_state;
std::map<uint32_t, uint32_t> s_byID, s_bySpell;

void Load(AscensionDBC& table, const char* filename, const char* format) {
    void* bytes = nullptr;
    uint32_t size = 0;
    table = {};
    if (!SFile::ReadFile(nullptr, filename, &bytes, &size, 0, 1, nullptr)) { return; }
    uint32_t locale = 0;
    const char* locales[] = { "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "esMX", "ruRU" };
    if (auto* setting = CVar::Lookup("locale")) {
        for (uint32_t i = 0; i < 9; ++i) { if (!std::strcmp(setting->m_stringValue.m_str, locales[i])) { locale = i; } }
    }
    table.AssignFormatted(bytes, size, format, locale);
    SFile::Unload(bytes);
}

uint32_t Row(const AscensionDBC& table, uint32_t id) {
    for (uint32_t row = 0; row < table.Count(); ++row) { if (table.Value(row, 0) == id) { return row; } }
    return UINT32_MAX;
}

void Number(lua_State* L, const char* key, double value) { lua_pushnumber(L, value); lua_setfield(L, -2, key); }
void Text(lua_State* L, const char* key, const char* value) { lua_pushstring(L, value); lua_setfield(L, -2, key); }

void Array(lua_State* L, const char* name, uint32_t row, uint32_t offset, uint32_t count) {
    lua_newtable(L);
    int index = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t value = s_entries.Value(row, offset / 4 + i);
        if (value) { lua_pushnumber(L, value); lua_rawseti(L, -2, ++index); }
    }
    lua_setfield(L, -2, name);
}

void PushEntry(lua_State* L, uint32_t row) {
    if (row >= s_entries.Count()) { lua_pushnil(L); return; }
    lua_newtable(L);
    const std::pair<const char*, uint32_t> numbers[] = {
        { "ID", 0 }, { "RequiredLevel", 0x68 }, { "AECost", 0x38 }, { "TECost", 0x3c },
        { "QualityCost", 0x44 }, { "Column", 0x78 }, { "Row", 0x7c }, { "Group", 0x74 },
        { "RequiredAEInvestment", 0x88 }, { "RequiredTEInvestment", 0x8c },
        { "RequiredClassAEInvestment", 0x90 }, { "RequiredClassTEInvestment", 0x94 },
        { "RequiredTabAEInvestment", 0x98 }, { "RequiredTabTEInvestment", 0x9c },
        { "Points", 0xa0 }, { "RequiredClassPoints", 0xa4 }, { "Flags", 0x124 }, { "ParentNode", 0x19c }
    };
    for (const auto& field : numbers) { Number(L, field.first, s_entries.Value(row, field.second / 4)); }
    const std::pair<const char*, uint32_t> strings[] = {
        { "Type", 4 }, { "Name", 0xbc }, { "Icon", 0xc0 }, { "Quality", 0xc4 },
        { "NodeType", 0xc8 }, { "Anchor", 0x1a4 }, { "Color", 0x1a8 }
    };
    for (const auto& field : strings) { Text(L, field.first, s_entries.String(row, field.second / 4)); }
    Text(L, "Class", s_classes.String(Row(s_classes, s_entries.Value(row, 32)), 1));
    Text(L, "Tab", s_tabs.String(Row(s_tabs, s_entries.Value(row, 33)), 1));
    const std::pair<const char*, uint32_t> floats[] = {
        { "PositionX", 0xd0 }, { "PositionY", 0xd4 }, { "SizeX", 0xd8 }, { "SizeY", 0xdc }, { "Distance", 0x1a0 }
    };
    for (const auto& field : floats) {
        uint32_t bits = s_entries.Value(row, field.second / 4);
        float value;
        std::memcpy(&value, &bits, 4);
        Number(L, field.first, std::isfinite(value) ? value : 0);
    }
    Array(L, "Spells", row, 0x14, 9);
    Array(L, "RequiredIDs", row, 8, 3);
    Array(L, "ConnectedNodes", row, 0xe4, 15);
    Array(L, "Masteries", row, 0x1b0, 3);
}

int GetAllEntries(lua_State* L) {
    lua_newtable(L);
    for (uint32_t row = 0; row < s_entries.Count(); ++row) { PushEntry(L, row); lua_rawseti(L, -2, row + 1); }
    return 1;
}
int GetEntriesForClass(lua_State* L) {
    const char* className = luaL_optstring(L, 1, "");
    const char* tabName = luaL_optstring(L, 2, "None");
    const char* kind = luaL_optstring(L, 3, "");
    lua_newtable(L);
    int index = 0;
    for (uint32_t row = 0; row < s_entries.Count(); ++row) {
        if (s_entries.Value(row, 0x124 / 4) & 0x809) { continue; }
        const char* classToken = s_classes.String(Row(s_classes, s_entries.Value(row, 32)), 1);
        const char* tabToken = s_tabs.String(Row(s_tabs, s_entries.Value(row, 33)), 1);
        if ((*className && std::strcmp(className, classToken)) || (std::strcmp(tabName, "None") && std::strcmp(tabName, tabToken))
            || (*kind && std::strcmp(kind, s_entries.String(row, 1)))) { continue; }
        PushEntry(L, row); lua_rawseti(L, -2, ++index);
    }
    return 1;
}
int GetEntry(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto& index = lua_toboolean(L, lua_upvalueindex(1)) ? s_bySpell : s_byID;
    auto found = index.find(id);
    PushEntry(L, found == index.end() ? UINT32_MAX : found->second);
    return 1;
}
int GetKnown(lua_State* L) {
    lua_newtable(L);
    for (const auto& entry : s_state.known) { lua_pushnumber(L, entry.second.rank); lua_rawseti(L, -2, entry.first); }
    return 1;
}
int GetActiveSpec(lua_State* L) { lua_pushinteger(L, s_state.received ? int32_t(s_state.active + 1) : -1); return 1; }
int GetActiveChrSpec(lua_State* L) {
    for (uint32_t row = 0; row < s_specs.Count(); ++row) {
        auto known = s_state.known.find(s_specs.Value(row, 28));
        if (known != s_state.known.end() && known->second.rank) { lua_pushnumber(L, s_specs.Value(row, 0)); return 1; }
    }
    return 0;
}
int GetSpecEntries(lua_State* L) {
    lua_newtable(L);
    for (uint32_t row = 0; row < s_specs.Count(); ++row) {
        lua_pushnumber(L, s_specs.Value(row, 28)); lua_rawseti(L, -2, s_specs.Value(row, 0));
    }
    return 1;
}
int IsLocked(lua_State* L) {
    auto found = s_state.known.find(static_cast<uint32_t>(luaL_checkinteger(L, 1)));
    lua_pushboolean(L, found != s_state.known.end() && found->second.locked); return 1;
}
int GetBudget(lua_State* L) {
    auto* player = ClntObjMgrGetActivePlayerObj();
    uint32_t level = static_cast<uint32_t>(luaL_optinteger(L, 1, player ? player->m_unit->m_unitFieldLevel : 1));
    uint32_t classID = player ? player->m_unit->m_unitFieldBytes0[1] : 0;
    uint32_t best = 0, ae = 0, te = 0;
    for (uint32_t row = 0; row < s_essence.Count(); ++row) {
        uint32_t required = s_essence.Value(row, 1);
        if (s_essence.Value(row, 2) == classID && required <= level && required >= best) {
            if (s_essence.Value(row, 3) || s_essence.Value(row, 4) || s_essence.Value(row, 5) || s_essence.Value(row, 6)) { continue; }
            best = required; ae = s_essence.Value(row, 7); te = s_essence.Value(row, 8);
        }
    }
    lua_pushnumber(L, ae); lua_pushnumber(L, te); return 2;
}
int Upload(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!ClientServices::Connection()->m_authenticated || CGlueMgr::s_scriptFunctionsLoaded) {
        lua_pushboolean(L, false); return 1;
    }
    std::map<uint32_t, uint32_t> upload;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        uint32_t id = static_cast<uint32_t>(lua_tointeger(L, -2));
        uint32_t rank = static_cast<uint32_t>(lua_tointeger(L, -1));
        if (!s_byID.count(id) || rank > 9 || upload.size() >= 16384) {
            lua_pop(L, 2); lua_pushboolean(L, false); return 1;
        }
        if (rank) { upload[id] = rank; }
        lua_pop(L, 1);
    }
    CDataStore message;
    message.Put(uint32_t(0x0727)); message.Put(static_cast<uint32_t>(upload.size()));
    for (const auto& entry : upload) {
        message.Put(entry.first); message.Put(entry.second); message.Put(uint32_t(0));
        message.Put(uint8_t(0)); message.Put(uint32_t(0)); message.Put(uint32_t(0));
    }
    message.Finalize(); ClientServices::Send2(&message);
    lua_pushboolean(L, true); return 1;
}
}

void AscensionProgressUI_Register() {
    Load(s_entries, "DBFilesClient\\CharacterAdvancement.dbc", "dddddddddddddddddddddddddddddddddddddddddddddddLdLLbbddddbdddddddddddddddddbbbbbddddddddddddddddddddddddddddddddddddddddddddddddddd");
    Load(s_classes, "DBFilesClient\\CharacterAdvancementClassTypes.dbc", "ddddddL");
    Load(s_tabs, "DBFilesClient\\CharacterAdvancementTabTypes.dbc", "ddL");
    Load(s_specs, "DBFilesClient\\ChrSpecs.dbc", "dddddddddddddddddddddddddddddLLdd");
    Load(s_essence, "DBFilesClient\\CharacterAdvancementEssence.dbc", "ddddddddd");
    s_byID.clear(); s_bySpell.clear();
    for (uint32_t row = 0; row < s_entries.Count(); ++row) {
        s_byID[s_entries.Value(row, 0)] = row;
        for (uint32_t i = 5; i < 14; ++i) { if (uint32_t spell = s_entries.Value(row, i)) { s_bySpell.emplace(spell, row); } }
    }
    lua_State* L = FrameScript_GetContext();
    const luaL_Reg api[] = {
        { "GetAllEntries", GetAllEntries }, { "_EntriesForClass", GetEntriesForClass }, { "GetActiveSpecID", GetActiveSpec },
        { "IsLockedID", IsLocked },
        { "GetActiveChrSpec", GetActiveChrSpec }, { "_SpecEntries", GetSpecEntries }, { "_Known", GetKnown }, { "_Budget", GetBudget }, { "_Upload", Upload }, { nullptr, nullptr }
    };
    luaL_register(L, "C_CharacterAdvancement", api);
    for (const auto& entry : { std::make_pair("GetEntryByInternalID", false), std::make_pair("GetEntryBySpellID", true) }) {
        lua_pushboolean(L, entry.second); lua_pushcclosure(L, GetEntry, 1); lua_setfield(L, -2, entry.first);
    }
    lua_pop(L, 1);
    FrameScript_Execute(R"lua(
local C = C_CharacterAdvancement
local pending
local function entry(id) return C.GetEntryByInternalID(id) end
local function ranks(preview) return preview and pending or C._Known() end
local function rank(id, preview) return (ranks(preview) or {})[id] or 0 end
local function investment(preview, cost, class, tab)
    local total = 0
    for id, n in pairs(ranks(preview)) do
        local e = entry(id)
        if e and (not class or e.Class == class) and (not tab or tab == "None" or e.Tab == tab) then total = total + (e[cost] or 0) * n end
    end
    return total
end
function C.IsPending() return pending ~= nil end
C.IsPendingBuildAvailable = C.IsPending
function C.GetTalentRankByID(id) local e = entry(id); return rank(id), e and #e.Spells or 0 end
C.GetPendingRankByEntryID = function(id) local e = entry(id); return rank(id, true), e and #e.Spells or 0 end
function C.GetTalentRankBySpellID(spell) local e = C.GetEntryBySpellID(spell); if e then return C.GetTalentRankByID(e.ID) end return 0, 0 end
function C.IsKnownID(id) return rank(id) > 0 end
function C.IsKnownSpellID(spell) local e = C.GetEntryBySpellID(spell); return e and C.IsKnownID(e.ID) or false end
function C.IsPendingEntryID(id) return rank(id, true) > 0 end
function C.IsTalentID(id) local e = entry(id); return e and e.Type == "Talent" or false end
function C.IsTalentAbilityID(id) local e = entry(id); return e and e.Type == "TalentAbility" or false end
function C.IsTalentSpellID(id) local e = C.GetEntryBySpellID(id); return e and C.IsTalentID(e.ID) or false end
function C.IsTalentAbilitySpellID(id) local e = C.GetEntryBySpellID(id); return e and C.IsTalentAbilityID(e.ID) or false end
function C.GetAbilityEssenceCost(spell) local e = C.GetEntryBySpellID(spell); return e and e.AECost or 0 end
function C.GetTalentEssenceCost(spell) local e = C.GetEntryBySpellID(spell); return e and e.TECost or 0 end
function C.GetGlobalAEInvestment() return investment(false, "AECost") end
function C.GetGlobalTEInvestment() return investment(false, "TECost") end
function C.GetPendingGlobalAEInvestment() return investment(true, "AECost") end
function C.GetPendingGlobalTEInvestment() return investment(true, "TECost") end
function C.GetTabTEInvestment(class, tab) return investment(false, "TECost", class, tab) end
function C.GetTabAEInvestment(class, tab) return investment(false, "AECost", class, tab) end
function C.GetPendingTabTEInvestment(class, tab) return investment(true, "TECost", class, tab) end
function C.GetPendingTabAEInvestment(class, tab) return investment(true, "AECost", class, tab) end
function C.GetClassPointInvestment(class) return investment(false, "Points", class) end
function C.GetPendingClassPointInvestment(class) return investment(true, "Points", class) end
function C.GetRemainingAE() return math.max(0, C._Budget() - C.GetGlobalAEInvestment()) end
function C.GetRemainingTE() local _, te = C._Budget(); return math.max(0, te - C.GetGlobalTEInvestment()) end
function C.GetPendingRemainingAE() return math.max(0, C._Budget() - C.GetPendingGlobalAEInvestment()) end
function C.GetPendingRemainingTE() local _, te = C._Budget(); return math.max(0, te - C.GetPendingGlobalTEInvestment()) end
function C.GetExpectedAE(level) return C._Budget(level) end
function C.GetLearnedAE(class, tab) return investment(false, "AECost", class, tab) end
function C.GetLearnedTE(class, tab) return investment(false, "TECost", class, tab) end
function C.GetQualityInfo(spell) local e = C.GetEntryBySpellID(spell); return e and e.Quality or "Normal", e and e.QualityCost or 0 end
function C.GetQualityCount(quality) local n = 0; for id, r in pairs(C._Known()) do local e = entry(id); if e and e.Quality == quality then n = n + e.QualityCost * r end end return n end
function C.GetQualityLimit() return nil end
local function list(class, tab, kind, known)
    local out = {}
    for _, e in ipairs(C._EntriesForClass(class, tab, kind)) do
        if (not class or e.Class == class) and (not tab or tab == "None" or e.Tab == tab) and (not kind or e.Type == kind)
            and (not known or C.IsKnownID(e.ID)) then out[#out+1] = e end
    end
    return out
end
function C.GetSpellsByClass(class, tab) return list(class, tab, "Ability") end
function C.GetTalentsByClass(class, tab) return list(class, tab, "Talent") end
function C.GetKnownSpellEntriesForClass(class, tab) return list(class, tab, "Ability", true) end
function C.GetKnownTalentEntriesForClass(class, tab) return list(class, tab, "Talent", true) end
function C.GetMasteriesByClass(class, tab) local out = {}; for _, e in ipairs(list(class, tab)) do if bit.band(e.Flags, 0x1000) ~= 0 then out[#out+1] = e end end return out end
function C.GetImplicitByClass(class, tab) local out = {}; for _, e in ipairs(list(class, tab)) do if e.Type == "Implicit" then out[#out+1] = e end end return out end
function C.IsMastery(spell) local e = C.GetEntryBySpellID(spell); return e and bit.band(e.Flags, 0x1000) ~= 0 or false end
local function canAdd(id, n, preview)
    local e = entry(id)
    if not e or n < 1 then return false, "CA_LEARN_BAD_ABILITY" end
    if bit.band(e.Flags, 9) ~= 0 then return false, "CA_LEARN_DISABLED" end
    if rank(id, preview) + n > #e.Spells then return false, "CA_LEARN_ALREADY_KNOWN" end
    if UnitLevel("player") < e.RequiredLevel then return false, "CA_LEARN_LOW_LEVEL" end
    for _, required in ipairs(e.RequiredIDs) do if rank(required, preview) == 0 then return false, "CA_LEARN_MISSING_REQUIRED_ID" end end
    if not C.MeetsInvestmentForAddByEntryID(id, n) then return false, "CA_LEARN_NOT_ENOUGH_INVESTED_TE" end
    local ae = preview and C.GetPendingRemainingAE() or C.GetRemainingAE()
    local te = preview and C.GetPendingRemainingTE() or C.GetRemainingTE()
    if ae < e.AECost*n then return false, "CA_LEARN_MISSING_AE" end
    if te < e.TECost*n then return false, "CA_LEARN_MISSING_TE" end
    return true
end
function C.MeetsInvestmentForAddByEntryID(id)
    local e = entry(id)
    if not e then return false end
    return C.GetPendingGlobalAEInvestment() >= (e.RequiredAEInvestment or 0)
        and C.GetPendingGlobalTEInvestment() >= (e.RequiredTEInvestment or 0)
        and investment(true, "AECost", e.Class) >= (e.RequiredClassAEInvestment or 0)
        and investment(true, "TECost", e.Class) >= (e.RequiredClassTEInvestment or 0)
        and investment(true, "AECost", e.Class, e.Tab) >= (e.RequiredTabAEInvestment or 0)
        and investment(true, "TECost", e.Class, e.Tab) >= (e.RequiredTabTEInvestment or 0)
end
function C.KnowsConnectedNodesFor(id)
    local e = entry(id); if not e then return false end
    for _, required in ipairs(e.RequiredIDs) do if rank(required, true) == 0 then return false end end
    return true
end
function C.IsConnectionAllowed(first, second)
    local a, b = entry(first), entry(second)
    if not a or not b then return false end
    if a.ParentNode == second or b.ParentNode == first then return true end
    for _, id in ipairs(a.ConnectedNodes or {}) do if id == second then return true end end
    for _, id in ipairs(b.ConnectedNodes or {}) do if id == first then return true end end
    for _, id in ipairs(b.RequiredIDs or {}) do if id == first then return true end end
    return false
end
function C.CanLearnID(id) return canAdd(id, 1, false) end
function C.CanAddByEntryID(id, n) return canAdd(id, n or 1, true) end
function C.CanUnlearnID(id) return C.IsKnownID(id) end
function C.CanRemoveByEntryID(id, n) return rank(id, true) >= (n or 1) end
local function change(id, n) if not pending then pending = C._Known() end pending[id] = math.max(0, rank(id, true) + n); SignalEvent("CHARACTER_ADVANCEMENT_PENDING_BUILD_UPDATED") end
function C.AddByEntryID(id, n) if C.CanAddByEntryID(id, n) then change(id, n or 1) end end
function C.RemoveByEntryID(id, n) if C.CanRemoveByEntryID(id, n) then change(id, -(n or 1)) end end
function C.CancelPendingBuild() pending = nil; SignalEvent("CHARACTER_ADVANCEMENT_PENDING_BUILD_UPDATED") end
function C.CanApplyPendingBuild() return pending ~= nil, nil, nil, nil, nil, 0, 0 end
function C.ApplyPendingBuild() if pending then return C._Upload(pending) end return false end
function C.LearnID(id) if C.CanLearnID(id) then change(id, 1); return C.ApplyPendingBuild() end return false end
function C.UnlearnID(id) if C.CanUnlearnID(id) then change(id, -1); return C.ApplyPendingBuild() end return false end
function C.ClearPendingBuildByTab(class, tab) pending = pending or C._Known(); for id in pairs(pending) do local e = entry(id); if e and e.Class == class and (not tab or e.Tab == tab) and e.TECost > 0 then pending[id] = 0 end end SignalEvent("CHARACTER_ADVANCEMENT_PENDING_BUILD_UPDATED") end
function C.ClearPendingBuild() pending = C._Known(); for id in pairs(pending) do local e = entry(id); if e and e.TECost > 0 then pending[id] = 0 end end SignalEvent("CHARACTER_ADVANCEMENT_PENDING_BUILD_UPDATED") end
function C.CanSwitchActiveChrSpec() return not InCombatLockdown() end
function C.SwitchActiveChrSpec(spec)
    local info = C_ClassInfo.GetSpecInfoByID(spec)
    if info and info.PassiveID and C.CanSwitchActiveChrSpec() then
        pending = C._Known()
        for _, identity in pairs(C._SpecEntries()) do pending[identity] = nil end
        pending[info.PassiveID] = 1
        SignalEvent("CHARACTER_ADVANCEMENT_PENDING_BUILD_UPDATED")
        return C._Upload(pending)
    end
    return false
end
local filtered, filteredIDs = {}, {}
function C.SetFilteredEntries(text) filtered = {}; filteredIDs = {}; text = (text or ""):lower(); for _, e in ipairs(C.GetAllEntries()) do if e.Name:lower():find(text, 1, true) then filtered[#filtered+1] = e; filteredIDs[e.ID] = text ~= "" end end end
function C.IsFiltered(id) return filteredIDs[id] == true end
function C.GetNumFilteredEntries() return #filtered end
function C.GetFilteredEntryAtIndex(index) return filtered[index] end
function C.ClearRecentlyLearnedEntries() end
)lua", "Ascension character advancement compatibility", nullptr);
}

void AscensionProgressUI_Reset() { s_state = {}; }

bool AscensionProgressUI_ProcessMessage(uint16_t opcode, const void* bytes, size_t size) {
    bool valid;
    if (opcode == 0x0725) { valid = s_state.ReadSpec(bytes, size); }
    else if (opcode == 0x0726) { valid = s_state.ReadKnown(bytes, size); }
    else if (opcode == 0x072C) {
        AscensionPacketReader packet(bytes, size);
        std::string result, reason;
        uint32_t entry, rank;
        if (packet.Text(result, 256) && packet.Text(reason, 256) && packet.Read(entry) && packet.Read(rank) && packet.Done()
            && !CGlueMgr::s_scriptFunctionsLoaded) {
            bool success = result == "CA_UPDATE_ENTRIES_OK";
            if (success) { FrameScript_Execute("C_CharacterAdvancement.CancelPendingBuild()", "Character advancement update acknowledgement", nullptr); }
            FrameScript_SignalEvent(FrameScript_RegisterInterfaceEvent("CHARACTER_ADVANCEMENT_UPDATE_ENTRIES_RESULT"),
                "%b%s%s%u%u", success, result.c_str(), reason.c_str(), entry, rank);
        }
        return true;
    } else { return false; }
    if (valid && !CGlueMgr::s_scriptFunctionsLoaded) {
        FrameScript_SignalEvent(FrameScript_RegisterInterfaceEvent("ASCENSION_CA_SPECIALIZATION_ACTIVE_ID_CHANGED"), "%u", s_state.active + 1);
        if (opcode == 0x0726) {
            FrameScript_SignalEvent(FrameScript_RegisterInterfaceEvent("CHARACTER_ADVANCEMENT_PENDING_BUILD_UPDATED"), nullptr);
        }
    }
    return true;
}
