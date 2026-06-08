#include "zar_header.h"
#include "zar_types.h"
#include "zar_utils.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

namespace {

using namespace zar;

struct RecordData {
    Header header;
    std::string title;
    std::vector<uint8_t> data;
};

static void headerFromDiskOrder(const Header& in, Header* out, ByteOrder fileOrder) {
    if (!out) return;
    *out = in;
    ByteOrder hostOrder = hostByteOrder();
    out->m_nextBlock = convert64(in.m_nextBlock, fileOrder, hostOrder);
    out->m_prevBlock = convert64(in.m_prevBlock, fileOrder, hostOrder);
    out->m_blockCount = convert64(in.m_blockCount, fileOrder, hostOrder);
    out->m_timestamp = convert64(in.m_timestamp, fileOrder, hostOrder);
    out->m_id = convert64(in.m_id, fileOrder, hostOrder);
    out->m_flags = convert64(in.m_flags, fileOrder, hostOrder);
    for (size_t i = 0; i < 14; ++i) out->m_counters[i] = convert64(in.m_counters[i], fileOrder, hostOrder);
    out->m_dataLength = convert64(in.m_dataLength, fileOrder, hostOrder);
    out->m_titleLength = convert32(in.m_titleLength, fileOrder, hostOrder);
    out->m_extra = convert32(in.m_extra, fileOrder, hostOrder);
}

static void headerToDiskOrder(const Header& in, Header* out, ByteOrder fileOrder) {
    if (!out) return;
    *out = in;
    ByteOrder hostOrder = hostByteOrder();
    out->m_nextBlock = convert64(in.m_nextBlock, hostOrder, fileOrder);
    out->m_prevBlock = convert64(in.m_prevBlock, hostOrder, fileOrder);
    out->m_blockCount = convert64(in.m_blockCount, hostOrder, fileOrder);
    out->m_timestamp = convert64(in.m_timestamp, hostOrder, fileOrder);
    out->m_id = convert64(in.m_id, hostOrder, fileOrder);
    out->m_flags = convert64(in.m_flags, hostOrder, fileOrder);
    for (size_t i = 0; i < 14; ++i) out->m_counters[i] = convert64(in.m_counters[i], hostOrder, fileOrder);
    out->m_dataLength = convert64(in.m_dataLength, hostOrder, fileOrder);
    out->m_titleLength = convert32(in.m_titleLength, hostOrder, fileOrder);
    out->m_extra = convert32(in.m_extra, hostOrder, fileOrder);
}

static bool parseByteOrder(const std::string& text, ByteOrder* out) {
    if (!out) return false;
    std::string s(text);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "little" || s == "le" || s == "little-endian") {
        *out = kLittleEndian;
        return true;
    }
    if (s == "big" || s == "be" || s == "big-endian") {
        *out = kBigEndian;
        return true;
    }
    return false;
}

static std::string byteOrderName(ByteOrder order) {
    return order == kLittleEndian ? "little" : "big";
}

static uint64_t ceilDiv(uint64_t a, uint64_t b) {
    return b == 0 ? 0 : ((a + b - 1) / b);
}

static bool readFileSize(std::fstream& file, uint64_t* out) {
    file.seekg(0, std::ios::end);
    std::streampos end = file.tellg();
    if (end < 0) return false;
    *out = static_cast<uint64_t>(end);
    return true;
}


static bool looksLikeReasonableHeader(const Header& h, uint64_t fileSize) {
    if (h.m_id == 0 || h.m_blockCount == 0) return false;
    if (h.m_titleLength > (kBlockSize - kHeaderSize)) return false;
    uint64_t capacity = kDataCapacityFirstBlock + (h.m_blockCount - 1) * kBlockSize;
    if (static_cast<uint64_t>(h.m_titleLength) + h.m_dataLength > capacity) return false;
    if (h.m_nextBlock != 0 && (h.m_nextBlock % kBlockSize != 0 || h.m_nextBlock >= fileSize + kBlockSize)) return false;
    if (h.m_prevBlock != 0 && (h.m_prevBlock % kBlockSize != 0 || h.m_prevBlock >= fileSize + kBlockSize)) return false;
    return true;
}

