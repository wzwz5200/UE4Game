#include "init/init.h"
#include "driver.h"
#include <cstdint>
#include <ios>
#include <unistd.h>
#include <vector>

#include <codecvt> // 用于宽字符转换
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream> // 引入 stringstream 用于缓冲
#include <string>
#include <thread> // 用于 sleep
#include <vector>
using namespace std;

using UTF16 = uint16_t;

// ==========================================
// 1. 类型定义修复 (解决 Unknown type name 'init32_t' 报错)
// ==========================================
#include <cstdint>
struct FVector {
  float x, y, z;
};
constexpr uintptr_t OFFSET_LOCALPLAYERS_ARRAY = 0x38;
void PrintGname() { std::cout << "Hello" << std::endl; }

static inline uintptr_t Align(uintptr_t val, uintptr_t align) {
  return (val + align - 1) & ~(align - 1);
}

namespace Offsets {
// 【必须修改】你需要填入逆向得到的 GNames 偏移
// 4.18 版本通常是一个全局变量的偏移
uintptr_t GNames = 0x134AD4F8;

// UE 4.18 结构偏移 (根据你提供的 "和平精英" 数据)
uintptr_t PointerSize = 8;              // 64位指针大小
uintptr_t FNameEntryToNameString = 0xC; // 名字字符串在结构体中的偏移
} // namespace Offsets
uintptr_t libUE4Base = 0; // 模块基址

// 获取真实地址
uintptr_t getRealOffset(uintptr_t offset) { return libUE4Base + offset; }

// 读取指针
uintptr_t getPtr(uintptr_t addr, Driver *Drv) {
  return Drv->read_safe<uintptr_t>(addr);
}

// 简单读取 ANSI 字符串
std::string ReadStr2(uintptr_t addr, int len, Driver *Drv) {
  if (len <= 0 || len > 1024)
    return ""; // 安全检查
  char *buffer = new char[len + 1];
  Drv->read_safe(addr, buffer, len);
  buffer[len] = '\0';
  std::string str(buffer);
  delete[] buffer;
  return str;
}

// ==========================================
// Unicode 处理 (针对 UE4 的 UTF-16)
// ==========================================
struct WideStr {
  static int is_surrogate(uint64_t uc) { return (uc - 0xd800u) < 2048u; }
  static int is_high_surrogate(UTF16 uc) { return (uc & 0xfffffc00) == 0xd800; }
  static int is_low_surrogate(UTF16 uc) { return (uc & 0xfffffc00) == 0xdc00; }
  static wchar_t surrogate_to_utf32(UTF16 high, UTF16 low) {
    return (high << 10) + low - 0x35fdc00;
  }

  static std::string getString(uintptr_t StrPtr, int StrLength, Driver *Drv) {
    if (StrLength > 512)
      StrLength = 512; // 限制长度

    // 读取原始 UTF16 数据
    vector<UTF16> source(StrLength);
    Drv->read_safe(StrPtr, source.data(), StrLength * sizeof(UTF16));

    // 转换为 wchar_t
    wstring wstr;
    wstr.reserve(StrLength);

    for (int i = 0; i < StrLength; i++) {
      const UTF16 uc = source[i];
      if (!is_surrogate(uc)) {
        wstr += (wchar_t)uc;
      } else {
        if (i + 1 < StrLength && is_high_surrogate(uc) &&
            is_low_surrogate(source[i + 1])) {
          wstr += surrogate_to_utf32(uc, source[i + 1]);
          i++; // 跳过下一个
        } else {
          wstr += L'?';
        }
      }
    }

    // 转换为 UTF-8 string (兼容性更好)
    // 注意：如果你是在 Windows 上编译，可能需要使用不同的转换方式
    // 这里使用 C++11 标准转换，但在 Android NDK 中通常有效
    try {
      std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
      return converter.to_bytes(wstr);
    } catch (...) {
      return "StringConvertError";
    }
  }
};

