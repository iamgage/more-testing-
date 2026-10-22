#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace TwitchBsr {
// Extracts a standard (non-zip64) zip held in memory into destDir.
// Rejects path traversal ("..", absolute paths). Returns false on any error.
bool ExtractZip(const uint8_t* data, size_t size, const std::string& destDir);
}
