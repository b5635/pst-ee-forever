// SPDX-License-Identifier: GPL-2.0-or-later
// Deflate decoder written from RFC 1951 (canonical Huffman codes decoded by code length counts).
#include "core/inflate.h"

namespace pstf {
namespace {

struct Bits {
  const uint8_t* p;
  size_t n, pos = 0;  // byte position
  uint32_t buf = 0;
  int cnt = 0;
  bool err = false;
  int Get(int need) {
    while (cnt < need) {
      if (pos >= n) {
        err = true;
        return 0;
      }
      buf |= uint32_t(p[pos++]) << cnt;
      cnt += 8;
    }
    const int v = int(buf & ((1u << need) - 1u));
    buf >>= need;
    cnt -= need;
    return v;
  }
  void Align() {
    buf = 0;
    cnt = 0;
  }
};

struct Huffman {
  uint16_t count[16];   // codes per length
  uint16_t symbol[320];  // symbols ordered by code
};

// Builds the table; returns false on an over-subscribed set (incomplete sets are allowed, as zlib does
// for single-code distance trees).
bool Build(Huffman* h, const uint8_t* lengths, int n) {
  for (int i = 0; i < 16; ++i) h->count[i] = 0;
  for (int i = 0; i < n; ++i) h->count[lengths[i]]++;
  if (h->count[0] == n) return true;
  int left = 1;
  for (int len = 1; len < 16; ++len) {
    left <<= 1;
    left -= h->count[len];
    if (left < 0) return false;
  }
  uint16_t offs[16];
  offs[1] = 0;
  for (int len = 1; len < 15; ++len) offs[len + 1] = uint16_t(offs[len] + h->count[len]);
  for (int i = 0; i < n; ++i)
    if (lengths[i]) h->symbol[offs[lengths[i]]++] = uint16_t(i);
  return true;
}

int Decode(Bits& b, const Huffman& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; ++len) {
    code |= b.Get(1);
    if (b.err) return -1;
    const int count = h.count[len];
    if (code - count < first) return h.symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(Bits& b, std::vector<uint8_t>* out, const Huffman& lit, const Huffman& dist) {
  for (;;) {
    int sym = Decode(b, lit);
    if (sym < 0) return false;
    if (sym < 256) {
      out->push_back(uint8_t(sym));
      continue;
    }
    if (sym == 256) return true;
    sym -= 257;
    if (sym >= 29) return false;
    const int len = kLenBase[sym] + b.Get(kLenExtra[sym]);
    const int ds = Decode(b, dist);
    if (ds < 0 || ds >= 30) return false;
    const size_t d = size_t(kDistBase[ds]) + size_t(b.Get(kDistExtra[ds]));
    if (b.err || d > out->size()) return false;
    const size_t from = out->size() - d;
    for (int i = 0; i < len; ++i) out->push_back((*out)[from + size_t(i)]);
  }
}

bool Fixed(Bits& b, std::vector<uint8_t>* out) {
  static Huffman lit, dist;
  static bool built = false;
  if (!built) {
    uint8_t l[288];
    int i = 0;
    for (; i < 144; ++i) l[i] = 8;
    for (; i < 256; ++i) l[i] = 9;
    for (; i < 280; ++i) l[i] = 7;
    for (; i < 288; ++i) l[i] = 8;
    Build(&lit, l, 288);
    for (i = 0; i < 30; ++i) l[i] = 5;
    Build(&dist, l, 30);
    built = true;
  }
  return Codes(b, out, lit, dist);
}

bool Dynamic(Bits& b, std::vector<uint8_t>* out) {
  static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  const int nlen = b.Get(5) + 257, ndist = b.Get(5) + 1, ncode = b.Get(4) + 4;
  if (b.err || nlen > 286 || ndist > 30) return false;
  uint8_t lengths[320] = {};
  for (int i = 0; i < ncode; ++i) lengths[order[i]] = uint8_t(b.Get(3));
  Huffman lencode, lit, dist;
  if (b.err || !Build(&lencode, lengths, 19)) return false;
  int index = 0;
  while (index < nlen + ndist) {
    int sym = Decode(b, lencode);
    if (sym < 0) return false;
    if (sym < 16) {
      lengths[index++] = uint8_t(sym);
      continue;
    }
    uint8_t len = 0;
    int rep;
    if (sym == 16) {
      if (index == 0) return false;
      len = lengths[index - 1];
      rep = 3 + b.Get(2);
    } else if (sym == 17) {
      rep = 3 + b.Get(3);
    } else {
      rep = 11 + b.Get(7);
    }
    if (b.err || index + rep > nlen + ndist) return false;
    while (rep--) lengths[index++] = len;
  }
  if (lengths[256] == 0) return false;
  if (!Build(&lit, lengths, nlen) || !Build(&dist, lengths + nlen, ndist)) return false;
  return Codes(b, out, lit, dist);
}

}  // namespace

bool RawInflate(const uint8_t* data, size_t size, std::vector<uint8_t>* out) {
  Bits b{data, size};
  int last;
  do {
    last = b.Get(1);
    const int type = b.Get(2);
    if (b.err) return false;
    if (type == 0) {
      b.Align();
      if (b.pos + 4 > size) return false;
      const unsigned len = data[b.pos] | (data[b.pos + 1] << 8);
      const unsigned nlen = data[b.pos + 2] | (data[b.pos + 3] << 8);
      b.pos += 4;
      if ((len ^ 0xFFFFu) != nlen || b.pos + len > size) return false;
      out->insert(out->end(), data + b.pos, data + b.pos + len);
      b.pos += len;
    } else if (type == 1) {
      if (!Fixed(b, out)) return false;
    } else if (type == 2) {
      if (!Dynamic(b, out)) return false;
    } else {
      return false;
    }
  } while (!last);
  return true;
}

bool ZlibInflate(const uint8_t* data, size_t size, std::vector<uint8_t>* out, size_t expected) {
  if (size < 2) return false;
  const unsigned cmf = data[0], flg = data[1];
  if ((cmf & 0x0F) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) return false;
  out->clear();
  if (expected) out->reserve(expected);
  if (!RawInflate(data + 2, size - 2, out)) return false;
  if (expected && out->size() != expected) return false;
  // Adler-32 trailer: the last 4 bytes, big endian.
  if (size >= 6) {
    uint32_t a = 1, s = 0;
    for (uint8_t v : *out) {
      a = (a + v) % 65521u;
      s = (s + a) % 65521u;
    }
    const uint8_t* t = data + size - 4;
    const uint32_t want = (uint32_t(t[0]) << 24) | (uint32_t(t[1]) << 16) | (uint32_t(t[2]) << 8) | t[3];
    if (((s << 16) | a) != want) return false;
  }
  return true;
}

}  // namespace pstf
