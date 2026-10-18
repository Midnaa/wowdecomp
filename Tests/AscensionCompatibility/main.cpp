#include "WoW/Source/UIUtil/AscensionDBC.h"
#include "WoW/Source/UIUtil/AscensionRealm.h"
#include "WoW/Source/UIUtil/AscensionAddons.h"
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

int main(int argc, char** argv) {
    try {
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

        if (argc > 1) {
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
