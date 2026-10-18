#pragma once
#include <cstddef>
#include <cstdint>
void AscensionTutorialUI_Register();
void AscensionTutorialUI_Reset();
bool AscensionTutorialUI_ProcessMessage(uint16_t opcode, const void* bytes, size_t size);
