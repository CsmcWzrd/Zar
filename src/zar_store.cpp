#include "zar_store.h"
#include "zar_utils.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string.h>

namespace zar {

namespace {

uint64_t ceilDiv(uint64_t n, uint64_t d) {
    return (n + d - 1) / d;
}

std::string sanitizeRelativePath(const std::string& input) {
    std::string s = input;
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s[0] == '/') s.erase(s.begin());
    while (s.find("..") != std::string::npos) s.replace(s.find(".."), 2, "__");
    if (s.empty()) s = "untitled.bin";
    return s;
}

void headerToDiskOrder(const Header& in, Header* out, ByteOrder fileOrder) {
    if (!out) return;
    *out = in;
    ByteOrder hostOrder = hostByteOrder();
    out->m_nextBlock = convert64(in.m_nextBlock, hostOrder, fileOrder);
    out->m_prevBlock = convert64(in.m_prevBlock, hostOrder, fileOrder);
    out->m_blockCount = convert64(in.m_blockCount, hostOrder, fileOrder);
    out->m_timestamp = convert64(in.m_timestamp, hostOrder, fileOrder);
    out->m_id = convert64(in.m_id, hostOrder, fileOrder);
    out->m_flags = convert64(in.m_flags, hostOrder, fileOrder);
    for (size_t i = 0; i < 14; ++i) {
        out->m_counters[i] = convert64(in.m_counters[i], hostOrder, fileOrder);
    }
    out->m_dataLength = convert64(in.m_dataLength, hostOrder, fileOrder);
    out->m_titleLength = convert32(in.m_titleLength, hostOrder, fileOrder);
    out->m_extra = convert32(in.m_extra, hostOrder, fileOrder);
}

void headerFromDiskOrder(const Header& in, Header* out, ByteOrder fileOrder) {
    if (!out) return;
    *out = in;
    ByteOrder hostOrder = hostByteOrder();
    out->m_nextBlock = convert64(in.m_nextBlock, fileOrder, hostOrder);
    out->m_prevBlock = convert64(in.m_prevBlock, fileOrder, hostOrder);
    out->m_blockCount = convert64(in.m_blockCount, fileOrder, hostOrder);
    out->m_timestamp = convert64(in.m_timestamp, fileOrder, hostOrder);
    out->m_id = convert64(in.m_id, fileOrder, hostOrder);
    out->m_flags = convert64(in.m_flags, fileOrder, hostOrder);
    for (size_t i = 0; i < 14; ++i) {
        out->m_counters[i] = convert64(in.m_counters[i], fileOrder, hostOrder);
    }
    out->m_dataLength = convert64(in.m_dataLength, fileOrder, hostOrder);
    out->m_titleLength = convert32(in.m_titleLength, fileOrder, hostOrder);
    out->m_extra = convert32(in.m_extra, fileOrder, hostOrder);
}

bool looksLikeReasonableHeader(const Header& h, uint64_t fileSize) {
    if (h.m_id == 0 || h.m_blockCount == 0) return false;
    if (h.m_titleLength > (kBlockSize - kHeaderSize)) return false;
    uint64_t capacity = kDataCapacityFirstBlock + (h.m_blockCount - 1) * kBlockSize;
    if (static_cast<uint64_t>(h.m_titleLength) + h.m_dataLength > capacity) return false;
    if (h.m_nextBlock != 0 && (h.m_nextBlock % kBlockSize != 0 || h.m_nextBlock >= fileSize + kBlockSize)) return false;
    if (h.m_prevBlock != 0 && (h.m_prevBlock % kBlockSize != 0 || h.m_prevBlock >= fileSize + kBlockSize)) return false;
    return true;
}

} // namespace

Store::Store() : m_nextId(1), m_fileByteOrder(kLittleEndian) {}
Store::~Store() { close(); }

void Store::setFileByteOrder(ByteOrder order) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fileByteOrder = order;
}

ByteOrder Store::getFileByteOrder() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_fileByteOrder;
}

ByteOrder Store::getHostByteOrder() const {
    return hostByteOrder();
}

