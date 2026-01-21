cmake_minimum_required(VERSION 3.20)

# 查找 Fcitx5
find_package(Fcitx5Core 5.0 REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS Pinyin)
find_package(Fcitx5Utils REQUIRED)
find_package(ECM REQUIRED NO_MODULE)

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(FeatureSummary)
include(ECMSetupVersion)
include(ECMUninstallTarget)

# Fcitx5 安装路径
if(NOT DEFINED FCITX5_ADDON_INSTALL_DIR)
    set(FCITX5_ADDON_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/fcitx5")
endif()

if(NOT DEFINED FCITX5_DATA_INSTALL_DIR)
    set(FCITX5_DATA_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/fcitx5")
endif()

if(NOT DEFINED FCITX5_CONFIG_INSTALL_DIR)
    set(FCITX5_CONFIG_INSTALL_DIR "${FCITX5_DATA_INSTALL_DIR}/inputmethod")
endif()

if(NOT DEFINED FCITX5_ADDON_CONFIG_INSTALL_DIR)
    set(FCITX5_ADDON_CONFIG_INSTALL_DIR "${FCITX5_DATA_INSTALL_DIR}/addon")
endif()

# 构建 Sime fcitx5 插件
add_library(sime MODULE
    sime.cpp
    sime-state.cpp
)

target_include_directories(sime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sime
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Utils
    sime_core  # Sime 核心库
)

# 设置输出名称
set_target_properties(sime PROPERTIES
    PREFIX ""  # 不添加 "lib" 前缀
    OUTPUT_NAME "sime"
)

# 安装插件
install(TARGETS sime
    LIBRARY DESTINATION "${FCITX5_ADDON_INSTALL_DIR}"
)

# 配置文件处理
configure_file(data/sime.conf.in sime.conf @ONLY)
configure_file(data/sime-addon.conf.in sime-addon.conf @ONLY)

# 安装配置文件
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sime.conf"
        DESTINATION "${FCITX5_CONFIG_INSTALL_DIR}")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sime-addon.conf"
        DESTINATION "${FCITX5_ADDON_CONFIG_INSTALL_DIR}")

# 安装图标
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/icon/sime.png")
    install(FILES data/icon/sime.png
            DESTINATION "${FCITX5_DATA_INSTALL_DIR}/inputmethod")
endif()

message(STATUS "Fcitx5 Sime plugin configuration:")
message(STATUS "  Plugin install dir: ${FCITX5_ADDON_INSTALL_DIR}")
message(STATUS "  Config install dir: ${FCITX5_CONFIG_INSTALL_DIR}")
message(STATUS "  Addon config dir: ${FCITX5_ADDON_CONFIG_INSTALL_DIR}")
