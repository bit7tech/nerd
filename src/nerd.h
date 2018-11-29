//----------------------------------------------------------------------------------------------------------------------
// Nerd public API
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <stdint.h>

//----------------------------------------------------------------------------------------------------------------------
// Types
//----------------------------------------------------------------------------------------------------------------------

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// The Nerd VM
typedef struct _Nerd* Nerd;

// The memory operation callback.
typedef void* (*NeMemoryFunc) (Nerd, void*, i64, i64);

//----------------------------------------------------------------------------------------------------------------------
// Data structures
//----------------------------------------------------------------------------------------------------------------------

typedef enum
{
    AT_Nil,
    AT_Integer,
    AT_String,
}
AtomType;

typedef const char* NeString;

typedef struct _Atom
{
    AtomType type;
    union {
        i64 i;
        NeString str;
    };
}
Atom;

typedef struct
{
    NeMemoryFunc memoryFunc;
}
NeConfig;

//----------------------------------------------------------------------------------------------------------------------
// Nerd configuration
//----------------------------------------------------------------------------------------------------------------------

// Initialise configuration structure with default settings.
void NeDefaultConfig(NeConfig* config);

//----------------------------------------------------------------------------------------------------------------------
// Lifetime management
//----------------------------------------------------------------------------------------------------------------------

// Create a Nerd VM.
Nerd NeOpen(NeConfig* config);

// Destroy a Nerd VM.
void NeClose(Nerd N);

//----------------------------------------------------------------------------------------------------------------------
// Memory management via the VM
//----------------------------------------------------------------------------------------------------------------------

// Allocate memory.
void* NeAlloc(Nerd N, i64 bytes);

// Reallocate memory.
void* NeRealloc(Nerd N, void* address, i64 oldBytes, i64 newBytes);

// Free memory.
void NeFree(Nerd N, void* address, i64 oldBytes);

//----------------------------------------------------------------------------------------------------------------------
// Execution
//----------------------------------------------------------------------------------------------------------------------

// Return 1 or 0 on successful execution.  Result is written to outResult (if not NULL).  Origin is the description
// of where the code comes from (for error messages), source is the char array containing the source code, and
// size is the length (or -1 to use strlen()).
int NeRun(Nerd N, char* origin, char* source, i64 size, Atom* outResult);

//----------------------------------------------------------------------------------------------------------------------
// Printing
//----------------------------------------------------------------------------------------------------------------------

typedef enum
{
    NSM_Normal,
    NSM_REPL,
    NSM_Code,
}
NeStringMode;

// Convert an atom to a string representation.
NeString NeToString(Nerd N, Atom value, NeStringMode mode);

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
