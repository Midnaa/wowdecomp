#include "Engine/Source/FrameScript/ErrorHandler.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lualib.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <string>

// Only the runtime's platform services are replaced in this standalone test.
void OsValidateFunctionPointer(const void*) {}
uint64_t OsGetAsyncTimeClocks() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}
int64_t OsGetAsyncClocksPerSecond() {
    return std::chrono::steady_clock::period::den / std::chrono::steady_clock::period::num;
}

static int32_t active;
static int reference;
static int calls;

static void Require(bool ok, const char* message) {
    if (!ok) { throw std::runtime_error(message); }
}

static int FailingHandler(lua_State* L) {
    ++calls;
    return luaL_error(L, "error reporter failed");
}

static int NestedHandler(lua_State* L) {
    ++calls;
    lua_pushstring(L, "nested error");
    Require(!FrameScript_CallErrorHandler(L, reference, active), "Nested callback failed");
    Require(calls == 1, "Error callback reentered");
    lua_pop(L, 1);
    lua_pushstring(L, "formatted original error");
    return 1;
}

static void Execute(lua_State* L, const std::string& code) {
    int result = luaL_loadbuffer(L, code.data(), code.size(), "UI compatibility regression");
    if (!result) { result = lua_pcall(L, 0, 0, 0); }
    if (result) { throw std::runtime_error(lua_tostring(L, -1)); }
}

static void EmbeddedChunks(lua_State* L, const char* filename) {
    std::ifstream file(std::string(TEST_SOURCE_ROOT) + "/WoW/Source/UIUtil/" + filename);
    std::string text((std::istreambuf_iterator<char>(file)), {});
    Require(!text.empty(), "Compatibility source missing");
    size_t position = 0;
    while ((position = text.find("R\"lua(", position)) != std::string::npos) {
        position += 6;
        size_t end = text.find(")lua\"", position);
        Require(end != std::string::npos, "Compatibility Lua chunk unterminated");
        Execute(L, text.substr(position, end-position));
        position = end + 5;
    }
}

