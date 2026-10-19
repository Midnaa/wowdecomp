# Ascension UI session audit - 2026-10-19

Reviewed the complete latest `AscensionUI.log` session (17:49:04-17:51:15)
and `FrameXML.log`. Runtime logs, account data and archives stay outside Git.
The session used an older installed executable; committed tutorial/talent code
had not been deployed. The current build has not been tested interactively.

| Recorded error group | Current status |
| --- | --- |
| Missing C_Tutorial; Path mentor/appendix OnLoad failures; unconfigured scroll lists | Tutorial catalog/filter/objective/reward backend included in this deployment. Archive tables verified locally. Scroll failures are downstream of aborted initialization; user must verify rendered pages. Mentor service remains unavailable on the local server. |
| Missing C_CharacterAdvancement; specialization/primary-stat helpers | Advancement data and server state/upload backend included. CoA talent path still needs runtime validation; optional C_BuildCreator integration is not implemented. |
| RequestStatisticQuery, C_Aura, GetItemInfoInstant | Added wire request, real unit aura lookup and cached item metadata/PvE/PvP markers. Custom group statistic responses are not implemented by the local server handler; no invented statistics are returned. |
| Missing GetUnitPrimaryStat; live targets labeled Dead | Aura-based API included; its absence interrupted TargetFrame before dead-state refresh. Needs in-game confirmation. |
| Missing GetCustomGameMode, arena rewards | Server packet-backed implementations included. |
| Missing C_Manastorm and C_Spell.GetMaxLearnableRank | Basic active state, completed-level progress and spell-rank lookup included. Full Manastorm queue and catalog are incomplete. |
| Missing C_LootLockout | Bind query sender included; full custom bind response/UI API remains incomplete. |
| Sound Kit Player visible without requesting it | Archive debug frame is visible by default. Hide once after original FrameXML loads, preserving manual opening. |
| Empty character stats: item level/prestige dependencies | Added authoritative object-addon fields 5 (item-level float), 2/3 (character/account prestige); malformed packets preserve previous values. |
| SuperTrack/QuestLog, GetNumAutoQuestPopUps, GetZoneInstanceList, map JSON cache | Still unsupported; can break quest/map tracking. |
| C_MysticEnchant, C_CustomStore, C_AccountInfo, C_SkillCardCollection, C_BuildCreator | Still unsupported optional/custom services. |
| C_SpellActivationOverlay, C_Scenario, HandOfFate, C_ExtraActionButton | Still unsupported gameplay UI systems. |
| C_PlayerTicket, streaming status, alternative power, C_VanityCollection, game events, honorable combat zones | Still unsupported APIs. |
| GetCreatureTemplate, C_Appearance, C_TrialCreator, full C_Challenge catalog | Still unsupported; challenge-rule checks alone do not implement the full challenge browser. |
| CurrencyBar nil concatenation | Unresolved; obtain the next current-build stack/context before changing currency semantics. |
| UnitNameInteractiveNPC, entityShadows, farclip_raid, ObjectSelectionCircleMode, autoAssistCast, showQuestUnitCircles, chatBubblesNameplate CVars | Still missing engine/settings integration. Saving dummy settings would not implement their behavior. |
| Missing optional PTR/development/GM/localization files, AtlasLoot files, relative frame anchors, NineSlicePanelTempalte2 typo | Archive/addon content or XML issues remain. No archive content replaced or committed. |
| DecodeJSON receives nil | Caller/cache dependency remains unresolved; invalid data is not silently accepted as a valid JSON response. |

Logout/exit already use server logout responses. A server-approved delayed logout
shows the original CAMP/QUIT timer; rested/GM/other instant-logout responses do not.
New native diagnostics record response result and instant flags for the next test.

## Validation

Both x64 Release executables built. Both headless tests passed, including actual
client Lua error recovery, embedded tutorial/talent chunks, and malformed packet
handling. Local fixture checks decoded 11 archive tables, including 262 tutorials,
10,255 advancement entries and 5,600 essence rows. No client was launched.

Optional local archive checks (assets remain ignored):

```powershell
build/ascension-tests/Release/AscensionCompatibilityTests.exe --archive-tables build/x64/dbc
```
