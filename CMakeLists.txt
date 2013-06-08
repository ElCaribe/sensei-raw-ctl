cmake_minimum_required (VERSION 2.8.5)
project (sensei-raw-ctl C)
set (project_VERSION "1.0")

find_package (PkgConfig REQUIRED)
pkg_check_modules (dependencies REQUIRED libusb-1.0)
include_directories (${dependencies_INCLUDE_DIRS})

option (DEVELOPER_MODE "Developer mode" OFF)

include (GNUInstallDirs)
configure_file (${PROJECT_SOURCE_DIR}/config.h.in
	${PROJECT_BINARY_DIR}/config.h)
include_directories (${PROJECT_BINARY_DIR})

add_executable (${PROJECT_NAME} ${PROJECT_NAME}.c)
target_link_libraries (${PROJECT_NAME} ${dependencies_LIBRARIES})
install (TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})

pkg_check_modules (gtk3 gtk+-3.0)
set (BUILD_GUI ${gtk3_FOUND} CACHE BOOL "Whether to build the GTK+ frontend")

if (BUILD_GUI)
	include_directories (${gtk3_INCLUDE_DIRS})
	link_directories (${gtk3_LIBRARY_DIRS})

	set (ui_in  ${PROJECT_SOURCE_DIR}/${PROJECT_NAME}-gui.ui)
	set (ui_out ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-gui-ui.c)
	add_custom_command (OUTPUT ${ui_out}
		COMMAND ${CMAKE_COMMAND} -D "input=${ui_in}" -D "output=${ui_out}"
			-D var_name=ui -P "${PROJECT_SOURCE_DIR}/quote-file.cmake"
		DEPENDS ${ui_in}
		COMMENT "Wrapping the UI file into a source file" VERBATIM)

	configure_file (${PROJECT_SOURCE_DIR}/${PROJECT_NAME}-gui.desktop.in
		${PROJECT_BINARY_DIR}/${PROJECT_NAME}-gui.desktop)
	install (FILES ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-gui.desktop
		DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
	install (FILES ${PROJECT_SOURCE_DIR}/${PROJECT_NAME}-gui.svg
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

	set (polkit_id "org.${PROJECT_NAME}.policy")
	configure_file (${PROJECT_SOURCE_DIR}/${polkit_id}.in
		${PROJECT_BINARY_DIR}/${polkit_id})
	install (FILES ${PROJECT_BINARY_DIR}/${polkit_id}
		DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)

	add_executable (${PROJECT_NAME}-gui ${PROJECT_NAME}-gui.c ${ui_out})
	set_target_properties (${PROJECT_NAME}-gui PROPERTIES
		COMPILE_FLAGS "${gtk3_CFLAGS_OTHER}")
	target_link_libraries (${PROJECT_NAME}-gui ${gtk3_LIBRARIES})
	install (TARGETS ${PROJECT_NAME}-gui
		DESTINATION ${CMAKE_INSTALL_BINDIR})
endif (BUILD_GUI)

find_program (HELP2MAN_EXECUTABLE help2man)
if (NOT HELP2MAN_EXECUTABLE)
	message (FATAL_ERROR "help2man not found")
endif ()

foreach (page ${PROJECT_NAME})
	set (page_output "${PROJECT_BINARY_DIR}/${page}.1")
	list (APPEND project_MAN_PAGES "${page_output}")
	add_custom_command (OUTPUT ${page_output}
		COMMAND ${HELP2MAN_EXECUTABLE} -N
			"${PROJECT_BINARY_DIR}/${page}" -o ${page_output}
		DEPENDS ${PROJECT_NAME}
		COMMENT "Generating man page for ${page}" VERBATIM)
endforeach ()

add_custom_target (docs ALL DEPENDS ${project_MAN_PAGES})

foreach (page ${project_MAN_PAGES})
	string (REGEX MATCH "\\.([0-9])" manpage_suffix "${page}")
	install (FILES "${page}"
		DESTINATION "${CMAKE_INSTALL_MANDIR}/man${CMAKE_MATCH_1}")
endforeach ()

set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "SteelSeries Sensei Raw control utility")
set (CPACK_PACKAGE_VERSION ${project_VERSION})
set (CPACK_PACKAGE_VENDOR "Premysl Janouch")
set (CPACK_PACKAGE_CONTACT "Přemysl Janouch <p.janouch@gmail.com>")
set (CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")

set (CPACK_GENERATOR "TGZ;ZIP")
set (CPACK_PACKAGE_FILE_NAME
	"${PROJECT_NAME}-${project_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

set (CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set (CPACK_SOURCE_IGNORE_FILES "/\\\\.git;/build;/CMakeLists.txt.user")
set (CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${project_VERSION}")

set (CPACK_SET_DESTDIR TRUE)
include (CPack)
