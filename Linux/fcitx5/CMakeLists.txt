cmake_minimum_required(VERSION 3.20)

# 查找 Fcitx5
find_package(Fcitx5Core 5.0 REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5Config REQUIRED)

# Fcitx5 安装路径
# 获取 fcitx5 的实际安装前缀
execute_process(COMMAND pkg-config --variable=prefix Fcitx5Core
                OUTPUT_VARIABLE FCITX5_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT FCITX5_PREFIX)
    set(FCITX5_PREFIX "/usr")
endif()

message(STATUS "Fcitx5 prefix: ${FCITX5_PREFIX}")

if(NOT DEFINED FCITX5_ADDON_INSTALL_DIR)
    set(FCITX5_ADDON_INSTALL_DIR "${FCITX5_PREFIX}/lib/fcitx5")
endif()

if(NOT DEFINED FCITX5_DATA_INSTALL_DIR)
    set(FCITX5_DATA_INSTALL_DIR "${FCITX5_PREFIX}/share/fcitx5")
endif()

if(NOT DEFINED FCITX5_CONFIG_INSTALL_DIR)
    set(FCITX5_CONFIG_INSTALL_DIR "${FCITX5_DATA_INSTALL_DIR}/inputmethod")
endif()

if(NOT DEFINED FCITX5_ADDON_CONFIG_INSTALL_DIR)
    set(FCITX5_ADDON_CONFIG_INSTALL_DIR "${FCITX5_DATA_INSTALL_DIR}/addon")
endif()

# 构建 Sime fcitx5 插件
add_library(fcitx5_sime MODULE
    src/sime-ime.cc
    src/sime-state.cc
)

target_include_directories(fcitx5_sime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${FCITX5_PREFIX}/include/Fcitx5/Module
)

target_link_libraries(fcitx5_sime
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Utils
    sime_core  # Sime 核心库
)

# 设置输出名称
set_target_properties(fcitx5_sime PROPERTIES
    OUTPUT_NAME "sime"
)

# 安装插件
install(TARGETS fcitx5_sime
    LIBRARY DESTINATION "${FCITX5_ADDON_INSTALL_DIR}"
)

# 配置文件处理
configure_file(data/sime.conf.in sime.conf @ONLY)
configure_file(data/sime-addon.conf.in sime-addon.conf @ONLY)

# 安装配置文件
# InputMethod 配置 - 注意：文件名必须不能是 sime.conf，否则 fcitx5 无法识别
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sime.conf"
        DESTINATION "${FCITX5_CONFIG_INSTALL_DIR}"
        RENAME "sime-pinyin.conf")

# Addon 配置 - 注意：必须命名为 sime.conf（不是 sime-addon.conf）
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sime-addon.conf"
        DESTINATION "${FCITX5_ADDON_CONFIG_INSTALL_DIR}"
        RENAME "sime.conf")

# 安装图标
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/icon/sime.svg")
    install(FILES data/icon/sime.svg
            DESTINATION "${FCITX5_PREFIX}/share/icons/hicolor/scalable/apps")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/icon/sime.png")
    install(FILES data/icon/sime.png
            DESTINATION "${FCITX5_DATA_INSTALL_DIR}/inputmethod")
endif()

message(STATUS "Fcitx5 Sime plugin configuration:")
message(STATUS "  Plugin install dir: ${FCITX5_ADDON_INSTALL_DIR}")
message(STATUS "  Config install dir: ${FCITX5_CONFIG_INSTALL_DIR}")
message(STATUS "  Addon config dir: ${FCITX5_ADDON_CONFIG_INSTALL_DIR}")