bool Store::open(const std::string& path, bool createIfMissing) {
    std::lock_guard<std::mutex> lock(m_mutex);
    close();

    m_path = path;
    m_file.open(path.c_str(), std::ios::in | std::ios::out | std::ios::binary);

    if (!m_file && createIfMissing) {
        m_file.clear();
        std::ofstream create(path.c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!create) return false;
        std::vector<char> zeros(static_cast<size_t>(kBlockSize), 0);
        create.write(&zeros[0], static_cast<std::streamsize>(zeros.size()));
        create.close();
        m_file.open(path.c_str(), std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!m_file) return false;

    std::string error;
    if (!loadIndices(&error)) {
        if (!rebuildIndices(&error)) return false;
    }

    uint64_t maxId = 0;
    for (size_t i = 0; i < m_indices.size(); ++i) {
        Header h;
        if (readHeaderAt(m_indices[i], &h, &error) && h.m_id > maxId) maxId = h.m_id;
    }
    m_nextId = maxId + 1;
    return true;
}

bool Store::save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string error;
    if (!flushAllIndexBlocks(&error)) return false;
    m_file.flush();
    return m_file.good();
}

void Store::close() {
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
    m_indices.clear();
    m_path.clear();
    m_nextId = 1;
    m_streamState.payload.clear();
    m_streamState.cursor = 0;
}

bool Store::isOpen() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_file.is_open();
}

bool Store::readIndexEntryAt(uint64_t offset, uint64_t* value, std::string* error) {
    if (!value) return false;
    uint64_t raw = 0;
    m_file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    m_file.read(reinterpret_cast<char*>(&raw), sizeof(raw));
    if (!m_file.good()) {
        m_file.clear();
        if (error) *error = "failed to read index entry";
        return false;
    }
    *value = convert64(raw, m_fileByteOrder, hostByteOrder());
    return true;
}

bool Store::writeIndexEntryBlockAt(uint64_t offset, const std::vector<uint64_t>& entries, std::string* error) {
    std::vector<uint64_t> block(static_cast<size_t>(kEntriesPerIndexBlock), 0);
    const size_t n = std::min<size_t>(block.size(), entries.size());
    for (size_t i = 0; i < n; ++i) {
        block[i] = convert64(entries[i], hostByteOrder(), m_fileByteOrder);
    }
    m_file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    m_file.write(reinterpret_cast<const char*>(&block[0]), static_cast<std::streamsize>(kBlockSize));
    if (!m_file.good()) {
        if (error) *error = "failed to write index block";
        return false;
    }
    return true;
}

bool Store::loadIndices(std::string* error) {
    m_indices.clear();

    m_file.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(m_file.tellg());
    if (fileSize < kBlockSize) {
        if (error) *error = "file too small";
        return false;
    }

    uint64_t pos = 0;
    std::vector<uint64_t> all;
    while (pos + kBlockSize <= fileSize) {
        bool anyNonZero = false;
        for (uint64_t i = 0; i < kEntriesPerIndexBlock; ++i) {
            uint64_t entry = 0;
            if (!readIndexEntryAt(pos + i * kIndexEntrySize, &entry, error)) break;
            if (entry != 0) {
                anyNonZero = true;
                if (entry % kBlockSize != 0 || entry >= fileSize) {
                    if (error) *error = "invalid index entry found";
                    return false;
                }
                all.push_back(entry);
            }
        }
        if (!anyNonZero) break;
        pos += kBlockSize;
    }

    m_indices = all;
    return true;
}

