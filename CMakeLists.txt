cmake_minimum_required(VERSION 3.10)
project(XiaomiControllerEmulator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(XiaomiControllerEmulator main.cpp)

# Resolve vcpkg include/lib dirs even when no find_package() is available
# Prefer variables defined by the vcpkg toolchain; fallback to ENV if needed
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
	set(VCPKG_INCLUDE_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
	set(VCPKG_LIB_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
	set(VCPKG_LIB_DIR_DEBUG "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")
elseif(DEFINED ENV{VCPKG_ROOT})
	# Default to x64-windows triplet as installed
	set(VCPKG_INCLUDE_DIR "$ENV{VCPKG_ROOT}/installed/x64-windows/include")
	set(VCPKG_LIB_DIR "$ENV{VCPKG_ROOT}/installed/x64-windows/lib")
	set(VCPKG_LIB_DIR_DEBUG "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/lib")
endif()

if(DEFINED VCPKG_INCLUDE_DIR)
	target_include_directories(XiaomiControllerEmulator PRIVATE "${VCPKG_INCLUDE_DIR}")
endif()

if(DEFINED VCPKG_LIB_DIR)
	target_link_directories(XiaomiControllerEmulator PRIVATE "${VCPKG_LIB_DIR}")
endif()

if(DEFINED VCPKG_LIB_DIR_DEBUG)
	target_link_directories(XiaomiControllerEmulator PRIVATE "${VCPKG_LIB_DIR_DEBUG}")
endif()

# Link libraries (provided by Windows SDK and vcpkg install)
target_link_libraries(XiaomiControllerEmulator PRIVATE vigemclient setupapi hid) 