static bool rebuildIndicesByScan(std::fstream& file,
                                 ByteOrder order,
                                 std::vector<uint64_t>* outIndices,
                                 std::string* error) {
    outIndices->clear();
    uint64_t fileSize = 0;
    if (!readFileSize(file, &fileSize)) {
        if (error) *error = "failed to determine input file size";
        return false;
    }
    if (fileSize < kBlockSize) return true;

    uint64_t indexBlockCount = 1;
    while (true) {
        uint64_t dataStart = indexBlockCount * kBlockSize;
        if (dataStart >= fileSize) break;
        bool found = false;
        uint64_t offset = dataStart;
        while (offset + kBlockSize <= fileSize) {
            Header diskHeader;
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            file.read(reinterpret_cast<char*>(&diskHeader), sizeof(diskHeader));
            if (!file.good()) {
                file.clear();
                break;
            }
            Header header;
            headerFromDiskOrder(diskHeader, &header, order);
            if (looksLikeReasonableHeader(header, fileSize)) {
                found = true;
                break;
            }
            offset += kBlockSize;
        }
        if (!found) break;

        offset = dataStart;
        while (offset + kBlockSize <= fileSize) {
            Header diskHeader;
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            file.read(reinterpret_cast<char*>(&diskHeader), sizeof(diskHeader));
            if (!file.good()) {
                file.clear();
                break;
            }
            Header header;
            headerFromDiskOrder(diskHeader, &header, order);
            if (looksLikeReasonableHeader(header, fileSize)) {
                outIndices->push_back(offset);
                offset += header.m_blockCount * kBlockSize;
            } else {
                offset += kBlockSize;
            }
        }
        break;
    }
    return true;
}

static bool readIndexEntries(std::fstream& file,
                             ByteOrder order,
                             std::vector<uint64_t>* outIndices,
                             std::string* error) {
    outIndices->clear();
    uint64_t fileSize = 0;
    if (!readFileSize(file, &fileSize)) {
        if (error) *error = "failed to determine input file size";
        return false;
    }
    if (fileSize < kBlockSize) {
        if (error) *error = "input file is smaller than one block";
        return false;
    }

    uint64_t pos = 0;
    while (pos + kBlockSize <= fileSize) {
        bool anyNonZero = false;
        for (uint64_t i = 0; i < kEntriesPerIndexBlock; ++i) {
            uint64_t raw = 0;
            file.seekg(static_cast<std::streamoff>(pos + i * sizeof(uint64_t)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&raw), sizeof(raw));
            if (!file.good()) {
                file.clear();
                if (error) *error = "failed to read index entry";
                return false;
            }
            uint64_t entry = convert64(raw, order, hostByteOrder());
            if (entry != 0) {
                anyNonZero = true;
                if (entry % kBlockSize != 0 || entry >= fileSize) {
                    if (error) *error = "invalid index entry encountered";
                    return false;
                }
                outIndices->push_back(entry);
            }
        }
        if (!anyNonZero) break;
        pos += kBlockSize;
    }
    return true;
}

static bool readRecord(std::fstream& file,
                       uint64_t offset,
                       ByteOrder order,
                       RecordData* out,
                       std::string* error) {
    Header diskHeader;
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file.read(reinterpret_cast<char*>(&diskHeader), sizeof(diskHeader));
    if (!file.good()) {
        file.clear();
        if (error) *error = "failed to read record header";
        return false;
    }

    Header header;
    headerFromDiskOrder(diskHeader, &header, order);
    if (header.m_blockCount == 0) {
        if (error) *error = "invalid block count in record header";
        return false;
    }

    uint64_t totalPayload = static_cast<uint64_t>(header.m_titleLength) + header.m_dataLength;
    uint64_t capacity = kDataCapacityFirstBlock + (header.m_blockCount - 1) * kBlockSize;
    if (totalPayload > capacity) {
        if (error) *error = "record payload exceeds declared block capacity";
        return false;
    }

    std::vector<uint8_t> payload(static_cast<size_t>(totalPayload), 0);
    uint64_t copied = 0;

    uint64_t firstRead = std::min<uint64_t>(totalPayload, kDataCapacityFirstBlock);
    if (firstRead > 0) {
        file.seekg(static_cast<std::streamoff>(offset + kHeaderSize), std::ios::beg);
        file.read(reinterpret_cast<char*>(&payload[0]), static_cast<std::streamsize>(firstRead));
        if (!file.good()) {
            file.clear();
            if (error) *error = "failed to read first payload block";
            return false;
        }
        copied += firstRead;
    }

    for (uint64_t block = 1; block < header.m_blockCount && copied < totalPayload; ++block) {
        uint64_t chunk = std::min<uint64_t>(kBlockSize, totalPayload - copied);
        file.seekg(static_cast<std::streamoff>(offset + block * kBlockSize), std::ios::beg);
        file.read(reinterpret_cast<char*>(&payload[static_cast<size_t>(copied)]), static_cast<std::streamsize>(chunk));
        if (!file.good()) {
            file.clear();
            if (error) *error = "failed to read continuation block";
            return false;
        }
        copied += chunk;
    }

    if (!verifyChecksum(payload, header.m_checksum)) {
        if (error) *error = "checksum verification failed during conversion";
        return false;
    }

    out->header = header;
    out->title.assign(reinterpret_cast<const char*>(payload.data()), static_cast<size_t>(header.m_titleLength));
    out->data.assign(payload.begin() + header.m_titleLength, payload.end());
    return true;
}

