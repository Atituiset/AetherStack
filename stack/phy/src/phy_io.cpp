#include "phy/phy_io.h"
#include <cstring>

namespace phy {

std::vector<uint8_t> iq_to_bytes(const std::vector<std::complex<float>>& samples) {
    uint32_t count = static_cast<uint32_t>(samples.size());
    size_t data_len = sizeof(uint32_t) + count * sizeof(float) * 2;
    std::vector<uint8_t> buf(data_len);
    std::memcpy(buf.data(), &count, sizeof(uint32_t));
    float* ptr = reinterpret_cast<float*>(buf.data() + sizeof(uint32_t));
    for (uint32_t i = 0; i < count; ++i) {
        ptr[i * 2] = samples[i].real();
        ptr[i * 2 + 1] = samples[i].imag();
    }
    return buf;
}

std::vector<std::complex<float>> bytes_to_iq(const uint8_t* data, size_t len) {
    if (len < sizeof(uint32_t)) return {};
    uint32_t count;
    std::memcpy(&count, data, sizeof(uint32_t));
    size_t expected = sizeof(uint32_t) + count * sizeof(float) * 2;
    if (len < expected) return {};
    const float* ptr = reinterpret_cast<const float*>(data + sizeof(uint32_t));
    std::vector<std::complex<float>> samples(count);
    for (uint32_t i = 0; i < count; ++i) {
        samples[i] = {ptr[i * 2], ptr[i * 2 + 1]};
    }
    return samples;
}

std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits) {
    uint32_t bit_count = static_cast<uint32_t>(bits.size());
    size_t byte_count = (bit_count + 7) / 8;
    size_t data_len = sizeof(uint32_t) + byte_count;
    std::vector<uint8_t> buf(data_len, 0);
    std::memcpy(buf.data(), &bit_count, sizeof(uint32_t));
    for (uint32_t i = 0; i < bit_count; ++i) {
        if (bits[i]) {
            buf[sizeof(uint32_t) + i / 8] |= (1 << (i % 8));
        }
    }
    return buf;
}

std::vector<uint8_t> bytes_to_bits(const uint8_t* data, size_t len) {
    if (len < sizeof(uint32_t)) return {};
    uint32_t bit_count;
    std::memcpy(&bit_count, data, sizeof(uint32_t));
    size_t byte_count = (static_cast<size_t>(bit_count) + 7) / 8;
    if (byte_count > len - sizeof(uint32_t)) return {}; // truncated / malformed
    std::vector<uint8_t> bits(bit_count);
    for (uint32_t i = 0; i < bit_count; ++i) {
        uint8_t byte_val = data[sizeof(uint32_t) + i / 8];
        bits[i] = (byte_val >> (i % 8)) & 1;
    }
    return bits;
}

}
