#include "raster/image_io.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace nq {
namespace {

uint32_t crc32_u32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return crc;
}

uint32_t adler32_u32(const uint8_t* data, size_t len) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (size_t i = 0; i < len; ++i) {
    a = (a + data[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

void put_be32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void put_chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body) {
  put_be32(out, static_cast<uint32_t>(body.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), body.begin(), body.end());
  const uint32_t crc = crc32_u32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
  put_be32(out, crc);
}

}  // namespace

bool write_ppm(const Surface& surface, const char* path) {
  if (!surface.valid()) return false;
  std::FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", surface.width, surface.height);
  std::vector<uint8_t> row(static_cast<size_t>(surface.width) * 3);
  for (int y = 0; y < surface.height; ++y) {
    const uint8_t* src = surface.row(y);
    for (int x = 0; x < surface.width; ++x) {
      uint8_t rgb[3];
      const uint8_t* p = src + static_cast<size_t>(x) * 4;
      unpremul_on_white(p[0], p[1], p[2], p[3], rgb);
      row[static_cast<size_t>(x) * 3 + 0] = rgb[0];
      row[static_cast<size_t>(x) * 3 + 1] = rgb[1];
      row[static_cast<size_t>(x) * 3 + 2] = rgb[2];
    }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
  return true;
}

// 真 PNG（RGB8），deflate 用 stored 块：无压缩但格式完全合法，零依赖且易审计。
// 后续自研 inflate 落地后替换为固定/动态 Huffman，体积可降 10-50 倍。
bool write_png(const Surface& surface, const char* path) {
  if (!surface.valid()) return false;

  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(surface.height) *
              (static_cast<size_t>(surface.width) * 3 + 1));
  for (int y = 0; y < surface.height; ++y) {
    raw.push_back(0);  // filter type: none
    const uint8_t* src = surface.row(y);
    for (int x = 0; x < surface.width; ++x) {
      uint8_t rgb[3];
      const uint8_t* p = src + static_cast<size_t>(x) * 4;
      unpremul_on_white(p[0], p[1], p[2], p[3], rgb);
      raw.push_back(rgb[0]);
      raw.push_back(rgb[1]);
      raw.push_back(rgb[2]);
    }
  }

  std::vector<uint8_t> idat;
  idat.push_back(0x78);  // zlib header (deflate, 32K window)
  idat.push_back(0x01);
  size_t offset = 0;
  while (offset < raw.size()) {
    const size_t chunk = std::min<size_t>(65535, raw.size() - offset);
    const bool final = (offset + chunk) >= raw.size();
    idat.push_back(final ? 1 : 0);  // final + stored
    idat.push_back(static_cast<uint8_t>(chunk & 0xFF));
    idat.push_back(static_cast<uint8_t>((chunk >> 8) & 0xFF));
    idat.push_back(static_cast<uint8_t>((~chunk) & 0xFF));
    idat.push_back(static_cast<uint8_t>(((~chunk) >> 8) & 0xFF));
    idat.insert(idat.end(), raw.begin() + static_cast<long>(offset),
                raw.begin() + static_cast<long>(offset + chunk));
    offset += chunk;
  }
  const uint32_t adler = adler32_u32(raw.data(), raw.size());
  put_be32(idat, adler);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  put_be32(ihdr, static_cast<uint32_t>(surface.width));
  put_be32(ihdr, static_cast<uint32_t>(surface.height));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // color type: truecolor
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace
  put_chunk(png, "IHDR", ihdr);
  put_chunk(png, "IDAT", idat);
  put_chunk(png, "IEND", {});

  std::FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  const size_t written = std::fwrite(png.data(), 1, png.size(), f);
  std::fclose(f);
  return written == png.size();
}

}  // namespace nq