static bool writeZeroBlock(std::fstream& file, std::string* error) {
    std::vector<char> zeros(static_cast<size_t>(kBlockSize), 0);
    file.write(&zeros[0], static_cast<std::streamsize>(zeros.size()));
    if (!file.good()) {
        if (error) *error = "failed to write zero index block";
        return false;
    }
    return true;
}

static bool writeIndexBlocks(std::fstream& file,
                             const std::vector<uint64_t>& indices,
                             ByteOrder order,
                             std::string* error) {
    uint64_t blocksNeeded = std::max<uint64_t>(1, ceilDiv(static_cast<uint64_t>(indices.size()), kEntriesPerIndexBlock));
    for (uint64_t b = 0; b < blocksNeeded; ++b) {
        std::vector<uint64_t> block(static_cast<size_t>(kEntriesPerIndexBlock), 0);
        uint64_t begin = b * kEntriesPerIndexBlock;
        uint64_t end = std::min<uint64_t>(begin + kEntriesPerIndexBlock, static_cast<uint64_t>(indices.size()));
        for (uint64_t i = begin; i < end; ++i) {
            block[static_cast<size_t>(i - begin)] = convert64(indices[static_cast<size_t>(i)], hostByteOrder(), order);
        }
        file.seekp(static_cast<std::streamoff>(b * kBlockSize), std::ios::beg);
        file.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(kBlockSize));
        if (!file.good()) {
            if (error) *error = "failed to write index block";
            return false;
        }
    }
    return true;
}

static bool writeRecord(std::fstream& file,
                        uint64_t offset,
                        const RecordData& record,
                        uint64_t nextOffset,
                        uint64_t prevOffset,
                        ByteOrder order,
                        std::string* error) {
    std::vector<uint8_t> payload(record.title.begin(), record.title.end());
    payload.insert(payload.end(), record.data.begin(), record.data.end());

    Header header = record.header;
    header.m_prevBlock = prevOffset;
    header.m_nextBlock = nextOffset;
    header.m_titleLength = static_cast<uint32_t>(record.title.size());
    header.m_dataLength = static_cast<uint64_t>(record.data.size());
    if (payload.size() > kDataCapacityFirstBlock) {
        uint64_t remain = static_cast<uint64_t>(payload.size()) - kDataCapacityFirstBlock;
        header.m_blockCount = 1 + ceilDiv(remain, kBlockSize);
    } else {
        header.m_blockCount = 1;
    }
    computeChecksum(payload, header.m_checksum);

    Header diskHeader;
    headerToDiskOrder(header, &diskHeader, order);

    file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    file.write(reinterpret_cast<const char*>(&diskHeader), sizeof(diskHeader));
    if (!file.good()) {
        if (error) *error = "failed to write output record header";
        return false;
    }

    uint64_t firstWrite = std::min<uint64_t>(payload.size(), kDataCapacityFirstBlock);
    if (firstWrite > 0) {
        file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(firstWrite));
    }
    if (firstWrite < kDataCapacityFirstBlock) {
        std::vector<char> pad(static_cast<size_t>(kDataCapacityFirstBlock - firstWrite), 0);
        file.write(&pad[0], static_cast<std::streamsize>(pad.size()));
    }

    uint64_t copied = firstWrite;
    while (copied < payload.size()) {
        uint64_t chunk = std::min<uint64_t>(kBlockSize, payload.size() - copied);
        file.write(reinterpret_cast<const char*>(payload.data() + copied), static_cast<std::streamsize>(chunk));
        if (chunk < kBlockSize) {
            std::vector<char> pad(static_cast<size_t>(kBlockSize - chunk), 0);
            file.write(&pad[0], static_cast<std::streamsize>(pad.size()));
        }
        copied += chunk;
    }

    if (!file.good()) {
        if (error) *error = "failed to write output record payload";
        return false;
    }
    return true;
}

