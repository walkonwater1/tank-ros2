# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.0.3")

# 配置文件统一放在 config/ 下：生成的 sdkconfig 与构建默认值都在这里。
# ⚠️ idf_component.yml / Kconfig.projbuild 是 ESP-IDF 组件元数据，必须留在 main/。
set(SDKCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/config/sdkconfig")
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/config/sdkconfig.defaults;${CMAKE_CURRENT_SOURCE_DIR}/config/sdkconfig.defaults.esp32s3")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

