#include "../src/uia_request_queue.h"
#include <cassert>
#include <string>
int main() {
    uia::SingleSlotQueue<std::string> queue;
    assert(!queue.take());
    queue.submit("stale"); queue.submit("latest");
    auto request = queue.take();
    assert(request && *request == "latest");
    assert(!queue.take());
    queue.submit("cancelled"); queue.clear();
    assert(!queue.take());
}
