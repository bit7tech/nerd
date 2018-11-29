//----------------------------------------------------------------------------------------------------------------------
// Implementation of Nerd
//----------------------------------------------------------------------------------------------------------------------

#include <assert.h>
#include <nerd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

//----------------------------------------------------------------------------------------------------------------------
// Index of code:
//
//      ARENA       Arena management.
//      CONFIG      Handles default configuration.
//      DATA        Data structures and types.
//      EXEC        Execution of code.
//      LIFETIME    Lifetime management routines for the VM.
//      MEMORY      Basic memory management.
//      PRINT       Printing and conversions to strings.
//
//----------------------------------------------------------------------------------------------------------------------

#define NE_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NE_MAX(a, b) ((a) < (b) ? (b) : (a))

//----------------------------------------------------------------------------------------------------------------------{DATA}
//----------------------------------------------------------------------------------------------------------------------
// D A T A   S T R U C T U R E S
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------
// Memory arena structure

typedef struct
{
    u8*     start;      // Start of buffer allocated for arena.
    u8*     end;        // End of buffer allocated for arena (end - start == size of buffer).
    i64     cursor;     // Position within buffer;
    i64     restore;    // Most recent restore point.
}
Arena;

//----------------------------------------------------------------------------------------------------------------------
// The structure representing the VM context.

struct _Nerd
{
    NeConfig        config;         // Copy of the configuration.
    Arena           scratch;        // A place to construct data and strings.
};

//----------------------------------------------------------------------------------------------------------------------{CONFIG}
//----------------------------------------------------------------------------------------------------------------------
// C O N F I G U R A T I O N
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

static void* DefaultMemoryFunc(Nerd N, void* address, i64 oldSize, i64 newSize)
{
    void* p = 0;
    if (newSize == 0)
    {
        free(address);
    }
    else
    {
        p = realloc(address, newSize);
    }

    return p;
}

//----------------------------------------------------------------------------------------------------------------------

void NeDefaultConfig(NeConfig* config)
{
    config->memoryFunc = &DefaultMemoryFunc;
}

//----------------------------------------------------------------------------------------------------------------------{MEMORY}
//----------------------------------------------------------------------------------------------------------------------
// M E M O R Y   M A N A G E M E N T
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

static void* MemoryOp(Nerd N, void* address, i64 oldBytes, i64 newBytes)
{
    void* p = 0;

    if (N && N->config.memoryFunc)
    {
        p = N->config.memoryFunc(N, address, oldBytes, newBytes);
    }

    return p;
}

//----------------------------------------------------------------------------------------------------------------------

void* NeAlloc(Nerd N, i64 bytes)
{
    return MemoryOp(N, 0, 0, bytes);
}

//----------------------------------------------------------------------------------------------------------------------

void* NeRealloc(Nerd N, void* address, i64 oldBytes, i64 newBytes)
{
    return MemoryOp(N, address, oldBytes, newBytes);
}

//----------------------------------------------------------------------------------------------------------------------

void NeFree(Nerd N, void* address, i64 oldBytes)
{
    MemoryOp(N, address, oldBytes, 0);
}

//----------------------------------------------------------------------------------------------------------------------{ARENA}
//----------------------------------------------------------------------------------------------------------------------
// A R E N A   M A N A G E M E N T
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------
// Initialise an arena structure

