#include "AscensionCharacterCreation.h"
#include <algorithm>
#include <cmath>
#include "CharacterCreation.h"
#include "CharacterSelection.h"
#include "CGlueLoading.h"
#include "CSimpleModelFFX.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Services/Camera.h"
#include "Engine/Source/Services/CameraInt.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ChrClassesRec.h"
#include "WoW/Source/DB/Rec/ItemRec.h"
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "WoW/Source/UIUtil/AscensionDBC.h"

namespace {
bool s_enabled = false;
bool s_undressed = false;
int32_t s_previewClass = 0;
AscensionDBC s_classDetails;
AscensionDBC s_archetypeDetails;
AscensionDBC s_archetypes;
AscensionDBC s_roles;
AscensionDBC s_categories;
uint32_t s_role = 0, s_category = 0, s_archetype = 0;
bool s_cameraOverride = false;
C3Vector s_cameraPosition;
C3Vector s_zoomOrigin;
bool s_zooming = false;

C3Vector CameraPosition() {
    if (s_cameraOverride) {
        return s_cameraPosition;
    }
    C3Vector position;
    auto* frame = CCharacterCreation::m_charCustomizeFrame;
    if (frame && frame->m_camera) {
        DataMgrGetCoord(frame->m_camera, 7, &position);
    }
    return position;
}

void RefreshOutfit() {
    auto* character = CCharacterCreation::m_character;
    if (character && character->m_data.m_model) {
        CCharacterCreation::Dress();
        CGlueLoading::StartLoad(character, 1);
    }
}

int PreviewClassOutfit(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    for (int32_t i = 0; i < g_chrClassesDB.m_numRecords; ++i) {
        const auto& entry = g_chrClassesDB.m_records[i];
        if (entry.m_filename && !_stricmp(entry.m_filename, name)) {
            s_previewClass = entry.m_id;
            s_role = s_category = s_archetype = 0;
            RefreshOutfit();
            break;
        }
    }
    return 0;
}

int ClearClassPreview(lua_State* L) {
    s_previewClass = 0;
    RefreshOutfit();
    return 0;
}

int Dress(lua_State* L) {
    s_undressed = false;
    RefreshOutfit();
    return 0;
}

int Undress(lua_State* L) {
    s_undressed = true;
    RefreshOutfit();
    return 0;
}

bool HasRow(const AscensionDBC& table, uint32_t id, uint32_t parent = 0) {
    for (uint32_t row = 0; row < table.Count(); ++row) {
        if (table.Value(row, 0) == id && (!parent || table.Value(row, 1) == parent)) { return true; }
    }
    return false;
}

int SetSelectedArchetype(lua_State* L) {
    uint32_t role = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t category = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    uint32_t archetype = static_cast<uint32_t>(luaL_checkinteger(L, 3));
    if ((role && !HasRow(s_roles, role)) || (category && (!role || !HasRow(s_categories, category, role)))
        || (archetype && (!category || !HasRow(s_archetypes, archetype, category)))) {
        return luaL_error(L, "Invalid character archetype selection");
    }
    s_role = role;
    s_category = category;
    s_archetype = archetype;
    s_previewClass = 0;
    RefreshOutfit();
    return 0;
}

int CycleCustomization(lua_State* L) {
    lua_Integer id = luaL_checkinteger(L, 1);
    lua_Integer direction = luaL_checkinteger(L, 2);
    if (id < 1 || id > 5) { return luaL_error(L, "Invalid customization index"); }
    if (CCharacterCreation::m_character) {
        CCharacterCreation::CycleCharCustomization(static_cast<CHAR_CUSTOMIZATION_TYPE>(id - 1), direction < 0 ? -1 : 1);
        RefreshOutfit();
    }
    return 0;
}

int GetCameraPosition(lua_State* L) {
    auto position = CameraPosition();
    lua_pushnumber(L, position.x);
    lua_pushnumber(L, position.y);
    lua_pushnumber(L, position.z);
    return 3;
}

float FiniteNumber(lua_State* L, int index) {
    double value = luaL_checknumber(L, index);
    if (!std::isfinite(value) || std::abs(value) > 100000.0) {
        luaL_error(L, "Invalid character preview camera coordinate");
    }
    return static_cast<float>(value);
}

int SetCameraPosition(lua_State* L) {
    float x = FiniteNumber(L, 1);
    float y = FiniteNumber(L, 2);
    float z = FiniteNumber(L, 3);
    s_cameraPosition = C3Vector(x, y, z);
    s_cameraOverride = true;
    s_zooming = false;
    return 0;
}

int ResetCameraPosition(lua_State* L) {
    s_cameraOverride = false;
    s_zooming = false;
    return 0;
}

int ZoomIn(lua_State* L) {
    float step = FiniteNumber(L, 1);
    auto* character = CCharacterCreation::m_character;
    auto* frame = CCharacterCreation::m_charCustomizeFrame;
    if (step <= 0.0f || !character || !character->m_data.m_model || !frame || !frame->m_camera) {
        return 0;
    }
    C3Vector position = CameraPosition();
    C3Vector direction = position - character->m_data.m_model->GetAttachmentPosition(20);
    float distance = std::sqrt(direction.SquaredMag());
    if (distance <= step) {
        return 0;
    }
    if (!s_zooming) {
        s_zoomOrigin = position;
        s_zooming = true;
    }
    s_cameraPosition = position - direction * (step / distance);
    s_cameraOverride = true;
    return 0;
}

int ZoomOut(lua_State* L) {
    float step = FiniteNumber(L, 1);
    if (step <= 0.0f || !s_zooming) {
        return 0;
    }
    C3Vector position = CameraPosition();
    C3Vector direction = s_zoomOrigin - position;
    float distance = std::sqrt(direction.SquaredMag());
    s_cameraPosition = distance <= step ? s_zoomOrigin : position + direction * (step / distance);
    s_cameraOverride = true;
    return 0;
}

void RemoveEquipment(CCharacterComponent* character) {
    for (uint32_t i = 0; i < NUM_ITEM_SLOTS; ++i) {
        character->RemoveItem(static_cast<ITEM_SLOT>(i));
    }
    auto* model = character->m_data.m_model;
    CCharacterComponent::RemoveHandItem(model, INVSLOT_MAINHAND, SHEATHE_NONE, false);
    CCharacterComponent::RemoveHandItem(model, INVSLOT_OFFHAND, SHEATHE_NONE, false);
    CCharacterComponent::RemoveHandItem(model, INVSLOT_OFFHAND, SHEATHE_NONE, true);
    CCharacterComponent::RemoveHandItem(model, INVSLOT_RANGED, SHEATHE_NONE, false);
    for (uint32_t type = SHEATHE_MAINHAND; type <= SHEATHE_SHIELD; ++type) {
        CCharacterComponent::RemoveHandItem(model, INVSLOT_MAINHAND, static_cast<SHEATHE_TYPE>(type), false);
        CCharacterComponent::RemoveHandItem(model, INVSLOT_OFFHAND, static_cast<SHEATHE_TYPE>(type), false);
    }
    std::fill(std::begin(character->m_handItemDisplayID), std::end(character->m_handItemDisplayID), 0);
}
}

