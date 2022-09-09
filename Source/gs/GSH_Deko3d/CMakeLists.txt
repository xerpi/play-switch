cmake_minimum_required(VERSION 3.5)

set(CMAKE_MODULE_PATH
	${CMAKE_CURRENT_SOURCE_DIR}/../../../deps/Dependencies/cmake-modules
	${CMAKE_CURRENT_SOURCE_DIR}/../../../deps/Framework/build_cmake
	${CMAKE_MODULE_PATH}
)

include(Header)
include(Framework_Common)

project(GSH_Deko3d)

add_subdirectory(shader)

add_library(gsh_deko3d STATIC 
	GSH_Deko3d.cpp
	GSH_Deko3d.h
	#GSH_Deko3d_Shader.cpp
	#GSH_Deko3d_Texture.cpp
)

target_include_directories(gsh_deko3d PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/
	# FIXME
	../../../deps/Framework/include/
	/opt/devkitpro/portlibs/switch/include
)

target_link_libraries(gsh_deko3d
	${gsh_deko3d_PROJECT_LIBS}
	gsh_deko3d_shaders
)
