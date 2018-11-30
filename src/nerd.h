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

// Output operation callback.
typedef void (*NeOutputFunc) (Nerd, const char*);

//----------------------------------------------------------------------------------------------------------------------
// Data structures
//----------------------------------------------------------------------------------------------------------------------

typedef enum
{
    AT_Nil,
    AT_Integer,
    AT_Boolean,
    AT_Character,
    AT_Object,
}
AtomType;

// This represents a null-terminated string that is temporary and will be deleted later.
typedef const char* NeString;

//----------------------------------------------------------------------------------------------------------------------
// Garbage collected header

typedef struct _GcHeader
{
    u32 marked : 1;
    u32 type : 31;
    struct _GcHeader* next;
}
GcObj;

//----------------------------------------------------------------------------------------------------------------------
// A single Nerd value.

typedef struct _Atom
{
    AtomType type;
    union {
        i64 i;
        char c;
        GcObj* obj;
    };
}
Atom;

//----------------------------------------------------------------------------------------------------------------------
// Configuration structure when creating a VM.

typedef struct
{
    NeMemoryFunc memoryFunc;
    NeOutputFunc outputFunc;
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

// Garbage collect the VM.
void NeGarbageCollect(Nerd N);

//----------------------------------------------------------------------------------------------------------------------
// Atom construction
//----------------------------------------------------------------------------------------------------------------------

// Create a nil atom.
Atom NeMakeNil();

// Create an integer atom.
Atom NeMakeInt(i64 i);

// Create a boolean atom.
Atom NeMakeBool(int b);

// Create a character atom.
Atom NeMakeChar(char c);

// Create an atom that is just a type.
Atom NeMakeAtom(AtomType at);

// Create a string from a null terminated string.
Atom NeMakeString(Nerd N, const char* str);

// Create a string from a range from start up to an not including end.
Atom NeMakeStringRanged(Nerd N, const char* start, const char* end);

// Create an object atom.
Atom NeMakeObject(Nerd N, void* object);

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
// Scratch pad to generate strings.
//----------------------------------------------------------------------------------------------------------------------

void NeScratchFormatV(Nerd N, const char* format, va_list args);
void NeScratchFormat(Nerd N, const char* format, ...);
void NeScratchAdd(Nerd N, const char* start, const char* end);
void NeScratchAddChar(Nerd N, char c);

//----------------------------------------------------------------------------------------------------------------------
// Objects
//----------------------------------------------------------------------------------------------------------------------

typedef enum
{
    NSM_Normal,
    NSM_REPL,
    NSM_Code,
}
NeStringMode;

//----------------------------------------------------------------------------------------------------------------------
// Object callbacks

typedef int (*ObjectCreateFn) (Nerd N, void* obj, const void* data);
typedef void (*ObjectDeleteFn) (Nerd N, void* obj);
typedef int (*ObjectEvalFn) (Nerd N, Atom a, void* obj, Atom* outResult);
typedef void (*ObjectToStringFn) (Nerd N, void* obj, NeStringMode mode);

//----------------------------------------------------------------------------------------------------------------------
// Default behaviours of functions (if set to 0):
//
//      createFn        Fills the memory with 0.
//      deleteFn        Does nothing.
//      evalFn          Evaluates to itself.
//      toStringFn      Outputs: <name:address_in_hex>
//
// If you wish to change these behaviours create your own function.
//

typedef struct
{
    const char*         name;           // Name of object type.
    ObjectCreateFn      createFn;       // Pointer to function that initialises an object (memory handled by VM).
    ObjectDeleteFn      deleteFn;       // Pointer to function that destroys an object (memory handled by VM).
    ObjectEvalFn        evalFn;         // Pointer to function that evaluates an atom representing this object.
    ObjectToStringFn    toStringFn;     // Pointer to function that returns a string 
    i32                 size;           // Size of object in bytes.
}
ObjectInfo;

// Register an object type
int NeObjectRegister(Nerd N, ObjectInfo* info);

// Create an object of a particular type.
void* NeObjectCreate(Nerd N, int type, const void* data);

//----------------------------------------------------------------------------------------------------------------------
// Reading
//----------------------------------------------------------------------------------------------------------------------

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

// Convert an atom to a string representation.
NeString NeToString(Nerd N, Atom value, NeStringMode mode);

// Output a printf-style formatted string to the output callback.
void NeOut(Nerd N, const char* format, ...);

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