bool Store::rebuildIndices(std::string* error) {
    m_indices.clear();

    m_file.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(m_file.tellg());
    if (fileSize < kBlockSize) return true;

    uint64_t indexBlockCount = 1;
    while (true) {
        uint64_t dataStart = indexBlockCount * kBlockSize;
        if (dataStart >= fileSize) break;
        bool found = false;
        uint64_t offset = dataStart;
        while (offset + kBlockSize <= fileSize) {
            Header h;
            if (!readHeaderAt(offset, &h, error)) break;
            if (looksLikeReasonableHeader(h, fileSize)) {
                found = true;
                break;
            }
            offset += kBlockSize;
        }
        if (!found) break;

        offset = dataStart;
        while (offset + kBlockSize <= fileSize) {
            Header h;
            if (!readHeaderAt(offset, &h, error)) break;
            if (looksLikeReasonableHeader(h, fileSize)) {
                m_indices.push_back(offset);
                offset += h.m_blockCount * kBlockSize;
            } else {
                offset += kBlockSize;
            }
        }
        break;
    }

    return flushAllIndexBlocks(error);
}

bool Store::writeIndexBlock(uint64_t indexBlockNumber, const std::vector<uint64_t>& entries, std::string* error) {
    const uint64_t offset = indexBlockNumber * kBlockSize;
    return writeIndexEntryBlockAt(offset, entries, error);
}

bool Store::flushAllIndexBlocks(std::string* error) {
    uint64_t blocksNeeded = std::max<uint64_t>(1, ceilDiv(static_cast<uint64_t>(m_indices.size()), kEntriesPerIndexBlock));

    for (uint64_t b = 0; b < blocksNeeded; ++b) {
        size_t begin = static_cast<size_t>(b * kEntriesPerIndexBlock);
        size_t end = static_cast<size_t>(std::min<uint64_t>(begin + kEntriesPerIndexBlock, m_indices.size()));
        std::vector<uint64_t> slice;
        if (begin < end) slice.assign(m_indices.begin() + begin, m_indices.begin() + end);
        if (!writeIndexBlock(b, slice, error)) return false;
    }

    return true;
}

bool Store::readHeaderAt(uint64_t offset, Header* header, std::string* error) {
    if (!header) return false;
    Header diskHeader;
    m_file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    m_file.read(reinterpret_cast<char*>(&diskHeader), sizeof(diskHeader));
    if (!m_file.good()) {
        m_file.clear();
        if (error) *error = "failed to read header";
        return false;
    }
    headerFromDiskOrder(diskHeader, header, m_fileByteOrder);
    return true;
}

bool Store::writeHeaderAt(uint64_t offset, const Header& header, std::string* error) {
    Header diskHeader;
    headerToDiskOrder(header, &diskHeader, m_fileByteOrder);
    m_file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    m_file.write(reinterpret_cast<const char*>(&diskHeader), sizeof(diskHeader));
    if (!m_file.good()) {
        if (error) *error = "failed to write header";
        return false;
    }
    return true;
}

bool Store::readRecordAt(uint64_t offset, Record* record, std::string* error) {
    if (!record) return false;
    Header header;
    if (!readHeaderAt(offset, &header, error)) return false;
    if (header.m_blockCount == 0) {
        if (error) *error = "invalid block count";
        return false;
    }

    uint64_t totalPayload = static_cast<uint64_t>(header.m_titleLength) + header.m_dataLength;
    uint64_t capacity = kDataCapacityFirstBlock + (header.m_blockCount - 1) * kBlockSize;
    if (totalPayload > capacity) {
        if (error) *error = "payload exceeds block capacity";
        return false;
    }

    std::vector<uint8_t> payload(static_cast<size_t>(totalPayload), 0);
    uint64_t copied = 0;

    uint64_t firstRead = std::min<uint64_t>(totalPayload, kDataCapacityFirstBlock);
    if (firstRead) {
        m_file.seekg(static_cast<std::streamoff>(offset + kHeaderSize), std::ios::beg);
        m_file.read(reinterpret_cast<char*>(&payload[0]), static_cast<std::streamsize>(firstRead));
        if (!m_file.good()) {
            m_file.clear();
            if (error) *error = "failed to read first block payload";
            return false;
        }
        copied += firstRead;
    }

    for (uint64_t block = 1; block < header.m_blockCount && copied < totalPayload; ++block) {
        uint64_t chunk = std::min<uint64_t>(kBlockSize, totalPayload - copied);
        m_file.seekg(static_cast<std::streamoff>(offset + block * kBlockSize), std::ios::beg);
        m_file.read(reinterpret_cast<char*>(&payload[static_cast<size_t>(copied)]), static_cast<std::streamsize>(chunk));
        if (!m_file.good()) {
            m_file.clear();
            if (error) *error = "failed to read continuation block";
            return false;
        }
        copied += chunk;
    }

    if (!verifyChecksum(payload, header.m_checksum)) {
        if (error) *error = "checksum verification failed";
        return false;
    }

    record->offset = offset;
    record->header = header;
    record->title.assign(reinterpret_cast<const char*>(&payload[0]), reinterpret_cast<const char*>(&payload[0]) + header.m_titleLength);
    record->data.assign(payload.begin() + header.m_titleLength, payload.end());
    return true;
}

