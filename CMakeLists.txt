cmake_minimum_required(VERSION 3.15)
project(chrono_trigger_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Oracle core: vendored LakeSnes (snes/*.c). main.c from LakeSnes is NOT included
# (it pulls in SDL); the M0 harness is headless.
file(GLOB SNES_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/snes/*.c")

add_executable(ct
  src/main.c
  src/ct_cpu_infra.c src/rom_verify.c
  src/ct_shadow.c  # --shadow: the routine-level shadow (docs/SHADOW_PROPOSAL.md)
  src/ct_lake_state.c
  src/harness_drive.c
  src/boot.c
  src/lz.c
  src/scene.c
  src/assets.c
  src/ct_vm.c
  src/oam.c
  src/move.c
  src/input.c
  src/player.c
  src/collision.c
  src/world.c
  src/audio.c
  src/menu.c src/menu_cost.c   # --shadow: the inventory routines C2/8791 / C2/87D5 (ct_shadow.c)
  src/battle.c   # --sweepaitarget: the first bank-$C1 micro-execution harness needs the battle port
  src/objslot.c  # FD/E39C, the palette-effect records -- one body for the battle and the field VM
  src/tileanim.c # FD/DE98 + FD/E022, the field's animated BG tiles (--sweepbganim)
  src/seq_xlat.c src/xlat/seq_c3.c src/xlat/wipe_c3.c src/xlat/hdma_fd.c src/xlat/owwin_c6.c src/xlat/gwarp_fd.c  # --xlatcmp: the race's program translated from the ROM, lockstepped per instruction
  src/seq_ppu.c  # --seqppucmp: the sequence stages' vendored-PPU picture, graded against the oracle's own frame
  ${SNES_SRC}
)
target_include_directories(ct PRIVATE snes src)
if(UNIX)
  target_link_libraries(ct m)   # lround (ct_vm.c); MSVC's CRT carries libm itself
endif()

if(MSVC)
  target_compile_definitions(ct PRIVATE _CRT_SECURE_NO_WARNINGS)
  # The LakeSnes core emits many benign implicit-conversion warnings; quiet them.
  target_compile_options(ct PRIVATE /W2 /wd4244 /wd4267 /wd4018 /wd4146 /wd4334 /wd4101)
endif()

# Interactive SDL2 viewer for the C scene engine. Built only when SDL2 is present
# in third_party/SDL2 (downloaded, git-ignored). The headless 'ct' stays SDL-free;
# ct_viewer only needs the renderer (scene.c/lz.c/assets.c), not the oracle core.
set(SDL2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2)
if(WIN32 AND EXISTS ${SDL2_DIR}/include/SDL.h)
  set(CT_SDL_INC ${SDL2_DIR}/include)
  set(CT_SDL_LIB ${SDL2_DIR}/lib/x64/SDL2.lib)
  set(CT_SDL_DLL ${SDL2_DIR}/SDL2.dll)
elseif(UNIX)
  # Linux/macOS: the system SDL2 (e.g. libsdl2-dev). No DLL to copy next to the exes.
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    set(CT_SDL_INC ${SDL2_INCLUDE_DIRS})
    set(CT_SDL_LIB ${SDL2_LIBRARIES} m)
  endif()
endif()
if(CT_SDL_INC)
  add_executable(ct_viewer src/viewer.c src/scene.c src/lz.c src/assets.c)
  target_include_directories(ct_viewer PRIVATE src ${CT_SDL_INC})
  target_link_libraries(ct_viewer ${CT_SDL_LIB})
  if(MSVC)
    target_compile_definitions(ct_viewer PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ct_viewer PRIVATE /W2 /wd4244 /wd4267 /wd4018 /wd4146 /wd4334 /wd4101)
  endif()
  if(CT_SDL_DLL)
    add_custom_command(TARGET ct_viewer POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CT_SDL_DLL} $<TARGET_FILE_DIR:ct_viewer>)
  endif()
  # Native interactive field: VM scheduler (ct_vm.c) + renderer + sprite compositor + SDL.
  # ct_field also links the SOUND system: src/audio.c drives the vendored SPC700/DSP through bank
  # $C7's driver upload (disasm/notes/audio_boot.md); the whole vendored core (snes/*.c) is linked for
  # it and for the translated stages in src/xlat/. `snes` goes AFTER `src` on the include path so an
  # ambiguous name (input.h exists in both) still resolves to the engine's.
  # src/move.c runs live: ct_vm.c drives the ported C0/CAD9 animation tree through it (CtAnimDrainPass,
  # CtAnimDrawPass), the field runner CtFieldRunnerOne (which reaches the object loader CtObjLoad) and
  # most of the VM's op families (CtVm*).
  add_executable(ct_field src/field.c src/scene.c src/lz.c src/assets.c src/ct_vm.c src/collision.c src/player.c src/world.c src/battle.c src/objslot.c src/tileanim.c src/menu.c src/menu_cost.c src/field_cost.c src/oam.c src/settings.c src/present.c src/present_vk.c src/motion.c src/fieldnative.c
                          src/move.c src/audio.c src/title_intro.c src/seq_ppu.c src/seq_xlat.c src/xlat/seq_c3.c src/xlat/wipe_c3.c src/xlat/hdma_fd.c src/xlat/owwin_c6.c src/xlat/gwarp_fd.c src/ct_cpu_infra.c src/rom_verify.c src/ct_lake_state.c ${SNES_SRC})
  target_include_directories(ct_field PRIVATE src ${CT_SDL_INC} snes)
  target_link_libraries(ct_field ${CT_SDL_LIB})
  if(MSVC)
    # the host's pictures at the options (the title's mode 7 at the internal resolution): drawn on every core
    set_source_files_properties(src/title_intro.c src/fieldnative.c src/field.c PROPERTIES COMPILE_OPTIONS "/openmp")
    target_compile_definitions(ct_field PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ct_field PRIVATE /W2 /wd4244 /wd4267 /wd4018 /wd4146 /wd4334 /wd4101)
    if(DEFINED ENV{CT_ASAN})
      target_compile_options(ct_field PRIVATE /fsanitize=address /Zi)
    endif()
  endif()
  # THE F1 OPTIONS OVERLAY (src/overlay.cpp, Dear ImGui -- tools/get_imgui.ps1 puts it in third_party/imgui, git-ignored
  # like SDL2). Without it ct_field builds the stub and has no overlay; nothing else changes.
  # THE VULKAN BACKEND (src/present_vk.c): the Khronos headers only (tools/get_vulkan_headers.ps1 -> third_party/vulkan,
  # git-ignored); nothing is linked -- the driver's vulkan-1 is loaded at run time through SDL.
  set(VULKAN_INC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include)
  if(EXISTS ${VULKAN_INC}/vulkan/vulkan.h)
    target_compile_definitions(ct_field PRIVATE CT_HAVE_VULKAN)
    target_include_directories(ct_field PRIVATE ${VULKAN_INC})
  endif()
  set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
  if(EXISTS ${IMGUI_DIR}/imgui.h)
    enable_language(CXX)
    add_library(ct_imgui STATIC ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
                ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
                ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(ct_imgui PUBLIC ${IMGUI_DIR} ${CT_SDL_INC})
    if(EXISTS ${VULKAN_INC}/vulkan/vulkan.h)                    # ImGui's Vulkan backend, its functions loaded at run time
      target_sources(ct_imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
      target_include_directories(ct_imgui PUBLIC ${VULKAN_INC})
      target_compile_definitions(ct_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
    endif()
    target_sources(ct_field PRIVATE src/overlay.cpp)
    target_compile_definitions(ct_field PRIVATE CT_HAVE_IMGUI)
    target_include_directories(ct_field PRIVATE ${IMGUI_DIR})
    target_link_libraries(ct_field ct_imgui)
    if(MSVC)
      set_source_files_properties(src/overlay.cpp PROPERTIES COMPILE_OPTIONS "/utf-8")   # its Portuguese labels
    endif()
  else()
    target_sources(ct_field PRIVATE src/overlay_stub.c)
  endif()
  if(CT_SDL_DLL)
    add_custom_command(TARGET ct_field POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CT_SDL_DLL} $<TARGET_FILE_DIR:ct_field>)
  endif()
  # M3-B: interactive overworld camera viewer (live map from a WRAM/VRAM dump + the byte-exact camera).
  add_executable(ct_world src/ct_world.c src/world.c src/assets.c)
  target_include_directories(ct_world PRIVATE src ${CT_SDL_INC})
  target_link_libraries(ct_world ${CT_SDL_LIB})
  if(MSVC)
    target_compile_definitions(ct_world PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ct_world PRIVATE /W2 /wd4244 /wd4267 /wd4018 /wd4146 /wd4334 /wd4101)
  endif()
  if(CT_SDL_DLL)
    add_custom_command(TARGET ct_world POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CT_SDL_DLL} $<TARGET_FILE_DIR:ct_world>)
  endif()
  message(STATUS "SDL2 found: building ct_viewer + ct_field + ct_world")
else()
  message(STATUS "SDL2 not in third_party/SDL2 (nor the system's): skipping ct_viewer/ct_field (headless ct still builds)")
endif()

# Native Finder resource lookup and writable Application Support paths.
if(APPLE AND TARGET ct_field)
    enable_language(OBJC)
    target_sources(ct_field PRIVATE src/macos_paths.m src/macos_window.m src/macos_menu.m)
    set_source_files_properties(src/macos_paths.m src/macos_window.m src/macos_menu.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(ct_field "-framework AppKit")
endif()

# Keep normal Unix executable names and the .exe aliases used by the shared
# Python oracle/recipe/regression tools. Both names execute native machine code.
if(UNIX)
    foreach(target ct ct_field ct_world ct_viewer)
        if(TARGET ${target})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E remove -f "$<TARGET_FILE_DIR:${target}>/${target}.exe"
                COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE_NAME:${target}>"
                    "$<TARGET_FILE_DIR:${target}>/${target}.exe")
        endif()
    endforeach()
endif()
