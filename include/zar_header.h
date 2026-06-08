#pragma once

#include "zar_types.h"
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace zar {

#pragma pack(push, 1)
struct Header {
    uint64_t m_nextBlock;
    uint64_t m_prevBlock;
    uint64_t m_blockCount;
    uint64_t m_timestamp;
    uint64_t m_id;
    uint64_t m_flags;

    uint64_t m_counters[14];
    uint8_t  m_checksum[64];
    uint8_t  m_additional[16];

    uint64_t m_dataLength;
    uint32_t m_titleLength;
    uint32_t m_extra;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 256, "Header must be exactly 256 bytes");

class HeaderView {
public:
    HeaderView();
    explicit HeaderView(const Header& value);

    const Header& raw() const;
    Header& raw();

    uint64_t getNextBlock() const;
    void setNextBlock(uint64_t v);

    uint64_t getPrevBlock() const;
    void setPrevBlock(uint64_t v);

    uint64_t getBlockCount() const;
    void setBlockCount(uint64_t v);

    uint64_t getTimestamp() const;
    void setTimestamp(uint64_t v);

    uint64_t getId() const;
    void setId(uint64_t v);

    uint64_t getFlags() const;
    void setFlags(uint64_t v);

    uint64_t getCounter(size_t idx) const;
    void setCounter(size_t idx, uint64_t v);

    std::vector<uint8_t> getChecksum() const;
    void setChecksum(const uint8_t* data, size_t len);

    std::vector<uint8_t> getAdditional() const;
    void setAdditional(const uint8_t* data, size_t len);

    uint64_t getDataLength() const;
    void setDataLength(uint64_t v);

    uint32_t getTitleLength() const;
    void setTitleLength(uint32_t v);

    uint32_t getExtra() const;
    void setExtra(uint32_t v);

    void clear();

private:
    Header m_header;
};

} // namespace zar
