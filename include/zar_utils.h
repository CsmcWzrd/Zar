#pragma once

#include "zar_types.h"
#include <stdint.h>
#include <string>
#include <vector>

namespace zar {


ByteOrder hostByteOrder();
uint16_t byteSwap16(uint16_t v);
uint32_t byteSwap32(uint32_t v);
uint64_t byteSwap64(uint64_t v);
uint16_t convert16(uint16_t v, ByteOrder from, ByteOrder to);
uint32_t convert32(uint32_t v, ByteOrder from, ByteOrder to);
uint64_t convert64(uint64_t v, ByteOrder from, ByteOrder to);

std::string base64Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64Decode(const std::string& input);

void computeChecksum(const std::vector<uint8_t>& titleAndData, uint8_t out[64]);
bool verifyChecksum(const std::vector<uint8_t>& titleAndData, const uint8_t checksum[64]);

std::string jsonEscape(const std::string& input);
std::string bytesToHex(const uint8_t* data, size_t size);
std::vector<uint8_t> hexToBytes(const std::string& hex);

bool ensureParentDirectories(const std::string& filePath, std::string* error);
std::string joinPath(const std::string& left, const std::string& right);
std::string tempJsonPath(const std::string& prefix);

bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data, std::string* error);

} // namespace zar
