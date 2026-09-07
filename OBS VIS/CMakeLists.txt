cmake_minimum_required(VERSION 3.22)

project(excelvisus VERSION 0.4.0 LANGUAGES CXX)

option(SPECTRUM_CANVAS_BUILD_PLUGIN "Build the OBS plugin" ON)
option(SPECTRUM_CANVAS_BUILD_TESTS "Build standalone DSP tests" ON)

add_library(spectrum-canvas-dsp STATIC
  src/audio-analyzer.cpp
  src/audio-analyzer.hpp
)
target_compile_features(spectrum-canvas-dsp PUBLIC cxx_std_17)
target_include_directories(spectrum-canvas-dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(spectrum-canvas-dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(spectrum-canvas-dsp PRIVATE /W4 /permissive-)
else()
  target_compile_options(spectrum-canvas-dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SPECTRUM_CANVAS_BUILD_PLUGIN)
  find_package(libobs REQUIRED)

  add_library(obs-spectrum-canvas MODULE
    src/plugin-main.cpp
    src/visualizer-source.cpp
    src/visualizer-source.hpp
  )
  target_compile_features(obs-spectrum-canvas PRIVATE cxx_std_17)
  target_link_libraries(obs-spectrum-canvas PRIVATE OBS::libobs spectrum-canvas-dsp)
  set_target_properties(obs-spectrum-canvas PROPERTIES PREFIX "")

  if(MSVC)
    target_compile_options(obs-spectrum-canvas PRIVATE /W4 /permissive-)
  else()
    target_compile_options(obs-spectrum-canvas PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  include(GNUInstallDirs)
  install(TARGETS obs-spectrum-canvas
    RUNTIME DESTINATION "obs-plugins/64bit"
    LIBRARY DESTINATION "obs-plugins/64bit"
  )
  if(WIN32)
    install(DIRECTORY data/ DESTINATION "data/obs-plugins/obs-spectrum-canvas")
  else()
    install(DIRECTORY data/ DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/obs-spectrum-canvas")
  endif()
endif()

if(SPECTRUM_CANVAS_BUILD_TESTS)
  include(CTest)
  add_executable(spectrum-canvas-tests tests/analyzer-tests.cpp)
  target_link_libraries(spectrum-canvas-tests PRIVATE spectrum-canvas-dsp)
  add_test(NAME spectrum-canvas-tests COMMAND spectrum-canvas-tests)
endif()
