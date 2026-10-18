#include "AscensionUI.h"
#include "AscensionDBC.h"
#include "AscensionRealm.h"
#include "AscensionAddons.h"
#include "AscensionProtocol.h"
#include <windows.h>
#include <algorithm>
#include <initializer_list>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Event/Event.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ChrClassesRec.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/DB/Rec/SpellIconRec.h"
#include "WoW/Source/Glue/CharacterSelection.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/Glue/AscensionCharacterCreation.h"
#include "WoW/Source/Ui/QuestTextParser.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"

namespace {
AscensionDBC s_specs;
AscensionDBC s_classRoles;
AscensionRealm s_realm;
AscensionAddons s_addons;
AscensionCharacterList s_characterList;
AscensionConfig s_config;
struct CreationTable {
    AscensionDBC data;
    const char* filename;
    uint32_t fields;
    uint32_t orderColumn;
    int parentColumn;
    uint32_t resultColumn;
};
CreationTable s_creationTables[] = {
    { {}, "DBFilesClient\\CharacterCreationArchetypeRoles.dbc", 71, 70, -1, 0 },
    { {}, "DBFilesClient\\CharacterCreationArchetypeCategories.dbc", 73, 72, 1, 0 },
    { {}, "DBFilesClient\\CharacterCreationArchetypes.dbc", 157, 156, 1, 0 },
    { {}, "DBFilesClient\\CharacterCreationClassGuideRoles.dbc", 38, 37, -1, 0 },
    { {}, "DBFilesClient\\CharacterCreationClassGuideSubroles.dbc", 40, 39, 1, 0 },
    { {}, "DBFilesClient\\CharacterCreationClassGuideSubroleClasses.dbc", 4, 3, 1, 2 },
};
bool s_enabled = false;
std::unordered_set<std::string> s_loggedErrors;
int s_errorStackHeight = 0;
bool s_visitedArea52 = false;
std::string s_characterOrder;
bool s_characterOrderReceived = false;
struct CharacterExtras {
    bool modeReceived = false;
    bool mailReceived = false;
    uint32_t activeModes = 0, enabledModes = 0, team = 0;
    uint8_t mercenary = 0, ruleset = 0, mail = 0, storeMail = 0;
};
std::array<CharacterExtras, 129> s_characterExtras{};

int StackHeight(lua_State* L) {
    lua_Debug info;
    int height = 0;
    while (lua_getstack(L, height, &info)) {
        ++height;
    }
    return height;
}

int GetCallstackHeight(lua_State* L) {
    lua_pushinteger(L, StackHeight(L));
    return 1;
}

int GetErrorCallstackHeight(lua_State* L) {
    lua_pushinteger(L, s_errorStackHeight);
    return 1;
}

int SendSystemMessage(lua_State* L) {
    ConsoleWrite(luaL_checkstring(L, 1), DEFAULT_COLOR);
    return 0;
}

int CopyToClipboard(lua_State* L) {
    size_t length;
    const char* text = luaL_checklstring(L, 1, &length);
    if (length > 1024 * 1024 || std::memchr(text, 0, length)) {
        return luaL_error(L, "Clipboard text is too long or contains a null byte");
    }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, static_cast<int>(length), nullptr, 0);
    if ((!count && length) || !OpenClipboard(nullptr)) { lua_pushboolean(L, false); return 1; }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (size_t(count) + 1) * sizeof(wchar_t));
    bool success = false;
    if (memory) {
        auto* output = static_cast<wchar_t*>(GlobalLock(memory));
        if (output) {
            MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(length), output, count);
            output[count] = 0;
            GlobalUnlock(memory);
            success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
        }
        if (!success) { GlobalFree(memory); }
    }
    CloseClipboard();
    lua_pushboolean(L, success);
    return 1;
}

int TooltipAddSpacer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "AddLine");
    lua_pushvalue(L, 1);
    lua_pushstring(L, " ");
    lua_call(L, 2, 0);
    return 0;
}

void LogLuaError(const char* message) {
    s_errorStackHeight = StackHeight(FrameScript_GetContext());
    std::string text(message ? message : "Unknown Lua error");
    text.resize(std::min(text.size(), size_t(16384)));
    if (s_loggedErrors.size() >= 1000 || !s_loggedErrors.insert(text).second) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories("Logs", error);
    if (FILE* file = std::fopen("Logs/AscensionUI.log", "a")) {
        std::fprintf(file, "%s\n", text.c_str());
        std::fclose(file);
    }
}

int IsKeyDown(lua_State* L) {
    lua_Integer key = luaL_checkinteger(L, 1);
    lua_pushboolean(L, key >= 0 && key < KEY_LAST && EventIsKeyDown(static_cast<KEY>(key)));
    return 1;
}

