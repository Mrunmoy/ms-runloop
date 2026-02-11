#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rpc {

// Lock-free Single-Producer Single-Consumer ring buffer.
//
// Designed to live in shared memory. The control block (head/tail offsets)
// and data region are laid out contiguously so the entire buffer can be
// placed in a single mmap'd region.
//
// Template parameter `Size` must be a power of 2 (enables bitmask
// wraparound instead of modulo).

template <uint32_t Size>
class RingBuffer {
    static_assert(Size > 0 && (Size & (Size - 1)) == 0,
                  "RingBuffer size must be a power of 2");

public:
    // Control block — lives at the start of the shared memory region.
    // Offsets are monotonically increasing; masked when indexing into data_.
    struct alignas(64) ControlBlock {
        std::atomic<uint32_t> head{0};  // written by producer
        char pad1[60];                  // avoid false sharing
        std::atomic<uint32_t> tail{0};  // written by consumer
        char pad2[60];
    };

    RingBuffer() = default;

    // Not copyable or movable (lives in shared memory)
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Reset the buffer to empty state.
    void reset() {
        ctrl_.head.store(0, std::memory_order_relaxed);
        ctrl_.tail.store(0, std::memory_order_relaxed);
    }

    // ── Producer API ────────────────────────────────────────────────

    // Returns the number of bytes available for writing.
    uint32_t writeAvailable() const {
        uint32_t head = ctrl_.head.load(std::memory_order_relaxed);
        uint32_t tail = ctrl_.tail.load(std::memory_order_acquire);
        return Size - (head - tail);
    }

    // Write `len` bytes from `data` into the ring buffer.
    // Returns true on success, false if insufficient space.
    bool write(const void* data, uint32_t len) {
        uint32_t head = ctrl_.head.load(std::memory_order_relaxed);
        uint32_t tail = ctrl_.tail.load(std::memory_order_acquire);

        if (Size - (head - tail) < len) {
            return false;  // not enough space
        }

        uint32_t offset = head & (Size - 1);
        uint32_t firstChunk = Size - offset;

        if (firstChunk >= len) {
            // Contiguous write
            std::memcpy(data_ + offset, data, len);
        } else {
            // Wraps around
            std::memcpy(data_ + offset, data, firstChunk);
            std::memcpy(data_,
                        static_cast<const uint8_t*>(data) + firstChunk,
                        len - firstChunk);
        }

        ctrl_.head.store(head + len, std::memory_order_release);
        return true;
    }

    // ── Consumer API ────────────────────────────────────────────────

    // Returns the number of bytes available for reading.
    uint32_t readAvailable() const {
        uint32_t head = ctrl_.head.load(std::memory_order_acquire);
        uint32_t tail = ctrl_.tail.load(std::memory_order_relaxed);
        return head - tail;
    }

    // Peek at the next `len` bytes without consuming them.
    // Returns true if `len` bytes are available, false otherwise.
    bool peek(void* dest, uint32_t len) const {
        uint32_t head = ctrl_.head.load(std::memory_order_acquire);
        uint32_t tail = ctrl_.tail.load(std::memory_order_relaxed);

        if (head - tail < len) {
            return false;
        }

        uint32_t offset = tail & (Size - 1);
        uint32_t firstChunk = Size - offset;

        if (firstChunk >= len) {
            std::memcpy(dest, data_ + offset, len);
        } else {
            std::memcpy(dest, data_ + offset, firstChunk);
            std::memcpy(static_cast<uint8_t*>(dest) + firstChunk,
                        data_,
                        len - firstChunk);
        }
        return true;
    }

    // Read `len` bytes from the ring buffer into `dest`.
    // Returns true on success, false if insufficient data.
    bool read(void* dest, uint32_t len) {
        uint32_t head = ctrl_.head.load(std::memory_order_acquire);
        uint32_t tail = ctrl_.tail.load(std::memory_order_relaxed);

        if (head - tail < len) {
            return false;
        }

        uint32_t offset = tail & (Size - 1);
        uint32_t firstChunk = Size - offset;

        if (firstChunk >= len) {
            std::memcpy(dest, data_ + offset, len);
        } else {
            std::memcpy(dest, data_ + offset, firstChunk);
            std::memcpy(static_cast<uint8_t*>(dest) + firstChunk,
                        data_,
                        len - firstChunk);
        }

        ctrl_.tail.store(tail + len, std::memory_order_release);
        return true;
    }

    // Skip `len` bytes without reading them.
    // Returns true on success, false if insufficient data.
    bool skip(uint32_t len) {
        uint32_t head = ctrl_.head.load(std::memory_order_acquire);
        uint32_t tail = ctrl_.tail.load(std::memory_order_relaxed);

        if (head - tail < len) {
            return false;
        }

        ctrl_.tail.store(tail + len, std::memory_order_release);
        return true;
    }

    // ── Capacity ────────────────────────────────────────────────────

    static constexpr uint32_t capacity() { return Size; }

    bool empty() const { return readAvailable() == 0; }
    bool full()  const { return writeAvailable() == 0; }

private:
    ControlBlock ctrl_;
    uint8_t data_[Size];
};

} // namespace rpc