bool Store::writeRecord(const Header& sourceHeader, const std::string& title, const std::vector<uint8_t>& data,
                        uint64_t* firstBlockOffset, std::string* error) {
    std::vector<uint8_t> payload(title.begin(), title.end());
    payload.insert(payload.end(), data.begin(), data.end());

    uint64_t blockCount = 1;
    if (payload.size() > kDataCapacityFirstBlock) {
        uint64_t remain = static_cast<uint64_t>(payload.size()) - kDataCapacityFirstBlock;
        blockCount += ceilDiv(remain, kBlockSize);
    }

    Header header = sourceHeader;
    header.m_blockCount = blockCount;
    header.m_titleLength = static_cast<uint32_t>(title.size());
    header.m_dataLength = static_cast<uint64_t>(data.size());
    computeChecksum(payload, header.m_checksum);

    m_file.seekp(0, std::ios::end);
    uint64_t offset = static_cast<uint64_t>(m_file.tellp());
    if (offset % kBlockSize != 0) {
        uint64_t pad = kBlockSize - (offset % kBlockSize);
        std::vector<char> zeros(static_cast<size_t>(pad), 0);
        m_file.write(&zeros[0], static_cast<std::streamsize>(zeros.size()));
        offset += pad;
    }

    header.m_prevBlock = 0;
    header.m_nextBlock = blockCount > 1 ? offset + kBlockSize : 0;

    std::vector<uint8_t> firstPayload;
    uint64_t firstWrite = std::min<uint64_t>(payload.size(), kDataCapacityFirstBlock);
    firstPayload.assign(payload.begin(), payload.begin() + firstWrite);

    if (!writeHeaderAt(offset, header, error)) return false;
    if (!firstPayload.empty()) {
        m_file.write(reinterpret_cast<const char*>(&firstPayload[0]), static_cast<std::streamsize>(firstPayload.size()));
    }
    if (firstPayload.size() < kDataCapacityFirstBlock) {
        std::vector<char> pad(static_cast<size_t>(kDataCapacityFirstBlock - firstPayload.size()), 0);
        m_file.write(&pad[0], static_cast<std::streamsize>(pad.size()));
    }

    uint64_t copied = firstWrite;
    for (uint64_t block = 1; block < blockCount; ++block) {
        uint64_t thisOffset = offset + block * kBlockSize;
        uint64_t chunk = std::min<uint64_t>(kBlockSize, payload.size() - copied);
        m_file.seekp(static_cast<std::streamoff>(thisOffset), std::ios::beg);
        m_file.write(reinterpret_cast<const char*>(&payload[static_cast<size_t>(copied)]), static_cast<std::streamsize>(chunk));
        if (chunk < kBlockSize) {
            std::vector<char> pad(static_cast<size_t>(kBlockSize - chunk), 0);
            m_file.write(&pad[0], static_cast<std::streamsize>(pad.size()));
        }
        copied += chunk;
    }

    if (!m_file.good()) {
        if (error) *error = "failed to write record";
        return false;
    }

    if (firstBlockOffset) *firstBlockOffset = offset;
    return true;
}

