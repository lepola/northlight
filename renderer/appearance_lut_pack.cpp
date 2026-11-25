#include "sky/celestial_profiles.h"
#include "sky/grade_lut_pack.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using KeyBytes = std::array<uint8_t, NorthlightGradeLut::Pack::KeyBytes>;
constexpr size_t VolumeWords = size_t(NorthlightTone::CubeSize) * NorthlightTone::CubeSize *
    NorthlightTone::CubeSize * 4;

template<class Value>
void writeLe(std::ofstream& output, Value value) {
    for (unsigned i = 0; i < sizeof(Value); ++i) {
        output.put(char(uint8_t(uint64_t(value) >> (i * 8))));
    }
}

struct Record {
    uint64_t hash = 0;
    KeyBytes key{};
    std::vector<uint16_t> rgba;
};
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: appearance_lut_pack INPUT.ini OUTPUT.raw\n";
        return 2;
    }
    std::ifstream source(argv[1], std::ios::binary);
    if (!source) {
        std::cerr << "cannot read " << argv[1] << '\n';
        return 1;
    }
    std::string text, error;
    if (!NorthlightCelestialProfiles::readIniText(source, text, error)) {
        std::cerr << "invalid appearance INI: " << error << '\n';
        return 1;
    }
    std::istringstream decoded(text);
    NorthlightCelestialProfiles::Table table;
    if (!NorthlightCelestialProfiles::parse(decoded, table, error)) {
        std::cerr << "invalid appearance INI: " << error << '\n';
        return 1;
    }
    std::map<KeyBytes, Record> unique;
    auto addProfile = [&](const NorthlightCelestialProfiles::Profile& profile) {
        const auto keys = NorthlightTone::makeBakeKeys(profile.grade, profile.toneStrength);
        for (const auto& key : keys) {
            if (!key.strength) continue;
            const auto encoded = NorthlightGradeLut::Pack::encodeKey(key);
            const uint64_t hash = key.stableHash();
            if (NorthlightTone::stableHash(NorthlightGradeLut::Pack::decodeKey(encoded.data())) != hash)
                throw std::runtime_error("BakeKey stableHash serialization mismatch");
            if (unique.count(encoded)) continue;
            if (unique.size() >= NorthlightGradeLut::Pack::MaxEntries)
                throw std::runtime_error("Vibrant palette exceeds the runtime LUT pack entry limit");
            Record record;
            record.hash = hash;
            record.key = encoded;
            record.rgba.resize(VolumeWords);
            const unsigned n = NorthlightTone::CubeSize;
            NorthlightTone::bakeColourVolume(record.rgba.data(), n * 4 * sizeof(uint16_t),
                n * n * 4 * sizeof(uint16_t), key);
            unique.emplace(encoded, std::move(record));
        }
    };
    addProfile(table.fallback);
    for (const auto& row : table.mapFallbacks) addProfile(row.second);
    for (const auto& row : table.zones) addProfile(row.second);

    std::vector<const Record*> records;
    records.reserve(unique.size());
    for (const auto& row : unique) records.push_back(&row.second);
    std::sort(records.begin(), records.end(), [](const Record* a, const Record* b) {
        return a->hash != b->hash ? a->hash < b->hash : a->key < b->key;
    });

    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "cannot write " << argv[2] << '\n';
        return 1;
    }
    output.write("NLRAW001", 8);
    writeLe(output, uint16_t(1));
    writeLe(output, uint16_t(NorthlightTone::BakeMathSchemaVersion));
    writeLe(output, uint32_t(records.size()));
    for (const Record* record : records) {
        writeLe(output, record->hash);
        output.write(reinterpret_cast<const char*>(record->key.data()), record->key.size());
        for (uint16_t value : record->rgba) writeLe(output, value);
    }
    if (!output) {
        std::cerr << "failed writing raw LUT tables\n";
        return 1;
    }
    return 0;
}
