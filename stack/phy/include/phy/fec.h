#ifndef AETHER_PHY_FEC_H
#define AETHER_PHY_FEC_H

#include <cstdint>
#include <vector>

namespace phy {

// M9: forward error correction — convolutional code K=7 rate 1/2
// (polynomials 133o/171o, the classic NASA standard) with a hard-decision
// Viterbi decoder, plus CRC16-CCITT for transport-block integrity.
// Tail bits (K-1 zeros) flush the encoder so the decoder starts and ends
// in the zero state.

// CRC16-CCITT (poly 0x1021, init 0xFFFF). Returns FCS for `data`.
uint16_t crc16(const std::vector<uint8_t>& data);

// Append CRC16 (little-endian) to payload. Returns payload+2 bytes.
std::vector<uint8_t> crc_attach(const std::vector<uint8_t>& payload);

// Verify and strip: on success fills `payload` and returns true.
bool crc_verify_strip(const std::vector<uint8_t>& block,
                      std::vector<uint8_t>& payload);

// Number of coded bits produced by encode() for k information bits.
size_t coded_size(size_t n_info_bits);

// Convolutional encode: k bits -> 2k coded bits (G1 first, then G2).
std::vector<uint8_t> fec_encode(const std::vector<uint8_t>& bits);

// Hard-decision Viterbi decode of 2n coded bits -> n information bits.
// `soft` allows per-bit erasure marking (0/1 = decision, 2 = unknown):
// erased bits contribute a neutral metric instead of a hard penalty.
std::vector<uint8_t> fec_decode(const std::vector<uint8_t>& coded_bits,
                                const std::vector<uint8_t>* soft = nullptr);

// Convenience: bytes <-> bit vector (LSB-first within each byte), matching
// the project's air-bit convention.
std::vector<uint8_t> bytes_to_bits_fec(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> bits_to_bytes_fec(const std::vector<uint8_t>& bits);

}

#endif