int GetRealmId(lua_State* L) {
    lua_pushnumber(L, s_realm.id);
    return 1;
}

int HasVisitedArea52ThisSession(lua_State* L) {
    lua_pushboolean(L, s_visitedArea52);
    return 1;
}

int ScriptLog(lua_State* L) {
    int arguments = lua_gettop(L);
    luaL_checkstring(L, 1);
    if (!lua_checkstack(L, arguments + 3)) {
        return 0;
    }
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    for (int i = 1; i <= arguments; ++i) {
        lua_pushvalue(L, i);
    }
    if (lua_pcall(L, arguments, 1, 0)) {
        lua_settop(L, arguments);
        lua_pushvalue(L, 1);
    }
    LogLuaError(lua_tostring(L, -1));
    return 0;
}

int PushClassInfo(lua_State* L, const ChrClassesRec* entry) {
    if (entry) {
        lua_pushstring(L, entry->m_name ? entry->m_name : "");
        lua_pushstring(L, entry->m_filename ? entry->m_filename : "");
        lua_pushinteger(L, entry->m_id);
    } else {
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushnil(L);
    }
    return 3;
}

int GetClassInfo(lua_State* L) {
    lua_Integer id = luaL_checkinteger(L, 1);
    const ChrClassesRec* entry = id > 0 && id <= INT32_MAX
        ? g_chrClassesDB.GetRecord(static_cast<int32_t>(id)) : nullptr;
    return PushClassInfo(L, entry);
}

int GetSpellData(lua_State* L) {
    lua_Integer id = luaL_checkinteger(L, 1);
    SpellRec spell;
    if (id <= 0 || id > INT32_MAX || !g_spellDB.GetRecord(static_cast<int32_t>(id), &spell)) {
        return 0;
    }
    char description[16384] = {};
    SpellParserParseText(&spell, description, sizeof(description), 0, 0, 0, 0, 1, 0);
    const auto* icon = g_spellIconDB.GetRecord(spell.m_spellIconID);
    lua_pushstring(L, spell.m_name ? spell.m_name : "");
    lua_pushstring(L, description);
    lua_pushstring(L, icon && icon->m_textureFilename && *icon->m_textureFilename
        ? icon->m_textureFilename : "Interface\\Icons\\INV_Misc_QuestionMark");
    return 3;
}

int GetSpellDescription(lua_State* L) {
    int results = GetSpellData(L);
    if (!results) { return 0; }
    lua_pop(L, 1);
    return 1;
}

int GetCharacterCounts(lua_State* L) {
    if (!s_characterList.received) { return 0; }
    lua_pushnumber(L, s_characterList.maximum);
    lua_pushnumber(L, s_characterList.total);
    lua_pushnumber(L, s_characterList.active);
    lua_pushnumber(L, s_characterList.inactive);
    return 4;
}

