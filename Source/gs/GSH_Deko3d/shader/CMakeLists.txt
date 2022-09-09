cmake_minimum_required(VERSION 3.5)

project(GSH_Deko3d_Shaders)

set(VERTEX_SHADERS
	triangle_vsh.glsl
)

set(FRAGMENT_SHADERS
	color_fsh.glsl
)

foreach(shader ${VERTEX_SHADERS})
	get_filename_component(shader_we ${shader} NAME_WE)
	add_custom_command(OUTPUT "${shader_we}.dksh"
		COMMAND uam -s vert "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" -o "${shader_we}.dksh"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
		COMMENT "Compiling ${shader} to ${shader_we}.dksh"
	)
	list(APPEND SHADER_DKSHS "${shader_we}.dksh")
endforeach()

foreach(shader ${FRAGMENT_SHADERS})
	get_filename_component(shader_we ${shader} NAME_WE)
	add_custom_command(OUTPUT "${shader_we}.dksh"
		COMMAND uam -s frag "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" -o "${shader_we}.dksh"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
		COMMENT "Compiling ${shader} to ${shader_we}.dksh"
	)
	list(APPEND SHADER_DKSHS "${shader_we}.dksh")
endforeach()

foreach(dksh ${SHADER_DKSHS})
	get_filename_component(dksh_we ${dksh} NAME_WE)
	add_custom_command(OUTPUT "${dksh_we}_dksh.o"
		COMMAND ${CMAKE_OBJCOPY} -I binary -O elf64-littleaarch64 --binary-architecture aarch64 "${dksh}" "${dksh_we}_dksh.o"
		DEPENDS "${dksh}"
		COMMENT "Objcopying ${dksh} to ${dksh_we}_dksh.o"
	)
	list(APPEND SHADER_OBJECTS "${dksh_we}_dksh.o")
endforeach()

add_library(gsh_deko3d_shaders STATIC
	${SHADER_OBJECTS}
)

set_target_properties(gsh_deko3d_shaders
	PROPERTIES LINKER_LANGUAGE C
)

