#ifndef WOW_SOURCE_UIUTIL_ASCENSIONGAMEUI_H
#define WOW_SOURCE_UIUTIL_ASCENSIONGAMEUI_H
#include <cstddef>
#include <cstdint>
void AscensionGameUI_Register();
void AscensionGameUI_ResetRealm();
bool AscensionGameUI_ProcessMessage(uint16_t opcode, const void* data, size_t size);
#endif