int GetCharacterEntries(lua_State* L) {
    lua_newtable(L);
    int index = 1;
    for (const auto& entry : s_characterList.entries) {
        lua_newtable(L);
        const char* names[] = { "guid", "level", "race", "class", "gender", "zone" };
        const uint32_t values[] = { entry.guid, entry.level, entry.race, entry.classID, entry.sex, entry.zone };
        for (int i = 0; i < 6; ++i) {
            lua_pushnumber(L, values[i]);
            lua_setfield(L, -2, names[i]);
        }
        lua_pushlstring(L, entry.name.data(), entry.name.size());
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, entry.active);
        lua_setfield(L, -2, "active");
        lua_pushboolean(L, entry.online);
        lua_setfield(L, -2, "online");
        const auto* classInfo = g_chrClassesDB.GetRecord(entry.classID);
        if (classInfo) {
            lua_pushstring(L, classInfo->m_filename);
            lua_setfield(L, -2, "classFile");
            lua_pushstring(L, classInfo->m_name);
            lua_setfield(L, -2, "className");
            lua_pushstring(L, classInfo->m_name);
            lua_setfield(L, -2, "classLocalized");
        }
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int ChangeCharacterActive(lua_State* L) {
    lua_Number guid = luaL_checknumber(L, 1);
    if (!std::isfinite(guid) || guid < 1 || guid > UINT32_MAX || guid != std::floor(guid)) {
        return luaL_error(L, "Invalid character GUID");
    }
    bool known = false;
    for (const auto& entry : s_characterList.entries) {
        known |= entry.guid == static_cast<uint32_t>(guid);
    }
    if (!known || !ClientServices::Connection()->m_authenticated) {
        lua_pushboolean(L, false);
        return 1;
    }
    CDataStore message;
    message.Put(static_cast<uint32_t>(lua_tointeger(L, lua_upvalueindex(1))));
    message.Put(static_cast<uint32_t>(guid));
    message.Finalize();
    ClientServices::Send2(&message);
    lua_pushboolean(L, true);
    return 1;
}

template<class T> int PushConfig(lua_State* L, const std::map<std::string, T>& values, bool boolean = false) {
    const char* key = luaL_checkstring(L, 1);
    auto found = values.find(key);
    if (found == values.end()) { lua_pushnil(L); }
    else if (boolean) { lua_pushboolean(L, found->second != 0); }
    else { lua_pushnumber(L, found->second); }
    return 1;
}
int GetIntConfig(lua_State* L) { return PushConfig(L, s_config.integers); }
int GetBoolConfig(lua_State* L) { return PushConfig(L, s_config.booleans, true); }
int GetFloatConfig(lua_State* L) { return PushConfig(L, s_config.floats); }
int GetRateConfig(lua_State* L) { return PushConfig(L, s_config.rates); }
template<class T> int PushVectorConfig(lua_State* L, const std::map<std::string, std::vector<T>>& values) {
    const char* key = luaL_checkstring(L, 1);
    auto found = values.find(key);
    if (found == values.end()) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    int index = 1;
    for (T value : found->second) {
        lua_pushnumber(L, value);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}
int GetIntVectorConfig(lua_State* L) { return PushVectorConfig(L, s_config.integerVectors); }
int GetFloatVectorConfig(lua_State* L) { return PushVectorConfig(L, s_config.floatVectors); }

int GetClassInfoByName(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    for (int32_t i = 0; i < g_chrClassesDB.m_numRecords; ++i) {
        const auto& entry = g_chrClassesDB.m_records[i];
        if ((entry.m_name && !_stricmp(name, entry.m_name))
            || (entry.m_nameFemale && !_stricmp(name, entry.m_nameFemale))
            || (entry.m_nameMale && !_stricmp(name, entry.m_nameMale))
            || (entry.m_filename && !_stricmp(name, entry.m_filename))) {
            return PushClassInfo(L, &entry);
        }
    }
    return PushClassInfo(L, nullptr);
}

int GetCharacterSelectionSortOrder(lua_State* L) {
    if (s_characterOrderReceived) {
        lua_pushlstring(L, s_characterOrder.data(), s_characterOrder.size());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int SetCharacterSelectionSortOrder(lua_State* L) {
    size_t length;
    const char* order = luaL_checklstring(L, 1, &length);
    if (!length || length > 1023 || std::memchr(order, 0, length)) {
        return luaL_error(L, "Invalid character sort order");
    }
    if (ClientServices::Connection()->m_authenticated) {
        CDataStore message;
        message.Put(uint32_t(0x0772));
        message.PutString(order);
        message.Finalize();
        ClientServices::Send2(&message);
    }
    return 0;
}

int GetCharacterSelectionMailData(lua_State* L) {
    lua_Integer index = luaL_checkinteger(L, 1);
    const auto* extra = index > 0 && static_cast<size_t>(index) < s_characterExtras.size() ? &s_characterExtras[index] : nullptr;
    if (extra && extra->mailReceived) {
        lua_pushboolean(L, extra->mail);
        lua_pushboolean(L, extra->storeMail);
    } else {
        lua_pushnil(L);
        lua_pushnil(L);
    }
    return 2;
}

int GetCharacterSelectionGameModeData(lua_State* L) {
    lua_Integer index = luaL_checkinteger(L, 1);
    const auto* extra = index > 0 && static_cast<size_t>(index) < s_characterExtras.size() ? &s_characterExtras[index] : nullptr;
    if (extra && extra->modeReceived) {
        lua_pushnumber(L, extra->activeModes);
        lua_pushnumber(L, extra->enabledModes);
        lua_pushnumber(L, extra->team);
        lua_pushboolean(L, extra->mercenary);
        lua_pushnumber(L, extra->ruleset);
    } else {
        for (int i = 0; i < 5; ++i) {
            lua_pushnil(L);
        }
    }
    return 5;
}

int RealmFlag(lua_State* L) {
    int index = static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)));
    bool value = index == 8 ? (s_realm.flags[0] || s_realm.flags[1] || s_realm.flags[2])
        : index == 9 ? (s_realm.received && !s_realm.flags[6] && !s_realm.flags[7])
        : s_realm.flags[index] != 0;
    lua_pushboolean(L, value);
    return 1;
}

void RegisterFlag(lua_State* L, const char* name, int index) {
    lua_pushinteger(L, index);
    lua_pushcclosure(L, RealmFlag, 1);
    lua_setfield(L, -2, name);
}

int CanCreateClass(lua_State* L) {
    lua_Integer id = luaL_checkinteger(L, 1);
    lua_pushboolean(L, id > 0 && id <= 32 && s_realm.CanCreateClass(static_cast<uint32_t>(id)));
    return 1;
}

int GetSecureAddons(lua_State* L) {
    if (!lua_checkstack(L, static_cast<int>(s_addons.entries.size()))) {
        return luaL_error(L, "Too many addons in server response");
    }
    int count = 0;
    for (const auto& entry : s_addons.entries) {
        if (entry.second) {
            lua_pushlstring(L, entry.first.data(), entry.first.size());
            ++count;
        }
    }
    return count;
}

int IsKnownAddon(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    bool known = false;
    for (const auto& entry : s_addons.entries) {
        if (!_stricmp(entry.first.c_str(), name)) {
            known = true;
            break;
        }
    }
    lua_pushboolean(L, known);
    return 1;
}

bool LoadTable(AscensionDBC& table, const char* name, uint32_t fields) {
    void* data = nullptr;
    uint32_t size = 0;
    if (!SFile::ReadFile(nullptr, name, &data, &size, 0, 1, nullptr)) {
        return false;
    }
    bool valid = table.Assign(data, size, fields);
    SFile::Unload(data);
    return valid;
}

uint32_t LocaleColumn() {
    const char* names[] = { "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "esMX", "ruRU" };
    CVar* locale = CVar::Lookup("locale");
    if (locale) {
        for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            if (!std::strcmp(locale->m_stringValue.m_str, names[i])) {
                return i;
            }
        }
    }
    return 0;
}

const char* Localized(const AscensionDBC& table, uint32_t row, uint32_t column) {
    const char* value = table.String(row, column + LocaleColumn());
    return *value ? value : table.String(row, column);
}

int CreationList(lua_State* L) {
    const auto& source = s_creationTables[lua_tointeger(L, lua_upvalueindex(1))];
    uint32_t parent = source.parentColumn < 0 ? 0 : static_cast<uint32_t>(luaL_checkinteger(L, 1));
    std::vector<uint32_t> rows;
    for (uint32_t row = 0; row < source.data.Count(); ++row) {
        if (source.parentColumn < 0 || source.data.Value(row, source.parentColumn) == parent) {
            rows.push_back(row);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [&](uint32_t a, uint32_t b) {
        return source.data.Value(a, source.orderColumn) < source.data.Value(b, source.orderColumn);
    });
    lua_newtable(L);
    int index = 1;
    for (uint32_t row : rows) {
        lua_pushnumber(L, source.data.Value(row, source.resultColumn));
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int CreationInfo(lua_State* L, uint32_t table, std::initializer_list<uint32_t> columns) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto& data = s_creationTables[table].data;
    uint32_t row = 0;
    while (row < data.Count() && data.Value(row, 0) != id) {
        ++row;
    }
    for (uint32_t column : columns) {
        if (row == data.Count()) {
            lua_pushnil(L);
        } else if (column >= 2000) {
            lua_pushnumber(L, data.Value(row, column - 2000));
        } else if (column >= 1000) {
            lua_pushstring(L, Localized(data, row, column - 1000));
        } else {
            lua_pushstring(L, data.String(row, column));
        }
    }
    return static_cast<int>(columns.size());
}

int GetArchetypeRoleInfo(lua_State* L) {
    return CreationInfo(L, 0, { 18, 1019, 1036, 1053 });
}

int GetArchetypeCategoryInfo(lua_State* L) {
    return CreationInfo(L, 1, { 19, 20, 1021, 1038, 1055 });
}

int GetClassGuideRoleInfo(lua_State* L) {
    return CreationInfo(L, 3, { 1, 2, 1003, 1020 });
}

int GetClassGuideSubroleInfo(lua_State* L) {
    return CreationInfo(L, 4, { 2001, 2, 3, 4, 1005, 1022 });
}

int GetArchetypeInfo(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto& data = s_creationTables[2].data;
    for (uint32_t row = 0; row < data.Count(); ++row) {
        if (data.Value(row, 0) != id) { continue; }
        lua_pushstring(L, data.String(row, 2));
        lua_newtable(L);
        for (uint32_t column = 3; column < 8; ++column) {
            if (data.Value(row, column)) {
                lua_pushnumber(L, data.Value(row, column));
                lua_rawseti(L, -2, column - 2);
            }
        }
        lua_pushstring(L, data.String(row, 8));
        for (uint32_t first : { 9u, 12u }) {
            lua_newtable(L);
            for (uint32_t column = first; column < first + 3; ++column) {
                lua_pushstring(L, data.String(row, column));
                lua_rawseti(L, -2, column - first + 1);
            }
        }
        for (uint32_t column = 15; column < 19; ++column) { lua_pushstring(L, data.String(row, column)); }
        for (uint32_t column : { 19u, 36u, 53u }) { lua_pushstring(L, Localized(data, row, column)); }
        lua_pushstring(L, data.String(row, 155));
        return 13;
    }
    for (int i = 0; i < 13; ++i) { lua_pushnil(L); }
    return 13;
}

int GetArchetypeSpellDescription(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t spell = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    const auto& data = s_creationTables[2].data;
    for (uint32_t row = 0; row < data.Count(); ++row) {
        if (data.Value(row, 0) != id) { continue; }
        for (uint32_t column = 3; column < 8; ++column) {
            if (data.Value(row, column) == spell) {
                const char* description = Localized(data, row, 70 + (column - 3) * 17);
                lua_pushstring(L, *description ? description : nullptr);
                return 1;
            }
        }
    }
    lua_pushnil(L);
    return 1;
}

int NoRealmMerge(lua_State* L) {
    // The local CoA server has no realm-merge protocol; advertise no available merge.
    lua_pushboolean(L, false);
    return 1;
}
int GetMergeOptions(lua_State* L) { lua_newtable(L); return 1; }
int GetMergeTarget(lua_State* L) { lua_pushnil(L); return 1; }

int RegisterSavedCVar(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const char* value = luaL_checkstring(L, 2);
    uint32_t flags = 0x80000001u | (lua_toboolean(L, 3) ? 0x20u : 0u);
    CVar* var = CVar::Register(name, "", flags, value, nullptr, CATEGORY_DEBUG, false, nullptr, false);
    lua_pushboolean(L, var != nullptr);
    return 1;
}

bool IsSettingsName(const char* name, size_t length) {
    // Restrict this UI persistence API to settings; it must not modify game data or escape the client.
    if (!length || length > 120) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '_' || c == '-' || c == ' ')) {
            return false;
        }
    }
    const char* devices[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
        "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    for (const char* device : devices) {
        if (length == std::strlen(device) && !_strnicmp(name, device, length)) {
            return false;
        }
    }
    if (name[0] == ' ' || name[length - 1] == ' ') {
        return false;
    }
    return true;
}

