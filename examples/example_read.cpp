#include "zar_store.h"
#include <iostream>
#include <vector>

int main() {
    zar::Store store;
    store.setFileByteOrder(zar::kLittleEndian);
    if (!store.open("example.zar", false)) {
        std::cerr << "failed to open store\n";
        return 1;
    }

    std::cout << "host byte order=" << (store.getHostByteOrder() == zar::kLittleEndian ? "little" : "big") << "\n";
    std::cout << "archive byte order=" << (store.getFileByteOrder() == zar::kLittleEndian ? "little" : "big") << "\n";

    zar::Query q;
    q.id.enabled = true;
    q.id.min = 1;
    q.id.max = 100;

    std::vector<char> chunk(64);
    while (true) {
        size_t n = store.SelectAll(q, &chunk[0], chunk.size());
        if (n == 0) break;
        std::cout.write(&chunk[0], static_cast<std::streamsize>(n));
    }
    std::cout << "\n";

    zar::JsonFileResult out = store.SelectAllToJsonFile(q);
    if (out.ok) {
        std::cout << "JSON written to: " << out.path << "\n";
    }

    std::string error;
    if (!store.ExtractAll(q, "out_dir", &error)) {
        std::cerr << error << "\n";
        return 1;
    }

    return 0;
}
