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
    void* bytes = nullptr;
    uint32_t size = 0;
    s_classDetails = {};
    if (SFile::ReadFile(nullptr, "DBFilesClient\\CharacterCreationClassDetails.dbc", &bytes, &size, 0, 1, nullptr)) {
        s_classDetails.Assign(bytes, size, 28);
        SFile::Unload(bytes);
    }
    const luaL_Reg functions[] = {
        { "PreviewClassOutfit", PreviewClassOutfit }, { "ClearClassPreview", ClearClassPreview },
        { "Dress", Dress }, { "Undress", Undress },
        { "GetCameraPosition", GetCameraPosition }, { "SetCameraPosition", SetCameraPosition },
        { "ResetCameraPosition", ResetCameraPosition }, { "ZoomIn", ZoomIn }, { "ZoomOut", ZoomOut },
        { nullptr, nullptr }
    };
    luaL_register(L, nullptr, functions);
}

void AscensionCharacterCreation_Reset() {
    s_previewClass = 0;
    s_undressed = false;
    s_cameraOverride = false;
    s_zooming = false;
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
    for (uint32_t row = 0; s_previewClass && row < s_classDetails.Count(); ++row) {
        if (s_classDetails.Value(row, 1) != s_previewClass
            || s_classDetails.Value(row, 2) != preferences.m_raceID
            || s_classDetails.Value(row, 3) != preferences.m_sexID) {
            continue;
        }
        // ClassDetails stores twelve Item.dbc IDs, not display IDs or inventory slots.
        RemoveEquipment(character);
        bool mainHand = false;
        for (uint32_t column = 16; column < 28; ++column) {
            const auto* item = g_itemDB.GetRecord(s_classDetails.Value(row, column));
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
            bool sheathed = weapon && s_classDetails.Value(row, 15) != 0;
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