static void arenaInit(Nerd N, Arena* arena, i64 initialSize)
{
    assert(N);
    assert(arena);
    assert(initialSize > 0);

    u8* buffer = (u8 *)NeAlloc(N, initialSize);
    if (buffer)
    {
        arena->start = buffer;
        arena->end = buffer + initialSize;
        arena->cursor = 0;
        arena->restore = -1;
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Destroy an arena structure's memory resources.

static void arenaDone(Nerd N, Arena* arena)
{
    assert(N);
    assert(arena);

    NeFree(N, arena->start, (arena->end - arena->start));
    arena->start = 0;
    arena->end = 0;
    arena->cursor = 0;
    arena->restore = -1;
}

//----------------------------------------------------------------------------------------------------------------------
// Ensure we have enough space in the arena, expanding if necessary.

static int arenaEnsureSpace(Nerd N, Arena* arena, i64 numBytes)
{
    assert(N);
    assert(arena);
    assert(numBytes >= 0);

    if ((arena->start + arena->cursor + numBytes) > arena->end)
    {
        // We don't have enough room to contain those bytes.
        i64 currentSize = (arena->end - arena->start);
        i64 requiredSize = currentSize + numBytes;
        i64 newSize = currentSize + NE_MAX(requiredSize, 4096);

        u8* newArena = (u8 *)NeRealloc(N, arena->start, (i64)(arena->end - arena->cursor), newSize);
        if (newArena)
        {
            arena->start = newArena;
            arena->end = newArena + newSize;
        }
        else
        {
            return 0;
        }
    }

    return 1;
}

//----------------------------------------------------------------------------------------------------------------------
// Allocate memory on the arena.

static void* arenaAlloc(Nerd N, Arena* arena, i64 numBytes)
{
    assert(N);
    assert(arena);
    assert(numBytes >= 0);

    void* p = 0;
    if (arenaEnsureSpace(N, arena, numBytes))
    {
        p = arena->start + arena->cursor;
        arena->cursor += numBytes;
    }

    return p;
}

//----------------------------------------------------------------------------------------------------------------------
// Ensure that the next allocation is aligned to 16-byte boundary.

static void arenaAlign(Nerd N, Arena* arena)
{
    assert(N);
    assert(arena);

    i64 mod = (i64)(arena->start + arena->cursor) % 16;
    if (mod)
    {
        arenaAlloc(N, arena, 16 - mod);
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Make an allocation that's aligned on a 16-byte boundary.

static void* arenaAlignedAlloc(Nerd N, Arena* arena, i64 numBytes)
{
    assert(N);
    assert(arena);

    arenaAlign(N, arena);
    return arenaAlloc(N, arena, numBytes);
}

//----------------------------------------------------------------------------------------------------------------------
// Create a restore point so that any future allocations can be deallocated on one go.

static void arenaPush(Nerd N, Arena* arena)
{
    assert(N);
    assert(arena);

    arenaAlign(N, arena);

    i64* p = arenaAlloc(N, arena, sizeof(i64) * 2);
    p[0] = 0xaaaaaaaaaaaaaaaa;
    p[1] = arena->restore;
    arena->restore = (i64)((u8 *)p - arena->start);
}

//----------------------------------------------------------------------------------------------------------------------
// Deallocate memory allocated since the last restore point was created.

static void arenaPop(Nerd N, Arena* arena)
{
    assert(N);
    assert(arena);

    i64* p = 0;
    assert(arena->restore != -1);
    arena->cursor = arena->restore;
    p = (i64 *)(arena->start + arena->cursor);
    p[0] = 0xbbbbbbbbbbbbbbbb;
    arena->restore = p[1];
}

//----------------------------------------------------------------------------------------------------------------------
// Return the amount of space left in the current arena (before expansion is required)

static i64 arenaSpace(Arena* arena)
{
    return (arena->end - arena->start) - arena->cursor;
}

//----------------------------------------------------------------------------------------------------------------------
// Add characters according to the prinf-style format using va_list.

static char* arenaFormatV(Nerd N, Arena* arena, const char* format, va_list args)
{
    assert(N);
    assert(arena);
    assert(format);

    i64 c = arena->cursor;
    i64 maxSize = arenaSpace(arena);
    char* p = 0;

    int numChars = vsnprintf(arena->start + arena->cursor, maxSize, format, args);
    if (numChars < maxSize)
    {
        // The string fits in the space left.
        p = (char *)arenaAlloc(N, arena, numChars + 1);
    }
    else
    {
        // There wasn't enough room to hold the string.
        if (arenaEnsureSpace(N, arena, numChars + 1))
        {
            numChars = vsnprintf(arena->start + arena->cursor, numChars + 1, format, args);
            p = (char *)arenaAlloc(N, arena, numChars + 1);
        }
    }

    return p;
}

//----------------------------------------------------------------------------------------------------------------------
// Add characters according to the prinf-style format using variable arguments.

static char* arenaFormat(Nerd N, Arena* arena, const char* format, ...)
{
    assert(N);
    assert(arena);
    assert(format);

    va_list args;
    va_start(args, format);
    char* p = arenaFormatV(N, arena, format, args);
    va_end(args);
    return p;
}

//----------------------------------------------------------------------------------------------------------------------

// Helper macro to allocate memory for a particular data type.
#define ARENA_ALLOC(n, arena, t, count) (t *)arenaAlignedAlloc((n), (arena), (i64)(sizeof(t) * (count)))

//----------------------------------------------------------------------------------------------------------------------{SCRATCH}
//----------------------------------------------------------------------------------------------------------------------
// S C R A T C H   M A N A G E M E N T
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------
// Start a scratch session.

static const char* scratchStart(Nerd N)
{
    arenaPush(N, &N->scratch);
    return arenaAlloc(N, &N->scratch, 0);
}

//----------------------------------------------------------------------------------------------------------------------
// Add a printf-style formatted string with a va_list.

static void scratchFormatV(Nerd N, const char* format, va_list args)
{
    arenaFormatV(N, &N->scratch, format, args);
}

//----------------------------------------------------------------------------------------------------------------------
// Add a printf-style formatted string with variable arguments.

static void scratchFormat(Nerd N, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    scratchFormatV(N, format, args);
    va_end(args);
}

//----------------------------------------------------------------------------------------------------------------------
// End the scratch session.

static void scratchEnd(Nerd N)
{
    arenaPop(N, &N->scratch);
}

//----------------------------------------------------------------------------------------------------------------------{LIFETIME}
//----------------------------------------------------------------------------------------------------------------------
// L I F E T I M E   M A N A G E M E N T
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

Nerd NeOpen(NeConfig* config)
{
    // Create a default configuration if necessary.
    NeConfig defaultConfig;
    if (!config)
    {
        config = &defaultConfig;
        NeDefaultConfig(config);
    }

    // Create the Nerd structure.
    Nerd N = config->memoryFunc(0, 0, 0, sizeof(struct _Nerd));
    if (N)
    {
        // Copy configuration.
        N->config = *config;

        // Initialise the scratch.
        arenaInit(N, &N->scratch, 4096);
    }

    return N;
}

//----------------------------------------------------------------------------------------------------------------------

void NeClose(Nerd N)
{
    arenaDone(N, &N->scratch);
    NeFree(N, N, sizeof(struct _Nerd));
}

//----------------------------------------------------------------------------------------------------------------------{PRINT}
//----------------------------------------------------------------------------------------------------------------------
// P R I N T I N G
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

NeString NeToString(Nerd N, Atom value, NeStringMode mode)
{
    assert(N);
    assert(mode == NSM_Normal || mode == NSM_REPL || mode == NSM_Code);

    NeString p = 0;
    p = scratchStart(N);

    switch (value.type)
    {
    case AT_Nil:
        scratchFormat(N, "nil");
        break;

    case AT_Integer:
        scratchFormat(N, "%lli", value.i);
        break;

    case AT_String:
        switch (mode)
        {
        case NSM_Code:
        case NSM_REPL:
            scratchFormat(N, "\"%s\"", value.str);
            break;

        case NSM_Normal:
            scratchFormat(N, "%s", value.str);
            break;

        default:
            assert(0);
            scratchFormat(N, "<invalid mode>");
        }
        break;

    default:
        scratchFormat(N, "<invalid atom>");
        assert(0);
    }

    scratchEnd(N);
    return p;
}

//----------------------------------------------------------------------------------------------------------------------{EXEC}
//----------------------------------------------------------------------------------------------------------------------
// E X E C U T I O N
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

int NeRun(Nerd N, char* origin, char* source, i64 size, Atom* outResult)
{
    outResult->type = AT_Nil;
    return 0;
}


//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
