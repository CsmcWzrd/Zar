#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace zar {

enum ByteOrder {
    kLittleEndian = 0,
    kBigEndian = 1
};

static const uint64_t kBlockSize = 8192ULL;
static const uint64_t kHeaderSize = 256ULL;
static const uint64_t kIndexEntrySize = sizeof(uint64_t);
static const uint64_t kEntriesPerIndexBlock = kBlockSize / kIndexEntrySize; // 1024
static const uint64_t kDataCapacityFirstBlock = kBlockSize - kHeaderSize;

struct Range64 {
    bool enabled;
    uint64_t min;
    uint64_t max;
    Range64() : enabled(false), min(0), max(0) {}
};

struct Query {
    Range64 timestamp;
    Range64 id;

    bool hasFlags;
    uint64_t flags;

    bool hasChecksum;
    uint8_t checksum[64];

    bool hasAdditional;
    uint8_t additional[16];

    bool hasExtra;
    uint32_t extra;

    Query() : hasFlags(false), flags(0), hasChecksum(false), hasAdditional(false), hasExtra(false), extra(0) {}
};

struct JsonFileResult {
    bool ok;
    std::string path;
    std::string error;
    JsonFileResult() : ok(false) {}
};

} // namespace zar
