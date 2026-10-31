# ============================================================================
#  SimpleRTS - CMake 构建脚本
#
#  和原来的 SimpleRTS.sln 完全等价：同一个可执行文件、同一批源文件、
#  同一条 include 路径、同样链接那 6 个 .lib。只是把「在 VS 属性页里手工填的
#  路径」改成了脚本里写死的变量。
#
#  用法（在仓库根目录，也就是本文件所在目录执行）：
#     cmake -S . -B build -G "Visual Studio 18 2026" -A x64
#     cmake --build build --config Debug
#     .\build\Debug\SimpleRTS.exe
#
#  注意：thirdparty 里预编译的 SDL3 / RVO 只有 x64 版本，所以必须用 -A x64。
# ============================================================================

cmake_minimum_required(VERSION 3.20)

project(SimpleRTS
    VERSION 1.0
    DESCRIPTION "Simple RTS game built on SDL3"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# MSVC 运行时库统一用 Release 版 /MD
#
# 原因：thirdparty/RVO2/lib/RVO.lib 是 /MD 编译的静态库（只有 .lib，没有源码），
# 如果 Debug 配置用默认的 /MDd，链接时必定报：
#     LNK2038: "RuntimeLibrary" 值 "MD_DynamicRelease" 不匹配 "MDd_DynamicDebug"
# 而且 SDL3 的几个 DLL 也都是 Release 版，跨堆分配内存时同样要求 CRT 一致。
# 所以这里所有配置都用 /MD，Debug 配置依然保留 /Od + /Zi 和调试断点。
# ---------------------------------------------------------------------------
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# 预编译库只有 64 位，32 位构建会链接失败，这里提前拦住
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "SimpleRTS 只支持 64 位构建（thirdparty 中的 SDL3/RVO 均为 x64）。\n"
        "请在配置时加上 -A x64。")
endif()

set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
set(GAME_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/SimpleRTS")

# ---------------------------------------------------------------------------
# 1. 源文件
#    CONFIGURE_DEPENDS 让 CMake 每次构建前重新扫描目录，以后往 SimpleRTS/
#    里新增 .cpp 不需要再手动改这份 CMakeLists（和 VS 里「添加现有项」同理）。
# ---------------------------------------------------------------------------
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
file(GLOB GAME_HEADERS CONFIGURE_DEPENDS "${GAME_DIR}/*.h")

if(NOT GAME_SOURCES)
    message(FATAL_ERROR "在 ${GAME_DIR} 下没有找到任何 .cpp 源文件。")
endif()

add_executable(SimpleRTS ${GAME_SOURCES} ${GAME_HEADERS})

# ---------------------------------------------------------------------------
# 2. 头文件搜索路径 —— 对应「附加包含目录」
# ---------------------------------------------------------------------------
target_include_directories(SimpleRTS PRIVATE
    "${GAME_DIR}"                                 # 自己的头文件，源码里是 #include "xxx.h"
    "${THIRDPARTY_DIR}/SDL3/include"
    "${THIRDPARTY_DIR}/SDL3_image/include"
    "${THIRDPARTY_DIR}/SDL3_ttf/include"
    "${THIRDPARTY_DIR}/SDL3_gfx/include"
    "${THIRDPARTY_DIR}/SDL3_mixer/include"
    "${THIRDPARTY_DIR}/RVO2/include"
)

# 对应工程属性里的「字符集 = Unicode」和「子系统 = 控制台」
target_compile_definitions(SimpleRTS PRIVATE
    _CONSOLE
    $<$<BOOL:${WIN32}>:UNICODE;_UNICODE>
)

# ---------------------------------------------------------------------------
# 3. 第三方库 —— 对应「附加库目录 + 附加依赖项」
#    直接按 .lib 的绝对路径导入，省掉 link_directories 这种全局副作用。
# ---------------------------------------------------------------------------
function(rts_import_static_lib target_name lib_path)
    if(NOT EXISTS "${lib_path}")
        message(FATAL_ERROR "找不到预编译库：${lib_path}")
    endif()
    add_library(${target_name} UNKNOWN IMPORTED)
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION "${lib_path}")
endfunction()

