#define TESTING

#include <algorithm>
#include <array>
#include <cstring>
#include <list>
#include <mutex>
#include <thread>
#include <type_traits>

#include <lib/system/allocators.hpp>
#include <lib/system/queues.hpp>
#include <lib/system/structures.hpp>
#include <lib/system/random.hpp>

#include <boost/lockfree/spsc_queue.hpp>

#include "gtest/gtest.h"

void CreateRegionAllocatorAndThenAllocateInIt([[maybe_unused]] const uint32_t pageSize, [[maybe_unused]] const uint32_t initialNumberOfPages,
                                              const uint32_t numberOfAdditionalAllocations, const uint32_t additionalAllocationSize, uint32_t& finalNumberOfPages) {
    static_assert(std::is_destructible_v<RegionAllocator>, "RegionAllocator is not default constructible");

    RegionAllocator allocator;
    std::vector<RegionPtr> pointers;

    for (uint32_t i = 0; i < numberOfAdditionalAllocations; ++i) {
        pointers.push_back(allocator.allocateNext(additionalAllocationSize));

        ASSERT_EQ(pointers.back()->size(), additionalAllocationSize);
        *(reinterpret_cast<uint32_t*>(pointers.back()->data())) = i;
    }

    uint32_t i = 0;

    for (auto ptr : pointers) {
        ASSERT_EQ(i, *(reinterpret_cast<uint32_t*>(ptr->data())));
        ++i;
    }

    finalNumberOfPages = initialNumberOfPages;
}

// https://en.wikipedia.org/wiki/Data_structure_alignment#Computing_padding
constexpr int32_t AlignNumberToPowerOfTwo(const int32_t number, const uint32_t align) {
    auto alignment = static_cast<int32_t>(align);
    return number + ((-number) & (alignment - 1));
}

constexpr int32_t AlignNumberTo64(const int32_t number) {
    return AlignNumberToPowerOfTwo(number, 64);
}

TEST(RegionAllocator, NoAdditionalPageIsAddedIfInitialPagesAreEnough) {
    constexpr uint32_t kAllocationSize = 4, kPageSize = AlignNumberTo64(sizeof(Region) + kAllocationSize) * 10, kInitialNumberOfPages = 2, kNumberOfAllocations = 20;
    uint32_t finalNumberOfPages;
    CreateRegionAllocatorAndThenAllocateInIt(kPageSize, kInitialNumberOfPages, kNumberOfAllocations, kAllocationSize, finalNumberOfPages);
    ASSERT_EQ(finalNumberOfPages, kInitialNumberOfPages);
}

TEST(RegionAllocator, CorrectNumberOfPagesAllocated) {
    constexpr uint32_t kAllocationSize = 4, kPageSize = AlignNumberTo64(sizeof(Region) + kAllocationSize) * 10, kInitialNumberOfPages = 2, kNumberOfAllocations = 50;

    uint32_t finalNumberOfPages;

    CreateRegionAllocatorAndThenAllocateInIt(kPageSize, kInitialNumberOfPages, kNumberOfAllocations, kAllocationSize, finalNumberOfPages);
    ASSERT_EQ(finalNumberOfPages, kInitialNumberOfPages);

    CreateRegionAllocatorAndThenAllocateInIt(kPageSize, kInitialNumberOfPages, kNumberOfAllocations + 1, kAllocationSize, finalNumberOfPages);
    ASSERT_EQ(finalNumberOfPages, kInitialNumberOfPages);
}

TEST(RegionAllocator, AllocateAndFreeUnusedObject) {
    MockAllocator allocator;
    uint32_t allocations = 100;

    for (uint32_t i = 0; i < allocations; ++i) {
        // every iteration this object is destructed and automatically removed from
        // allocator, so allocator is empty every iteration
        auto destructedObject = allocator.allocateNext(i + 1);
        *(reinterpret_cast<char*>(destructedObject->data())) = 'x';
    }

    ASSERT_EQ(allocator.allocations(), allocations);
}

TEST(RegionAllocator, alloc_with_resizes) {
    [[maybe_unused]] constexpr uint32_t kPageSize = AlignNumberTo64(sizeof(Region) + 1) * 101;

    MockAllocator allocator;
    std::vector<RegionPtr> regs;
    uint32_t allocations = 100;

    for (uint32_t i = 0; i < allocations; ++i) {
        auto p = allocator.allocateNext(50);
        *(reinterpret_cast<char*>(p->data())) = 'x';

        regs.push_back(p);
    }

    ASSERT_EQ(allocator.allocations(), allocations);
}

