#include "WoW/Source/UIUtil/AscensionDBC.h"
#include "WoW/Source/UIUtil/AscensionRealm.h"
#include "WoW/Source/UIUtil/AscensionAddons.h"
#include "WoW/Source/UIUtil/AscensionProtocol.h"
#include "WoW/Source/UIUtil/AscensionAuraAmounts.h"
#include "WoW/Source/UIUtil/AscensionSpellCharges.h"
#include "WoW/Source/UIUtil/AscensionJSON.h"
#include "WoW/Source/UIUtil/AscensionLibraryName.h"
#include "WoW/Source/UIUtil/AscensionAdvancementState.h"
#include "WoW/Source/UIUtil/AscensionTutorialState.h"
#include "WoW/Source/UIUtil/AscensionWorldState.h"
#include "WoW/Source/UIUtil/AscensionObjectAddonState.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void Require(bool result, const char* message) {
    if (!result) {
        throw std::runtime_error(message);
    }
}

static void Put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

template<class T> static void Append(std::vector<uint8_t>& bytes, T value) {
    size_t offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
static void Text(std::vector<uint8_t>& bytes, const char* value) {
    bytes.insert(bytes.end(), value, value + std::strlen(value) + 1);
}
static void Key(std::vector<uint8_t>& bytes, const char* value) {
    Append(bytes, uint32_t(std::strlen(value)));
    bytes.insert(bytes.end(), value, value + std::strlen(value));
}

static void CheckGameUIData() {
    // Packed bytes followed by aligned floats/DWORDs and a localized field.
    std::vector<uint8_t> packed(20 + 77 + 4, 0);
    std::memcpy(packed.data(), "WDBC", 4);
    Put32(packed, 4, 1); Put32(packed, 8, 20); Put32(packed, 12, 77); Put32(packed, 16, 4);
    packed[20] = 7; Put32(packed, 21, 0x3f800000); Put32(packed, 25, 1);
    Put32(packed, 93, 42); std::memcpy(packed.data()+97, "\0Hi", 4);
    AscensionDBC decoded;
    Require(decoded.AssignFormatted(packed.data(), packed.size(), "bdLd"), "Packed localized table rejected");
    Require(decoded.Value(0, 0) == 7 && decoded.Value(0, 1) == 0x3f800000 && decoded.Value(0, 3) == 42,
        "Byte fields or localized strings shifted the native layout");
    Require(!std::strcmp(decoded.String(0, 2), "Hi"), "Localized field not decoded");
    Require(!decoded.AssignFormatted(packed.data(), packed.size()-1, "bdLd"), "Truncated packed table accepted");

    AscensionWorldState progress;
    std::vector<uint8_t> progressUpdate;
    Append(progressUpdate, uint32_t(8));
    for (uint32_t i = 0; i < 8; ++i) {
        Append(progressUpdate, uint32_t(2)); Append(progressUpdate, uint32_t(1)); Append(progressUpdate, i+2);
    }
    Require(progress.ReadProgress(progressUpdate.data(), progressUpdate.size()) && progress.completed[7] == 9,
        "Manastorm completed levels not decoded");
    Require(!progress.ReadProgress(progressUpdate.data(), progressUpdate.size()-1) && progress.completed[7] == 9,
        "Truncated Manastorm update replaced progress");
    AscensionObjectAddonState addonState;
    std::vector<uint8_t> addonUpdate;
    Append(addonUpdate, uint64_t(42)); Append(addonUpdate, uint32_t(2)); Append(addonUpdate, uint32_t(7));
    Require(addonState.Read(addonUpdate.data(), addonUpdate.size()) && addonState.Get(42, 2) == 7,
        "Prestige addon field not decoded");
    for (size_t size = 0; size < addonUpdate.size(); ++size) {
        if (size == 8) continue;
        Require(!addonState.Read(addonUpdate.data(), size) && addonState.Get(42, 2) == 7,
            "Truncated addon update changed existing fields");
    }
    Append(addonUpdate, uint32_t(4096)); Append(addonUpdate, uint32_t(99));
    Require(!addonState.Read(addonUpdate.data(), addonUpdate.size()) && addonState.Get(42, 2) == 7,
        "Invalid addon field accepted");
    AscensionAdvancementState advancement;
    std::vector<uint8_t> known;
    Append(known, uint32_t(1)); Append(known, uint32_t(4037)); Append(known, uint32_t(1));
    Append(known, uint32_t(1)); Append(known, uint8_t(1)); Append(known, uint32_t(12)); Append(known, uint32_t(0));
    Require(advancement.ReadKnown(known.data(), known.size()) && advancement.known.at(4037).locked, "Known talent state rejected");
    for (size_t i = 0; i < known.size(); ++i) {
        Require(!advancement.ReadKnown(known.data(), i) && advancement.known.at(4037).rank == 1, "Truncated talent state replaced known ranks");
    }
    std::vector<uint8_t> spec;
    Append(spec, uint32_t(2)); Append(spec, uint32_t(3));
    Require(advancement.ReadSpec(spec.data(), spec.size()) && advancement.active == 2, "Third specialization rejected");
    Put32(spec, 0, 3);
    Require(!advancement.ReadSpec(spec.data(), spec.size()) && advancement.active == 2, "Invalid active specialization accepted");

    AscensionTutorialState tutorials;
    std::vector<uint8_t> claims;
    Append(claims, uint32_t(2)); Append(claims, uint32_t(120)); Append(claims, uint32_t(76));
    Require(tutorials.ReadRewards(claims.data(), claims.size(), false) && tutorials.rewarded.count(76), "Claim history rejected");
    Require(!tutorials.ReadRewards(claims.data(), claims.size()-1, false) && tutorials.rewarded.size() == 2, "Partial claim history replaced state");
    std::vector<uint8_t> patch(45*4, 0);
    Put32(patch, 0, 120); Put32(patch, 33*4, 3);
    for (const char* text : { "icon", "auxiliary", "Welcome", "Original tutorial pages", "hint" }) { Text(patch, text); }
    Require(tutorials.ReadPatch(patch.data(), patch.size()) && tutorials.patches.at(120).strings[3] == "Original tutorial pages", "Tutorial patch text lost");
    Require(!tutorials.ReadPatch(patch.data(), patch.size()-1) && tutorials.patches.at(120).fields[33] == 3, "Malformed tutorial patch replaced state");

    AscensionWorldState world;
    std::vector<uint8_t> arena;
    for (uint32_t value : { 5000u, 125u, 2u, 0u, 0u, 30u, 20u }) { Append(arena, value); }
    Require(world.ReadArena(arena.data(), arena.size()) && world.arena[1] == 125, "Arena rewards rejected");
    Put32(arena, 8, 6);
    Require(!world.ReadArena(arena.data(), arena.size()) && world.arena[2] == 2, "Invalid arena bracket accepted");
    std::vector<uint8_t> active;
    Append(active, uint32_t(1));
    for (uint32_t value : { 0u, 188u, 1u, 1u, 0u, 0u }) { Append(active, value); }
    Append(active, uint16_t(0));
    Require(world.ReadChallenges(active.data(), active.size()) && world.challenges.at(188) == 1, "Challenge restrictions rejected");
    Require(!world.ReadChallenges(active.data(), active.size()-1) && world.challenges.count(188), "Truncated challenge packet removed restrictions");
    Require(AscensionLibraryName("AceAddon-3.0") && AscensionLibraryName("LibDataBroker-1.1"), "Archive library names rejected");
    for (const char* name : { "", "table: 000001AF5E04E340", "Frame Name", "..", "../outside", "folder\\library" }) {
        Require(!AscensionLibraryName(name), "Unsafe library probe accepted as an archive path");
    }
    Require(!AscensionLibraryName(std::string_view("Ace\0Addon", 9)), "Embedded NUL accepted in library path");
    AscensionAuraAmounts amounts;
    std::vector<uint8_t> aura;
    Append(aura, uint64_t(123)); Append(aura, int32_t(7));
    Append(aura, uint64_t(456)); Append(aura, uint32_t(17));
    Append(aura, int32_t(400)); Append(aura, int32_t(20)); Append(aura, int32_t(-5));
    uint64_t target = 0;
    Require(amounts.Read(aura.data(), aura.size(), target), "Aura amounts rejected");
    Require(target == 123 && amounts.units.at(123).at(7).amounts[0] == 400, "Aura amount keyed incorrectly");
    for (size_t i = 0; i < aura.size(); ++i) {
        Require(!amounts.Read(aura.data(), i, target), "Truncated aura amounts accepted");
    }
    Require(amounts.units.at(123).at(7).amounts[0] == 400, "Invalid aura packet replaced state");
    aura.resize(24);
    Put32(aura, 8, UINT32_MAX);
    Require(amounts.Read(aura.data(), aura.size(), target) && amounts.units.empty(), "Aura removal did not clear amounts");

    AscensionSpellCharges charges;
    std::vector<uint8_t> charge;
    Append(charge, uint32_t(542)); Append(charge, uint32_t(5000)); Append(charge, uint32_t(2));
    Require(charges.Read(0x09C5, charge.data(), charge.size(), 1000), "Charge update rejected");
    const auto& state = charges.entries.at(542);
    Require(state.Restored(5999, 10000) == 0 && state.Restored(6000, 10000) == 1
        && state.Restored(16000, 10000) == 2, "Charge recovery time incorrect");
    for (size_t i = 0; i < charge.size(); ++i) {
        Require(!charges.Read(0x09C5, charge.data(), i, 2000), "Truncated charge update accepted");
    }
    Require(charges.entries.at(542).received == 1000, "Malformed charge update replaced state");
    std::vector<uint8_t> bulk;
    Append(bulk, uint32_t(1)); Append(bulk, uint32_t(323)); Append(bulk, uint32_t(800)); Append(bulk, uint8_t(1));
    Require(charges.Read(0x09C4, bulk.data(), bulk.size(), 3000)
        && charges.entries.count(323) && !charges.entries.count(542), "Charge snapshot did not replace old categories");
    Require(!charges.Read(0x09C2, bulk.data(), bulk.size(), 3000), "Charge reset accepted unexpected payload");
    Require(charges.Read(0x09C2, nullptr, 0, 3000) && charges.entries.empty(), "Charge reset failed");

    AscensionJSON json;
    AscensionJSONValue value;
    const char* text = "{\"enabled\":false,\"count\":1.25e2,\"values\":[null,true,\"\\uD83D\\uDE00\"]}";
    Require(json.Parse(text, std::strlen(text), value), "Valid settings JSON rejected");
    Require(value.object[0].second.type == AscensionJSONValue::Boolean && value.object[0].second.number == 0
        && value.object[1].second.number == 125, "JSON false or numeric value lost");
    Require(value.object[2].second.array[0].type == AscensionJSONValue::Null
        && value.object[2].second.array[2].string == "\xF0\x9F\x98\x80", "JSON Unicode or null value lost");
    for (const char* invalid : { "", "{", "[1,]", "01", "1.", "nullx", "1e999", "\"\\uD800\"", "{\"x\":true,}" }) {
        Require(!json.Parse(invalid, std::strlen(invalid), value), "Malformed JSON accepted");
    }
    std::string deep(100, '[');
    deep += "0";
    deep.append(100, ']');
    Require(!json.Parse(deep.data(), deep.size(), value), "Unbounded JSON nesting accepted");
}

static void CheckExtendedPackets() {
    AscensionCharacterList characters;
    std::vector<uint8_t> list;
    for (uint32_t count : { 128u, 2u, 1u, 1u }) { Append(list, count); }
    for (uint8_t active : { uint8_t(1), uint8_t(0) }) {
        Append(list, uint32_t(100 + active));
        for (uint8_t field : { active, uint8_t(0), uint8_t(60), uint8_t(1), uint8_t(32), uint8_t(0) }) { Append(list, field); }
        Append(list, uint32_t(12));
        Text(list, active ? "Active Ranger" : "Inactive Ranger");
    }
    Require(characters.Read(list.data(), list.size()), "Server character list rejected");
    Require(characters.maximum == 128 && characters.entries.size() == 2
        && characters.entries[0].classID == 32 && characters.entries[1].active == 0,
        "Character class or active state lost");
    for (size_t i = 0; i < list.size(); ++i) {
        Require(!characters.Read(list.data(), i), "Truncated character list accepted");
    }
    Require(characters.entries.size() == 2, "Invalid character list replaced previous state");
    Put32(list, 8, 2);
    Require(!characters.Read(list.data(), list.size()), "Inconsistent character counts accepted");
    Put32(list, 8, 1);
    list.push_back(0);
    Require(!characters.Read(list.data(), list.size()), "Trailing character bytes accepted");

    AscensionConfig config;
    std::vector<uint8_t> settings;
    Append(settings, uint32_t(1)); Key(settings, "CONFIG_MAX_LEVEL"); Append(settings, int32_t(60));
    Append(settings, uint32_t(1)); Key(settings, "CONFIG_CHARACTER_CREATION_ARCHETYPES_ENABLED"); Append(settings, uint8_t(1));
    Append(settings, uint32_t(1)); Key(settings, "FLOAT_CONFIG"); Append(settings, 2.5f);
    Append(settings, uint32_t(1)); Key(settings, "RATE_XP_GLOBAL"); Append(settings, 3.0f);
    Append(settings, uint32_t(0)); Append(settings, uint32_t(0));
    Require(config.Read(settings.data(), settings.size()), "Server configuration rejected");
    Require(config.integers.at("CONFIG_MAX_LEVEL") == 60
        && config.booleans.at("CONFIG_CHARACTER_CREATION_ARCHETYPES_ENABLED") == 1
        && config.rates.at("RATE_XP_GLOBAL") == 3.0f, "Configuration sections mixed up");
    for (size_t i = 0; i < settings.size(); ++i) {
        Require(!config.Read(settings.data(), i), "Truncated configuration accepted");
    }
    Require(config.integers.at("CONFIG_MAX_LEVEL") == 60, "Invalid configuration replaced previous state");
    Put32(settings, 0, UINT32_MAX);
    Require(!config.Read(settings.data(), settings.size()), "Unbounded configuration count accepted");
}

int main(int argc, char** argv) {
    try {
        CheckGameUIData();
        CheckExtendedPackets();
        AscensionDBC table;
        Require(!table.Assign(nullptr, 0, 2), "Empty DBC accepted");
        std::vector<uint8_t> bytes(32);
        std::memcpy(bytes.data(), "WDBC", 4);
        Put32(bytes, 4, 1);
        Put32(bytes, 8, 2);
        Put32(bytes, 12, 8);
        Put32(bytes, 16, 4);
        Put32(bytes, 20, 32);
        Put32(bytes, 24, 0);
        std::memcpy(bytes.data() + 28, "CoA", 4);
        Require(table.Assign(bytes.data(), bytes.size(), 2), "Valid DBC rejected");
        Require(table.Value(0, 0) == 32, "Class ID changed");
        Require(std::strcmp(table.String(0, 1), "CoA") == 0, "String offset zero rejected");
        Require(!*table.String(99, 1) && !*table.String(0, 99), "Out-of-bounds DBC access");
        Require(!table.Assign(bytes.data(), 31, 2), "Truncated DBC accepted");
        Require(table.Count() == 0, "Invalid load retained previous data");
        Put32(bytes, 24, 99);
        Require(table.Assign(bytes.data(), bytes.size(), 2), "DBC rejected before string validation");
        Require(!*table.String(0, 1), "Invalid string offset accepted");
        Put32(bytes, 24, 0);
        bytes.back() = 'X';
        Require(table.Assign(bytes.data(), bytes.size(), 2), "Unterminated string fixture rejected early");
        Require(!*table.String(0, 1), "Unterminated string escaped bounds");
        Put32(bytes, 4, UINT32_MAX);
        Require(!table.Assign(bytes.data(), bytes.size(), 2), "Overflowing row count accepted");

        AscensionRealm realm;
        Require(!realm.CanCreateClass(12), "Creation enabled before realm negotiation");
        std::vector<uint8_t> packet(47);
        packet[36] = 1;
        packet[42] = 1;
        packet[46] = 1;
        Require(realm.Read(packet.data(), packet.size()), "Valid realm packet rejected");
        Require(realm.flags[0] && realm.flags[6], "Realm flags read from wrong offset");
        Require(realm.CanCreateClass(12) && realm.CanCreateClass(32), "CoA classes excluded");
        Require(!realm.CanCreateClass(0) && !realm.CanCreateClass(33), "Invalid class accepted");
        Require(!realm.CanCreateClass(1) && !realm.CanCreateClass(10), "Wrong class model accepted");
        for (size_t i = 0; i < packet.size(); ++i) {
            Require(!realm.Read(packet.data(), i), "Truncated realm packet accepted");
        }
        packet[44] = 'x';
        packet[45] = 'y';
        Require(!realm.Read(packet.data(), packet.size()), "Missing string terminator accepted");
        Require(realm.flags[6], "Malformed packet replaced realm state");
        packet[44] = packet[45] = packet[42] = 0;
        packet[43] = 1;
        Require(realm.Read(packet.data(), packet.size()), "WCR realm rejected");
        Require(realm.CanCreateClass(1) && realm.CanCreateClass(11), "WCR classes excluded");
        Require(!realm.CanCreateClass(6) && !realm.CanCreateClass(12), "WCR class restriction lost");
        auto classCount = [&]() {
            uint32_t count = 0;
            for (uint32_t id = 1; id <= 32; ++id) { count += realm.CanCreateClass(id); }
            return count;
        };
        Require(classCount() == 9, "WCR must expose nine classes");
        packet[43] = 0;
        Require(realm.Read(packet.data(), packet.size()), "Hero realm rejected");
        Require(classCount() == 1 && realm.CanCreateClass(10), "Hero must expose only Hero");
        packet[42] = 1;
        Require(realm.Read(packet.data(), packet.size()), "CoA realm rejected");
        Require(classCount() == 21, "CoA must expose all twenty-one classes");
        for (uint32_t id = 12; id <= 32; ++id) { Require(realm.CanCreateClass(id), "CoA class missing"); }

        AscensionAddons addons;
        const uint8_t addonPacket[] = { 2, 0, 0, 0, 'U', 'I', 0, 1, 'A', 'd', 'd', 'o', 'n', 0, 0 };
        Require(addons.Read(addonPacket, sizeof(addonPacket)), "Valid addon list rejected");
        Require(addons.entries.size() == 2 && addons.entries[0].second && !addons.entries[1].second,
            "Addon security flags lost");
        for (size_t i = 0; i < sizeof(addonPacket); ++i) {
            Require(!addons.Read(addonPacket, i), "Truncated addon packet accepted");
        }
        Require(addons.entries.size() == 2, "Malformed packet replaced addon list");
        const uint8_t emptyAddons[] = { 0, 0, 0, 0 };
        Require(addons.Read(emptyAddons, sizeof(emptyAddons)) && addons.entries.empty(), "Empty addon list rejected");

        if (argc > 2 && std::string(argv[1]) == "--archive-tables") {
            const std::pair<const char*, const char*> fixtures[] = {
                { "Tutorial", "dddddddddddddddddddddddddddddddddddddLLLddddd" },
                { "TutorialCategories", "dL" }, { "TutorialRewards", "dddd" },
                { "TutorialObjectives", "dddddddL" }, { "TutorialKeywords", "ddddddLLLdddddddd" },
                { "MentorSpecializations", "ddL" }, { "TutorialObjectiveTypes", "ddL" },
                { "CharacterAdvancement", "dddddddddddddddddddddddddddddddddddddddddddddddLdLLbbddddbdddddddddddddddddbbbbbddddddddddddddddddddddddddddddddddddddddddddddddddd" },
                { "CharacterAdvancementClassTypes", "ddddddL" },
                { "CharacterAdvancementTabTypes", "ddL" }, { "CharacterAdvancementEssence", "ddddddddd" }
            };
            for (const auto& fixture : fixtures) {
                std::ifstream file(std::string(argv[2]) + "/" + fixture.first + ".dbc", std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
                Require(table.AssignFormatted(bytes.data(), bytes.size(), fixture.second), fixture.first);
                Require(table.Count() > 0, "Archive table contains no rows");
                if (std::string(fixture.first) == "Tutorial") {
                    Require(*table.String(0, 37), "Tutorial content missing after decoding");
                }
                std::cout << "Validated " << fixture.first << ": " << table.Count() << " rows\n";
            }
        } else if (argc > 1) {
            std::ifstream file(argv[1], std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
            Require(table.Assign(data.data(), data.size(), 65), "Local ChrSpecs.dbc layout mismatch");
            Require(table.Count() > 0, "Local class specs are empty");
            for (uint32_t row = 0; row < table.Count(); ++row) {
                Require(*table.String(row, 1) && *table.String(row, 2), "Local class/spec names missing");
                Require(*table.String(row, 29), "Local localized spec name missing");
            }
            std::cout << "Validated " << table.Count() << " local class specs\n";
        }
        std::cout << "Ascension compatibility parser tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
