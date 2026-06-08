cmake_minimun_required(VERSION 3.15)
project(fbx VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(fbx
	src/framebuffer.cpp
)

add_library(fbx::fbx ALIAS fbx)

target_include_directories(fbx
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

target_compile_features(fbx PUBLIC cxx_std_17)


option(FBX_BUILD_TESTS "Build tests" ON)
option(FBX_BUILD_EXAMPLES "Build examples" ON)

if(FBX_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(FBX_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()


include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS fbx
	EXPORT fbxTargets
	LIBRARY DESTINATION $(CMAKE_INSTALL_LIBDIR)
	ARCHIVE DESTINATION $(CMAKE_INSTALL_LIBDIR)
	RUNTIME DESTINATION $(CMAKE_INSTALL_LIBDIR)
)

install(DIRECTORY include/ DESTINATION $(CMAKE_INSTALL_INCLUDEDIR))

install(EXPORT fbxTargets
	FILE fbxTargets.cmake
	NAMESPACE fbx::
	DESTINATION $(CMAKE_INSTALL_LIBDIR)/cmake/fbx
)

write_basic_package_version_file(
	"$(CMAKE_CURRENT_SOURCE_DIR)/fbxConfig.cmake.in"
	"$(CMAKE_CURRENT_BINARY_DIR)/fbxConfig.cmake"
	INSTALL_DESTINATION $(CMAKE_INSTALL_LIBDIR)/cmake/fbx
)

configure_package_config_file(
	"$(CMAKE_CURRENT_SOURCE_DIR)/fbxConfig.cmake.in"
	"$(CMAKE_CURRENT_BINARY_DIR)/fbxConfig.cmake"
	INSTALL_DESTINATION $(CMAKE_INSTALL_LIBDIR)/cmake/fbx
)

install(FILES 
	"$(CMAKE_CURRENT_SOURCE_DIR)/fbxConfig.cmake"
	"$(CMAKE_CURRENT_BINARY_DIR)/fbcConfigVersion.cmake"
	DESTINATION $(CMAKE_INSTALL_LIBDIR)/cmake/fbx
)


