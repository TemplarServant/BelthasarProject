#!/bin/bash
# Cloud sessions only (claude.ai/code): give a fresh container what the Linux build needs, then build what builds
# without the ROM. The ROM is never in the repository (docs/ROM.md); docs/INFRA.md "Cloud sessions" says how a
# session gets it and what to run next. Idempotent: every step is skipped when its result is already there.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

cd "$CLAUDE_PROJECT_DIR"

# SDL2 headers: the graphical targets (Belthasar, ct_world, ct_viewer) need them; ct.exe does not.
if ! dpkg -s libsdl2-dev >/dev/null 2>&1; then
  (apt-get update -qq && apt-get install -y -qq --no-install-recommends libsdl2-dev) >/dev/null 2>&1 \
    || echo "session-start: libsdl2-dev did not install -- only build/ct.exe will build"
fi

# Dear ImGui (the F1 overlay); tools/get_imgui.py falls back to git where the archive link is refused.
if [ ! -f third_party/imgui/imgui.h ]; then
  python3 tools/get_imgui.py >/dev/null 2>&1 || echo "session-start: Dear ImGui did not fetch -- Belthasar builds without the overlay"
fi

# The public battery saves (verified by SHA-256). Some hosts may be outside the environment's network policy.
python3 tools/fetch_saves.py 2>&1 | tail -1 || true

# A fresh clone has no generated file: the shapes stand in for the missing ones (never over a real one).
python3 tools/gen/shapes.py install >/dev/null
python3 tools/build.py >/dev/null 2>&1 || echo "session-start: tools/build.py failed -- run it to see why"

if [ -f ct_assets.dat ]; then
  echo "session-start: built from the ROM's data (ct_assets.dat present): python3 tools/regress.py runs the suite"
else
  rom="${CT_ROM:-}"
  [ -z "$rom" ] && [ -f Chrono_Trigger_USA_SNES.sfc ] && rom=Chrono_Trigger_USA_SNES.sfc
  [ -z "$rom" ] && rom=$(ls /root/.claude/uploads/*/*.sfc 2>/dev/null | head -1 || true)
  if [ -n "$rom" ]; then
    echo "session-start: a ROM is here but not set up: python3 tools/setup.py --rom $rom (15-20 min), then python3 tools/fixtures.py make all"
  else
    echo "session-start: NO ROM in this container -- only the shapes build exists; nearly every check SKIPs. Ask the owner to attach the ROM in the chat (docs/INFRA.md, Cloud sessions)"
  fi
fi
