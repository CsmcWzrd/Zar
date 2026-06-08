#pragma once

#include "zar_header.h"
#include "zar_types.h"
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace zar {

class Store {
public:
    Store();
    ~Store();

    bool open(const std::string& path, bool createIfMissing = true);

    void setFileByteOrder(ByteOrder order);
    ByteOrder getFileByteOrder() const;
    ByteOrder getHostByteOrder() const;
    bool save();
    void close();
    bool isOpen() const;

    bool append(const Header& headerTemplate,
                const std::string& title,
                const std::vector<uint8_t>& data,
                uint64_t* outId,
                std::string* error);

    size_t SelectOne(const Query& query, char* buffer, size_t bufferLen);
    JsonFileResult SelectOneToJsonFile(const Query& query, const std::string& outputPath = "");

    size_t SelectAll(const Query& query, char* buffer, size_t bufferLen);
    JsonFileResult SelectAllToJsonFile(const Query& query, const std::string& outputPath = "");

    size_t SelectReverseOne(const Query& query, char* buffer, size_t bufferLen);
    JsonFileResult SelectReverseOneToJsonFile(const Query& query, const std::string& outputPath = "");

    size_t SelectReverseAll(const Query& query, char* buffer, size_t bufferLen);
    JsonFileResult SelectReverseAllToJsonFile(const Query& query, const std::string& outputPath = "");

    bool ExtractOne(const Query& query, const std::string& outputRoot, std::string* error);
    bool ExtractAll(const Query& query, const std::string& outputRoot, std::string* error);

    std::vector<uint64_t> getIndices() const;

private:
    struct Record {
        uint64_t offset;
        Header header;
        std::string title;
        std::vector<uint8_t> data;
    };

    struct StreamState {
        std::string payload;
        size_t cursor;
        StreamState() : cursor(0) {}
    };

    enum Direction {
        kForward,
        kReverse
    };

    enum Cardinality {
        kOne,
        kAll
    };

    mutable std::mutex m_mutex;
    std::fstream m_file;
    std::string m_path;
    std::vector<uint64_t> m_indices;
    uint64_t m_nextId;
    ByteOrder m_fileByteOrder;
    StreamState m_streamState;

    bool loadIndices(std::string* error);
    bool rebuildIndices(std::string* error);
    bool writeIndexBlock(uint64_t indexBlockNumber, const std::vector<uint64_t>& entries, std::string* error);
    bool flushAllIndexBlocks(std::string* error);

    bool readHeaderAt(uint64_t offset, Header* header, std::string* error);
    bool writeHeaderAt(uint64_t offset, const Header& header, std::string* error);
    bool readIndexEntryAt(uint64_t offset, uint64_t* value, std::string* error);
    bool writeIndexEntryBlockAt(uint64_t offset, const std::vector<uint64_t>& entries, std::string* error);
    bool readRecordAt(uint64_t offset, Record* record, std::string* error);
    bool writeRecord(const Header& header, const std::string& title, const std::vector<uint8_t>& data,
                     uint64_t* firstBlockOffset, std::string* error);

    bool matches(const Header& header, const Query& query) const;
    std::string recordToJson(const Record& record) const;
    std::string recordsToJson(const std::vector<Record>& records) const;
    std::vector<Record> selectRecords(const Query& query, Direction direction, Cardinality cardinality, std::string* error);

    size_t streamJson(const std::string& payload, char* buffer, size_t bufferLen);
    JsonFileResult selectToJsonFile(const Query& query, Direction direction, Cardinality cardinality, const std::string& outputPath);

    bool extractRecords(const std::vector<Record>& records, const std::string& outputRoot, bool oneOnly, std::string* error);
};

} // namespace zar
