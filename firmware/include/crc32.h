#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace crc32util {

uint32_t init();
uint32_t update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t finalize(uint32_t crc);
uint32_t compute(const uint8_t *data, size_t len);

}  // namespace crc32util
