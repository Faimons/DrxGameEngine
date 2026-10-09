# Shader compilation (GLSL -> SPIR-V) using glslangValidator from the Vulkan SDK

find_program(DRX_GLSLANG_VALIDATOR
    NAMES glslangValidator glslang
    HINTS
        $ENV{VULKAN_SDK}/bin
        $ENV{VULKAN_SDK}/Bin
)

if(NOT DRX_GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found. Install the Vulkan SDK (or glslang-tools) and set VULKAN_SDK.")
endif()

# Compiles shaders into C headers (uint32 arrays) and makes them available to `target`.
# Each shader `name.ext` becomes "Shaders/name.ext.h" with the array `kShader_name_ext`.
function(drx_embed_shaders target)
    set(output_dir ${CMAKE_BINARY_DIR}/generated/Shaders)
    file(MAKE_DIRECTORY ${output_dir})

    set(generated_headers)
    foreach(shader ${ARGN})
        get_filename_component(shader_name ${shader} NAME)
        string(REPLACE "." "_" symbol ${shader_name})
        set(header ${output_dir}/${shader_name}.h)

        add_custom_command(
            OUTPUT ${header}
            COMMAND ${DRX_GLSLANG_VALIDATOR} -V --target-env vulkan1.2 --vn kShader_${symbol} -o ${header} ${shader}
            DEPENDS ${shader}
            COMMENT "Compiling shader ${shader_name}"
            VERBATIM
        )
        list(APPEND generated_headers ${header})
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${generated_headers})
    add_dependencies(${target} ${target}_shaders)
    target_include_directories(${target} PRIVATE ${CMAKE_BINARY_DIR}/generated)
endfunction()

# Compiles shaders to .spv files next to the executables (for runtime loading)
function(drx_compile_shader_assets target output_subdir)
    set(output_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${output_subdir})
    file(MAKE_DIRECTORY ${output_dir})

    set(spirv_files)
    foreach(shader ${ARGN})
        get_filename_component(shader_name ${shader} NAME)
        set(spirv ${output_dir}/${shader_name}.spv)

        add_custom_command(
            OUTPUT ${spirv}
            COMMAND ${DRX_GLSLANG_VALIDATOR} -V --target-env vulkan1.2 -o ${spirv} ${shader}
            DEPENDS ${shader}
            COMMENT "Compiling shader asset ${shader_name}"
            VERBATIM
        )
        list(APPEND spirv_files ${spirv})
    endforeach()

    add_custom_target(${target} ALL DEPENDS ${spirv_files})
endfunction()
