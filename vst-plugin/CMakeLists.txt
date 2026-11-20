# Plugin VST3 "NovaonFocus Send" para a DAW (FL Studio, Reaper, Ableton...).
#
#   cmake -DNOVAON_BUILD_VST=ON -DNOVAON_VST3_SDK_DIR=<vst3sdk> ..
#
# Usa o VST3 SDK da Steinberg (licença MIT desde a versão 3.8). Só os
# submódulos base, pluginterfaces e public.sdk são necessários. Os arquivos
# do SDK são compilados direto no plugin, sem a infraestrutura CMake dele,
# o que deixa o build igual no MSVC e no MinGW.

set(NOVAON_VST3_SDK_DIR "" CACHE PATH "Checkout do vst3sdk (v3.8 ou mais novo)")
if(NOT NOVAON_VST3_SDK_DIR)
  message(FATAL_ERROR "Defina NOVAON_VST3_SDK_DIR com o caminho do vst3sdk")
endif()
set(sdk "${NOVAON_VST3_SDK_DIR}")

add_library(novaonfocus-send MODULE
  novaonfocus-send.cpp
  ${sdk}/base/source/baseiids.cpp
  ${sdk}/base/source/fbuffer.cpp
  ${sdk}/base/source/fdebug.cpp
  ${sdk}/base/source/fobject.cpp
  ${sdk}/base/source/fstreamer.cpp
  ${sdk}/base/source/fstring.cpp
  ${sdk}/base/source/updatehandler.cpp
  ${sdk}/base/thread/source/flock.cpp
  ${sdk}/pluginterfaces/base/conststringtable.cpp
  ${sdk}/pluginterfaces/base/coreiids.cpp
  ${sdk}/pluginterfaces/base/funknown.cpp
  ${sdk}/pluginterfaces/base/ustring.cpp
  ${sdk}/public.sdk/source/common/commoniids.cpp
  ${sdk}/public.sdk/source/common/commonstringconvert.cpp
  ${sdk}/public.sdk/source/common/pluginview.cpp
  ${sdk}/public.sdk/source/main/dllmain.cpp
  ${sdk}/public.sdk/source/main/moduleinit.cpp
  ${sdk}/public.sdk/source/main/pluginfactory.cpp
  ${sdk}/public.sdk/source/vst/vstbus.cpp
  ${sdk}/public.sdk/source/vst/vstcomponentbase.cpp
  ${sdk}/public.sdk/source/vst/vstinitiids.cpp
  ${sdk}/public.sdk/source/vst/vstparameters.cpp
  # (inclui o vsteditcontroller.cpp com setState renomeado; não listar os dois)
  ${sdk}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
)
target_include_directories(novaonfocus-send PRIVATE "${sdk}" "${PROJECT_SOURCE_DIR}/src/core")
target_compile_definitions(novaonfocus-send PRIVATE RELEASE=1)
# Um .vst3 de arquivo único é uma DLL com outra extensão.
set_target_properties(novaonfocus-send PROPERTIES PREFIX "" SUFFIX ".vst3")
if(MSVC)
  # Os avisos do SDK não são nossos.
  target_compile_options(novaonfocus-send PRIVATE /W1)
else()
  target_compile_options(novaonfocus-send PRIVATE -w)
  target_link_options(novaonfocus-send PRIVATE -static)
endif()

install(TARGETS novaonfocus-send LIBRARY DESTINATION vst3)

# Teste de ponta a ponta: plugin -> fonte send -> mixer -> bus.
add_executable(send_smoke
  ${PROJECT_SOURCE_DIR}/tests/send_smoke.cpp
  ${sdk}/pluginterfaces/base/coreiids.cpp
  ${sdk}/pluginterfaces/base/funknown.cpp
  ${sdk}/public.sdk/source/vst/vstinitiids.cpp
)
target_include_directories(send_smoke PRIVATE "${sdk}")
target_link_libraries(send_smoke PRIVATE novaon_core)
if(MINGW)
  target_link_options(send_smoke PRIVATE -municode -static)
endif()
