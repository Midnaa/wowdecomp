#include "AscensionTutorialUI.h"
#include "AscensionTutorialState.h"
#include "AscensionDBC.h"
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"

namespace {
AscensionDBC s_tables[7];
AscensionTutorialState s_state;

int Catalog(lua_State* L) {
    uint32_t table = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (table >= 7) { return luaL_error(L, "Invalid tutorial table"); }
    const auto& data = s_tables[table];
    lua_newtable(L);
    const uint32_t widths[] = { 45, 2, 4, 8, 17, 3, 19 };
    for (uint32_t row = 0; row < data.Count(); ++row) {
        uint32_t id = data.Value(row, 0);
        auto patch = s_state.patches.find(id);
        lua_newtable(L);
        for (uint32_t column = 0; column < widths[table]; ++column) {
            bool string = table == 0 ? column >= 35 && column < 40
                : table == 1 ? column == 1 : table == 3 ? column == 7
                : table == 4 ? column >= 4 && column <= 8 : table == 5 ? column == 1 || column == 2 : false;
            if (string) {
                const char* text = table == 0 && patch != s_state.patches.end()
                    ? patch->second.strings[column - 35].c_str() : data.String(row, column);
                lua_pushstring(L, text);
            } else {
                uint32_t value = table == 0 && patch != s_state.patches.end()
                    ? patch->second.fields[column] : data.Value(row, column);
                lua_pushnumber(L, value);
            }
            lua_rawseti(L, -2, column + 1);
        }
        lua_rawseti(L, -2, row + 1);
    }
    return 1;
}

int Rewarded(lua_State* L) {
    lua_pushboolean(L, s_state.rewarded.count(static_cast<uint32_t>(luaL_checkinteger(L, 1))) != 0);
    return 1;
}
int Identity(lua_State* L) {
    auto* player = ClntObjMgrGetActivePlayerObj();
    if (!player) { return 0; }
    lua_pushinteger(L, player->m_unit->m_unitFieldBytes0[0]);
    lua_pushinteger(L, player->m_unit->m_unitFieldBytes0[1]);
    return 2;
}
int Request(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (!id || !ClientServices::Connection()->m_authenticated || CGlueMgr::s_scriptFunctionsLoaded) {
        lua_pushboolean(L, false); return 1;
    }
    bool quest = lua_toboolean(L, lua_upvalueindex(1));
    CDataStore packet;
    packet.Put(uint32_t(quest ? 0x189 : 0x06A8));
    if (quest) { packet.Put(uint64_t(0)); }
    packet.Put(id);
    if (quest) { packet.Put(uint32_t(0)); }
    packet.Finalize(); ClientServices::Send2(&packet);
    lua_pushboolean(L, true); return 1;
}
}

