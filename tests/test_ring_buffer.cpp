#include "check.hpp"
#include "wsc/ring_buffer.hpp"

#include <thread>

TEST(ring_buffer_fifo_order) {
    wsc::RingBuffer<int> rb(8);
    CHECK(!rb.pop().has_value());
    for (int i = 0; i < 8; ++i) CHECK(rb.push(i));
    CHECK(!rb.push(99));  // full
    for (int i = 0; i < 8; ++i) CHECK(rb.pop().value() == i);
    CHECK(!rb.pop().has_value());
}

TEST(ring_buffer_wraps_without_loss) {
    wsc::RingBuffer<int> rb(4);
    for (int round = 0; round < 100; ++round) {
        CHECK(rb.push(round));
        CHECK(rb.pop().value() == round);
    }
}

TEST(ring_buffer_two_threads) {
    wsc::RingBuffer<int> rb(1024);
    constexpr int N = 200000;
    std::thread producer([&] {
        for (int i = 0; i < N;) if (rb.push(i)) ++i;
    });
    int expected = 0;
    while (expected < N) {
        if (auto v = rb.pop()) {
            CHECK(*v == expected);
            ++expected;
        }
    }
    producer.join();
    CHECK(rb.size() == 0);
}
