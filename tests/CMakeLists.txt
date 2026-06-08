include($(CMAKE_SOURCE_DIR)/cmake/CPM.cmake)

CPMAddPackage("gh:catchorg/Catch2@3.5.4")

add_executable(fbx_tests
	test_franebuffer.cpp 
)

target_link_libraries(fbx_tests PRIVATE
	fbx::fbx
	Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(fbx_tests)