std::filesystem::path CustomPath(const char* name, size_t length) {
    return std::filesystem::path("WTF") / "Custom" / (std::string(name, length) + ".json");
}

const char* ReadSettings(const char* name, size_t length, std::string& value) {
    auto path = CustomPath(name, length);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (file) {
        auto size = file.tellg();
        if (size < 0 || size > 16 * 1024 * 1024) {
            return "Custom WTF file is too large";
        }
        value.resize(static_cast<size_t>(size));
        file.seekg(0);
        if (!file.read(value.data(), static_cast<std::streamsize>(value.size()))) {
            return "Could not read custom WTF file";
        }
    }
    return nullptr;
}

const char* WriteSettings(const char* name, size_t nameLength, const char* value, size_t valueLength) {
    auto path = CustomPath(name, nameLength);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return "Could not create custom WTF directory";
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.write(value, valueLength) || !file.flush()) {
            return "Could not write custom WTF file";
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return "Could not save custom WTF file";
    }
    return nullptr;
}

int ReadCustomWTF(lua_State* L) {
    size_t length = 0;
    const char* name = luaL_checklstring(L, 1, &length);
    if (!IsSettingsName(name, length)) {
        return luaL_error(L, "ReadCustomWTF requires a settings name, not a path");
    }
    const char* error;
    {
        std::string value;
        error = ReadSettings(name, length, value);
        if (!error) {
            lua_pushlstring(L, value.data(), value.size());
        }
    }
    // Release file streams and paths before Lua's longjmp error handling.
    if (error) {
        return luaL_error(L, "%s", error);
    }
    return 1;
}

