cmake_minimum_required(VERSION 3.21)

project(bat_img_cpp VERSION 0.1.0 LANGUAGES CXX)

option(BAT_IMG_ENABLE_METADATA "Enable metadata editing with Exiv2" ON)
option(BAT_IMG_ENABLE_HEIC "Enable HEIC/HEIF input and output with libheif" ON)

find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)

add_executable(bat_img src/main.cpp)
target_compile_features(bat_img PRIVATE cxx_std_17)
target_link_libraries(bat_img PRIVATE opencv_core opencv_imgcodecs opencv_imgproc)

if(MSVC)
  target_compile_options(bat_img PRIVATE /W4 /permissive-)
else()
  target_compile_options(bat_img PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BAT_IMG_ENABLE_METADATA)
  find_package(Exiv2 QUIET)
  if(Exiv2_FOUND)
    if(TARGET Exiv2::exiv2)
      target_link_libraries(bat_img PRIVATE Exiv2::exiv2)
      target_compile_definitions(bat_img PRIVATE BAT_IMG_WITH_EXIV2=1)
    elseif(TARGET Exiv2::exiv2lib)
      target_link_libraries(bat_img PRIVATE Exiv2::exiv2lib)
      target_compile_definitions(bat_img PRIVATE BAT_IMG_WITH_EXIV2=1)
    else()
      message(WARNING "Exiv2 was found but does not export a supported CMake target")
      set(Exiv2_FOUND FALSE)
    endif()
  else()
    message(STATUS "Exiv2 not found: --strip-gps and --strip-all are unavailable")
  endif()
endif()

if(BAT_IMG_ENABLE_HEIC)
  find_package(libheif CONFIG QUIET)
  if(TARGET libheif::libheif)
    target_link_libraries(bat_img PRIVATE libheif::libheif)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBHEIF REQUIRED IMPORTED_TARGET libheif)
    target_link_libraries(bat_img PRIVATE PkgConfig::LIBHEIF)
  endif()
  target_compile_definitions(bat_img PRIVATE BAT_IMG_WITH_LIBHEIF=1)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME cli-heic COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
           --binary $<TARGET_FILE:bat_img> --mode heic)
  if(Exiv2_FOUND)
    find_program(EXIV2_EXECUTABLE exiv2)
    if(EXIV2_EXECUTABLE)
      add_test(NAME cli-strip-metadata COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
               --binary $<TARGET_FILE:bat_img> --mode metadata --exiv2 ${EXIV2_EXECUTABLE})
    endif()
  endif()
endif()

install(TARGETS bat_img
  RUNTIME DESTINATION bat_img_cpp/bin COMPONENT default
  BUNDLE DESTINATION bat_img_cpp/bin COMPONENT default)