// ==========================================
// 核心：UE 4.18 GNames 解析
// ==========================================
string GetFNameFromID(int32_t index, Driver *Drv) {
  // 缓存 GNames 基址，避免重复读取和解密
  static uintptr_t TNameEntryArray = 0;

  if (TNameEntryArray == 0) {
    // 1. 获取 GNames 全局指针的地址
    uintptr_t GNamesPtrAddr = getRealOffset(Offsets::GNames);

    // 2. 读取 GNames 指向的 TNameEntryArray
    // 注意：如果有加密（如 XOR），在这里处理。例如：
    // uintptr_t encValue = getPtr(GNamesPtrAddr);
    // TNameEntryArray = encValue ^ 0x7878787878787878;

    // 如果无加密：
    TNameEntryArray = getPtr(GNamesPtrAddr, Drv);

    if (TNameEntryArray == 0) {
      // 尝试直接读取（视具体实现而定，有时 GNames 本身就是基址而不是指针）
      // TNameEntryArray = GNamesPtrAddr;
      return "Error: GNames Base is NULL";
    }
  }

  // 3. 计算 Chunk 索引和内部偏移 (UE 4.18 标准: 16384 / 0x4000)
  int32_t chunkIdx = index / 0x4000;
  int32_t withinChunkIdx = index % 0x4000;

  // 4. 读取 Chunk 指针
  // TNameEntryArray 是一个指针数组，每个指针指向一个 Chunk
  uintptr_t ChunkPtr =
      getPtr(TNameEntryArray + (chunkIdx * Offsets::PointerSize), Drv);
  if (ChunkPtr == 0)
    return "None";

  // 5. 读取 FNameEntry 指针
  // Chunk 也是一个指针数组，每个元素指向具体的 FNameEntry
  uintptr_t FNameEntryPtr =
      getPtr(ChunkPtr + (withinChunkIdx * Offsets::PointerSize), Drv);
  if (FNameEntryPtr == 0)
    return "None";

  // 6. 读取 FNameEntry 头部信息 (获取长度和类型)
  // 4.18 中，Header 通常是 FNameEntry 的第一个成员 (Index 字段有时复用做
  // Header) 根据你的描述，这里可能是 int16
  int16_t FNameEntryHeader = Drv->read_safe<int16_t>(FNameEntryPtr);

  // 获取长度 (假设 Len 在 Header 中，且需要右移 6 位，这是 UE4 的典型压缩方式)
  int StrLength = FNameEntryHeader >> 6;

  if (StrLength > 0 && StrLength < 250) {
    bool wide = FNameEntryHeader & 1; // 宽字符标志位
    uintptr_t StrPtr = FNameEntryPtr + Offsets::FNameEntryToNameString;

    if (wide) {
      return WideStr::getString(StrPtr, StrLength, Drv);
    } else {
      return ReadStr2(StrPtr, StrLength, Drv);
    }
  }

  return "None";
}

void DumpStrings(Driver *Drv) {
  cout << "Dumping Strings (UE 4.18 Mode)..." << endl;
  ofstream file("/sdcard/Strings.txt");

  if (!file.is_open()) {
    cout << "无法创建 Strings.txt" << endl;
    return;
  }

  uint32_t count = 0;
  // UE 4.18 这里的 Limit 通常取决于游戏，通常 20万左右足够
  uint32_t GNameLimit = 200000;

  for (uint32_t i = 0; i < GNameLimit; i++) {
    string s = GetFNameFromID(i, Drv);
    if (s != "None" && !s.empty() && s.find("Error") == string::npos) {
      file << "[" << i << "]: " << s << endl;
      count++;
      if (count % 1000 == 0)
        cout << "Progress: " << i << endl;
    }
  }

  file.close();
  cout << "Dump 完成，共: " << count << " 条。" << endl;
}

