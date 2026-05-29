# ============================================================
# MyoSign ESP32-S3 固件 — 顶层 CMake 配置
# ============================================================
# ESP-IDF 使用 CMake 构建系统
# 这个文件告诉 ESP-IDF 项目名称、依赖组件、源文件路径

cmake_minimum_required(VERSION 3.16)

# ---- 包含 ESP-IDF 构建系统 ----
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ---- 项目名称（会出现在编译输出中）----
project(myosign_core)
