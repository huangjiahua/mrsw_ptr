//
// Created by jiahua on 2019/9/26.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <atomic>

struct MrswPtrTrail {
    std::atomic<uint64_t> data_;

    explicit MrswPtrTrail(long *ptr) : data_(PtrToData(ptr, 0)) {}

    ~MrswPtrTrail() {
        long *ptr = DataToPtr(data_.load(), nullptr);
        delete ptr;
    }

    long *Ptr() {
        return DataToPtr(data_.load(), nullptr);
    }

    static long *DataToPtr(uint64_t data, uint64_t *meta) {
        if (meta) *meta = data & 0xffffull;
        return reinterpret_cast<long *>(data >> 16u);
    }

    static uint64_t PtrToData(long *ptr, uint64_t meta) {
        meta &= 0xffffull;
        uint64_t data = reinterpret_cast<uint64_t>(ptr) << 16u;
        return data | meta;
    }

    static bool IsFree(uint64_t meta) {
        return (meta & 0xC000ull) == 0;
    }

    static bool IsBeingWritten(uint64_t meta) {
        return (meta & 0x8000ull) != 0;
    }

    static bool IsBeingRead(uint64_t meta) {
        return (meta & 0x4000ull) != 0;
    }

    static uint64_t GetMeta(uint64_t value) {
        return (value & 0xffffull);
    }

    static uint64_t GetCounter(uint64_t value) {
        return (value & 0x3fffull);
    }

    void PrepareWrite() {
        start:
        uint64_t value = data_.load();
        again:
        uint64_t meta = GetMeta(value);

        if (IsFree(meta)) {
            // good, no one is touching this, I'm gonna start write mode by setting the write bit
            uint64_t desired = value | 0x8000ull;
            bool ok = data_.compare_exchange_strong(value, desired);
            if (!ok) {
                // oops, seems that other thread changes this, maybe starts reading or writing or increase writing and
                // not yet start read mode. Anyway, let's see what happened
                goto again;
            }
        } else {
            // no, someone is using this, I need to wait for it(by busy waiting, so I need to load again)
            goto start;
        }
    }

    void FinishWrite() {
        // I can assure that no one can touch this right now, so I just need to clear the write bit
        data_.fetch_and(~(0x8000ull));
    }

    void PrepareRead() {
        start:
        uint64_t value = data_.fetch_add(1);
        value += 1;
        again:
        uint64_t meta = GetMeta(value);
        uint64_t counter = GetCounter(value);

        if (IsBeingRead(meta)) {
            // good, other threads(or one thread) are reading, just join them(the counter represents how many
            // threads are currently reading)
            return;
        } else if (IsBeingWritten(meta)) {
            // bad, one thread is writing, try again and do busy waiting(the counter represents how many
            // threads are waiting for the writer to end)
            value = data_.load();
            goto again;
        } else {
            // well, it seems that this thread should start read mode(by setting the read bit, and the counter
            // represents how many threads are trying to initiate read mode. Only one can do this by getting true
            // in the CAS and others will get false and try again)
            uint64_t desired = value | 0x4000ull;
            bool ok = data_.compare_exchange_strong(value, desired);
            if (!ok) {
                // no! the value has been changed by others, let's look how it changes(the 'value' has been
                // changed by the CAS)
                goto again;
            }
        }
    }

    void FinishRead() {
        uint64_t value = data_.fetch_sub(1);
        value -= 1;
        again:
        uint64_t counter = GetCounter(value);
        if (counter == 0) {
            // it seems that I'm the last reader, let's try to end the read mode(by clearing the read bit)
            uint64_t desired = value & (~0x4000ull);
            data_.compare_exchange_strong(value, desired);
            // oops, at least one thread has changed this value. there is no need to try again, because the only
            // possible change is someone start reading it and increase the counter, so I can say I'm not the last
            // reader and it is not my responsibility to stop the read mode
        }
    }
};
