#include "zar_utils.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

namespace zar {

ByteOrder hostByteOrder() {
    const uint16_t probe = 0x0102;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&probe);
    return bytes[0] == 0x02 ? kLittleEndian : kBigEndian;
}

uint16_t byteSwap16(uint16_t v) {
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

uint32_t byteSwap32(uint32_t v) {
    return ((v & 0x000000FFU) << 24) |
           ((v & 0x0000FF00U) << 8)  |
           ((v & 0x00FF0000U) >> 8)  |
           ((v & 0xFF000000U) >> 24);
}

uint64_t byteSwap64(uint64_t v) {
    return ((v & 0x00000000000000FFULL) << 56) |
           ((v & 0x000000000000FF00ULL) << 40) |
           ((v & 0x0000000000FF0000ULL) << 24) |
           ((v & 0x00000000FF000000ULL) << 8)  |
           ((v & 0x000000FF00000000ULL) >> 8)  |
           ((v & 0x0000FF0000000000ULL) >> 24) |
           ((v & 0x00FF000000000000ULL) >> 40) |
           ((v & 0xFF00000000000000ULL) >> 56);
}

uint16_t convert16(uint16_t v, ByteOrder from, ByteOrder to) {
    return from == to ? v : byteSwap16(v);
}

uint32_t convert32(uint32_t v, ByteOrder from, ByteOrder to) {
    return from == to ? v : byteSwap32(v);
}

uint64_t convert64(uint64_t v, ByteOrder from, ByteOrder to) {
    return from == to ? v : byteSwap64(v);
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    size_t i = 0;
    while (i < data.size()) {
        uint32_t octet_a = i < data.size() ? data[i++] : 0;
        uint32_t octet_b = i < data.size() ? data[i++] : 0;
        uint32_t octet_c = i < data.size() ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out.push_back(kB64[(triple >> 18) & 0x3F]);
        out.push_back(kB64[(triple >> 12) & 0x3F]);
        out.push_back((i - 1) <= data.size() ? kB64[(triple >> 6) & 0x3F] : '=');
        out.push_back(i <= data.size() ? kB64[triple & 0x3F] : '=');
    }

    size_t mod = data.size() % 3;
    if (mod) {
        out[out.size() - 1] = '=';
        if (mod == 1) out[out.size() - 2] = '=';
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& input) {
    std::vector<int> map(256, -1);
    for (int i = 0; i < 64; ++i) map[(unsigned char)kB64[i]] = i;

    std::vector<uint8_t> out;
    int val = 0;
    int valb = -8;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (input[i] == '=') break;
        if (map[c] == -1) continue;
        val = (val << 6) + map[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

void computeChecksum(const std::vector<uint8_t>& titleAndData, uint8_t out[64]) {
    static const uint64_t seed[8] = {
        0x123456789ABCDEF0ULL,
        0x0FEDCBA987654321ULL,
        0xA5A5A5A5A5A5A5A5ULL,
        0x5A5A5A5A5A5A5A5AULL,
        0x1122334455667788ULL,
        0x8877665544332211ULL,
        0xCAFEBABEDEADBEEFULL,
        0x0BADC0DEFEEDFACEULL
    };

    uint64_t accum[8];
    ::memcpy(accum, seed, sizeof(accum));

    for (size_t i = 0; i < titleAndData.size(); i += 8) {
        uint64_t chunk = 0;
        size_t remain = std::min<size_t>(8, titleAndData.size() - i);
        ::memcpy(&chunk, &titleAndData[i], remain);
        accum[(i / 8) % 8] ^= chunk;
    }

    ::memcpy(out, accum, 64);
}

bool verifyChecksum(const std::vector<uint8_t>& titleAndData, const uint8_t checksum[64]) {
    uint8_t actual[64];
    computeChecksum(titleAndData, actual);
    return ::memcmp(actual, checksum, 64) == 0;
}

std::string jsonEscape(const std::string& input) {
    std::ostringstream oss;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"': oss << "\\\""; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    oss << static_cast<char>(c);
                }
        }
    }
    return oss.str();
}

std::string bytesToHex(const uint8_t* data, size_t size) {
    std::ostringstream oss;
    for (size_t i = 0; i < size; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(data[i]);
    }
    return oss.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    if (hex.size() % 2 != 0) return out;
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int v = 0;
        std::istringstream iss(hex.substr(i, 2));
        iss >> std::hex >> v;
        out.push_back(static_cast<uint8_t>(v));
    }
    return out;
}

static bool pathExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

static bool createDir(const std::string& path) {
#ifdef _WIN32
    if (_mkdir(path.c_str()) == 0) return true;
#else
    if (::mkdir(path.c_str(), 0755) == 0) return true;
#endif
    return pathExists(path);
}

bool ensureParentDirectories(const std::string& filePath, std::string* error) {
    std::string normalized = filePath;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    size_t pos = 0;

#ifdef _WIN32
    if (normalized.size() >= 2 && normalized[1] == ':') {
        pos = 2;
    }
#endif

    while (true) {
        pos = normalized.find('/', pos + 1);
        if (pos == std::string::npos) break;
        std::string sub = normalized.substr(0, pos);
        if (sub.empty()) continue;
        if (!createDir(sub)) {
            if (error) *error = "failed to create directory: " + sub;
            return false;
        }
    }
    return true;
}

std::string joinPath(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    char last = left[left.size() - 1];
    if (last == '/' || last == '\\') return left + right;
#ifdef _WIN32
    return left + "\\" + right;
#else
    return left + "/" + right;
#endif
}

std::string tempJsonPath(const std::string& prefix) {
#ifdef _WIN32
    const char* tmp = std::getenv("TEMP");
    std::string base = (tmp && *tmp) ? tmp : "C:\\Temp";
#else
    std::string base = "/tmp";
#endif
    std::ostringstream oss;
    oss << prefix << "_" << static_cast<unsigned long long>(std::time(NULL)) << "_" << ::rand() << ".json";
    return joinPath(base, oss.str());
}

bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data, std::string* error) {
    if (!ensureParentDirectories(path, error)) return false;
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out) {
        if (error) *error = "failed to open output file: " + path;
        return false;
    }
    if (!data.empty()) out.write(reinterpret_cast<const char*>(&data[0]), static_cast<std::streamsize>(data.size()));
    if (!out.good()) {
        if (error) *error = "failed to write output file: " + path;
        return false;
    }
    return true;
}

} // namespace zar
