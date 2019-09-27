//
// Created by jiahua on 2019/9/27.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <atomic>

template<typename T>
class MrswPtr {
public:
    class Reader {
        friend class MrswPtr;

    private:
        MrswPtr *ptr_;

        explicit Reader(MrswPtr<T> *ptr) : ptr_(ptr) {}

    public:
        Reader(const Reader &other) {
            other.ptr_->PrepareRead();
            ptr_ = other.ptr_;
        };

        Reader(Reader &&other) noexcept : ptr_(other.ptr_) {
            other.ptr_ = nullptr;
        }

        const T &operator*() const {
            return *ptr_->Get();
        }

        const T *operator->() const {
            return ptr_->Get();
        }

        const T *Get() const {
            return ptr_->Get();
        }

        explicit operator bool() const {
            return ptr_->Get() != nullptr;
        }

        ~Reader() {
            if (ptr_)
                ptr_->FinishRead();
        }
    };

    class Writer {
        friend class MrswPtr;

    private:
        MrswPtr *ptr_;

        explicit Writer(MrswPtr *ptr) : ptr_(ptr) {}

    public:
        Writer(const Writer &other) = delete;

        Writer(Writer &&other) noexcept: ptr_(other.ptr_) {
            other.ptr_ = nullptr;
        }

        ~Writer() {
            if (ptr_)
                ptr_->FinishWrite();
        }

        T &operator*() {
            return *ptr_->Get();
        }

        T *operator->() {
            return ptr_->Get();
        }

        T *Swap(T *ptr) {
            return ptr_->Swap(ptr);
        }
    };

public:
    explicit MrswPtr(T *ptr) : data_(PtrToData(ptr, 0)) {}

    MrswPtr(const MrswPtr &other) : data_(other.data_.load()) {}

    MrswPtr(MrswPtr &&other) noexcept: data_(other.data_.load()) {
        other.data_.store(0);
    }

    Reader GetReader() {
        PrepareRead();
        return std::move(Reader(this));
    }

    T Load() {
        Reader r = GetReader();
        return *r;
    }

    Writer GetWriter() {
        PrepareWrite();
        return std::move(Writer(this));
    }

    void Store(const T &value) {
        Writer w = GetWriter();
        *w = value;
    }

    void Store(T &&value) noexcept {
        Writer w = GetWriter();
        *w = value;
    }

private:
    std::atomic<uint64_t> data_;
private:
    static T *DataToPtr(uint64_t data, uint64_t *meta) {
        if (meta) *meta = data & 0xffffull;
        return reinterpret_cast<T *>(data >> 16ull);
    }

    static uint64_t PtrToData(T *ptr, uint64_t meta) {
        meta &= 0xffffull;
        uint64_t data = reinterpret_cast<uint64_t>(ptr) << 16ull;
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

    T *Swap(T *p) {
        // Swap must be used under the protection of PrepareWrite
        uint64_t value = data_.load();
        again:
        uint64_t meta = GetMeta(value);
        uint64_t new_value = PtrToData(p, meta);
        bool ok = data_.compare_exchange_strong(value, new_value);
        if (!ok) {
            // this can only be changed by thread who increase the read counter
            goto again;
        }
        return DataToPtr(value, nullptr);
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

    T *Get() {
        return DataToPtr(data_.load(), nullptr);
    }


};
