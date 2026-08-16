cmake_minimum_required(VERSION 3.21)

project(img_bat VERSION 0.1.1 LANGUAGES CXX)

option(IMG_BAT_ENABLE_METADATA "Enable metadata editing with Exiv2" ON)
option(IMG_BAT_ENABLE_HEIC "Enable HEIC/HEIF input and output with libheif" ON)

find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)

add_executable(img_bat src/main.cpp)
target_compile_features(img_bat PRIVATE cxx_std_17)
target_link_libraries(img_bat PRIVATE opencv_core opencv_imgcodecs opencv_imgproc)
target_compile_definitions(img_bat PRIVATE IMG_BAT_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(img_bat PRIVATE /W4 /permissive-)
else()
  target_compile_options(img_bat PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(IMG_BAT_ENABLE_METADATA)
  find_package(Exiv2 QUIET)
  if(Exiv2_FOUND)
    if(TARGET Exiv2::exiv2)
      target_link_libraries(img_bat PRIVATE Exiv2::exiv2)
      target_compile_definitions(img_bat PRIVATE IMG_BAT_WITH_EXIV2=1)
    elseif(TARGET Exiv2::exiv2lib)
      target_link_libraries(img_bat PRIVATE Exiv2::exiv2lib)
      target_compile_definitions(img_bat PRIVATE IMG_BAT_WITH_EXIV2=1)
    else()
      message(WARNING "Exiv2 was found but does not export a supported CMake target")
      set(Exiv2_FOUND FALSE)
    endif()
  else()
    message(STATUS "Exiv2 not found: --strip-gps and --strip-all are unavailable")
  endif()
endif()

if(IMG_BAT_ENABLE_HEIC)
  find_package(libheif CONFIG QUIET)

  if(TARGET heif)
    message(STATUS "Using libheif target: heif")
    target_link_libraries(img_bat PRIVATE heif)

  elseif(TARGET libheif::libheif)
    message(STATUS "Using libheif target: libheif::libheif")
    target_link_libraries(img_bat PRIVATE libheif::libheif)

  elseif(UNIX)
    message(STATUS "Using libheif via pkg-config")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBHEIF REQUIRED IMPORTED_TARGET libheif)
    target_link_libraries(img_bat PRIVATE PkgConfig::LIBHEIF)

  else()
    message(FATAL_ERROR "libheif not found")
  endif()

  target_compile_definitions(img_bat PRIVATE IMG_BAT_WITH_LIBHEIF=1)
endif()



include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME cli-heic COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
           --binary $<TARGET_FILE:img_bat> --mode heic)
  add_test(NAME cli-info COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
           --binary $<TARGET_FILE:img_bat> --mode info)
  add_test(NAME cli-status COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
           --binary $<TARGET_FILE:img_bat> --mode status)
  add_test(NAME cli-version COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
           --binary $<TARGET_FILE:img_bat> --mode version)
  if(Exiv2_FOUND)
    find_program(EXIV2_EXECUTABLE exiv2)
    if(EXIV2_EXECUTABLE)
      add_test(NAME cli-strip-metadata COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
               --binary $<TARGET_FILE:img_bat> --mode metadata --exiv2 ${EXIV2_EXECUTABLE})
    endif()
  endif()
endif()

install(TARGETS img_bat
  RUNTIME DESTINATION img_bat/bin COMPONENT default
  BUNDLE DESTINATION img_bat/bin COMPONENT default)

if(MSVC)
    # Copy libheif plugin DLLs to <target_dir>/libheif so as to find the plugins at runtime
    add_custom_command(TARGET img_bat POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/libheif"
            "$<TARGET_FILE_DIR:img_bat>/libheif"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/bin/libheif"
            "$<TARGET_FILE_DIR:img_bat>/libheif"
        COMMENT "Copying libheif plugin DLLs to target folder..."
    )
endif()
