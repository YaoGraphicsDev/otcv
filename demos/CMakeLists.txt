file(GLOB CHILDREN
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*"
)

foreach(child ${CHILDREN})
    set(project_dir "${CMAKE_CURRENT_SOURCE_DIR}/${child}")

    if(IS_DIRECTORY "${project_dir}")
        file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
            "${project_dir}/*.cpp"
            "${project_dir}/*.h"
            "${project_dir}/*.hpp"
        )

        if(PROJECT_SOURCES)
            # Use the directory name as the executable target name.
            set(target_name "${child}")

            add_executable(${target_name}
                ${PROJECT_SOURCES}
            )

            target_include_directories(${target_name}
                PRIVATE
                    "${project_dir}"
                    "${CMAKE_BINARY_DIR}/include"
            )

            target_link_libraries(${target_name}
                PRIVATE
                    ${OTCV_BASE_LIB}
                    ${OTCV_FRAME_GRAPH_LIB}
                    ${OTCV_IMGUI_LIB}
            )

            add_dependencies(${target_name}
                ${OTCV_SHADER_TOOL}
            )
        endif()
    endif()
endforeach()