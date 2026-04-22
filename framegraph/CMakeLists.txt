set(OTCV_FRAME_GRAPH_LIB "OTCVFrameGraphLib" CACHE INTERNAL "")

file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "*.h" "*.hpp")
add_library(${OTCV_FRAME_GRAPH_LIB} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${OTCV_FRAME_GRAPH_LIB} 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} # propagates to top most project
)

target_link_libraries(${OTCV_FRAME_GRAPH_LIB} PUBLIC ${OTCV_BASE_LIB})
