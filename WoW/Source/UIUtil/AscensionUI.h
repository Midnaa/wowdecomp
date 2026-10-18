#ifndef WOW_SOURCE_UIUTIL_ASCENSIONUI_H
#define WOW_SOURCE_UIUTIL_ASCENSIONUI_H

#include <cstdint>
class CDataStore;

void AscensionUI_RegisterFunctions();
void AscensionUI_ResetRealm();
bool AscensionUI_ProcessMessage(uint16_t opcode, CDataStore* message);
bool AscensionUI_GetClassRoles(uint32_t classID, uint8_t& roles);

#endif