void AscensionCharacterCreation_Register(lua_State* L) {
    s_enabled = true;
    AscensionCharacterCreation_Reset();
    auto load = [](AscensionDBC& table, const char* name, uint32_t fields) {
        void* bytes = nullptr;
        uint32_t size = 0;
        table = {};
        if (SFile::ReadFile(nullptr, name, &bytes, &size, 0, 1, nullptr)) {
            table.Assign(bytes, size, fields);
            SFile::Unload(bytes);
        }
    };
    load(s_classDetails, "DBFilesClient\\CharacterCreationClassDetails.dbc", 28);
    load(s_archetypeDetails, "DBFilesClient\\CharacterCreationArchetypeDetails.dbc", 28);
    load(s_archetypes, "DBFilesClient\\CharacterCreationArchetypes.dbc", 157);
    load(s_roles, "DBFilesClient\\CharacterCreationArchetypeRoles.dbc", 71);
    load(s_categories, "DBFilesClient\\CharacterCreationArchetypeCategories.dbc", 73);
    const luaL_Reg functions[] = {
        { "PreviewClassOutfit", PreviewClassOutfit }, { "ClearClassPreview", ClearClassPreview },
        { "Dress", Dress }, { "Undress", Undress },
        { "GetCameraPosition", GetCameraPosition }, { "SetCameraPosition", SetCameraPosition },
        { "ResetCameraPosition", ResetCameraPosition }, { "ZoomIn", ZoomIn }, { "ZoomOut", ZoomOut },
        { "SetSelectedArchetype", SetSelectedArchetype }, { "CycleCustomizationSkipLocked", CycleCustomization },
        { nullptr, nullptr }
    };
    luaL_register(L, nullptr, functions);
}