bool Store::append(const Header& headerTemplate,
                   const std::string& title,
                   const std::vector<uint8_t>& data,
                   uint64_t* outId,
                   std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open()) {
        if (error) *error = "store is not open";
        return false;
    }

    Header h = headerTemplate;
    h.m_id = m_nextId++;
    if (h.m_timestamp == 0) {
        h.m_timestamp = static_cast<uint64_t>(std::time(NULL));
    }

    uint64_t offset = 0;
    if (!writeRecord(h, title, data, &offset, error)) return false;
    m_indices.push_back(offset);
    if (!flushAllIndexBlocks(error)) return false;
    if (!m_file.good()) return false;
    if (outId) *outId = h.m_id;
    return true;
}
bool Store::matches(const Header& header, const Query& query) const {
    if (query.timestamp.enabled && (header.m_timestamp < query.timestamp.min || header.m_timestamp > query.timestamp.max)) return false;
    if (query.id.enabled && (header.m_id < query.id.min || header.m_id > query.id.max)) return false;
    if (query.hasFlags && header.m_flags != query.flags) return false;
    if (query.hasExtra && header.m_extra != query.extra) return false;
    if (query.hasChecksum && ::memcmp(header.m_checksum, query.checksum, 64) != 0) return false;
    if (query.hasAdditional && ::memcmp(header.m_additional, query.additional, 16) != 0) return false;
    return true;
}

std::string Store::recordToJson(const Record& record) const {
    std::ostringstream oss;
    std::vector<uint8_t> titleBytes(record.title.begin(), record.title.end());
    std::vector<uint8_t> payload(titleBytes);
    payload.insert(payload.end(), record.data.begin(), record.data.end());

    oss << "{";
    oss << "\"offset\":" << record.offset << ",";
    oss << "\"header\":{";
    oss << "\"nextBlock\":" << record.header.m_nextBlock << ",";
    oss << "\"prevBlock\":" << record.header.m_prevBlock << ",";
    oss << "\"blockCount\":" << record.header.m_blockCount << ",";
    oss << "\"timestamp\":" << record.header.m_timestamp << ",";
    oss << "\"id\":" << record.header.m_id << ",";
    oss << "\"flags\":" << record.header.m_flags << ",";
    oss << "\"counters\":[";
    for (size_t i = 0; i < 14; ++i) {
        if (i) oss << ",";
        oss << record.header.m_counters[i];
    }
    oss << "],";
    oss << "\"checksum\":\"" << bytesToHex(record.header.m_checksum, 64) << "\",";
    oss << "\"additional\":\"" << bytesToHex(record.header.m_additional, 16) << "\",";
    oss << "\"dataLength\":" << record.header.m_dataLength << ",";
    oss << "\"titleLength\":" << record.header.m_titleLength << ",";
    oss << "\"extra\":" << record.header.m_extra;
    oss << "},";
    oss << "\"title\":\"" << jsonEscape(record.title) << "\",";
    oss << "\"binary\":\"" << base64Encode(record.data) << "\",";
    oss << "\"titleAndData\":\"" << base64Encode(payload) << "\"";
    oss << "}";
    return oss.str();
}

std::string Store::recordsToJson(const std::vector<Record>& records) const {
    std::ostringstream oss;
    oss << "{\"count\":" << records.size() << ",\"records\":[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i) oss << ",";
        oss << recordToJson(records[i]);
    }
    oss << "]}";
    return oss.str();
}

std::vector<Store::Record> Store::selectRecords(const Query& query, Direction direction, Cardinality cardinality, std::string* error) {
    std::vector<Record> records;
    if (direction == kForward) {
        for (size_t i = 0; i < m_indices.size(); ++i) {
            Record r;
            if (!readRecordAt(m_indices[i], &r, error)) continue;
            if (!matches(r.header, query)) continue;
            records.push_back(r);
            if (cardinality == kOne) break;
        }
    } else {
        for (size_t i = m_indices.size(); i > 0; --i) {
            Record r;
            if (!readRecordAt(m_indices[i - 1], &r, error)) continue;
            if (!matches(r.header, query)) continue;
            records.push_back(r);
            if (cardinality == kOne) break;
        }
    }
    return records;
}

