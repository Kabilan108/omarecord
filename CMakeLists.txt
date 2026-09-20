cmake_minimum_required(VERSION 3.24)
project(omarecord VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CTest)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Test Widgets)
find_package(LayerShellQt REQUIRED)

add_library(omarecord-core STATIC
  src/icons.cpp
  src/icons.hpp
  src/instance-lock.cpp
  src/instance-lock.hpp
  src/niri.cpp
  src/niri.hpp
  src/overlay.cpp
  src/overlay.hpp
  src/select.cpp
  src/select.hpp
  src/selector-window.cpp
  src/selector-window.hpp
  src/rect.cpp
  src/rect.hpp
)
qt_add_resources(omarecord-core "omarecord-fonts"
  PREFIX "/fonts"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/Neucha.ttf"
)
target_include_directories(omarecord-core PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(omarecord-core PUBLIC cxx_std_23)
target_compile_options(omarecord-core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(omarecord-core PUBLIC
  LayerShellQt::Interface
  Qt6::Core
  Qt6::Gui
  Qt6::Network
  Qt6::Widgets
)

qt_add_executable(omarecord src/main.cpp)
target_compile_options(omarecord PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(omarecord PRIVATE OMARECORD_VERSION="${PROJECT_VERSION}")
set_target_properties(omarecord PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
target_link_libraries(omarecord PRIVATE omarecord-core)

if(BUILD_TESTING)
qt_add_executable(omarecord-smoke
  tests/smoke-main.cpp
  tests/rect-smoke.cpp
  tests/rect-smoke.hpp
  tests/niri-smoke.cpp
  tests/niri-smoke.hpp
  tests/select-smoke.cpp
  tests/select-smoke.hpp
)
target_include_directories(omarecord-smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_options(omarecord-smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(omarecord-smoke PRIVATE omarecord-core Qt6::Test)
add_test(NAME omarecord-smoke COMMAND omarecord-smoke)
set_tests_properties(omarecord-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS omarecord RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES assets/OFL.txt DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omarecord" RENAME Neucha-OFL.txt)
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omarecord" RENAME MIT.txt)