void AscensionCharacterCreation_Reset() {
    s_previewClass = 0;
    s_undressed = false;
    s_cameraOverride = false;
    s_zooming = false;
    s_role = s_category = s_archetype = 0;
}

bool AscensionCharacterCreation_Dress() {
    auto* character = CCharacterCreation::m_character;
    if (!s_enabled || !character || !character->m_data.m_model) {
        return false;
    }
    if (s_undressed) {
        RemoveEquipment(character);
        return true;
    }
    const auto& preferences = character->m_data.m_preferences;
    const auto& table = s_previewClass ? s_classDetails : s_archetype ? s_archetypeDetails : s_category ? s_categories : s_roles;
    uint32_t selected = s_previewClass ? s_previewClass : s_archetype ? s_archetype : s_category ? s_category : s_role;
    uint32_t firstItem = s_previewClass || s_archetype ? 16 : s_category ? 7 : 6;
    for (uint32_t row = 0; selected && row < table.Count(); ++row) {
        bool detailed = s_previewClass || s_archetype;
        if ((detailed && (table.Value(row, 1) != selected
            || table.Value(row, 2) != preferences.m_raceID || table.Value(row, 3) != preferences.m_sexID))
            || (!detailed && table.Value(row, 0) != selected)) {
            continue;
        }
        // ClassDetails stores twelve Item.dbc IDs, not display IDs or inventory slots.
        RemoveEquipment(character);
        bool mainHand = false;
        for (uint32_t column = firstItem; column < firstItem + 12; ++column) {
            const auto* item = g_itemDB.GetRecord(table.Value(row, column));
            if (!item || item->m_displayInfoID <= 0) {
                continue;
            }
            INVENTORY_TYPES type = static_cast<INVENTORY_TYPES>(item->m_inventoryType);
            if (type == INVTYPE_WEAPON && mainHand) {
                type = INVTYPE_WEAPONOFFHAND;
            }
            bool ranged = type == INVTYPE_RANGED || type == INVTYPE_RANGEDRIGHT || type == INVTYPE_THROWN;
            bool weapon = type == INVTYPE_WEAPON || type == INVTYPE_2HWEAPON
                || type == INVTYPE_WEAPONMAINHAND || type == INVTYPE_WEAPONOFFHAND || ranged;
            bool sheathed = weapon && table.Value(row, firstItem - 1) != 0;
            if (ranged || sheathed) {
                ItemDisplayInfoRec display;
                if (g_itemDisplayInfoDB.GetRecord(item->m_displayInfoID, &display)) {
                    auto slot = ranged ? INVSLOT_RANGED : (type == INVTYPE_WEAPONOFFHAND ? INVSLOT_OFFHAND : INVSLOT_MAINHAND);
                    auto link = CCharacterComponent::AddHandItem(character->m_data.m_model, &display, slot,
                        static_cast<SHEATHE_TYPE>(item->m_sheatheType), sheathed, false,
                        type == INVTYPE_RANGEDRIGHT || type == INVTYPE_THROWN, 0);
                    if (!sheathed && item->m_subclassID == 2 && ranged) {
                        for (auto* attached = character->m_data.m_model->m_attachList; attached; attached = attached->m_attachNext) {
                            if (attached->m_attachmentId == static_cast<uint32_t>(link)) {
                                attached->SetLoadedCallback(&CCharacterSelection::ModelLoadedCallback, nullptr);
                                break;
                            }
                        }
                    }
                }
            } else {
                character->AddItemByType(type, item->m_displayInfoID);
            }
            if (type == INVTYPE_WEAPON || type == INVTYPE_WEAPONMAINHAND || type == INVTYPE_2HWEAPON) {
                mainHand = true;
            }
        }
        return true;
    }
    return false;
}

bool AscensionCharacterCreation_Render(CSimpleModelFFX* frame) {
    if (!s_enabled || !s_cameraOverride || frame != CCharacterCreation::m_charCustomizeFrame || !frame->m_camera) {
        return false;
    }
    // Keep the model's animated camera intact. Override only this frame's render.
    CCamera* source = frame->m_camera;
    CCamera* camera = CameraDuplicate(source);
    if (!camera) {
        return false;
    }
    HandleDuplicate(reinterpret_cast<HOBJECT>(source));
    CameraSetCoord(camera, 7, s_cameraPosition);
    frame->SetCamera(camera);
    CSimpleModel::RenderModel(frame);
    frame->SetCamera(source);
    HandleClose(reinterpret_cast<HOBJECT>(source));
    HandleClose(reinterpret_cast<HOBJECT>(camera));
    return true;
}
