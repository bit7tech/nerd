//----------------------------------------------------------------------------------------------------------------------
// Implementation of Nerd
//----------------------------------------------------------------------------------------------------------------------

#include <assert.h>
#include <nerd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------------------------------------------------
// Index of code:
//
//      ARENA       Arena management.
//      CONFIG      Setting up default configuration.
//      DATA        Data structures and types.
//      EXEC        Execution of code.
//      LEX         Lexical analysis.
//      LIFETIME    Lifetime management routines for the VM.
//      MEMORY      Basic memory management.
//      PRINT       Printing and conversions to strings.
//      READ        Reading tokens.
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
    config->outputFunc = 0;
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

//----------------------------------------------------------------------------------------------------------------------{ATOM}
//----------------------------------------------------------------------------------------------------------------------
// A T O M   M A N A G M E N T
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

Atom NeMakeNil()
{
    Atom a = {
        .type = AT_Nil
    };
    return a;
}

//----------------------------------------------------------------------------------------------------------------------

Atom NeMakeInt(i64 i)
{
    Atom a = {
        .type = AT_Integer,
        .i = i
    };
    return a;
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

//----------------------------------------------------------------------------------------------------------------------

static void NeOutV(Nerd N, const char* format, va_list args)
{
    if (N->config.outputFunc)
    {
        const char* msg = scratchStart(N);
        scratchFormatV(N, format, args);
        scratchEnd(N);
        N->config.outputFunc(N, msg);
    }
}

//----------------------------------------------------------------------------------------------------------------------

void NeOut(Nerd N, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    NeOutV(N, format, args);
    va_end(args);
}

//----------------------------------------------------------------------------------------------------------------------{LEX}
//----------------------------------------------------------------------------------------------------------------------
// L E X I C A L   A N A L Y S I S
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

typedef enum _NeToken
{
    // Errors
    NeToken_Unknown = -100,
    NeToken_Error,

    // End of token stream
    NeToken_EOF = 0,

    // Literals
    NeToken_Number,         // e.g. 42, -34
}
NeToken;

//----------------------------------------------------------------------------------------------------------------------

#define NE_IS_WHITESPACE(c) (' ' == (c) || '\t' == (c) || '\n' == (c))
#define NE_IS_CLOSE_PAREN(c) (')' == (c) || ']' == (c) || '}' == (c))
#define NE_IS_TERMCHAR(c) (NE_IS_WHITESPACE(c) || NE_IS_CLOSE_PAREN(c) || ':' == (c) || '\\' == (c) || 0 == (c))

//----------------------------------------------------------------------------------------------------------------------

typedef struct _NeLexInfo
{
    const char*     start;          // Start of text in source code.
    const char*     end;            // One past the end of text in the source code.
    i64             line;           // Line number of token.
    NeToken         token;          // Token type.
    Atom            atom;           // Atom generated from token.
}
NeLexInfo;

//----------------------------------------------------------------------------------------------------------------------

typedef struct _NeLex
{
    i64                 line;           // The current line number in the source.
    i64                 lastLine;       // The previous line number of the last character read.
    const char*         cursor;         // The current read position in the lexical stream.
    const char*         lastCursor;     // The last read position of the last character read.
    const char*         end;
}
NeLex;

//----------------------------------------------------------------------------------------------------------------------
// Fetch the next character in the stream.  This function keeps tracks of the newlines.  All the different newline
// representations are converted to just '\n'.  If there are no more characters in the stream, a 0 is returned.

static char nextChar(NeLex* L)
{
    char c;

    L->lastCursor = L->cursor;
    L->lastLine = L->line;

    if (L->cursor == L->end) return 0;

    c = *L->cursor++;

    if ('\r' == c || '\n' == c)
    {
        // Handle new-lines
        ++L->line;

        if (c == '\r')
        {
            if ((L->cursor < L->end) && (*L->cursor == '\n'))
            {
                ++L->cursor;
            }

            c = '\n';
        }
    }

    return c;
}

//----------------------------------------------------------------------------------------------------------------------
// Return the cursor to the previous character.  You can only call this once after calling nextChar().

static void ungetChar(NeLex* L)
{
    L->line = L->lastLine;
    L->cursor = L->lastCursor;
}

//----------------------------------------------------------------------------------------------------------------------
// Add an info to the end of the arena.

static NeToken lexBuild(Nerd N, Arena* info, const char* start, const char* end, i64 line, NeToken token, Atom atom)
{
    NeLexInfo* li = ARENA_ALLOC(N, info, NeLexInfo, 1);
    li->start = start;
    li->end = end;
    li->line = line;
    li->token = token;
    li->atom = atom;

    return token;
}

//----------------------------------------------------------------------------------------------------------------------
// Return an error

NeToken lexErrorV(Nerd N, NeLex* L, const char* origin, const char* format, va_list args)
{
    const char* errorMsg = scratchStart(N);
    scratchFormatV(N, format, args);

    NeOut(N, "%s(%d): LEX ERROR: %s\n", origin, L->line, errorMsg);

    scratchEnd(N);
    return NeToken_Error;
}

//----------------------------------------------------------------------------------------------------------------------

NeToken lexError(Nerd N, NeLex* L, const char* origin, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    lexErrorV(N, L, origin, format, args);
    va_end(args);
    return NeToken_Error;
}

//----------------------------------------------------------------------------------------------------------------------
// Fetch the next token

static NeToken lexNext(Nerd N, Arena* info, NeLex* L, const char* origin)
{
    if (L->cursor == L->end) return NeToken_EOF;

    // Find the next meaningful character, skipping whitespace and comments.  Comments are delimited by ';' or '# ',
    // or '#!' (notice the space) to the end of the line, and between '#|' and '|#'.  All comments are nestable.
    char c = nextChar(L);

    for (;;)
    {
        if (0 == c)
        {
            // End of stream reached.
            return NeToken_EOF;
        }

        // Check for whitespace.  If found, ignore, and get the next character in the stream.
        if (NE_IS_WHITESPACE(c))
        {
            c = nextChar(L);
            continue;
        }

        // Check for comments.
        if (';' == c)
        {
            while ((c != 0) && (c != '\n')) c = nextChar(L);
            continue;
        }
        else if ('#' == c)
        {
            c = nextChar(L);

            if ('|' == c)
            {
                // Nestable, multi-line comment.
                int depth = 1;
                while (c != 0 && depth)
                {
                    c = nextChar(L);
                    if ('#' == c)
                    {
                        // Check for nested #|...|#
                        c = nextChar(L);
                        if ('|' == c)
                        {
                            ++depth;
                        }
                    }
                    else if ('|' == c)
                    {
                        // Check for terminator |#
                        c = nextChar(L);
                        if ('#' == c)
                        {
                            --depth;
                        }
                    }
                }
                continue;
            }
            else if (NE_IS_WHITESPACE(c))
            {
                // Line-base comment.
                while ((c != 0) && (c != '\n')) c = nextChar(L);
                continue;
            }
            else
            {
                // Possible prefix character.
                continue;
            }
        }

        // If we've reached this point, we have a meaningful character.
        break;
    }

    const char* s0 = L->cursor - 1;

    //------------------------------------------------------------------------------------------------------------------
    // Check for numbers
    //------------------------------------------------------------------------------------------------------------------

    if (
        // Check for digit
        (c >= '0' && c <= '9') ||
        // Check for '-' or '+'
        ('-' == c || '+' == c))
    {
        // We run the characters through the state machine and either reach state 100 (our terminal state) or EOF.
        int state = 0;
        i64 sign = 1;
        i64 intPart = 0;
        i64 base = 10;

        // Each state (except the start) always fetches the next character for the next state (i.e. consumes the 
        // current character).
        for (;;)
        {
            switch (state)
            {
            case 0:         // START
                if (c >= '0' && c <= '9')
                {
                    state = 2;
                }
                else if (c == '-' || c == '+')
                {
                    state = 1;
                }
                else
                {
                    // Cannot possibly reach here since we check for valid starting characters earlier.  If this
                    // triggers, then the validity check does not match valid state transitions (i.e. if does not
                    // match the case statements).
                    assert(0);
                }
                break;

            case 1:         // +/-
                if (c == '-')
                {
                    sign = -1;
                }
                c = nextChar(L);
                state = 2;
                break;

            case 2:         // Integer digits.
                intPart = intPart * base + (c - '0');
                c = nextChar(L);
                if (c < '0' || c > '9') state = 100;
                break;

            case 100:
                ungetChar(L);
                return lexBuild(N, info, s0, L->cursor, L->line, NeToken_Number, NeMakeInt(sign * intPart));
            }
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // Unknown token
    //------------------------------------------------------------------------------------------------------------------

    else
    {
        return lexError(N, L, origin, "Unknown token");
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Analyse some source code and obtains it's tokens.  It returns an arena full of NeLexInfo structures.

static Arena lex(Nerd N, const char* origin, const char* start, const char* end)
{
    NeLex L;
    L.line = 1;
    L.lastLine = 1;
    L.cursor = start;
    L.lastCursor = start;
    L.end = end;
    
    Arena info;
    arenaInit(N, &info, 4096);

    NeToken t = NeToken_Unknown;
    while (t != NeToken_Error && t != NeToken_EOF)
    {
        t = lexNext(N, &info, &L, origin);
    }

    return info;
}

//----------------------------------------------------------------------------------------------------------------------{READ}
//----------------------------------------------------------------------------------------------------------------------
// R E A D I N G
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

static int nextAtom(Nerd N, const NeLexInfo** tokens, const NeLexInfo* end, Atom* outAtom)
{
    const NeLexInfo* t = (*tokens)++;
    int result = 1;

    switch (t->token)
    {
    case NeToken_Number:
        // The lexical analyser did the hard work for us!
        *outAtom = t->atom;
        break;

    default:
        // #todo: add error message.
        result = 0;
    }

    return result;
}

//----------------------------------------------------------------------------------------------------------------------{EXEC}
//----------------------------------------------------------------------------------------------------------------------
// E X E C U T I O N
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

static int eval(Nerd N, Atom a, Atom* outResult)
{
    switch (a.type)
    {
    case AT_Nil:
    case AT_Integer:
    case AT_String:
        // These evaluate to themselves.
        *outResult = a;
        break;

    default:
        assert(0);
        *outResult = NeMakeNil();
        return 0;
    }

    return 1;
}

//----------------------------------------------------------------------------------------------------------------------

int NeRun(Nerd N, char* origin, char* source, i64 size, Atom* outResult)
{
    if (size == -1)
    {
        size = (i64)strlen(source);
    }

    // Fetch the lexemes.
    Arena tokenArena = lex(N, origin, source, source + size);
    const NeLexInfo* tokens = (NeLexInfo *)tokenArena.start;
    const NeLexInfo* endToken = tokens + (tokenArena.cursor / sizeof(NeLexInfo));

    int result = 1;

    *outResult = NeMakeNil();
    while (tokens != endToken)
    {
        if (!nextAtom(N, &tokens, endToken, outResult)) return 0;
        if (!eval(N, *outResult, outResult)) return 0;
    }

    arenaDone(N, &tokenArena);
    return 1;
}


//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
