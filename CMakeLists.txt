# ===========================================================================
#  SuperCapacitor - 超级电容应用
#  芯片/板卡: HPM5151 / hpm5100evk      构建类型: flash_xip
#
#  代码分层（都在 src\ 下，只放你自己的代码；层级镜像 STM32 工程 User\）：
#    src\app\   应用层：业务逻辑、状态机、main()       <- User\App
#    src\hal\   硬件抽象层：hal_*                     <- User\HAL
#    src\drv\   驱动层：外设驱动，直接调用 SDK        <- User\Driver
#    src\lib\   算法 / 通用库层：lib_*                <- User\Lib
#    src\platform_config.h  全局配置                 <- User\platform_config.h
#
#  新增 .c/.h 文件：放进对应层即可，sdk_app_src_glob 自动纳入（需重新 cmake）。
#  .h 不用在 sdk_app_inc 里逐个登记，各层目录已加入头文件搜索路径；
#  但要把 .h 加进 app target，SES 工程树里才看得到、才能跳转。
#  CMAKE_EXPORT_COMPILE_COMMANDS 生成 compile_commands.json 供 clangd 索引。
# ===========================================================================

cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 工程名、固件名都取文件夹名 -> SuperCapacitor.elf/.bin/.emProject
get_filename_component(APP_DIR_NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
set(APP_NAME ${APP_DIR_NAME})

# 所有 SDK 配置开关必须写在 find_package 之前，写后面不生效
# set(CONFIG_FREERTOS 1)
# set(CONFIG_LWIP 1)
# set(HEAP_SIZE 0x4000)

# 板卡/构建类型默认值（命令行 -DBOARD=... -DHPM_BUILD_TYPE=... 可覆盖）
set(BOARD hpm5100evk CACHE STRING "Board name")
set(HPM_BUILD_TYPE flash_xip CACHE STRING "Build type")

# ===========================================================================
#  HPM SDK 环境自举
#  - SDK 源码已随工程携带：默认使用 ./hpm_sdk（drivers/soc/arch/boards 等）
#  - -DHPM_SDK_USE_EXTERNAL=ON 可切回 HPM_SDK_BASE 指向的外部 SDK
#  - openocd 已随工程携带：tools/openocd（SES 调试直接用，不依赖外部环境）
#  - 外部只用到"工具"：RISC-V GCC 工具链 / CMake / Ninja / Python，
#    来自官方 SDK 环境（HPM_SDK_EXTERNAL_ROOT）或系统 PATH
#  - SDK 自带 Python 前置到 PATH：SES/IAR 工程生成依赖其 jinja2，
#    系统 Python 通常没装 jinja2，否则 configure 阶段生成 IDE 工程会失败
# ===========================================================================
set(HPM_SDK_USE_EXTERNAL OFF CACHE BOOL "使用外部 HPM_SDK_BASE，而不是工程内 hpm_sdk/")

if(NOT HPM_SDK_USE_EXTERNAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/hpm_sdk/cmake/hpm-sdk-config.cmake")
    set(ENV{HPM_SDK_BASE} "${CMAKE_CURRENT_SOURCE_DIR}/hpm_sdk")
endif()
if(NOT DEFINED ENV{HPM_SDK_BASE} OR "$ENV{HPM_SDK_BASE}" STREQUAL "")
    message(FATAL_ERROR "未找到工程内 hpm_sdk/，且 HPM_SDK_BASE 未设置")
endif()
get_filename_component(HPM_SDK_ROOT "$ENV{HPM_SDK_BASE}" ABSOLUTE)

# 官方 SDK 环境根目录：工具链、SDK 自带 Python、openocd 从这里取
set(HPM_SDK_EXTERNAL_ROOT "F:/HPM_SDK" CACHE PATH "官方 HPM SDK 环境根目录")

if(NOT DEFINED ENV{GNURISCV_TOOLCHAIN_PATH} OR "$ENV{GNURISCV_TOOLCHAIN_PATH}" STREQUAL "")
    set(_hpm_tc_name "rv32imac_zicsr_zifencei_multilib_b_ext-win")
    foreach(_hpm_tc "${HPM_SDK_ROOT}/../toolchains/${_hpm_tc_name}" "${HPM_SDK_EXTERNAL_ROOT}/toolchains/${_hpm_tc_name}")
        if(EXISTS "${_hpm_tc}/bin/riscv32-unknown-elf-gcc.exe")
            set(ENV{GNURISCV_TOOLCHAIN_PATH} "${_hpm_tc}")
            break()
        endif()
    endforeach()
    if(NOT DEFINED ENV{GNURISCV_TOOLCHAIN_PATH} OR "$ENV{GNURISCV_TOOLCHAIN_PATH}" STREQUAL "")
        message(FATAL_ERROR "GNURISCV_TOOLCHAIN_PATH 未设置且自动推导失败")
    endif()
endif()

if(EXISTS "${HPM_SDK_ROOT}/../tools/python3")
    set(HPM_PYTHON_ROOT "${HPM_SDK_ROOT}/../tools/python3")
else()
    set(HPM_PYTHON_ROOT "${HPM_SDK_EXTERNAL_ROOT}/tools/python3")
endif()
# openocd 优先用工程内自带的 tools/openocd，缺失时才回退官方环境
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/openocd/openocd.exe")
    set(HPM_OPENOCD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tools/openocd")
else()
    set(HPM_OPENOCD_ROOT "${HPM_SDK_EXTERNAL_ROOT}/tools/openocd")
endif()
set(_hpm_path_old "$ENV{PATH}")
set(ENV{PATH} "${HPM_PYTHON_ROOT};${HPM_OPENOCD_ROOT};${_hpm_path_old}")

# SES 调试配置生成依赖 openocd 的配置文件目录（与 SDK start_cmd.cmd 保持一致）
if(NOT DEFINED ENV{OPENOCD_SCRIPTS} OR "$ENV{OPENOCD_SCRIPTS}" STREQUAL "")
    set(ENV{OPENOCD_SCRIPTS} "${HPM_SDK_ROOT}/boards/openocd")
endif()

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(${APP_DIR_NAME})

sdk_app_inc(src src/app src/hal src/drv src/lib src/lib/SEGGER_RTT)

sdk_app_src_glob(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drv/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/*.c   # 递归包含 src/lib/SEGGER_RTT/*.c
)

# 头文件同样登记到 app target（CMake 不会编译 .h，只让 SES 工程树显示出来）
# sdk_app_src_glob 内部是 GLOB_RECURSE，src/*.h 已递归包含 app/hal/drv/lib 下全部头文件，
# 不要再逐层追加通配符，否则同一文件会以重复节点出现在 SES 工程树里。
sdk_app_src_glob(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

sdk_compile_definitions(SUPERCAP_LOG=1)
sdk_compile_options(-Wall)

generate_ide_projects()              # 生成 SES / IAR 工程文件

# SES 工程保留在官方生成位置 build/<preset>/segger_embedded_studio；
# 这里把其中的本机绝对路径改为工程内相对路径（tools/openocd + hpm_sdk），
# 使该目录可以直接随仓库提交，别人 clone 后用 SES 打开即可编译/调试。
# 需要随仓库提交的生成物：segger_embedded_studio/ 与 build_tmp/generated/include/（见 .gitignore）。
execute_process(
    COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fix_ses_project.py" "${CMAKE_BINARY_DIR}"
    RESULT_VARIABLE _ses_fix_rc
)
if(NOT _ses_fix_rc EQUAL 0)
    message(FATAL_ERROR "修正 SES 工程路径失败 (rc=${_ses_fix_rc})")
endif()