void AscensionTutorialUI_Register() {
    const char* names[] = { "Tutorial", "TutorialCategories", "TutorialRewards", "TutorialObjectives", "TutorialKeywords", "MentorSpecializations", "TutorialObjectiveTypes" };
    const char* formats[] = { "dddddddddddddddddddddddddddddddddddddLLLddddd", "dL", "dddd", "dddddddL", "ddddddLLLdddddddd", "ddL", "ddL" };
    uint32_t locale = 0;
    if (auto* setting = CVar::Lookup("locale")) {
        const char* locales[] = { "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "esMX", "ruRU" };
        for (uint32_t i = 0; i < 9; ++i) { if (!std::strcmp(setting->m_stringValue.m_str, locales[i])) { locale = i; } }
    }
    for (uint32_t i = 0; i < 7; ++i) {
        std::string path = std::string("DBFilesClient\\") + names[i] + ".dbc";
        void* bytes = nullptr;
        uint32_t size = 0;
        s_tables[i] = {};
        if (SFile::ReadFile(nullptr, path.c_str(), &bytes, &size, 0, 1, nullptr)) {
            s_tables[i].AssignFormatted(bytes, size, formats[i], locale);
            SFile::Unload(bytes);
        }
    }
    lua_State* L = FrameScript_GetContext();
    const luaL_Reg api[] = { { "_Catalog", Catalog }, { "_Identity", Identity }, { "IsRewardCollected", Rewarded }, { nullptr, nullptr } };
    luaL_register(L, "C_Tutorial", api);
    for (const auto& entry : { std::make_pair("CollectReward", false), std::make_pair("StartQuest", true) }) {
        lua_pushboolean(L, entry.second); lua_pushcclosure(L, Request, 1); lua_setfield(L, -2, entry.first);
    }
    lua_pop(L, 1);
    FrameScript_Execute(R"lua(
local C = C_Tutorial
local tutorials, categories, rewards, objectives, keywords, specializations
local filtered, keywordFilter = {}, {}
local function reload()
    tutorials, categories, rewards, objectives, keywords, specializations = C._Catalog(0), C._Catalog(1), C._Catalog(2), C._Catalog(3), C._Catalog(4), C._Catalog(5)
end
reload()
C._ReloadCatalog = reload
local function find(rows, id) for _, row in ipairs(rows) do if row[1] == id then return row end end end
local function info(t) if t then return t[1], t[38], t[4] ~= 0 and t[4] or nil, t[21], t[36] ~= "" and t[36] or nil, t[3] ~= 0 and t[3] or nil, t[37], t[40] end end
function C.GetCategories() local ids = {}; for _, row in ipairs(categories) do ids[#ids+1] = row[1] end return ids end
function C.GetCategoryInfo(id) local row = find(categories, id); return row and row[2] end
local function applicable(t)
    local race, class = C._Identity()
    if race and race > 0 and bit.band(t[16], bit.lshift(1, race-1)) == 0 then return false end
    if class and class > 0 then
        local mask = class > 32 and t[18] or t[17]
        if bit.band(mask, bit.lshift(1, (class-1)%32)) == 0 then return false end
    end
    local modes = GetCustomGameMode()
    if t[32] ~= 0 and bit.band(t[32], modes) == 0 or bit.band(t[33], modes) ~= 0 then return false end
    return true
end
function C.IsCategoryEnabled(id) for _, t in ipairs(tutorials) do if t[2] == id and applicable(t) then return true end end return false end
function C.SetTutorialFilter(category, text)
    filtered = {}; text = (text or ""):lower()
    for _, t in ipairs(tutorials) do if (not category or category == t[2]) and applicable(t) and t[38]:lower():find(text, 1, true) then filtered[#filtered+1] = t end end
end
function C.GetNumTutorials() return #filtered end
function C.GetTutorialAtIndex(index) return info(filtered[index]) end
function C.GetTutorialByID(id) return info(find(tutorials, id)) end
function C.GetTutorialDisplay(id) local t = find(tutorials, id); if t then return t[38], t[39], t[4] ~= 0 and t[4] or nil end end
function C.IsTutorialComplete(id)
    if C.IsRewardCollected(id) then return true end
    local t = find(tutorials, id)
    if not t then return false end
    if id == 120 and t[3] == 0 and t[4] == 0 then return true end
    if t[3] ~= 0 then local _, _, _, complete = GetAchievementInfo(t[3]); return complete == true or complete == 1 end
    return false
end
function C.IsTutorialAvailable(id)
    local t = find(tutorials, id)
    if not t or not applicable(t) then return false, FEATURE_UNAVAILABLE_AT_THIS_TIME end
    if UnitLevel("player") < 8 then return false, format(FEATURE_BECOMES_AVAILABLE_AT_LEVEL, 8) end
    local needsPrevious, hasPrevious = false, false
    for i = 5, 14 do if t[i] ~= 0 then needsPrevious = true; hasPrevious = hasPrevious or C.IsTutorialComplete(t[i]) end end
    if needsPrevious and not hasPrevious then return false, "Complete an earlier Path to Ascension objective first." end
    local needsSpell, hasSpell = false, false
    for i = 23, 31 do if t[i] ~= 0 then needsSpell = true; hasSpell = hasSpell or IsSpellKnown(t[i]) end end
    if needsSpell and not hasSpell then return false, SPELL_FAILED_NOT_KNOWN end
    return true
end
function C.CanCollectReward(id) return C.IsTutorialAvailable(id) and C.IsTutorialComplete(id) and not C.IsRewardCollected(id) end
function C.AnyUnclaimedRewards() for _, t in ipairs(tutorials) do if C.CanCollectReward(t[1]) then return true end end return false end
function C.GetCategoryProgress(category) local complete, collected, count = 0, 0, 0; for _, t in ipairs(tutorials) do if t[2] == category and applicable(t) then count=count+1; if C.IsTutorialComplete(t[1]) then complete=complete+1 end; if C.IsRewardCollected(t[1]) then collected=collected+1 end end end return complete, collected, count end
function C.GetObjectives(id) local ids = {}; for _, row in ipairs(objectives) do if row[2] == id then ids[#ids+1] = row[1] end end return ids end
function C.GetObjectiveInfo(id) local row = find(objectives, id); return row and row[8] end
function C.GetRewards(id) local out = {}; for _, r in ipairs(rewards) do if r[2] == id then out[r[3]] = (out[r[3]] or 0) + r[4] end end return out end
local function keywordInfo(row) if row then return row[1], row[7], row[8], row[5], row[6] end end
function C.SetKeywordFilter(text) keywordFilter = {}; text=(text or ""):lower(); for _, row in ipairs(keywords) do if row[7]:lower():find(text, 1, true) then keywordFilter[#keywordFilter+1] = row end end end
function C.GetNumKeywords() return #keywordFilter end
function C.GetKeywordAtIndex(index) return keywordInfo(keywordFilter[index]) end
function C.GetKeywordInfo(id) return keywordInfo(find(keywords, id)) end
function C.GetIndexByKeywordID(id) for i, row in ipairs(keywordFilter) do if row[1] == id then return i end end end
function C.GetKeywordID(name) for _, row in ipairs(keywords) do if row[7]:lower() == tostring(name):lower() then return row[1] end end end
function C.GetMentorSpecializations() local ids = {}; for _, row in ipairs(specializations) do ids[#ids+1] = row[1] end return ids end
function C.GetMentorSpecializationInfo(id) local row = find(specializations, id); if row then return row[3], row[2] end end
-- This server advertises no mentor service. Keep its controls unavailable while
-- tutorial browsing and reward requests use their supported protocols.
function C.IsMentorStatusActive() return false end
function C.CanToggleMentorStatus() return false, FEATURE_UNAVAILABLE_AT_THIS_TIME end
function C.GetMentorSpecializationActive() return false end
function C.GetNumAvailableMentors() return 0 end
function C.SetAvailableMentorFilter() end
function C.QueryActiveMentors() SignalEvent("QUERY_MENTOR_WHOIS_RESULT", "FEATURE_UNAVAILABLE_AT_THIS_TIME") end
function C.IsTutorialRequiredForMentorStatus(id) local t = find(tutorials, id); return t and t[35] ~= 0 or false end
function C.GetTutorialsRequiredForMentorStatus() local ids = {}; for _, t in ipairs(tutorials) do if C.IsTutorialRequiredForMentorStatus(t[1]) and not C.IsTutorialComplete(t[1]) then ids[#ids+1]=t[1] end end return ids end
C.SetKeywordFilter("")
filtered = tutorials
)lua", "Ascension tutorial compatibility", nullptr);
}

void AscensionTutorialUI_Reset() { s_state = {}; }

bool AscensionTutorialUI_ProcessMessage(uint16_t opcode, const void* bytes, size_t size) {
    bool valid = false;
    const char* event = nullptr;
    uint32_t id = 0;
    if (opcode == 0x06A5) {
        valid = s_state.ReadPatch(bytes, size);
        event = "TUTORIAL_CONTAINER_RESET";
    } else if (opcode == 0x06B0 || opcode == 0x06B1) {
        valid = s_state.ReadRewards(bytes, size, opcode == 0x06B1);
        event = opcode == 0x06B0 ? "TUTORIAL_CONTAINER_RESET" : "TUTORIAL_ENTRY_UPDATE";
        if (valid && opcode == 0x06B1) { std::memcpy(&id, bytes, 4); }
    } else if (opcode == 0x06AF) {
        AscensionPacketReader packet(bytes, size);
        std::string result;
        if (packet.Text(result, 4096) && packet.Done() && !CGlueMgr::s_scriptFunctionsLoaded) {
            FrameScript_SignalEvent(FrameScript_RegisterInterfaceEvent("CLAIM_TUTORIAL_REWARDS_RESULT"), "%s", result.c_str());
        }
        return true;
    } else { return false; }
    if (valid && !CGlueMgr::s_scriptFunctionsLoaded) {
        if (opcode == 0x06A5) { FrameScript_Execute("C_Tutorial._ReloadCatalog()", "Tutorial archive patch", nullptr); }
        FrameScript_SignalEvent(FrameScript_RegisterInterfaceEvent(event), "%u", id);
    }
    return true;
}
