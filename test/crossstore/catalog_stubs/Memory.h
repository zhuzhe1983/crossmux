#pragma once
#include <cstdint>
#include <memory>
namespace memory {
using ByteBuffer = std::unique_ptr<uint8_t[]>;
}
