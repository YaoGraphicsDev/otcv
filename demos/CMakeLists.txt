# CMakeLists.txt inside the subdirectory
file(GLOB CPP_FILES "*.cpp")

foreach(cpp_file ${CPP_FILES})
    get_filename_component(target_name ${cpp_file} NAME_WE)
    add_executable(${target_name} ${cpp_file})

    target_link_libraries(${target_name}
        PRIVATE ${OTCV_BASE_LIB}
        PRIVATE ${OTCV_FRAME_GRAPH_LIB}
        PRIVATE ${OTCV_IMGUI_LIB})
    add_dependencies(${target_name} ${OTCV_SHADER_TOOL})

endforeach()