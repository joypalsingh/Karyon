#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

namespace dexe {

// Thrown when an input file (zip, dex, ...) is malformed.
class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Bounds-checked little-endian reader over an in-memory byte buffer.
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data, size_t pos = 0) : data_(data) { seek(pos); }

    size_t pos() const { return pos_; }
    size_t remaining() const { return data_.size() - pos_; }

    void seek(size_t pos) {
        if (pos > data_.size()) throw FormatError("read position out of bounds");
        pos_ = pos;
    }
    void skip(size_t n) {
        need(n);
        pos_ += n;
    }

    uint8_t u1() {
        need(1);
        return data_[pos_++];
    }
    uint16_t u2() {
        need(2);
        uint16_t v = uint16_t(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }
    uint32_t u4() {
        need(4);
        uint32_t v = uint32_t(data_[pos_]) | uint32_t(data_[pos_ + 1]) << 8 |
                     uint32_t(data_[pos_ + 2]) << 16 | uint32_t(data_[pos_ + 3]) << 24;
        pos_ += 4;
        return v;
    }

    uint32_t uleb128() {
        uint32_t result = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            uint8_t b = u1();
            result |= uint32_t(b & 0x7f) << shift;
            if (!(b & 0x80)) return result;
        }
        throw FormatError("malformed uleb128");
    }
    int32_t sleb128() {
        uint32_t result = 0;
        int shift = 0;
        uint8_t b;
        do {
            if (shift >= 35) throw FormatError("malformed sleb128");
            b = u1();
            result |= uint32_t(b & 0x7f) << shift;
            shift += 7;
        } while (b & 0x80);
        if (shift < 32 && (b & 0x40)) result |= ~0u << shift;
        return int32_t(result);
    }
    // uleb128p1: encodes -1 (NO_INDEX) as 0.
    int64_t uleb128p1() { return int64_t(uleb128()) - 1; }

    std::span<const uint8_t> bytes(size_t n) {
        need(n);
        auto s = data_.subspan(pos_, n);
        pos_ += n;
        return s;
    }

private:
    void need(size_t n) const {
        if (n > data_.size() - pos_) throw FormatError("unexpected end of data");
    }

    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

}  // namespace dexe
