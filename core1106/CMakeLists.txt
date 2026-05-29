# ============================================================
# MyoSign Core1106 (RV1106) 应用 — 顶层 CMake
# ============================================================
# 交叉编译：默认使用幸狐 RV1106 Buildroot SDK 的工具链
#   export PATH=$RV1106_SDK/output/host/bin:$PATH
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/rv1106-toolchain.cmake
#   cmake --build build -j
# 也可以本机 x86_64 跑 -DENABLE_SIM=ON 进入仿真模式（不使用真硬件）

cmake_minimum_required(VERSION 3.16)
project(myosign_core1106 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_SIM        "PC 本地仿真编译（不用真实硬件）" OFF)
option(ENABLE_NPU        "启用 RKNN NPU 推理（需要 librknnrt.so）" OFF)
option(ENABLE_ASR        "链接离线 ASR 引擎（sherpa-onnx）"      OFF)
option(ENABLE_TTS        "链接离线 TTS 引擎"                    OFF)

add_compile_options(-Wall -Wextra -Wno-unused-parameter -O2 -g)

if(ENABLE_SIM)
    add_compile_definitions(MYOSIGN_SIM=1)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# ---- 核心模块（始终编译，不依赖 ML 模型）----
set(CORE_SRCS
    src/main.c
    src/protocol.c
    src/uart_rx.c
    src/lcd.c
    src/audio_out.c
    src/mic_in.c
    src/power.c
    src/log.c
    src/ring_buf.c
    src/event_bus.c
    src/ui.c
)

# ---- 模型相关源文件（ML 模型就绪后再编译进来）----
set(ML_SRCS
    src/emg_infer.c        # 当前为 stub
    src/asr_engine.c       # 当前为 stub
    src/tts_engine.c       # 当前为 stub
    src/sentence_fixer.c   # 语序整理模型 stub
)

add_executable(myosign ${CORE_SRCS} ${ML_SRCS})

target_link_libraries(myosign PRIVATE pthread m)

if(NOT ENABLE_SIM)
    target_link_libraries(myosign PRIVATE asound)   # ALSA for I²S
endif()

if(ENABLE_NPU)
    target_link_libraries(myosign PRIVATE rknnrt)
    target_compile_definitions(myosign PRIVATE MYOSIGN_ENABLE_NPU=1)
endif()

if(ENABLE_ASR)
    target_compile_definitions(myosign PRIVATE MYOSIGN_ENABLE_ASR=1)
endif()

if(ENABLE_TTS)
    target_compile_definitions(myosign PRIVATE MYOSIGN_ENABLE_TTS=1)
endif()

install(TARGETS myosign DESTINATION /opt/myosign/bin)
install(DIRECTORY assets/ DESTINATION /opt/myosign/assets)
