# ============================================================
# RV1106 (幸狐 Core1106) 交叉编译工具链
# ============================================================
# 默认假设 SDK 解压在 ~/rv1106-sdk，主机工具链路径如下；
# 用 -DRV1106_SDK=/path/to/sdk 覆盖。

if(NOT DEFINED RV1106_SDK)
    set(RV1106_SDK "$ENV{HOME}/rv1106-sdk")
endif()

set(TOOLCHAIN_PREFIX "${RV1106_SDK}/output/host/bin/arm-rockchip830-linux-uclibcgnueabihf-")

set(CMAKE_SYSTEM_NAME       Linux)
set(CMAKE_SYSTEM_PROCESSOR  arm)

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_AR           "${TOOLCHAIN_PREFIX}ar")
set(CMAKE_STRIP        "${TOOLCHAIN_PREFIX}strip")

set(CMAKE_SYSROOT      "${RV1106_SDK}/output/host/arm-rockchip830-linux-uclibcgnueabihf/sysroot")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

add_compile_options(-march=armv7-a -mfpu=neon -mfloat-abi=hard)
