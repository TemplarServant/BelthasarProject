# The Android build of the playable host (docs/ANDROID.md), included by the root CMakeLists.txt when the NDK's
# toolchain sets ANDROID. android/app/build.gradle runs it for each ABI.
#
# libSDL2.so is built from SDL's source (tools/get_sdl2_src.ps1 -> third_party/SDL2-src, the version of the Windows dev
# libraries); libmain.so is Belthasar: SDL's Java activity loads both and calls SDL_main (field.c's main, renamed by
# SDL_main.h). The F1 overlay (Dear ImGui) draws the options and the virtual gamepad on SDL_Renderer / OpenGL ES.
set(SDL2_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-src)
if(NOT EXISTS ${SDL2_SRC_DIR}/include/SDL.h)
  message(FATAL_ERROR "SDL2's source is not in third_party/SDL2-src: run tools/get_sdl2_src.ps1 (docs/ANDROID.md)")
endif()
foreach(gen src/data/title_intro.h)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${gen})
    message(FATAL_ERROR "${gen} is missing: run tools/setup.py with your ROM first (docs/ROM.md)")
  endif()
endforeach()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL2_SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/SDL2)

add_library(main SHARED ${CT_FIELD_SRC} src/android.c)
target_include_directories(main PRIVATE src ${SDL2_SRC_DIR}/include snes)
target_compile_options(main PRIVATE -O2 -Wno-everything)
target_link_libraries(main SDL2 m log android)

# The frames between and the higher internal resolution draw on every core (field.c's MSVC /openmp, here libomp,
# linked statically so the APK carries no extra library).
set_source_files_properties(src/title_intro.c src/fieldnative.c src/field.c PROPERTIES COMPILE_OPTIONS "-fopenmp")
target_link_options(main PRIVATE -fopenmp -static-openmp)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
if(EXISTS ${IMGUI_DIR}/imgui.h)
  enable_language(CXX)
  add_library(ct_imgui STATIC ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
              ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
              ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(ct_imgui PUBLIC ${IMGUI_DIR} ${SDL2_SRC_DIR}/include)
  target_compile_options(ct_imgui PRIVATE -O2)
  target_link_libraries(ct_imgui PUBLIC SDL2 GLESv3)
  target_sources(main PRIVATE src/overlay.cpp)
  target_compile_definitions(main PRIVATE CT_HAVE_IMGUI)
  target_include_directories(main PRIVATE ${IMGUI_DIR})
  target_link_libraries(main ct_imgui)
else()
  # Without the overlay there is no options screen and no virtual gamepad to draw: the build would not be playable.
  message(FATAL_ERROR "Dear ImGui is not in third_party/imgui: run tools/get_imgui.ps1 (it draws the touch controls)")
endif()