int WriteCustomWTF(lua_State* L) {
    size_t nameLength = 0, valueLength = 0;
    const char* name = luaL_checklstring(L, 1, &nameLength);
    const char* value = luaL_checklstring(L, 2, &valueLength);
    if (!IsSettingsName(name, nameLength) || valueLength > 16 * 1024 * 1024) {
        return luaL_error(L, "Invalid custom WTF name or size");
    }
    if (const char* error = WriteSettings(name, nameLength, value, valueLength)) {
        return luaL_error(L, "%s", error);
    }
    return 0;
}

int GetAllSpecs(lua_State* L) {
    const char* className = luaL_checkstring(L, 1);
    lua_newtable(L);
    int index = 1;
    for (uint32_t row = 0; row < s_specs.Count(); ++row) {
        if (!std::strcmp(className, s_specs.String(row, 1))) {
            lua_pushstring(L, s_specs.String(row, 2));
            lua_rawseti(L, -2, index++);
        }
    }
    return 1;
}

void StringField(lua_State* L, const char* name, const char* value) {
    lua_pushstring(L, value);
    lua_setfield(L, -2, name);
}

void NumberField(lua_State* L, const char* name, uint32_t value) {
    lua_pushnumber(L, value);
    lua_setfield(L, -2, name);
}

