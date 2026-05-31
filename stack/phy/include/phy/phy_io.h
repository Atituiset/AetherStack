#ifndef AETHER_PHY_PHY_IO_H
#define AETHER_PHY_PHY_IO_H

#include <complex>
#include <cstdint>
#include <vector>

namespace phy {

// Serialize IQ samples to byte buffer: [uint32_t count, float re0, float im0, ...]
std::vector<uint8_t> iq_to_bytes(const std::vector<std::complex<float>>& samples);

// Deserialize IQ samples from byte buffer
std::vector<std::complex<float>> bytes_to_iq(const uint8_t* data, size_t len);

// Serialize bitstream to byte buffer: [uint32_t bit_count, uint8_t packed_bits...]
std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits);

// Deserialize bitstream from byte buffer
std::vector<uint8_t> bytes_to_bits(const uint8_t* data, size_t len);

}

#endif
