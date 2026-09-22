#include "path_util.h"
#include "board_config.h"
#include <string.h>
#include <ctype.h>

bool isSafeFileNameComponent(const char *component) {
  if (!component || !*component) {
    return false;
  }
  if (strcmp(component, ".") == 0 || strcmp(component, "..") == 0) {
    return false;
  }
  for (const char *p = component; *p; ++p) {
    const unsigned char c = (unsigned char)*p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-' || c == ' ';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool normalizePath(const char *input, char *out, size_t outLen) {
  if (!input || !out || outLen < 2) {
    return false;
  }
  if (strchr(input, '\\') != nullptr) {
    return false;
  }
  if (strlen(input) >= 2 && isalpha((unsigned char)input[0]) && input[1] == ':') {
    return false;
  }
  if (strlen(input) > COMSTORAGE_MAX_PATH) {
    return false;
  }

  char tmp[COMSTORAGE_MAX_PATH + 1];
  strncpy(tmp, input, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  char assembled[COMSTORAGE_MAX_PATH + 1];
  assembled[0] = '/';
  assembled[1] = '\0';
  size_t aLen = 1;

  char *save = nullptr;
  for (char *tok = strtok_r(tmp, "/", &save); tok != nullptr; tok = strtok_r(nullptr, "/", &save)) {
    if (tok[0] == '\0' || strcmp(tok, ".") == 0) {
      continue;
    }
    if (strcmp(tok, "..") == 0) {
      return false;
    }
    if (!isSafeFileNameComponent(tok)) {
      return false;
    }
    const size_t tLen = strlen(tok);
    const size_t need = (aLen > 1 ? 1 : 0) + tLen;
    if (aLen + need >= sizeof(assembled)) {
      return false;
    }
    if (aLen > 1) {
      assembled[aLen++] = '/';
    }
    memcpy(assembled + aLen, tok, tLen);
    aLen += tLen;
    assembled[aLen] = '\0';
  }

  if (aLen >= outLen) {
    return false;
  }
  memcpy(out, assembled, aLen + 1);
  return true;
}
