cmake_minimum_required(VERSION 3.21)

project(img_bat VERSION 0.1.0 LANGUAGES CXX)

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

  if(TARGET libheif::libheif)
    message(STATUS "libheif: using libheif::libheif")
    target_link_libraries(img_bat PRIVATE libheif::libheif)
  elseif(TARGET libheif::libheif-static)
    message(STATUS "libheif: using libheif::libheif-static")
    target_link_libraries(img_bat PRIVATE libheif::libheif-static)
  elseif(UNIX)
    message(STATUS "libheif CMake target not found; trying pkg-config")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBHEIF REQUIRED IMPORTED_TARGET libheif)
    target_link_libraries(img_bat PRIVATE PkgConfig::LIBHEIF)
  else()
    message(FATAL_ERROR
      "libheif was not found as a CMake package on Windows. "
      "Verify libheif:x64-windows is installed by vcpkg."
    )
  endif()

  target_compile_definitions(img_bat PRIVATE IMG_BAT_WITH_LIBHEIF=1)
endif()


include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME cli-heic COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
           --binary $<TARGET_FILE:img_bat> --mode heic)
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
