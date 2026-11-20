cmake_minimum_required(VERSION 3.20)
project(NovaonFocus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "NovaonFocus usa WASAPI e só compila para Windows.")
endif()

option(NOVAON_BUILD_OBS_PLUGIN "Compila o plugin do OBS (precisa do libobs)" OFF)
option(NOVAON_BUILD_GUI "Compila a interface gráfica (baixa o Dear ImGui)" ON)
option(NOVAON_BUILD_VST "Compila o plugin VST3 NovaonFocus Send (precisa do vst3sdk)" OFF)

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive-)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_library(novaon_core STATIC
  src/core/AudioDevices.cpp
  src/core/Config.cpp
  src/core/Mixer.cpp
  src/core/WasapiSource.cpp
)
target_include_directories(novaon_core PUBLIC src/core)
target_link_libraries(novaon_core PUBLIC ole32 avrt mmdevapi uuid)

add_executable(novaonfocus src/app/main.cpp)
target_link_libraries(novaonfocus PRIVATE novaon_core)
if(MINGW)
  target_link_options(novaonfocus PRIVATE -municode -static)
endif()

add_executable(resampler_test tests/resampler_test.cpp)
target_include_directories(resampler_test PRIVATE src/core)
if(MINGW)
  target_link_options(resampler_test PRIVATE -static)
endif()

install(TARGETS novaonfocus RUNTIME DESTINATION .)

# Interface gráfica: novaonfocus-gui.exe (Win32 + Direct3D 11 + Dear ImGui).
if(NOVAON_BUILD_GUI)
  include(FetchContent)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui)

  add_executable(novaonfocus-gui WIN32
    src/gui/main.cpp
    src/gui/App.cpp
    src/gui/Theme.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  )
  target_include_directories(novaonfocus-gui PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(novaonfocus-gui PRIVATE novaon_core d3d11 dxgi d3dcompiler dwmapi shell32)
  # O código do ImGui não precisa passar no /W4 do projeto.
  set_source_files_properties(
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")
  if(MINGW)
    target_link_options(novaonfocus-gui PRIVATE -municode -static)
  endif()
  install(TARGETS novaonfocus-gui RUNTIME DESTINATION .)
endif()
install(FILES config/novaonfocus.ini DESTINATION .)

if(NOVAON_BUILD_OBS_PLUGIN)
  add_subdirectory(obs-plugin)
endif()

if(NOVAON_BUILD_VST)
  add_subdirectory(vst-plugin)
endif()
