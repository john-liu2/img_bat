cmake_minimum_required(VERSION 3.21)

project(bat_img_cpp VERSION 0.1.0 LANGUAGES CXX)

option(BAT_IMG_ENABLE_METADATA "Enable metadata editing with Exiv2" ON)
option(BAT_IMG_ENABLE_HEIC "Accept HEIC when the installed OpenCV codec supports it" ON)

find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgcodecs imgproc)

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
    target_link_libraries(bat_img PRIVATE Exiv2::exiv2)
    target_compile_definitions(bat_img PRIVATE BAT_IMG_WITH_EXIV2=1)
  else()
    message(STATUS "Exiv2 not found: --strip-gps and --strip-all are unavailable")
  endif()
endif()

if(BAT_IMG_ENABLE_HEIC)
  target_compile_definitions(bat_img PRIVATE BAT_IMG_HEIC_REQUESTED=1)
endif()

install(TARGETS bat_img
  RUNTIME DESTINATION bat_img_cpp/bin
  BUNDLE DESTINATION bat_img_cpp/bin)
