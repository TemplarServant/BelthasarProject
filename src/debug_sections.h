// debug_sections.h -- a debugging aid: the program's writable sections (its globals) to a file, so two runs' process
// state can be diffed symbol by symbol (the .map names them). Windows only; elsewhere it writes nothing.
#ifndef CT_DEBUG_SECTIONS_H
#define CT_DEBUG_SECTIONS_H

// <path>: per writable section `uint32 rva, uint32 size` then its bytes. Returns the sections written.
int CtDebugDumpWritableSections(const char* path);

#endif
