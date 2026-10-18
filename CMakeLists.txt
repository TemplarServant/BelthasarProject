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

# DETERMINISTIC BUILD (MSVC): the same sources link the same code bytes. The suite's oracle cache
# (tools/ctlib/oracle_cache.py) is keyed by the sha256 of the exe it runs, and MSVC stamps a time into every PE it
# writes -- so a rebuild that changed nothing (a touch, the restore at the end of a --mutate run) threw the whole cache
# away. /Brepro drops the stamps from the objects; incremental linking pads the image differently on every link, so
# it goes too. The linker still writes a fresh identity per link (the PE stamp, the PDB GUID, the repro hash: 69 bytes,
# the code identical), which the cache's key leaves out (oracle_cache._exe_sha).
if(MSVC)
  add_compile_options(/Brepro)
  add_link_options(/Brepro /INCREMENTAL:NO)
endif()

# The playable host's sources (Belthasar below; the Android app's libmain.so, android/CMakeLists.cmake).
set(CT_FIELD_SRC src/field.c src/seq_race.c src/seq_racepic.c src/seq_pic.c src/seq_ending.c src/seq_endingpic.c src/seq_entry3.c src/seq_entry3pic.c src/seq_entry4.c src/seq_entry4pic.c src/seq_entry6.c src/seq_entry6pic.c src/seq_entry7.c src/seq_entry7pic.c src/seq_gwarp.c src/scene.c src/lz.c src/assets.c src/ct_vm.c src/collision.c src/player.c src/world.c src/vwf.c src/battle.c src/objslot.c src/tileanim.c src/menu.c src/menu_cost.c src/field_cost.c src/oam.c src/settings.c src/present.c src/present_vk.c src/motion.c src/fieldnative.c
                 src/move.c src/audio.c src/title_intro.c src/seq_ppu.c src/seq_xlat.c src/xlat/seq_c3.c src/xlat/wipe_c3.c src/xlat/hdma_fd.c src/xlat/owwin_c6.c src/ct_cpu_infra.c src/rom_verify.c src/ct_lake_state.c src/touchpad.c ${SNES_SRC})

# ANDROID (docs/ANDROID.md): only the playable host, as the shared library SDL's Java activity loads. The NDK's CMake
# reaches this file through android/app/build.gradle.
if(ANDROID)
  include(${CMAKE_CURRENT_SOURCE_DIR}/android/CMakeLists.cmake)
  return()
endif()

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
  src/world.c src/vwf.c
  src/audio.c
  src/menu.c src/menu_cost.c   # --shadow: the inventory routines C2/8791 / C2/87D5 (ct_shadow.c)
  src/battle.c   # --sweepaitarget: the first bank-$C1 micro-execution harness needs the battle port
  src/objslot.c  # FD/E39C, the palette-effect records -- one body for the battle and the field VM
  src/tileanim.c # FD/DE98 + FD/E022, the field's animated BG tiles (--sweepbganim)
  src/seq_xlat.c src/xlat/seq_c3.c src/xlat/wipe_c3.c src/xlat/hdma_fd.c src/xlat/owwin_c6.c src/xlat/gwarp_fd.c  # --xlatcmp: the race's program translated from the ROM, lockstepped per instruction
  src/seq_race.c src/seq_racepic.c # --racestep: the race's tasks, native (sequence_table.md sec. 12)
  src/seq_ending.c src/seq_endingpic.c # --seqstage: the ending's stage, native (sequence_table.md sec. 13)
  src/seq_entry3.c src/seq_entry3pic.c # --seqstage: entry 3 from the story (FF 23), native (sequence_table.md sec. 14)
  src/seq_entry4.c src/seq_entry4pic.c src/seq_entry6.c src/seq_entry6pic.c src/seq_entry7.c src/seq_entry7pic.c # --seqstage: entry 4 (FF 24, the Tyranno Lair: Lavos falls), native (sequence_table.md sec. 15)
  src/seq_ppu.c  # --seqppucmp: the sequence stages' vendored-PPU picture, graded against the oracle's own frame
  src/seq_gwarp.c  # --gwarpcmp: the gate warp, native (seq_gwarp.c), lockstepped against the oracle
  src/seq_pic.c  # --seqpiccmp: the sequence stages' NATIVE picture, graded against the oracle's own frame
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

# The ORACLE alone: main.c with only --peek / --monitor / --hdma / --fb dispatched (CT_ORACLE_ONLY, src/main.c). The
# suite's oracle cache runs it for those modes and keys on its bytes, so a change to the port does not throw the
# cartridge's runs away (tools/ctlib/oracle_cache.py). Its sources are what those four modes LINK -- the linker's list,
# not a guess (2026-10-16: nothing else resolves a symbol they reach): the vendored core, the rule's CPU infra, the ROM
# reader, the state loader and --fb's play script. `ct` depends on it, so the two are always built together.
add_executable(ct_oracle
  src/main.c
  src/ct_cpu_infra.c src/rom_verify.c src/ct_lake_state.c src/harness_drive.c
  ${SNES_SRC}
)
target_compile_definitions(ct_oracle PRIVATE CT_ORACLE_ONLY)
target_include_directories(ct_oracle PRIVATE snes src)
if(UNIX)
  target_link_libraries(ct_oracle m)
