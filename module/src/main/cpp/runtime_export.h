#ifndef IL2CPP_RUNTIME_EXPORT_H
#define IL2CPP_RUNTIME_EXPORT_H
#include <cstdint>
#include <string>
// 返回 false 时保留原 dump.cs，并通过 error 记录具体失败。
bool export_runtime_files(const std::string &directory, uint64_t image_base, std::string &error);
#endif
