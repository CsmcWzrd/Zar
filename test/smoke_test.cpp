#include "zar_store.h"
#include <cassert>
#include <iostream>

int main() {
    zar::Store store;
    assert(store.open("smoke.zar", true));

    zar::HeaderView hv;
    hv.setFlags(7);
    hv.setExtra(9);

    std::vector<uint8_t> a(10000, 'A');
    std::vector<uint8_t> b(17, 'B');

    uint64_t id1 = 0, id2 = 0;
    std::string error;
    assert(store.append(hv.raw(), "dir/a.bin", a, &id1, &error));
    assert(store.append(hv.raw(), "dir/b.bin", b, &id2, &error));
    assert(store.save());

    zar::Query q;
    q.id.enabled = true;
    q.id.min = id1;
    q.id.max = id2;

    char buf[128];
    size_t total = 0;
    while (true) {
        size_t n = store.SelectAll(q, buf, sizeof(buf));
        if (n == 0) break;
        total += n;
    }
    assert(total > 0);

    assert(store.ExtractAll(q, "smoke_out", &error));
    std::cout << "smoke test passed\n";
    return 0;
}
