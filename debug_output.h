#pragma once

#include <windows.h>
#include <string>

namespace PbkitSdlGpu {
#define PBKITSDLGPU_ASSERT(c) \
  if (!(c)) { \
    PbkitSdlGpu::PrintAssertAndWaitForever(#c, __FILE__, __LINE__); \
  }

template <typename... VarArgs>
inline void PrintMsg(const char* fmt, VarArgs&&... args) {
  int string_length = snprintf_(nullptr, 0, fmt, args...);
  std::string buf;
  buf.resize(string_length);

  snprintf_(&buf[0], string_length + 1, fmt, args...);
  DbgPrint("%s", buf.c_str());
}

void PrintAssertAndWaitForever(const char* assert_code, const char* filename, uint32_t line);

}  // namespace PbkitSdlGpu

