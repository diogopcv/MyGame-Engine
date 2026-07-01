#include "physics/broadphase.h"
#include "physics/collision.h"
#include <algorithm>
#include <new>

namespace ge::physics {

void Broadphase::init(u32 max_proxies, Allocator alloc) {
    alloc_ = alloc;
    proxies_ = alloc_.alloc_t<Array<Proxy>>();
    new (proxies_) Array<Proxy>(alloc_);
    proxies_->reserve(max_proxies);
}

void Broadphase::shutdown() {
    proxies_->~Array();
    alloc_.free(proxies_, 0);
    proxies_ = nullptr;
}

void Broadphase::clear() {
    proxies_->clear();
}

void Broadphase::add(u32 id, const AABB& box) {
    proxies_->push(Proxy{id, box});
}

void Broadphase::compute_pairs(Array<Pair>& out_pairs) {
    Array<Proxy>& proxies = *proxies_;
    std::sort(proxies.begin(), proxies.end(), [](const Proxy& a, const Proxy& b) {
        return a.box.min.x < b.box.min.x;
    });

    // Sorted by min.x ascending, so for i < j: min.x[i] <= min.x[j] always.
    // The intervals [min.x[i], max.x[i]] and [min.x[j], max.x[j]] can only
    // fail to overlap if min.x[j] > max.x[i] (the other failure mode,
    // max.x[j] < min.x[i], is impossible given the sort order) -- so once
    // that holds, every later j (even larger min.x) is out of range too.
    usize n = proxies.size();
    for (usize i = 0; i < n; ++i) {
        for (usize j = i + 1; j < n; ++j) {
            if (proxies[j].box.min.x > proxies[i].box.max.x) break;
            if (aabb_overlap(proxies[i].box, proxies[j].box))
                out_pairs.push(Pair{proxies[i].id, proxies[j].id});
        }
    }
}

} // namespace ge::physics
