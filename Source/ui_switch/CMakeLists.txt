cmake_minimum_required(VERSION 3.5)

include(Header)

project(Play_Switch)

link_libraries(
	nx
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
	deko3dd
)

nx_generate_nacp(${PROJECT_NAME}
	NAME "${PROJECT_NAME}"
	AUTHOR "${PROJECT_NAME} authors"
	VERSION "${PROJECT_Version}"
)

nx_create_nro(${PROJECT_NAME})
