#pragma once        // 告诉编译器这个文件只包含一次
#include "driver.h" // 假设你的头文件名为 driver.h
#include <iostream>
std::string GetNameFromIndex(Driver *Drv, uintptr_t GNameArrayAddr,
                             uint32_t Index);

#include <cstdint>
template <class T> struct TArray {
  friend class FString;

public:
  T *Data;       // 0x00: 指向堆内存中实际数据的指针
  int32_t Count; // 0x08: 当前数组元素数量
  int32_t Max;   // 0x0C: 数组最大容量
};

void InitCheat(Driver *Drv);
void PrintGname();