TEST(RegionAllocator, multithreaded_stress) {
    RegionAllocator a;
    uint64_t total = 0;

    std::mutex mutex;
    std::list<RegionPtr> regs;
    uint64_t lTot = 0;

    std::thread wr([&]() {
        for (uint32_t i = 0; i < 1000000; ++i) {
            uint32_t s = cs::Random::generateValue<uint32_t>(0, 100) + 4;

            auto p = a.allocateNext(s);
            *(reinterpret_cast<uint32_t*>(p->data())) = i;
            total += i;

            {
                std::lock_guard<std::mutex> lock(mutex);
                regs.push_back(p);
            }
        }
    });

    auto rrout = [&]() {
        std::vector<RegionPtr> inCase;
        uint64_t t = 0;

        for (uint32_t i = 0; i < 250000; ++i) {
            for (;;) {
                RegionPtr p;

                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (regs.empty())
                        continue;
                    p = regs.front();
                    regs.pop_front();
                }

                t += *(reinterpret_cast<uint32_t*>(p->data()));

                if (i % 17 == 0) {
                    inCase.push_back(p);
                }

                break;
            }
        }

        std::lock_guard<std::mutex> l(mutex);
        lTot += t;
    };

    std::thread p1(rrout);
    std::thread p2(rrout);
    std::thread p3(rrout);
    std::thread p4(rrout);

    wr.join();
    p1.join();
    p2.join();
    p3.join();
    p4.join();

    ASSERT_EQ(lTot, total);
}

TEST(fuqueue, consecutive) {
    FUQueue<uint32_t, 1000> q;

    for (uint32_t i = 0; i < 1000; ++i) {
        auto s = q.lockWrite();
        s->element = i;
        q.unlockWrite(s);
    }

    for (uint32_t i = 0; i < 1000; ++i) {
        auto s = q.lockRead();
        ASSERT_EQ(s->element, i);
        q.unlockRead(s);
    }
}

TEST(fuqueue, overwrite) {
    FUQueue<uint32_t, 1000> q;

    for (int i = 0; i < 100; ++i) {
        for (uint32_t j = 0; j < 200; ++j) {
            auto s = q.lockWrite();
            s->element = j;
            q.unlockWrite(s);
        }

        for (uint32_t j = 0; j < 200; ++j) {
            auto s = q.lockRead();
            ASSERT_EQ(s->element, j);
            q.unlockRead(s);
        }
    }
}

TEST(fuqueue, DISABLED_multithreaded_stress) {
    FUQueue<uint32_t, 1000> q;

    std::atomic<uint64_t> wSum = {0};
    std::atomic<uint64_t> rSum = {0};

    auto wrFunc = [&]() {
        for (int i = 0; i < 100000; ++i) {
            const uint32_t t = cs::Random::generateValue<uint32_t>(0, 500);
            auto s = q.lockWrite();
            s->element = t;
            q.unlockWrite(s);

            wSum.fetch_add(t, std::memory_order_relaxed);
        }
    };

    auto rdFunc = [&]() {
        for (int i = 0; i < 100000; ++i) {
            auto s = q.lockRead();
            rSum.fetch_add(s->element, std::memory_order_relaxed);
            q.unlockRead(s);
        }
    };

    std::thread w1(wrFunc);
    std::thread w2(wrFunc);
    std::thread w3(wrFunc);

    std::thread r1(rdFunc);
    std::thread r2(rdFunc);
    std::thread r3(rdFunc);

    w1.join();
    w2.join();
    w3.join();

    r1.join();
    r2.join();
    r3.join();
}

