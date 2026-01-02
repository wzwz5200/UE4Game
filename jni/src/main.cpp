
#include "driver.h"
#include "init/init.h"
#include <chrono>
#include <iostream>
#include <thread> // std::this_thread::sleep_for
#include <vector>
int main() {

  Driver myDriver;
  InitCheat(&myDriver); // 使用 & 取地址符
}