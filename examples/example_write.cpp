#include "zar_store.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

void print_usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --archive <path>        Output archive path (default: example.zar)\n"
        << "  --title <value>         Record title/path stored in archive (default: docs/hello.txt)\n"
        << "  --text-size <bytes>     Generate text payload of the requested size\n"
        << "  --input-file <path>     Read payload bytes from a file\n"
        << "  --file-byte-order <v>   little or big (default: little)\n"
        << "  --help                  Show this help\n"
        << "\n"
        << "Behavior:\n"
        << "  - If --input-file is provided, its contents become the record payload.\n"
        << "  - Otherwise, the example generates text data.\n"
        << "  - Without --text-size, generated text defaults to the example sentence.\n";
}

bool parse_uint64(const std::string& text, uint64_t* value) {
    if (value == NULL || text.empty()) {
        return false;
    }

    char* end = NULL;
    errno = 0;
    unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || end == NULL || *end != '\0') {
        return false;
    }

    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool read_file_bytes(const std::string& path, std::vector<uint8_t>* out, std::string* error) {
    if (out == NULL) {
        if (error != NULL) {
            *error = "output vector is null";
        }
        return false;
    }

    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        if (error != NULL) {
            *error = "failed to open input file: " + path;
        }
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::ifstream::pos_type end_pos = input.tellg();
    if (end_pos < 0) {
        if (error != NULL) {
            *error = "failed to determine input file size: " + path;
        }
        return false;
    }
    input.seekg(0, std::ios::beg);

    const uint64_t file_size = static_cast<uint64_t>(end_pos);
    if (file_size > static_cast<uint64_t>(std::numeric_limits<std::vector<uint8_t>::size_type>::max())) {
        if (error != NULL) {
            *error = "input file is too large for this example process: " + path;
        }
        return false;
    }

    out->assign(static_cast<std::vector<uint8_t>::size_type>(file_size), 0);
    if (!out->empty()) {
        input.read(reinterpret_cast<char*>(&(*out)[0]), static_cast<std::streamsize>(out->size()));
        if (!input) {
            if (error != NULL) {
                *error = "failed to read input file: " + path;
            }
            return false;
        }
    }

    return true;
}

std::vector<uint8_t> make_text_payload(uint64_t requested_size) {
    static const std::string kSeed = "Hello from ZAR example\n";

    std::vector<uint8_t> data;
    if (requested_size == 0) {
        return data;
    }

    if (requested_size <= static_cast<uint64_t>(std::numeric_limits<std::vector<uint8_t>::size_type>::max())) {
        data.reserve(static_cast<std::vector<uint8_t>::size_type>(requested_size));
    }

    while (static_cast<uint64_t>(data.size()) < requested_size) {
        const uint64_t remaining = requested_size - static_cast<uint64_t>(data.size());
        const std::string::size_type copy_count =
            (remaining < static_cast<uint64_t>(kSeed.size()))
                ? static_cast<std::string::size_type>(remaining)
                : kSeed.size();
        data.insert(data.end(), kSeed.begin(), kSeed.begin() + static_cast<std::ptrdiff_t>(copy_count));
    }

    return data;
}

bool parse_byte_order(const std::string& text, zar::ByteOrder* order) {
    if (order == NULL) {
        return false;
    }
    if (text == "little") {
        *order = zar::kLittleEndian;
        return true;
    }
    if (text == "big") {
        *order = zar::kBigEndian;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    std::string archive_path = "example.zar";
    std::string title = "docs/hello.txt";
    std::string input_file;
    bool have_text_size = false;
    uint64_t text_size = 0;
    zar::ByteOrder file_byte_order = zar::kLittleEndian;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if ((arg == "--archive" || arg == "--title" || arg == "--text-size" ||
             arg == "--input-file" || arg == "--file-byte-order") && i + 1 >= argc) {
            std::cerr << "missing value for " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
        if (arg == "--archive") {
            archive_path = argv[++i];
        } else if (arg == "--title") {
            title = argv[++i];
        } else if (arg == "--text-size") {
            if (!parse_uint64(argv[++i], &text_size)) {
                std::cerr << "invalid --text-size value\n";
                return 1;
            }
            have_text_size = true;
        } else if (arg == "--input-file") {
            input_file = argv[++i];
        } else if (arg == "--file-byte-order") {
            if (!parse_byte_order(argv[++i], &file_byte_order)) {
                std::cerr << "invalid --file-byte-order value, expected little or big\n";
                return 1;
            }
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    zar::Store store;
    store.setFileByteOrder(file_byte_order);
    if (!store.open(archive_path, true)) {
        std::cerr << "failed to open store\n";
        return 1;
    }

    zar::HeaderView hv;
    hv.setTimestamp(1711324800ULL);
    hv.setFlags(0x1ULL);
    hv.setExtra(42);
    hv.setCounter(0, 100);
    hv.setCounter(1, 200);
    const uint8_t additional[16] = {'d','e','m','o'};
    hv.setAdditional(additional, sizeof(additional));

    std::vector<uint8_t> data;
    std::string error;
    if (!input_file.empty()) {
        if (!read_file_bytes(input_file, &data, &error)) {
            std::cerr << error << "\n";
            return 1;
        }
    } else if (have_text_size) {
        data = make_text_payload(text_size);
    } else {
        data = make_text_payload(static_cast<uint64_t>(std::string("Hello from ZAR example").size()));
        const std::string fallback = "Hello from ZAR example";
        data.assign(fallback.begin(), fallback.end());
    }

    uint64_t id = 0;
    if (!store.append(hv.raw(), title, data, &id, &error)) {
        std::cerr << error << "\n";
        return 1;
    }

    if (!store.save()) {
        std::cerr << "save failed\n";
        return 1;
    }

    std::cout << "host byte order="
              << (store.getHostByteOrder() == zar::kLittleEndian ? "little" : "big") << "\n";
    std::cout << "archive byte order="
              << (store.getFileByteOrder() == zar::kLittleEndian ? "little" : "big") << "\n";
    std::cout << "archive path=" << archive_path << "\n";
    std::cout << "title=" << title << "\n";
    if (!input_file.empty()) {
        std::cout << "payload source=file\n";
        std::cout << "input file=" << input_file << "\n";
    } else {
        std::cout << "payload source=generated text\n";
        std::cout << "text size=" << data.size() << "\n";
    }
    std::cout << "appended record id=" << id << "\n";
    store.close();
    return 0;
}