void StringArray(lua_State* L, uint32_t row, const char* name, uint32_t first, const char* empty) {
    lua_newtable(L);
    int index = 1;
    for (uint32_t column = first; column < first + 3; ++column) {
        const char* value = s_specs.String(row, column);
        if (*value && std::strcmp(value, empty)) {
            lua_pushstring(L, value);
            lua_rawseti(L, -2, index++);
        }
    }
    lua_setfield(L, -2, name);
}

int GetSpecInfo(lua_State* L) {
    const char* className = luaL_checkstring(L, 1);
    const char* specName = luaL_checkstring(L, 2);
    for (uint32_t row = 0; row < s_specs.Count(); ++row) {
        if (std::strcmp(className, s_specs.String(row, 1)) || std::strcmp(specName, s_specs.String(row, 2))) {
            continue;
        }
        lua_newtable(L);
        NumberField(L, "ID", s_specs.Value(row, 0));
        StringField(L, "Class", className);
        StringField(L, "Spec", specName);
        StringField(L, "SpecFilename", s_specs.String(row, 3));
        const char* armor[] = { "Cloth", "Leather", "Mail", "Plate" };
        for (uint32_t i = 0; i < 4; ++i) {
            lua_pushboolean(L, s_specs.Value(row, i + 4) != 0);
            lua_setfield(L, -2, armor[i]);
        }
        const char* roles[] = { "MeleeDPS", "RangedDPS", "CasterDPS", "Tank", "Healer", "Support" };
        for (uint32_t i = 0; i < 6; ++i) {
            lua_pushboolean(L, s_specs.Value(row, i + 11) != 0);
            lua_setfield(L, -2, roles[i]);
        }
        StringArray(L, row, "PrimaryStats", 8, "None");
        StringField(L, "DifficultyRating", s_specs.String(row, 17));
        StringArray(L, row, "PrimaryResources", 18, "NONE");
        StringArray(L, row, "SecondaryResources", 21, "NONE");
        lua_newtable(L);
        int index = 1;
        for (uint32_t i = 24; i < 27; ++i) {
            if (s_specs.Value(row, i)) {
                lua_pushnumber(L, s_specs.Value(row, i));
                lua_rawseti(L, -2, index++);
            }
        }
        lua_setfield(L, -2, "ExampleSpells");
        NumberField(L, "PassiveSpell", s_specs.Value(row, 27));
        NumberField(L, "PassiveID", s_specs.Value(row, 28));
        StringField(L, "Name", Localized(s_specs, row, 29));
        StringField(L, "Description", Localized(s_specs, row, 46));
        NumberField(L, "SortOrder", s_specs.Value(row, 63));
        NumberField(L, "LootSpecID", s_specs.Value(row, 64));
        return 1;
    }
    lua_pushnil(L);
    return 1;
}
}

