#pragma once
#include <cstddef>
#include <cstdint>

void AscensionProgressUI_Register();
void AscensionProgressUI_Reset();
bool AscensionProgressUI_ProcessMessage(uint16_t opcode, const void* bytes, size_t size);