TEST(boost_spsc_queue, DISABLED_multithreaded_stress) {
    boost::lockfree::spsc_queue<uint32_t, boost::lockfree::capacity<10000>> queue;

    std::atomic<uint64_t> wSum = {0};
    std::atomic<uint64_t> rSum = {0};

    std::atomic<uint64_t> executions = {0};
    std::atomic<uint64_t> enqueues = {0};

    size_t count = 10000;

    auto wrFunc = [&]() {
        for (size_t i = 0; i < count; ++i) {
            const uint32_t t = cs::Random::generateValue<uint32_t>(0, 500);

            while (!queue.push(t)) {
                executions.fetch_add(1, std::memory_order_acq_rel);
                wSum.fetch_add(t, std::memory_order_acq_rel);
            }
        }
    };

    auto rdFunc = [&]() {
        while (executions.load(std::memory_order_acquire) != enqueues.load(std::memory_order_acquire)) {
            uint32_t value = 0;
            while (queue.pop(value)) {
                rSum.fetch_add(value, std::memory_order_acq_rel);
                enqueues.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    };

    std::thread w1(wrFunc);
    std::thread w2(wrFunc);
    std::thread w3(wrFunc);

    std::thread r1(rdFunc);
    std::thread r2(rdFunc);
    std::thread r3(rdFunc);

    w1.join();
    w2.join();
    w3.join();

    r1.join();
    r2.join();
    r3.join();

    ASSERT_EQ(enqueues.load(std::memory_order_acquire), executions.load(std::memory_order_acquire));
    ASSERT_EQ(wSum.load(std::memory_order_acquire), rSum.load(std::memory_order_acquire));
}

// TODO: Enable test and fix crush on linux
TEST(typed_allocator, DISABLED_one_page) {
    TypedAllocator<uint32_t> allocator(100);

    std::array<MemPtr<TypedSlot<uint32_t>>, 100> uis = {};

    for (uint32_t i = 0; i < 100; ++i) {
        uis[i] = allocator.emplace(i);
    }

    for (uint32_t i = 0; i < 100; ++i) {
        ASSERT_EQ(**(uis[i]), i);
    }

    for (uint32_t i = 0; i < 100; ++i) {
        uis[i] = MemPtr<TypedSlot<uint32_t>>();
    }
}

TEST(typed_allocator, multiple_pages) {
    TypedAllocator<uint32_t> allocator(100);

    std::array<MemPtr<TypedSlot<uint32_t>>, 1000> uis;

    for (uint32_t i = 0; i < 1000; ++i) {
        uis[i] = allocator.emplace(i);
    }

    for (uint32_t i = 0; i < 1000; ++i) {
        ASSERT_EQ(**(uis[i]), i);
    }

    for (uint32_t i = 0; i < 1000; ++i) {
        uis[i] = MemPtr<TypedSlot<uint32_t>>();
    }
}

TEST(typed_allocator, reput) {
    TypedAllocator<uint32_t> allocator(1);

    auto first = *allocator.emplace(42);

    for (uint32_t i = 0; i < 10; ++i) {
        ASSERT_EQ(*allocator.emplace(i), first);
    }
}

TEST(typed_allocator, inconsistent) {
    TypedAllocator<uint32_t> allocator(100);

    std::array<MemPtr<TypedSlot<uint32_t>>, 1000> uis;

    for (uint32_t i = 0; i < 1000; ++i) {
        uis[i] = allocator.emplace(i);

        if (i % 7 == 0) {
            uis[i] = MemPtr<TypedSlot<uint32_t>>();
        }
    }

    for (uint32_t i = 0; i < 1000; ++i) {
        if (i % 7) {
            ASSERT_EQ(**(uis[i]), i);
        }
    }
}

template <>
uint16_t getHashIndex(const uint32_t& h) {
    return static_cast<uint16_t>(h % (1 << 16));
}

TEST(FixedHashMap, base) {
    FixedHashMap<uint32_t, uint64_t, uint16_t, 100000> hm;
    const uint32_t COUNT = 100000ll;
    uint32_t hs[COUNT];

    for (uint32_t i = 0; i < COUNT; ++i) {
        hs[i] = i;
        auto& c = hm.tryStore(hs[i]);
        ASSERT_EQ(c, 0);
        c = 2;
    }

    for (uint32_t i = 0; i < COUNT; ++i) {
        ASSERT_EQ(hm.tryStore(hs[i]), 2);
    }
}

TEST(FixedHashMap, depush) {
    FixedHashMap<uint32_t, uint64_t, uint16_t, 10> hm;
    uint32_t hs[1000];

    for (uint32_t i = 0; i < 1000; ++i) {
        hs[i] = i;
        auto& c = hm.tryStore(hs[i]);
        ASSERT_EQ(c, 0);
        c = 2;
    }

    for (uint32_t i = 990; i < 1000; ++i) {
        auto& c = hm.tryStore(hs[i]);
        ASSERT_EQ(c, 2);
    }

    for (uint32_t i = 0; i < 990; ++i) {
        auto& c = hm.tryStore(hs[i]);
        ASSERT_EQ(c, 0);
    }
}

template <>
uint8_t getHashIndex(const uint16_t& h) {
    return static_cast<uint8_t>(h % (1 << 8));
}

TEST(FixedHashMap, heap) {
    const uint16_t COUNT = 10000;
    FixedHashMap<uint16_t, uint32_t, uint8_t, COUNT> hm;
    uint16_t hs[COUNT];

    for (uint16_t i = 0; i < COUNT; ++i) {
        hs[i] = i;
        auto& c = hm.tryStore(hs[i]);
        ASSERT_EQ(c, 0);
        c = 7;
    }

    for (uint16_t i = 0; i < COUNT; ++i) {
        auto c = hm.tryStore(hs[i]);
        ASSERT_EQ(c, 7);
    }
}

struct IntWithCounter {
    static uint32_t counter;
    uint32_t i;
    IntWithCounter(uint32_t _i)
    : i(_i) {
        ++counter;
    }
    IntWithCounter() {
        ++counter;
    }
    ~IntWithCounter() {
        --counter;
    }
};

uint32_t IntWithCounter::counter = 0;

TEST(FixedHashMap, destroy) {
    IntWithCounter::counter = 0;

    {
        FixedHashMap<uint16_t, IntWithCounter, uint8_t, 10> hm;

        for (uint16_t i = 0; i < 100; ++i) {
            hm.tryStore(i);
        }

        ASSERT_EQ(IntWithCounter::counter, 10);
    }

    // Todo: Typed allocator destructor
    ASSERT_EQ(IntWithCounter::counter, 0);
}

TEST(FixedCircularBuffer, BasicCreation) {
    IntWithCounter::counter = 0;
    FixedCircularBuffer<IntWithCounter, 32> buffer;

    for (uint32_t i = 0; i < 30; ++i) {
        buffer.emplace(i);
    }

    uint32_t j = 0;
    for (auto& x : buffer) {
        ASSERT_EQ(x.i, j);
        ++j;
    }

    ASSERT_EQ(j, 30);
    buffer.clear();
    ASSERT_EQ(IntWithCounter::counter, 0);
}

TEST(FixedCircularBuffer, OverlappingWhenAddingMoreThanBufferSize) {
    IntWithCounter::counter = 0;

    {
        FixedCircularBuffer<IntWithCounter, 32> buffer;
        for (uint32_t i = 0; i < 40; ++i) {
            buffer.emplace(i);
        }

        auto iter = buffer.begin();

        for (uint32_t i = 8; i < 40; ++i) {
            ASSERT_EQ(iter->i, i);
            ++iter;
        }

        ASSERT_EQ(IntWithCounter::counter, 32);
    }

    ASSERT_EQ(IntWithCounter::counter, 0);
}

TEST(FixedCircularBuffer, RemovingTwoElements) {
    FixedCircularBuffer<uint32_t, 32> buffer;
    for (uint32_t i = 0; i < 20; ++i) {
        buffer.emplace(i);
    }

    buffer.remove(buffer.frontPtr() + 10);
    buffer.remove(buffer.frontPtr() + 15);

    auto iter = buffer.begin();
    for (uint32_t i = 0; i < 18; ++i) {
        ASSERT_EQ(*iter, (i < 10 ? i : i + (i < 15 ? 1 : 2)));
        ++iter;
    }
}

TEST(FixedVector, base) {
    FixedVector<int, 10> fv;

    for (uint32_t i = 0; i < 10; ++i) {
        fv.emplace(i);
    }

    uint32_t count = 0;
    for (auto& i : fv) {
        ASSERT_EQ(count, i);
        ++count;
    }

    fv.remove(fv.begin() + 7);

    auto it = fv.begin();
    ASSERT_EQ(fv.end(), it + 9);

    for (uint32_t i = 0; i < 9; ++i) {
        ASSERT_EQ(*it, (i < 7 ? i : i + 1));
        ++it;
    }
}
