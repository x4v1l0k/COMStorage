#pragma once

#include <Arduino.h>
#include <stddef.h>

// Path validation / normalization for the virtual filesystem root.
// Rejects traversal, Windows separators, absolute escapes, and unsafe chars.

bool normalizePath(const char *input, char *out, size_t outLen);
bool isSafeFileNameComponent(const char *component);
