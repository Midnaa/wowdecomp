#ifndef WOW_SOURCE_GLUE_ASCENSIONCHARACTERCREATION_H
#define WOW_SOURCE_GLUE_ASCENSIONCHARACTERCREATION_H

struct lua_State;
class CSimpleModelFFX;

void AscensionCharacterCreation_Register(lua_State* L);
void AscensionCharacterCreation_Reset();
bool AscensionCharacterCreation_Dress();
bool AscensionCharacterCreation_Render(CSimpleModelFFX* frame);

#endif
