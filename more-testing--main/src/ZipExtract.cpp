#include "ZipExtract.hpp"

#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

static uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t U32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool SafeName(std::string& name) {
    for (char& c : name) if (c == '\\') c = '/';
    if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos) return false;
    size_t i = 0;
    while (i <= name.size()) {
        size_t j = name.find('/', i);
        if (j == std::string::npos) j = name.size();
        if (name.substr(i, j - i) == "..") return false;
        i = j + 1;
    }
    return true;
}

bool TwitchBsr::ExtractZip(const uint8_t* d, size_t size, const std::string& destDir) {
    if (size < 22) return false;
    // Find End Of Central Directory record.
    size_t eocd = std::string::npos;
    size_t lo = size > 65557 ? size - 65557 : 0;
    for (size_t i = size - 22 + 1; i-- > lo;) {
        if (U32(d + i) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd == std::string::npos) return false;
    uint16_t entries = U16(d + eocd + 10);
    uint32_t cdOff = U32(d + eocd + 16);
    if (cdOff >= size) return false;

    fs::create_directories(destDir);
    size_t p = cdOff;
    for (uint16_t e = 0; e < entries; e++) {
        if (p + 46 > size || U32(d + p) != 0x02014b50) return false;
        uint16_t method = U16(d + p + 10);
        uint32_t csize = U32(d + p + 20), usize = U32(d + p + 24);
        uint16_t nlen = U16(d + p + 28), xlen = U16(d + p + 30), clen = U16(d + p + 32);
        uint32_t lho = U32(d + p + 42);
        if (p + 46 + nlen > size) return false;
        std::string name((const char*)d + p + 46, nlen);
        p += 46 + nlen + xlen + clen;

        if (!SafeName(name)) return false;
        if (name.back() == '/') { fs::create_directories(fs::path(destDir) / name); continue; }
        if (usize > 256u * 1024u * 1024u) return false;
        if ((size_t)lho + 30 > size || U32(d + lho) != 0x04034b50) return false;
        size_t dataOff = (size_t)lho + 30 + U16(d + lho + 26) + U16(d + lho + 28);
        if (dataOff + csize > size) return false;

        std::vector<uint8_t> out(usize);
        if (method == 0) {
            if (csize != usize) return false;
            if (usize) memcpy(out.data(), d + dataOff, usize);
        } else if (method == 8) {
            z_stream zs{};
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
            zs.next_in = (Bytef*)(d + dataOff);
            zs.avail_in = csize;
            zs.next_out = out.data();
            zs.avail_out = usize;
            int rc = inflate(&zs, Z_FINISH);
            inflateEnd(&zs);
            if (rc != Z_STREAM_END && !(rc == Z_OK && usize == 0)) return false;
        } else {
            return false;  // unsupported compression
        }
        fs::path outPath = fs::path(destDir) / name;
        fs::create_directories(outPath.parent_path());
        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write((const char*)out.data(), (std::streamsize)out.size());
    }
    return true;
}