void AscensionUI_RegisterFunctions() {
    CVar* enabled = CVar::Register("ascensionUICompatibility", "Enable experimental Ascension UI APIs", 1, "0",
        nullptr, CATEGORY_DEFAULT, false, nullptr, false);
    s_enabled = enabled && enabled->m_intValue;
    s_loggedErrors.clear();
    FrameScript_SetErrorObserver(s_enabled ? LogLuaError : nullptr);
    if (!s_enabled) {
        return;
    }
    lua_State* L = FrameScript_GetContext();
    CVar::Register("loadUnknownAddOns", "Show addons not listed by the realm", 1, "0",
        nullptr, CATEGORY_DEFAULT, false, nullptr, false);
    FrameScript_RegisterFunction("IsKeyDown", IsKeyDown);
    FrameScript_RegisterFunction("GetRealmId", GetRealmId);
    FrameScript_RegisterFunction("HasVisitedArea52ThisSession", HasVisitedArea52ThisSession);
    FrameScript_RegisterFunction("GetClassInfo", GetClassInfo);
    FrameScript_RegisterFunction("GetClassInfoByName", GetClassInfoByName);
    FrameScript_RegisterFunction("GetSpellData", GetSpellData);
    FrameScript_RegisterFunction("GetSpellDescription", GetSpellDescription);
    FrameScript_RegisterFunction("GetCharacterSelectionSortOrder", GetCharacterSelectionSortOrder);
    FrameScript_RegisterFunction("SetCharacterSelectionSortOrder", SetCharacterSelectionSortOrder);
    FrameScript_RegisterFunction("GetCharacterSelectionMailData", GetCharacterSelectionMailData);
    FrameScript_RegisterFunction("GetCharacterSelectionGameModeData", GetCharacterSelectionGameModeData);
    const luaL_Reg logger[] = {
        { "Error", ScriptLog }, { "Warning", ScriptLog }, { "Info", ScriptLog },
        { "Debug", ScriptLog }, { "Trace", ScriptLog }, { "Fatal", ScriptLog }, { nullptr, nullptr }
    };
    luaL_register(L, "C_Logger", logger);
    lua_pop(L, 1);
    const luaL_Reg config[] = {
        { "GetIntConfig", GetIntConfig }, { "GetBoolConfig", GetBoolConfig },
        { "GetFloatConfig", GetFloatConfig }, { "GetRateConfig", GetRateConfig },
        { "GetIntVectorConfig", GetIntVectorConfig }, { "GetFloatVectorConfig", GetFloatVectorConfig },
        { nullptr, nullptr }
    };
    luaL_register(L, "C_Config", config);
    lua_pop(L, 1);
    const luaL_Reg characterList[] = { { "GetCounts", GetCharacterCounts }, { "GetEntries", GetCharacterEntries }, { nullptr, nullptr } };
    luaL_register(L, "C_CharacterList", characterList);
    for (const auto& action : { std::make_pair("RequestActivate", 0x072E), std::make_pair("RequestDeactivate", 0x072F) }) {
        lua_pushinteger(L, action.second);
        lua_pushcclosure(L, ChangeCharacterActive, 1);
        lua_setfield(L, -2, action.first);
    }
    lua_pop(L, 1);
    const luaL_Reg merge[] = {
        { "IsMergeAvailable", NoRealmMerge }, { "NeedsSetTargetRealm", NoRealmMerge },
        { "GetAvailableOptions", GetMergeOptions }, { "GetTargetRealm", GetMergeTarget },
        { "SetTargetRealm", NoRealmMerge }, { nullptr, nullptr }
    };
    luaL_register(L, "C_RealmMerge", merge);
    lua_pop(L, 1);
    FrameScript_RegisterFunction("RegisterSavedCVar", RegisterSavedCVar);
    FrameScript_RegisterFunction("ReadCustomWTF", ReadCustomWTF);
    FrameScript_RegisterFunction("WriteCustomWTF", WriteCustomWTF);
    FrameScript_RegisterFunction("GetSecureAddons", GetSecureAddons);
    FrameScript_RegisterFunction("IsKnownAddon", IsKnownAddon);
    FrameScript_RegisterFunction("GetCallstackHeight", GetCallstackHeight);
    FrameScript_RegisterFunction("GetErrorCallstackHeight", GetErrorCallstackHeight);
    FrameScript_RegisterFunction("SendSystemMessage", SendSystemMessage);
    FrameScript_RegisterFunction("Internal_CopyToClipboard", CopyToClipboard);
    FrameScript_RegisterFunction("GameTooltip_AddSpacer", TooltipAddSpacer);
    LoadTable(s_specs, "DBFilesClient\\ChrSpecs.dbc", 65);
    LoadTable(s_classRoles, "DBFilesClient\\ChrClassesRoles.dbc", 11);
    const luaL_Reg classInfo[] = { { "GetAllSpecs", GetAllSpecs }, { "GetSpecInfo", GetSpecInfo }, { nullptr, nullptr } };
    luaL_register(L, "C_ClassInfo", classInfo);
    lua_pop(L, 1);

    lua_newtable(L);
    const char* realmNames[] = { "IsLive", "IsSeasonal", "IsLeague", "IsPTR", "IsDevelopment" };
    for (int i = 0; i < 5; ++i) {
        RegisterFlag(L, realmNames[i], i);
    }
    RegisterFlag(L, "IsProduction", 8);
    lua_setglobal(L, "C_Realm");

    lua_newtable(L);
    RegisterFlag(L, "CanCreateCoA", 6);
    RegisterFlag(L, "CanCreateWCR", 7);
    RegisterFlag(L, "CanCreateHero", 9);
    lua_pushcfunction(L, CanCreateClass);
    lua_setfield(L, -2, "CanCreateClass");
    const char* listNames[] = { "GetArchetypeRoles", "GetArchetypeCategories", "GetArchetypes",
        "GetClassGuideRoles", "GetClassGuideSubroles", "GetClassGuideSubroleClasses" };
    for (uint32_t i = 0; i < sizeof(s_creationTables) / sizeof(s_creationTables[0]); ++i) {
        auto& table = s_creationTables[i];
        LoadTable(table.data, table.filename, table.fields);
        lua_pushinteger(L, i);
        lua_pushcclosure(L, CreationList, 1);
        lua_setfield(L, -2, listNames[i]);
    }
    const luaL_Reg creationInfo[] = {
        { "GetArchetypeRoleInfo", GetArchetypeRoleInfo },
        { "GetArchetypeCategoryInfo", GetArchetypeCategoryInfo },
        { "GetClassGuideRoleInfo", GetClassGuideRoleInfo },
        { "GetClassGuideSubroleInfo", GetClassGuideSubroleInfo },
        { "GetArchetypeInfo", GetArchetypeInfo },
        { "GetArchetypeSpellDescription", GetArchetypeSpellDescription },
        { nullptr, nullptr }
    };
    luaL_register(L, nullptr, creationInfo);
    AscensionCharacterCreation_Register(L);
    lua_setglobal(L, "C_CharacterCreate");

    AscensionDBC strings;
    if (LoadTable(strings, "DBFilesClient\\GlobalStrings.dbc", 20)) {
        for (uint32_t row = 0; row < strings.Count(); ++row) {
            const char* name = strings.String(row, 2);
            if (*name) {
                lua_pushstring(L, Localized(strings, row, 3));
                lua_setglobal(L, name);
            }
        }
    }
}

