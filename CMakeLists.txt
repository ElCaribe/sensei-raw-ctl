cmake_minimum_required (VERSION 2.8.4)
project (sensei-raw-ctl C)
set (project_VERSION "1.0")

find_package (PkgConfig REQUIRED)
pkg_check_modules (dependencies REQUIRED libusb-1.0)
include_directories (${dependencies_INCLUDE_DIRS})

configure_file (${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
	${CMAKE_CURRENT_BINARY_DIR}/config.h)
include_directories (${CMAKE_CURRENT_BINARY_DIR})

add_executable (${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_NAME}.c)
target_link_libraries (${CMAKE_PROJECT_NAME} ${dependencies_LIBRARIES})

include (GNUInstallDirs)
install (TARGETS ${CMAKE_PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})

