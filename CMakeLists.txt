cmake_minimum_required(VERSION 3.16)
project(Application VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ====== 查找 Qt6 ======
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets OpenGLWidgets)
find_package(Qt6Mqtt REQUIRED)

# ====== Protobuf 配置 ======
set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)

# 获取 protoc 路径（用于生成 .pb 文件）
# 方法1：使用 find_package 设置的变量（推荐）
if(NOT Protobuf_PROTOC_EXECUTABLE)
    # 方法2：直接在 PATH 中查找 protoc 可执行文件
    find_program(Protobuf_PROTOC_EXECUTABLE protoc REQUIRED)
endif()
message(STATUS "Using protoc: ${Protobuf_PROTOC_EXECUTABLE}")

# ====== 项目源文件 ======
set(PROJECT_SOURCES
    main.cpp
    widget.cpp
    widget.h
    widget.ui
    play.h
    play.cpp
    videowidget.h
    videowidget.cpp
    # radarwidget.h
    # radarwidget.cpp
    hevcnalparser.h
    hevcnalparser.cpp
)

# ====== Protobuf 生成 ======
set(ROBOMASTER_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/robomaster_protocol.proto)
set(GENERATED_PROTO_CC ${CMAKE_CURRENT_BINARY_DIR}/robomaster_protocol.pb.cc)
set(GENERATED_PROTO_H  ${CMAKE_CURRENT_BINARY_DIR}/robomaster_protocol.pb.h)

if(NOT EXISTS "${ROBOMASTER_PROTO_FILE}")
    message(FATAL_ERROR "Proto file not found: ${ROBOMASTER_PROTO_FILE}")
endif()

add_custom_command(
    OUTPUT ${GENERATED_PROTO_CC} ${GENERATED_PROTO_H}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
        -I${CMAKE_CURRENT_SOURCE_DIR}/proto
        ${ROBOMASTER_PROTO_FILE}
    DEPENDS ${ROBOMASTER_PROTO_FILE}
    COMMENT "Generating robomaster_protocol protobuf"
)
list(APPEND PROJECT_SOURCES ${GENERATED_PROTO_CC})

# ====== FFmpeg 配置 (使用 pkg-config) ======
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
pkg_check_modules(AVFILTER REQUIRED libavfilter)
pkg_check_modules(AVDEVICE REQUIRED libavdevice)

# ====== 创建可执行文件 ======
qt_add_executable(Application
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    play.ui
)

# ====== 包含目录 ======
target_include_directories(Application PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
    ${AVFILTER_INCLUDE_DIRS}
    ${AVDEVICE_INCLUDE_DIRS}
)

# ====== 链接库 ======
target_link_libraries(Application PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Mqtt
    Qt6::OpenGLWidgets
    protobuf::libprotobuf
    absl::log_internal_nullguard
    absl::strings
    absl::synchronization
    absl::time
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
    ${AVFILTER_LIBRARIES}
    ${AVDEVICE_LIBRARIES}
)

# ====== 平台特定配置 ======
if(WIN32)
    # 假设 FFMPEG_DIR 在 Windows 下已定义（例如通过外部变量）
    if(NOT DEFINED FFMPEG_DIR)
        set(FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg)
    endif()
    # 复制 FFmpeg DLL
    add_custom_command(TARGET Application POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${FFMPEG_DIR}/bin
        $<TARGET_FILE_DIR:Application>
        COMMENT "复制 FFmpeg DLL 到输出目录"
    )
else()
    # Linux 下复制 QSS 和可能需要的资源
    add_custom_command(TARGET Application POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/style.qss
            $<TARGET_FILE_DIR:Application>
        COMMENT "Copying style.qss"
    )
endif()

# ====== 基础属性 ======
set_target_properties(Application PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# ====== 安装规则 ======
include(GNUInstallDirs)
install(TARGETS Application
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(Application)