bool AscensionUI_IsClassAllowed(uint32_t classID) {
    return !s_enabled || !s_realm.received || s_realm.CanCreateClass(classID);
}

bool AscensionUI_GetClassRoles(uint32_t classID, uint8_t& roles) {
    if (s_enabled) {
        for (uint32_t row = 0; row < s_classRoles.Count(); ++row) {
            if (s_classRoles.Value(row, 0) == classID) {
                // Base roles; conditional roles that require learned spells are separate.
                roles = static_cast<uint8_t>((s_classRoles.Value(row, 1) & 0x0E) | 1);
                return true;
            }
        }
    }
    return false;
}

void AscensionUI_ResetRealm() {
    s_realm = {};
    s_addons.entries.clear();
    s_characterOrder.clear();
    s_characterOrderReceived = false;
    s_characterExtras = {};
    s_characterList = {};
    s_config = {};
}

bool AscensionUI_ProcessMessage(uint16_t opcode, CDataStore* message) {
    if (!s_enabled || (opcode != 0x09BC && opcode != 0x094E
        && opcode != 0x076F && opcode != 0x0770 && opcode != 0x0771
        && opcode != 0x075E && opcode != 0x075F && opcode != 0x0760 && opcode != 0x058D)) {
        return false;
    }
    if (message->m_read <= message->m_size) {
        uint32_t size = message->m_size - message->m_read;
        void* data = nullptr;
        message->GetDataInSitu(data, size);
        if (opcode == 0x09BC) {
            if (s_realm.Read(data, size) && s_realm.id == 11) {
                s_visitedArea52 = true;
            }
        } else if (opcode == 0x094E) {
            s_addons.Read(data, size);
        } else if (opcode == 0x058D) {
            s_config.Read(data, size);
        } else if (opcode == 0x075E) {
            if (s_characterList.Read(data, size) && CGlueMgr::s_scriptFunctionsLoaded) {
                FrameScript_SignalEvent(GLUE_EVENT_CHARACTER_LIST_UPDATED, nullptr);
            }
        } else if (opcode == 0x075F || opcode == 0x0760) {
            AscensionPacketReader packet(data, size);
            std::string result;
            if (packet.Text(result, 128) && packet.Done() && CGlueMgr::s_scriptFunctionsLoaded) {
                lua_State* L = FrameScript_GetContext();
                int top = lua_gettop(L);
                bool success = result == (opcode == 0x075F ? "ACTIVATE_CHARACTER_OK" : "DEACTIVATE_CHARACTER_OK");
                lua_pushboolean(L, success);
                lua_pushlstring(L, result.data(), result.size());
                FrameScript_SignalEvent(opcode == 0x075F ? GLUE_EVENT_CHARACTER_ACTIVATE_RESULT
                    : GLUE_EVENT_CHARACTER_DEACTIVATE_RESULT, L, 2);
                lua_settop(L, top);
            }
        } else if (opcode == 0x076F) {
            const auto* order = static_cast<const char*>(data);
            if (size && size <= 1024 && order[size - 1] == 0 && !std::memchr(order, 0, size - 1)) {
                s_characterOrder.assign(order, size - 1);
                s_characterOrderReceived = true;
            }
        } else if ((opcode == 0x0770 && size == 6) || (opcode == 0x0771 && size == 18)) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            uint32_t index;
            std::memcpy(&index, bytes, 4);
            if (index > 0 && index < s_characterExtras.size()) {
                auto& extra = s_characterExtras[index];
                if (opcode == 0x0770 && bytes[4] <= 1 && bytes[5] <= 1) {
                    extra.mail = bytes[4];
                    extra.storeMail = bytes[5];
                    extra.mailReceived = true;
                } else if (opcode == 0x0771 && bytes[16] <= 1) {
                    std::memcpy(&extra.activeModes, bytes + 4, 4);
                    std::memcpy(&extra.enabledModes, bytes + 8, 4);
                    std::memcpy(&extra.team, bytes + 12, 4);
                    extra.mercenary = bytes[16];
                    extra.ruleset = bytes[17];
                    extra.modeReceived = true;
                }
            }
        }
    }
    message->Reset();
    return true;
}
