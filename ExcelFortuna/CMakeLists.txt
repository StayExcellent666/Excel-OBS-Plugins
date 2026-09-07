cmake_minimum_required(VERSION 3.22)

project(excelfortuna VERSION 0.1.0 LANGUAGES CXX)

option(EXCELFORTUNA_BUILD_PLUGIN "Build the OBS plugin" ON)
option(EXCELFORTUNA_BUILD_TESTS "Build standalone model tests" ON)

add_library(excelfortuna-model STATIC
  src/fortuna-model.cpp
  src/fortuna-model.hpp
)
target_compile_features(excelfortuna-model PUBLIC cxx_std_17)
target_include_directories(excelfortuna-model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(excelfortuna-model PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(excelfortuna-model PRIVATE /W4 /permissive-)
else()
  target_compile_options(excelfortuna-model PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EXCELFORTUNA_BUILD_PLUGIN)
  find_package(libobs REQUIRED)

  add_library(obs-excelfortuna MODULE
    src/plugin-main.cpp
    src/fortuna-network.cpp
    src/fortuna-network.hpp
    src/fortuna-source.cpp
    src/fortuna-source.hpp
  )
  target_compile_features(obs-excelfortuna PRIVATE cxx_std_17)
  target_link_libraries(obs-excelfortuna PRIVATE OBS::libobs excelfortuna-model)
  set_target_properties(obs-excelfortuna PROPERTIES PREFIX "")

  if(WIN32)
    target_compile_definitions(obs-excelfortuna PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(obs-excelfortuna PRIVATE winhttp)
  endif()

  if(MSVC)
    target_compile_options(obs-excelfortuna PRIVATE /W4 /permissive-)
  else()
    target_compile_options(obs-excelfortuna PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  include(GNUInstallDirs)
  install(TARGETS obs-excelfortuna
    RUNTIME DESTINATION "obs-plugins/64bit"
    LIBRARY DESTINATION "obs-plugins/64bit"
  )
  if(WIN32)
    install(DIRECTORY data/ DESTINATION "data/obs-plugins/obs-excelfortuna")
  else()
    install(DIRECTORY data/ DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/obs-excelfortuna")
  endif()
endif()

if(EXCELFORTUNA_BUILD_TESTS)
  include(CTest)
  add_executable(excelfortuna-model-tests tests/model-tests.cpp)
  target_link_libraries(excelfortuna-model-tests PRIVATE excelfortuna-model)
  add_test(NAME excelfortuna-model-tests COMMAND excelfortuna-model-tests)
endif()