void InitCheat(Driver *Drv) {

  pid_t pid = Drv->get_pid("com.tencent.tmgp.pubgmhd");
  if (pid <= 0) {
    cout << "未找到游戏进程" << endl;
    return;
  }
  Drv->initpid(pid);
  cout << "PID: " << pid << endl;

  // 3. 获取模块基址
  libUE4Base = Drv->get_module_base(pid, "libUE4.so");
  if (libUE4Base == 0) {
    cout << "未找到 libUE4.so" << endl;
    return;
  }

  cout << "libUE4 Base: " << hex << libUE4Base << dec << endl;
  Offsets::GNames = 0x134AD4F8; // <--- 修改这里
  // DumpStrings(Drv);
  uintptr_t GWorld = Drv->read_safe<uintptr_t>(libUE4Base + 0x1373A758);

  uintptr_t PersistentLevel = Drv->read_safe<uintptr_t>(GWorld + 0xB0);

  // 3. 获取 Actors 数组地址和数量
  uintptr_t ActorArrayPtr = Drv->read_safe<uintptr_t>(PersistentLevel + 0xA0);
  int ActorCount = Drv->read_safe<int>(PersistentLevel + 0xA8);

  printf("[+] GWorld: %p | Level: %p | Count: %d\n", (void *)GWorld,
         (void *)PersistentLevel, ActorCount);

  printf("[*] Starting Full Actor Dump (Total: %d)...\n", ActorCount);

  std::cout << std::hex << ActorArrayPtr << std::endl;
  constexpr uintptr_t UObject_NameIndex = 0x18; // 重点：必要时你自己校
  constexpr int MAX_ACTORS_SANITY = 200000;

  while (true) {
    // 1. 创建字符串流作为缓冲区 (比直接 cout 快且不会闪烁)
    std::ostringstream buffer;

    // 2. 写入 ANSI 转义码：清屏 + 光标归位 (左上角)
    // \033[2J 清屏, \033[H 光标移回 (0,0)
    buffer << "\033[2J\033[H";

    buffer << "=== Actor Monitor ===\n"; // 标题

    for (int i = 0; i < ActorCount && i < MAX_ACTORS_SANITY; i++) {
      uintptr_t Actor =
          Drv->read_safe<uintptr_t>(ActorArrayPtr + i * sizeof(uintptr_t));

      if (!Actor)
        continue;

      // UObject::NamePrivate (int32)
      int NameId = Drv->read_safe<int>(Actor + UObject_NameIndex);
      if (NameId <= 0)
        continue;

      // 关键：通过 GNames 拿字符串
      std::string Name = GetFNameFromID(NameId, Drv);

      uintptr_t object = Drv->read_safe<uintptr_t>(Actor + 0x278);

      FVector Pos;
      Drv->read_safe(object + 0x200, &Pos, sizeof(Pos));

      // 过滤坐标为0的对象 (通常是无效的)
      if (Pos.x == 0 && Pos.y == 0 && Pos.z == 0)
        continue;

      // 3. 将数据写入缓冲区，而不是直接 printf
      // 建议统一格式，之前你同时用了 cout 和 printf，这里统一整理

      // 1. 获取 GameInstance 地址

      auto GameInstance = Drv->read_safe<uintptr_t>(GWorld + 0xae8);
      if (!GameInstance)
        return;

      // 1. 定义偏移常量

      // 2. 读取 TArray 的 Data 指针
      // TArray 结构体位于 GameInstance + 0x48，结构体的第一个成员就是 Data 指针
      uintptr_t LocalPlayersData =
          Drv->read_safe<uintptr_t>(GameInstance + OFFSET_LOCALPLAYERS_ARRAY);

      // (可选) 读取数组大小进行检查，防止越界或读取无效内存
      int32_t LocalPlayersCount = Drv->read_safe<int32_t>(
          GameInstance + OFFSET_LOCALPLAYERS_ARRAY + 0x8);

      if (!LocalPlayersData || LocalPlayersCount <= 0) {
        return; // 数组为空或读取失败
      }

      // 3. 获取第一个 LocalPlayer (index 0)
      // LocalPlayersData 指向的是一个指针列表 [ptr1, ptr2, ...]
      // 我们读取第0个位置的值，这个值就是 FLocalPlayer 的基地址
      uintptr_t pLocalPlayer = Drv->read_safe<uintptr_t>(LocalPlayersData);

      if (!pLocalPlayer) {
        return;
      }

      // ---------------------------------------------------------
      // 现在你拥有了 pLocalPlayer，可以根据你提供的结构体读取成员了
      // ---------------------------------------------------------

      // 示例：读取 ControllerId (偏移 0x108)
      int32_t ControllerId = Drv->read_safe<int32_t>(pLocalPlayer + 0x108);

      // 示例：读取 ViewportClient (偏移 0x0058)
      uintptr_t ViewportClient =
          Drv->read_safe<uintptr_t>(pLocalPlayer + 0x0058);

      // 示例：读取 PendingLevelPlayerControllerClass (偏移 0x0080)
      uintptr_t PendingController =
          Drv->read_safe<uintptr_t>(pLocalPlayer + 0x0080);
      float LastSpectatorStateSynchTime =
          Drv->read_safe<float>(PendingController + 0x0694);
      std::cout << std::hex << GWorld << endl;
    }

    // 4. 一次性将缓冲区内容输出到控制台
    std::cout << buffer.str() << std::flush;

    // 5. 必须加 Sleep，否则 CPU 占用过高且刷新太快人眼看不清
    std::this_thread::sleep_for(
        std::chrono::milliseconds(100)); // 100ms = 10 FPS
  }
}

