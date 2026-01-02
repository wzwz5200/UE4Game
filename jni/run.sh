#!/bin/bash

# --- 配置区 ---
BINARY_NAME="UE4Game"              # 对应 CMake 中的可执行文件名
REMOTE_PATH="/data/local/tmp"      # Android 端的临时存放路径
BUILD_DIR="build_android"          # 编译存放目录

# 1. 检查环境变量


# 2. 编译流程
echo "🚀 开始编译..."
mkdir -p $BUILD_DIR && cd $BUILD_DIR

cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=Debug

make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "❌ 编译失败！"
    exit 1
fi

cd ..

# 3. 推送与运行
echo "📲 正在推送到设备: $REMOTE_PATH/$BINARY_NAME"

# 检查设备连接
adb wait-for-device

# 推送二进制文件
adb push $BUILD_DIR/$BINARY_NAME $REMOTE_PATH/

# 赋予执行权限并运行
echo "----------------------------------------"
echo "🔥 运行输出:"
adb shell "su -c 'chmod 777 $REMOTE_PATH/$BINARY_NAME && $REMOTE_PATH/$BINARY_NAME'"