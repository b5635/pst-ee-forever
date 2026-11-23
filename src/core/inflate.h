// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal zlib (RFC 1950) / deflate (RFC 1951) decoder for our own offline data files (sprite depth
// .DEP streams). Pure logic, no allocation beyond the output vector.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pstf {

// Inflates a zlib stream (2-byte header, deflate blocks, Adler-32 trailer checked when present).
// `expected` > 0 reserves the output and rejects any other size. False on any format error.
bool ZlibInflate(const uint8_t* data, size_t size, std::vector<uint8_t>* out, size_t expected = 0);
// Raw deflate blocks (no header).
bool RawInflate(const uint8_t* data, size_t size, std::vector<uint8_t>* out);

}  // namespace pstf
