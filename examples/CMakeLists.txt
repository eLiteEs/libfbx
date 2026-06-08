add_executable(demo_layers demo_layers.cpp)
target_link_libraries(demo_layers PRIVATE fbx)

add_executable(demo_stream demo_stream.cpp)
target_link_libraries(demo_stream PRIVATE fbx)

add_executable(demo_keys demo_keys.cpp)
target_link_libraries(demo_keys PRIVATE fbx)

