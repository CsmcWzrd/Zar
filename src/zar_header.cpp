#include "zar_header.h"
#include <stdexcept>

namespace zar {

HeaderView::HeaderView() {
    clear();
}

HeaderView::HeaderView(const Header& value) : m_header(value) {}

const Header& HeaderView::raw() const { return m_header; }
Header& HeaderView::raw() { return m_header; }

uint64_t HeaderView::getNextBlock() const { return m_header.m_nextBlock; }
void HeaderView::setNextBlock(uint64_t v) { m_header.m_nextBlock = v; }

uint64_t HeaderView::getPrevBlock() const { return m_header.m_prevBlock; }
void HeaderView::setPrevBlock(uint64_t v) { m_header.m_prevBlock = v; }

uint64_t HeaderView::getBlockCount() const { return m_header.m_blockCount; }
void HeaderView::setBlockCount(uint64_t v) { m_header.m_blockCount = v; }

uint64_t HeaderView::getTimestamp() const { return m_header.m_timestamp; }
void HeaderView::setTimestamp(uint64_t v) { m_header.m_timestamp = v; }

uint64_t HeaderView::getId() const { return m_header.m_id; }
void HeaderView::setId(uint64_t v) { m_header.m_id = v; }

uint64_t HeaderView::getFlags() const { return m_header.m_flags; }
void HeaderView::setFlags(uint64_t v) { m_header.m_flags = v; }

uint64_t HeaderView::getCounter(size_t idx) const {
    if (idx >= 14) throw std::out_of_range("counter index");
    return m_header.m_counters[idx];
}
void HeaderView::setCounter(size_t idx, uint64_t v) {
    if (idx >= 14) throw std::out_of_range("counter index");
    m_header.m_counters[idx] = v;
}

std::vector<uint8_t> HeaderView::getChecksum() const {
    return std::vector<uint8_t>(m_header.m_checksum, m_header.m_checksum + 64);
}
void HeaderView::setChecksum(const uint8_t* data, size_t len) {
    ::memset(m_header.m_checksum, 0, sizeof(m_header.m_checksum));
    if (data && len) {
        ::memcpy(m_header.m_checksum, data, len > 64 ? 64 : len);
    }
}

std::vector<uint8_t> HeaderView::getAdditional() const {
    return std::vector<uint8_t>(m_header.m_additional, m_header.m_additional + 16);
}
void HeaderView::setAdditional(const uint8_t* data, size_t len) {
    ::memset(m_header.m_additional, 0, sizeof(m_header.m_additional));
    if (data && len) {
        ::memcpy(m_header.m_additional, data, len > 16 ? 16 : len);
    }
}

uint64_t HeaderView::getDataLength() const { return m_header.m_dataLength; }
void HeaderView::setDataLength(uint64_t v) { m_header.m_dataLength = v; }

uint32_t HeaderView::getTitleLength() const { return m_header.m_titleLength; }
void HeaderView::setTitleLength(uint32_t v) { m_header.m_titleLength = v; }

uint32_t HeaderView::getExtra() const { return m_header.m_extra; }
void HeaderView::setExtra(uint32_t v) { m_header.m_extra = v; }

void HeaderView::clear() {
    ::memset(&m_header, 0, sizeof(m_header));
}

} // namespace zar