endif()
if(MSVC)
  target_compile_definitions(ct_oracle PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(ct_oracle PRIVATE /W2 /wd4244 /wd4267 /wd4018 /wd4146 /wd4334 /wd4101 /wd4505)
endif()
add_dependencies(ct ct_oracle)

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
  # Belthasar also links the SOUND system: src/audio.c drives the vendored SPC700/DSP through bank
  # $C7's driver upload (disasm/notes/audio_boot.md); the whole vendored core (snes/*.c) is linked for
  # it and for the translated stages in src/xlat/. `snes` goes AFTER `src` on the include path so an
  # ambiguous name (input.h exists in both) still resolves to the engine's.
  # src/move.c runs live: ct_vm.c drives the ported C0/CAD9 animation tree through it (CtAnimDrainPass,
  # CtAnimDrawPass), the field runner CtFieldRunnerOne (which reaches the object loader CtObjLoad) and
  # most of the VM's op families (CtVm*).
  add_executable(Belthasar ${CT_FIELD_SRC})
  target_include_directories(Belthasar PRIVATE src ${CT_SDL_INC} snes)
  target_link_libraries(Belthasar ${CT_SDL_LIB})
  target_compile_definitions(Belthasar PRIVATE CT_XLAT_NO_GWARP)   # the gate warp is native (src/seq_gwarp.c)
  if(MSVC)
    # the host's pictures at the options (the title's mode 7 at the internal resolution): drawn on every core
    set_source_files_properties(src/title_intro.c src/fieldnative.c src/field.c PROPERTIES COMPILE_OPTIONS "/openmp")
    target_compile_definitions(Belthasar PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(Belthasar PRIVATE /W2 /wd4244 /wd4267 /wd4018 /wd4146 /wd4334 /wd4101)
    if(DEFINED ENV{CT_ASAN})
      target_compile_options(Belthasar PRIVATE /fsanitize=address /Zi)
    endif()
  endif()
  # THE F1 OPTIONS OVERLAY (src/overlay.cpp, Dear ImGui -- tools/get_imgui.ps1 puts it in third_party/imgui, git-ignored
  # like SDL2). Without it Belthasar builds the stub and has no overlay; nothing else changes.
  # THE VULKAN BACKEND (src/present_vk.c): the Khronos headers only (tools/get_vulkan_headers.ps1 -> third_party/vulkan,
  # git-ignored); nothing is linked -- the driver's vulkan-1 is loaded at run time through SDL.
  set(VULKAN_INC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include)
  if(EXISTS ${VULKAN_INC}/vulkan/vulkan.h)
    target_compile_definitions(Belthasar PRIVATE CT_HAVE_VULKAN)
    target_include_directories(Belthasar PRIVATE ${VULKAN_INC})
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
    target_sources(Belthasar PRIVATE src/overlay.cpp)
    target_compile_definitions(Belthasar PRIVATE CT_HAVE_IMGUI)
    target_include_directories(Belthasar PRIVATE ${IMGUI_DIR})
    target_link_libraries(Belthasar ct_imgui)
    if(MSVC)
      set_source_files_properties(src/overlay.cpp PROPERTIES COMPILE_OPTIONS "/utf-8")   # its Portuguese labels
    endif()
  else()
    target_sources(Belthasar PRIVATE src/overlay_stub.c)
  endif()
  if(CT_SDL_DLL)
    add_custom_command(TARGET Belthasar POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CT_SDL_DLL} $<TARGET_FILE_DIR:Belthasar>)
  endif()
  # M3-B: interactive overworld camera viewer (live map from a WRAM/VRAM dump + the byte-exact camera).
  add_executable(ct_world src/ct_world.c src/world.c src/vwf.c src/lz.c src/assets.c)   # lz.c: the world scripts' far call C6/E028
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
  message(STATUS "SDL2 found: building ct_viewer + Belthasar + ct_world")
else()
  message(STATUS "SDL2 not in third_party/SDL2 (nor the system's): skipping ct_viewer/Belthasar (headless ct still builds)")
endif()

# Native Finder resource lookup and writable Application Support paths.
if(APPLE AND TARGET Belthasar)
    enable_language(OBJC)
    target_sources(Belthasar PRIVATE src/macos_paths.m src/macos_window.m src/macos_menu.m)
    set_source_files_properties(src/macos_paths.m src/macos_window.m src/macos_menu.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(Belthasar "-framework AppKit")
endif()

# Keep normal Unix executable names and the .exe aliases used by the shared
# Python oracle/recipe/regression tools. Both names execute native machine code.
if(UNIX)
    foreach(target ct Belthasar ct_world ct_viewer)
        if(TARGET ${target})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E remove -f "$<TARGET_FILE_DIR:${target}>/${target}.exe"
                COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE_NAME:${target}>"
                    "$<TARGET_FILE_DIR:${target}>/${target}.exe")
        endif()
    endforeach()
endif()
