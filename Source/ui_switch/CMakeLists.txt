cmake_minimum_required(VERSION 3.5)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/Dependencies/cmake-switch")

include(Header)
include(SwitchTools)

find_package(Libnx REQUIRED)
find_package(Deko3d REQUIRED)

project(Play_Switch)

link_libraries(
	switch::libnx
)

if(NOT TARGET PlayCore)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../
		${CMAKE_CURRENT_BINARY_DIR}/Source
	)
endif()
list(APPEND PROJECT_LIBS PlayCore)

if(NOT TARGET gsh_deko3d)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../gs/GSH_Deko3d
		${CMAKE_CURRENT_BINARY_DIR}/gs/GSH_Deko3d
	)
endif()
list(APPEND PROJECT_LIBS gsh_deko3d)

add_executable(${PROJECT_NAME}
	main.cpp
)

target_link_libraries(${PROJECT_NAME}
	${PROJECT_LIBS}
	switch::deko3dd
)

set_target_properties(${PROJECT_NAME} PROPERTIES
	APP_TITLE "${PROJECT_NAME}"
	APP_AUTHOR "${PROJECT_NAME} authors"
	APP_VERSION "${PROJECT_Version}")

add_nro_target(${PROJECT_NAME})