static void CheckUICompatibility(lua_State* L) {
    luaL_openlibs(L);
    Execute(L, R"lua(
local entries = {
    [101] = {ID=101, Type="Talent", Class="Necromancer", Tab="Necromancy", Spells={1001,1002}, AECost=0, TECost=1, Flags=0, Points=1, RequiredLevel=10, RequiredIDs={}, Name="Talent"},
    [201] = {ID=201, Type="Ability", Class="Necromancer", Tab="Necromancy", Spells={2001}, AECost=0, TECost=0, Flags=0, Points=0, RequiredLevel=1, RequiredIDs={}, Name="Identity one"},
    [202] = {ID=202, Type="Ability", Class="Necromancer", Tab="Death", Spells={2002}, AECost=0, TECost=0, Flags=0, Points=0, RequiredLevel=1, RequiredIDs={}, Name="Identity two"},
}
bit = { band=function(a,b) local n,p=0,1; while a>0 and b>0 do if a%2==1 and b%2==1 then n=n+p end; a=math.floor(a/2);b=math.floor(b/2);p=p*2 end return n end, lshift=function(a,b) return a*2^b end }
C_CharacterAdvancement = {
    _Known=function() return {[101]=1,[201]=1} end,
    _Budget=function() return 8,5 end,
    GetEntryByInternalID=function(id) return entries[id] end,
    GetEntryBySpellID=function(id) for _, e in pairs(entries) do for _, spell in ipairs(e.Spells) do if spell==id then return e end end end end,
    GetAllEntries=function() return {entries[101],entries[201],entries[202]} end,
    _EntriesForClass=function() return {entries[101],entries[201],entries[202]} end,
    _SpecEntries=function() return {[1]=201,[2]=202} end,
    _Upload=function(pending) uploaded={};for id,rank in pairs(pending) do uploaded[id]=rank end return true end,
}
function UnitLevel() return 20 end
function InCombatLockdown() return false end
function SignalEvent(name) lastEvent = name end
C_ClassInfo = {GetSpecInfoByID=function() return {PassiveID=202} end}
function GetCustomGameMode() return 0 end
function GetAchievementInfo() return nil,nil,nil,false end
function IsSpellKnown() return false end
FEATURE_UNAVAILABLE_AT_THIS_TIME="Unavailable"
local tutorial={};for i=1,45 do tutorial[i]=0 end
tutorial[1]=120;tutorial[2]=1;tutorial[16]=4294967295;tutorial[17]=4294967295;tutorial[18]=4294967295
tutorial[36]="icon";tutorial[37]="";tutorial[38]="Welcome";tutorial[39]="Original pages";tutorial[40]="hint"
C_Tutorial = {
    _Identity=function() return 1,12 end,
    _Catalog=function(id) if id==0 then return {tutorial} elseif id==1 then return {{1,"Getting Started"}} elseif id==2 then return {{1,120,777,2}} else return {} end end,
    IsRewardCollected=function() return false end,
}
)lua");
    EmbeddedChunks(L, "AscensionProgressUI.cpp");
    EmbeddedChunks(L, "AscensionTutorialUI.cpp");
    Execute(L, R"lua(
local C=C_CharacterAdvancement
assert(C.GetRemainingTE()==4)
C.AddByEntryID(101,1)
assert(C.IsPending() and C.GetPendingRankByEntryID(101)==2)
assert(C.GetPendingRemainingTE()==3 and C.GetRemainingTE()==4)
assert(C.ApplyPendingBuild() and uploaded[101]==2)
assert(C.GetTalentRankByID(101)==1, "Unacknowledged build replaced server ranks")
C.CancelPendingBuild()
assert(not C.IsPending() and C.GetTalentRankByID(101)==1)
assert(not C.CanAddByEntryID(101,2), "Excess talent rank accepted")
local ok,_,_,_,_,marks,gold=C.CanApplyPendingBuild()
assert(not ok and marks==0 and gold==0)
assert(C.SwitchActiveChrSpec(2) and not uploaded[201] and uploaded[202]==1 and uploaded[101]==1)
assert(C.GetTalentRankByID(201)==1, "Unacknowledged spec switch replaced server identity")
local T=C_Tutorial
T.SetTutorialFilter(1, "Welcome")
assert(T.GetNumTutorials()==1)
local id,name,quest=T.GetTutorialAtIndex(1)
assert(id==120 and name=="Welcome" and quest==nil)
assert(T.IsTutorialComplete(120) and T.CanCollectReward(120))
assert(T.GetRewards(120)[777]==2)
local completed,collected,total=T.GetCategoryProgress(1)
assert(completed==1 and collected==0 and total==1)
T.SetTutorialFilter(1, "absent")
assert(T.GetNumTutorials()==0)
)lua");
}

int main() {
    lua_State* L = luaL_newstate();
    if (!L) { return 1; }
    try {
        lua_pushcfunction(L, FailingHandler);
        reference = luaL_ref(L, LUA_REGISTRYINDEX);
        for (int i = 0; i < 100; ++i) {
            lua_pushstring(L, "original error");
            Require(FrameScript_CallErrorHandler(L, reference, active) != 0, "Reporter failure escaped protected call");
            Require(active == 0 && lua_gettop(L) == 2, "Reporter failure leaked state or stack");
            Require(!std::strcmp(lua_tostring(L, 1), "original error"), "Original error was lost");
            Require(std::strstr(lua_tostring(L, 2), "error reporter failed") != nullptr, "Secondary error was lost");
            lua_settop(L, 0);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, reference);
        lua_pushcfunction(L, NestedHandler);
        reference = luaL_ref(L, LUA_REGISTRYINDEX);
        calls = 0;
        lua_pushstring(L, "original error");
        Require(!FrameScript_CallErrorHandler(L, reference, active), "Successful reporter failed");
        Require(active == 0 && calls == 1 && lua_gettop(L) == 1, "Nested reporter corrupted state");
        Require(!std::strcmp(lua_tostring(L, 1), "formatted original error"), "Reporter result was not returned");
        lua_settop(L, 0);
        CheckUICompatibility(L);
        lua_close(L);
        std::puts("FrameScript error recovery passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        lua_close(L);
        return 1;
    }
}
