# Plugin do OBS (OBS Studio 30 ou mais novo).
#
# Opção 1, com o libobs já compilado:
#   cmake -DNOVAON_BUILD_OBS_PLUGIN=ON -Dlibobs_DIR=<obs-studio/build/libobs> ..
# Opção 2, só com o código-fonte do OBS (é o que o CI usa):
#   cmake -DNOVAON_BUILD_OBS_PLUGIN=ON -DNOVAON_OBS_SOURCE_DIR=<obs-studio> ..
#   Os headers vêm de <obs-studio>/libobs e a lib de importação do obs.dll é
#   gerada a partir de obs.def.

set(NOVAON_OBS_SOURCE_DIR "" CACHE PATH "Checkout do obs-studio (só headers)")

add_library(novaonfocus-obs MODULE novaonfocus-obs.cpp)
set_target_properties(novaonfocus-obs PROPERTIES PREFIX "")

if(NOVAON_OBS_SOURCE_DIR)
  set(gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
  file(MAKE_DIRECTORY "${gen}")
  file(WRITE "${gen}/obsconfig.h"
    "#pragma once\n#define OBS_RELEASE_CANDIDATE 0\n#define OBS_BETA 0\n")
  set(implib "${CMAKE_CURRENT_BINARY_DIR}/obs.lib")
  if(MSVC)
    add_custom_command(OUTPUT "${implib}"
      COMMAND "${CMAKE_AR}" /nologo /def:${CMAKE_CURRENT_SOURCE_DIR}/obs.def /machine:x64 /out:${implib}
      DEPENDS obs.def)
  else()
    find_program(DLLTOOL NAMES x86_64-w64-mingw32-dlltool dlltool REQUIRED)
    add_custom_command(OUTPUT "${implib}"
      COMMAND "${DLLTOOL}" -d ${CMAKE_CURRENT_SOURCE_DIR}/obs.def -D obs.dll -l ${implib}
      DEPENDS obs.def)
  endif()
  add_custom_target(obs_implib DEPENDS "${implib}")
  add_dependencies(novaonfocus-obs obs_implib)
  target_include_directories(novaonfocus-obs PRIVATE "${gen}" "${NOVAON_OBS_SOURCE_DIR}/libobs")
  target_link_libraries(novaonfocus-obs PRIVATE "${implib}")
else()
  find_package(libobs REQUIRED)
  target_link_libraries(novaonfocus-obs PRIVATE OBS::libobs)
endif()

if(MINGW)
  target_link_options(novaonfocus-obs PRIVATE -static)
endif()

install(TARGETS novaonfocus-obs LIBRARY DESTINATION obs-plugins/64bit)