// void InitCheat(Driver *Drv) {
//   if (!Drv->gid) {
//     std::cout << "驱动连接错误\n";
//     return;
//   }

//   Drv->cpuset(0, 4);
//   pid_t pid = Drv->get_pid("com.tencent.tmgp.pubgmhd");

//   if (0 < pid && pid < 32768) {
//     Drv->initpid(pid);
//   }
//   std::cout << "Pid: " << std::dec << pid << std::endl;

//   uint64_t so = Drv->get_module_base(pid, "libUE4.so");
//   std::cout << "so: " << std::hex << so << std::dec << std::endl;

//   // 定位 GNameArray
//   uintptr_t gNamesGlobalVar = so + 0x134AD4F8;
//   uintptr_t GNameArrayAddr = Drv->read_safe<uintptr_t>(gNamesGlobalVar);

//   if (!GNameArrayAddr) {
//     std::cout << "[-] 无法获取 GNames 基地址。" << std::endl;
//     return;
//   }

//   // 读取数量
//   int32_t numElements = Drv->read_safe<int32_t>(GNameArrayAddr + 5120);
//   int32_t numChunks = Drv->read_safe<int32_t>(GNameArrayAddr + 5124);

//   printf("[+] GNames: %p | Elements: %d | Chunks: %d\n", (void
//   *)GNameArrayAddr,
//          numElements, numChunks);

//   // 遍历所有 Index
//   //   for (int32_t i = 0; i < numElements; ++i) {
//   //     std::string name = GetNameFromIndex(Drv, GNameArrayAddr, i);

//   //     // 过滤掉无效或不需要显示的名称
//   //     if (name != "None" && !name.empty()) {
//   //       // 简单的可见字符过滤
//   //       if ((unsigned char)name[0] >= 32) {
//   //         printf("ID[%6d] %s\n", i, name.c_str());
//   //       }
//   //     }
//   //   }

//   // 1. 定位 GWorld

//   uintptr_t GWorld = Drv->read_safe<uintptr_t>(so + 0x1373A758);

//   if (GWorld) {

//     // 2. 获取 PersistentLevel (使用别人项目的 0x30 偏移)
//     uintptr_t PersistentLevel = Drv->read_safe<uintptr_t>(GWorld + 0x30);
//     if (!PersistentLevel) {
//       // 如果 0x30 没读到，尝试你 Dump 出来的 0xB0
//       PersistentLevel = Drv->read_safe<uintptr_t>(GWorld + 0xB0);
//     }

//     // 3. 获取 Actors 数组地址和数量 (使用 0xA0 和 0xA8)
//     uintptr_t ActorArrayPtr = Drv->read_safe<uintptr_t>(PersistentLevel +
//     0xA0); int ActorCount = Drv->read_safe<int>(PersistentLevel + 0xA8);

//     printf("[+] GWorld: %p | Level: %p | Count: %d\n", (void *)GWorld,
//            (void *)PersistentLevel, ActorCount);

//     printf("[*] Starting Full Actor Dump (Total: %d)...\n", ActorCount);

//     for (int i = 0; i < ActorCount; i++) {
//       // 1. 读取原始指针
//       uintptr_t currentActor =
//           Drv->read_safe<uintptr_t>(ActorArrayPtr + (i * 8));

//       // 如果指针是 0，说明这个位置是空的
//       if (!currentActor) {
//         printf("[%d] Address: NULL\n", i);
//         continue;
//       }

//       // 2. 读取 NameIndex (偏移 0x18)
//       init32_t_t nameIndex = Drv->read_safe<init32_t_t>(currentActor + 0x18);

//       // 3. 解析名称
//       std::string actorName = GetNameFromIndex(Drv, GNameArrayAddr,
//       nameIndex);

//       // 4. 强制输出所有信息
//       printf("[%d] Addr: %p | Idx: %u | Name: %s\n", i, (void *)currentActor,
//              nameIndex, actorName.c_str());
//     }
//   }

//   // 2. 直接从 World 读取 ActiveLevelActors (根据你的 Dump 偏移是 0xA80)
//   // TArray 结构：+0x0 是 DataPtr, +0x8 是 Count
// }