static int convertArchive(const std::string& inputPath,
                          const std::string& outputPath,
                          ByteOrder sourceOrder,
                          ByteOrder targetOrder) {
    if (inputPath == outputPath) {
        std::cerr << "Input and output paths must differ.\n";
        return 2;
    }

    std::fstream in(inputPath.c_str(), std::ios::in | std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open input archive: " << inputPath << "\n";
        return 2;
    }

    std::vector<uint64_t> indices;
    std::string error;
    if (!readIndexEntries(in, sourceOrder, &indices, &error)) {
        in.clear();
        indices.clear();
        if (!rebuildIndicesByScan(in, sourceOrder, &indices, &error) || indices.empty()) {
            std::cerr << "Failed to read source archive indices: " << error << "\n";
            return 2;
        }
    }

    std::vector<RecordData> records;
    records.reserve(indices.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        RecordData record;
        if (!readRecord(in, indices[i], sourceOrder, &record, &error)) {
            std::cerr << "Failed to read source record at offset " << indices[i] << ": " << error << "\n";
            return 2;
        }
        records.push_back(record);
    }
    in.close();

    std::fstream out(outputPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Failed to create output archive: " << outputPath << "\n";
        return 2;
    }

    uint64_t indexBlocks = std::max<uint64_t>(1, ceilDiv(static_cast<uint64_t>(records.size()), kEntriesPerIndexBlock));
    for (uint64_t i = 0; i < indexBlocks; ++i) {
        if (!writeZeroBlock(out, &error)) {
            std::cerr << error << "\n";
            return 2;
        }
    }

    std::vector<uint64_t> outIndices;
    outIndices.reserve(records.size());
    uint64_t nextOffset = indexBlocks * kBlockSize;
    for (size_t i = 0; i < records.size(); ++i) {
        const RecordData& r = records[i];
        uint64_t payloadSize = static_cast<uint64_t>(r.title.size()) + static_cast<uint64_t>(r.data.size());
        uint64_t blockCount = 1;
        if (payloadSize > kDataCapacityFirstBlock) blockCount += ceilDiv(payloadSize - kDataCapacityFirstBlock, kBlockSize);
        outIndices.push_back(nextOffset);
        nextOffset += blockCount * kBlockSize;
    }

    for (size_t i = 0; i < records.size(); ++i) {
        uint64_t prev = 0;
        uint64_t next = 0;
        uint64_t payloadSize = static_cast<uint64_t>(records[i].title.size()) + static_cast<uint64_t>(records[i].data.size());
        uint64_t blockCount = 1;
        if (payloadSize > kDataCapacityFirstBlock) blockCount += ceilDiv(payloadSize - kDataCapacityFirstBlock, kBlockSize);
        if (blockCount > 1) {
            prev = 0;
            next = outIndices[i] + kBlockSize;
        }
        if (!writeRecord(out, outIndices[i], records[i], next, prev, targetOrder, &error)) {
            std::cerr << "Failed to write converted record: " << error << "\n";
            return 2;
        }
    }

    if (!writeIndexBlocks(out, outIndices, targetOrder, &error)) {
        std::cerr << "Failed to write output index blocks: " << error << "\n";
        return 2;
    }

    out.flush();
    if (!out.good()) {
        std::cerr << "Failed to flush output archive.\n";
        return 2;
    }

    std::cout << "Converted archive '" << inputPath << "' from " << byteOrderName(sourceOrder)
              << " to " << byteOrderName(targetOrder) << " endian as '" << outputPath << "'.\n";
    std::cout << "Records converted: " << records.size() << "\n";
    return 0;
}

static void printUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " --input <source.zar> --output <dest.zar> --from <little|big> --to <little|big>\n"
        << "\n"
        << "Examples:\n"
        << "  " << argv0 << " --input archive_be.zar --output archive_le.zar --from big --to little\n"
        << "  " << argv0 << " --input archive_le.zar --output archive_be.zar --from little --to big\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string inputPath;
    std::string outputPath;
    std::string fromText;
    std::string toText;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if ((arg == "--input" || arg == "-i") && i + 1 < argc) {
            inputPath = argv[++i];
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "--from" && i + 1 < argc) {
            fromText = argv[++i];
        } else if (arg == "--to" && i + 1 < argc) {
            toText = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    if (inputPath.empty() || outputPath.empty() || fromText.empty() || toText.empty()) {
        printUsage(argv[0]);
        return 2;
    }

    ByteOrder fromOrder;
    ByteOrder toOrder;
    if (!parseByteOrder(fromText, &fromOrder)) {
        std::cerr << "Invalid --from value: " << fromText << "\n";
        return 2;
    }
    if (!parseByteOrder(toText, &toOrder)) {
        std::cerr << "Invalid --to value: " << toText << "\n";
        return 2;
    }
    if (fromOrder == toOrder) {
        std::cerr << "Source and destination byte order are the same; nothing to convert.\n";
        return 2;
    }

    return convertArchive(inputPath, outputPath, fromOrder, toOrder);
}