size_t Store::streamJson(const std::string& payload, char* buffer, size_t bufferLen) {
    if (m_streamState.payload != payload) {
        m_streamState.payload = payload;
        m_streamState.cursor = 0;
    }
    if (m_streamState.cursor >= m_streamState.payload.size()) {
        return 0;
    }
    if (!buffer || bufferLen == 0) return payload.size() - m_streamState.cursor;
    size_t remain = m_streamState.payload.size() - m_streamState.cursor;
    size_t n = std::min(remain, bufferLen);
    if (n) {
        ::memcpy(buffer, m_streamState.payload.data() + m_streamState.cursor, n);
        m_streamState.cursor += n;
    }
    return n;
}

JsonFileResult Store::selectToJsonFile(const Query& query, Direction direction, Cardinality cardinality, const std::string& outputPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    JsonFileResult result;
    if (!m_file.is_open()) {
        result.error = "store is not open";
        return result;
    }
    std::string error;
    std::vector<Record> records = selectRecords(query, direction, cardinality, &error);
    std::string json = recordsToJson(records);
    result.path = outputPath.empty() ? tempJsonPath("zar_select") : outputPath;
    std::vector<uint8_t> bytes(json.begin(), json.end());
    if (!writeBinaryFile(result.path, bytes, &error)) {
        result.error = error;
        return result;
    }
    result.ok = true;
    return result;
}

size_t Store::SelectOne(const Query& query, char* buffer, size_t bufferLen) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string error;
    std::string payload = recordsToJson(selectRecords(query, kForward, kOne, &error));
    return streamJson(payload, buffer, bufferLen);
}

JsonFileResult Store::SelectOneToJsonFile(const Query& query, const std::string& outputPath) {
    return selectToJsonFile(query, kForward, kOne, outputPath);
}

size_t Store::SelectAll(const Query& query, char* buffer, size_t bufferLen) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string error;
    std::string payload = recordsToJson(selectRecords(query, kForward, kAll, &error));
    return streamJson(payload, buffer, bufferLen);
}

JsonFileResult Store::SelectAllToJsonFile(const Query& query, const std::string& outputPath) {
    return selectToJsonFile(query, kForward, kAll, outputPath);
}

size_t Store::SelectReverseOne(const Query& query, char* buffer, size_t bufferLen) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string error;
    std::string payload = recordsToJson(selectRecords(query, kReverse, kOne, &error));
    return streamJson(payload, buffer, bufferLen);
}

JsonFileResult Store::SelectReverseOneToJsonFile(const Query& query, const std::string& outputPath) {
    return selectToJsonFile(query, kReverse, kOne, outputPath);
}

size_t Store::SelectReverseAll(const Query& query, char* buffer, size_t bufferLen) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string error;
    std::string payload = recordsToJson(selectRecords(query, kReverse, kAll, &error));
    return streamJson(payload, buffer, bufferLen);
}

JsonFileResult Store::SelectReverseAllToJsonFile(const Query& query, const std::string& outputPath) {
    return selectToJsonFile(query, kReverse, kAll, outputPath);
}

bool Store::extractRecords(const std::vector<Record>& records, const std::string& outputRoot, bool oneOnly, std::string* error) {
    for (size_t i = 0; i < records.size(); ++i) {
        std::string rel = sanitizeRelativePath(records[i].title);
        std::string full = joinPath(outputRoot, rel);
        if (!writeBinaryFile(full, records[i].data, error)) return false;
        if (oneOnly) break;
    }
    return true;
}

bool Store::ExtractOne(const Query& query, const std::string& outputRoot, std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open()) {
        if (error) *error = "store is not open";
        return false;
    }
    std::vector<Record> records = selectRecords(query, kForward, kOne, error);
    return extractRecords(records, outputRoot, true, error);
}

bool Store::ExtractAll(const Query& query, const std::string& outputRoot, std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open()) {
        if (error) *error = "store is not open";
        return false;
    }
    std::vector<Record> records = selectRecords(query, kForward, kAll, error);
    return extractRecords(records, outputRoot, false, error);
}

std::vector<uint64_t> Store::getIndices() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_indices;
}

} // namespace zar