rts_import_static_lib(RVO        "${THIRDPARTY_DIR}/RVO2/lib/RVO.lib")
rts_import_static_lib(SDL3       "${THIRDPARTY_DIR}/SDL3/lib/x64/SDL3.lib")
rts_import_static_lib(SDL3_image "${THIRDPARTY_DIR}/SDL3_image/lib/x64/SDL3_image.lib")
rts_import_static_lib(SDL3_ttf   "${THIRDPARTY_DIR}/SDL3_ttf/lib/x64/SDL3_ttf.lib")
rts_import_static_lib(SDL3_mixer "${THIRDPARTY_DIR}/SDL3_mixer/lib/x64/SDL3_mixer.lib")
rts_import_static_lib(SDL3_gfx   "${THIRDPARTY_DIR}/SDL3_gfx/lib/SDL3_gfx.lib")

target_link_libraries(SimpleRTS PRIVATE
    RVO
    SDL3
    SDL3_ttf
    SDL3_gfx
    SDL3_image
    SDL3_mixer
)

# ---------------------------------------------------------------------------
# 4. 编译器 / 链接器细节
# ---------------------------------------------------------------------------
if(MSVC)
    # 字符集：源码目前是混合编码 —— 大部分旧文件是 GBK(CP936)，新增/改动过的
    # 文件（以及 game_object.h）保存为 UTF-8 with BOM，由 BOM 自动识别。
    # 代码里同时用了两种写法：
    #   u8"AI 玩家"        -> 必须被转成 UTF-8，交给 SDL_ttf 渲染
    #   MessageBoxA(..., "错误：...")  -> 必须保持 GBK，交给 ANSI API
    # 所以显式写死源字符集和执行字符集都是 936，结果和 VS 在中文系统上的默认行为
    # 完全一致，同时不受构建机当前代码页影响（否则在 UTF-8 代码页下会出 C4876 乱码）。
    # 注意：带 UTF-8 BOM 的文件，BOM 优先级高于 /source-charset，不受该选项影响。
    target_compile_options(SimpleRTS PRIVATE
        /source-charset:.936
        /execution-charset:.936
        /W3)

    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT SimpleRTS)
else()
    # 非 MSVC 工具链（如 MinGW）也能编，但需要自己保证能找到 x64 的 import lib
    target_compile_options(SimpleRTS PRIVATE -Wall)
endif()

# ---------------------------------------------------------------------------
# 5. 运行期依赖
#    程序在运行时按相对路径读取 "font/..." 和 "maps/"，并且要能找到 SDL3 的
#    几个 DLL。这里把它们统一拷到 exe 旁边，然后把调试工作目录设成 exe 目录，
#    这样 F5 直接能跑，不依赖源码目录。
# ---------------------------------------------------------------------------
set(RUNTIME_DLLS
    "${GAME_DIR}/SDL3.dll"
    "${GAME_DIR}/SDL3_image.dll"
    "${GAME_DIR}/SDL3_ttf.dll"
    "${GAME_DIR}/SDL3_mixer.dll"
)

foreach(_dll IN LISTS RUNTIME_DLLS)
    if(NOT EXISTS "${_dll}")
        message(WARNING "运行期 DLL 缺失：${_dll}")
    endif()
endforeach()

add_custom_command(TARGET SimpleRTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:SimpleRTS>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GAME_DIR}/font"
            "$<TARGET_FILE_DIR:SimpleRTS>/font"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GAME_DIR}/maps"
            "$<TARGET_FILE_DIR:SimpleRTS>/maps"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GAME_DIR}/defs"
            "$<TARGET_FILE_DIR:SimpleRTS>/defs"
    COMMENT "拷贝运行期 DLL / font / maps / defs 到输出目录"
    VERBATIM)

set_target_properties(SimpleRTS PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:SimpleRTS>"
)
