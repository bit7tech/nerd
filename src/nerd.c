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
//      OBJECTS     Object management.
//      PRINT       Printing and conversions to strings.
//      STRINGS     String management
//      READ        Reading tokens.
//
//----------------------------------------------------------------------------------------------------------------------

#define NE_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NE_MAX(a, b) ((a) < (b) ? (b) : (a))

#define NE_DEBUG_SYMBOL_HASHES      0

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
    Arena           objectInfo;     // All object types.

    // Built-in object types.
    int             stringType;

    // Garbage collection
    GcObj*          gcObjs;
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
    // #todo: Check usage of null-terminators.
    assert(N);
    assert(arena);
    assert(format);

    i64 maxSize = arenaSpace(arena);
    char* p = 0;

    int numChars = vsnprintf(arena->start + arena->cursor, maxSize, format, args);
    if (numChars < maxSize)
    {
        // The string fits in the space left.
        p = (char *)arenaAlloc(N, arena, numChars);
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

//----------------------------------------------------------------------------------------------------------------------{UTILITIES}
//----------------------------------------------------------------------------------------------------------------------
// U T I L T I E S
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

static u64 hash(const char* start, const char* end)
{
    u64 h = 14695981039346656037;
    for (const char* s = start; s != end; ++s)
    {
        h ^= *s;
        h *= (u64)1099511628211ull;
    }

    return h;
}

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

void NeScratchFormatV(Nerd N, const char* format, va_list args)
{
    arenaFormatV(N, &N->scratch, format, args);
}

//----------------------------------------------------------------------------------------------------------------------
// Add a printf-style formatted string with variable arguments.

void NeScratchFormat(Nerd N, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    NeScratchFormatV(N, format, args);
    va_end(args);
}

//----------------------------------------------------------------------------------------------------------------------
// Add a buffer to scratch

void NeScratchAdd(Nerd N, const char* start, const char* end)
{
    char* p = (char *)arenaAlloc(N, &N->scratch, (i64)(end - start));
    if (p) memcpy(p, start, (size_t)(end - start));
}

//----------------------------------------------------------------------------------------------------------------------
// Add a single character to scratch.

void NeScratchAddChar(Nerd N, char c)
{
    char* p = (char *)arenaAlloc(N, &N->scratch, 1);
    *p = c;
}

//----------------------------------------------------------------------------------------------------------------------
// End the scratch session.

static void scratchEnd(Nerd N)
{
    char* p = (char *)arenaAlloc(N, &N->scratch, 1);
    *p = 0;
    arenaPop(N, &N->scratch);
}

//----------------------------------------------------------------------------------------------------------------------{OBJECTS}
//----------------------------------------------------------------------------------------------------------------------
// O B J E C T   M A N A G E M E N T
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

static ObjectInfo* objectType(Nerd N, GcObj* obj)
{
    return (ObjectInfo *)N->objectInfo.start + obj->type;
}

static void objectDelete(Nerd N, void* obj)
{
    GcObj* gcObj = (GcObj *)obj - 1;
    ObjectInfo* info = objectType(N, gcObj);
    if (info->deleteFn)
    {
        info->deleteFn(N, obj);
    }
    NeFree(N, gcObj, sizeof(GcObj) + info->size);
}

//----------------------------------------------------------------------------------------------------------------------

static int objectEval(Nerd N, Atom a, Atom* outResult)
{
    assert(a.type == AT_Object);
    ObjectInfo* info = objectType(N, a.obj);
    if (info->evalFn)
    {
        return info->evalFn(N, a, a.obj + 1, outResult);
    }
    else
    {
        *outResult = a;
        return 1;
    }
}

//----------------------------------------------------------------------------------------------------------------------

int NeObjectRegister(Nerd N, ObjectInfo* info)
{
    int type = (int)(N->objectInfo.cursor / sizeof(ObjectInfo));
    ObjectInfo* newInfo = (ObjectInfo *)arenaAlloc(N, &N->objectInfo, sizeof(ObjectInfo));
    *newInfo = *info;
    return type;
}

//----------------------------------------------------------------------------------------------------------------------

void* NeObjectCreate(Nerd N, int type, const void* data)
{
    ObjectInfo* info = (ObjectInfo *)N->objectInfo.start + type;
    GcObj* newObj = (GcObj *)NeAlloc(N, sizeof(GcObj) + info->size);
    if (newObj)
    {
        newObj->next = N->gcObjs;
        newObj->type = type;
        memset(newObj + 1, 0, info->size);

        if (info->createFn)
        {
            if (!info->createFn(N, newObj + 1, data))
            {
                objectDelete(N, newObj + 1);
                return 0;
            }
        }

        N->gcObjs = newObj;
        return newObj + 1;
    }
    else
    {
        return 0;
    }
}

//----------------------------------------------------------------------------------------------------------------------{STRINGS}
//----------------------------------------------------------------------------------------------------------------------
// S T R I N G S
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

typedef struct  
{
    i64 size;
    char* str;
}
StringObject;

typedef struct  
{
    const char* start;
    const char* end;
}
Range;

static int stringCreate(Nerd N, void* obj, const void* data)
{
    StringObject* str = (StringObject *)obj;
    Range* range = (Range *)data;

    // Check the final length of string
    int strLen = (int)(range->end - range->start);
    int len = 0;
    for (int i = 0; i < strLen; ++i)
    {
        if (range->start[i] == '\\')
        {
            if ((i < (strLen - 1)) && (range->start[i + 1] == '\\')) ++len;
        }
        else
        {
            ++len;
        }
    }

    str->size = len;
    str->str = (char *)NeAlloc(N, str->size + 1);
    if (str->str)
    {
        int j = 0;
        for (int i = 0; i < strLen; ++i)
        {
            if (range->start[i] == '\\')
            {
                ++i;
                if (i < strLen)
                {
                    switch (range->start[i])
                    {
                    case 'n': str->str[j++] = '\n'; break;
                    case 'r': str->str[j++] = '\r'; break;
                    case 't': str->str[j++] = '\t'; break;
                    case 'b': str->str[j++] = '\b'; break;
                    default:  str->str[j++] = range->start[i]; break;
                    }
                }
            }
            else
            {
                str->str[j++] = range->start[i];
            }
        }
        assert(j == len);
        str->str[str->size] = 0;
        return 1;
    }
    else
    {
        return 0;
    }
}

static void stringDelete(Nerd N, void* obj)
{
    StringObject* str = (StringObject *)obj;
    NeFree(N, str->str, str->size + 1);
}

static void stringToString(Nerd N, void* obj, NeStringMode mode)
{
    StringObject* str = (StringObject *)obj;

    if (mode == NSM_Normal)
    {
        NeScratchAdd(N, str->str, str->str + str->size);
    }
    else
    {
        NeScratchFormat(N, "\"");
        for (int i = 0; i < str->size; ++i)
        {
            switch (str->str[i])
            {
            case '\n': NeScratchFormat(N, "\\n"); break;
            case '\r': NeScratchFormat(N, "\\r"); break;
            case '\t': NeScratchFormat(N, "\\t"); break;
            case '\b': NeScratchFormat(N, "\\b"); break;

            default:
                NeScratchAddChar(N, str->str[i]);
            }
        }
        NeScratchFormat(N, "\"");
    }
}

static int registerStringType(Nerd N)
{
    ObjectInfo strObjectInfo = {
        .name = "string",
        .createFn = &stringCreate,
        .deleteFn = &stringDelete,
        .evalFn = 0,
        .toStringFn = &stringToString,
        .size = sizeof(StringObject)
    };
    return NeObjectRegister(N, &strObjectInfo);
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

        // Initialise object types
        N->gcObjs = 0;
        arenaInit(N, &N->objectInfo, sizeof(ObjectInfo) * 16);
        N->stringType = registerStringType(N);
    }

    return N;
}

//----------------------------------------------------------------------------------------------------------------------

void NeClose(Nerd N)
{
    while (N->gcObjs)
    {
        GcObj* nextObj = N->gcObjs->next;
        objectDelete(N, N->gcObjs + 1);
        N->gcObjs = nextObj;
    }

    arenaDone(N, &N->scratch);
    arenaDone(N, &N->objectInfo);
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

//----------------------------------------------------------------------------------------------------------------------

Atom NeMakeBool(int b)
{
    Atom a = {
        .type = AT_Boolean,
        .i = b ? 1 : 0
    };
    return a;
}

//----------------------------------------------------------------------------------------------------------------------

Atom NeMakeAtom(AtomType at)
{
    Atom a = {
        .type = at,
    };
    return a;
}

//----------------------------------------------------------------------------------------------------------------------

Atom NeMakeChar(char c)
{
    Atom a = {
        .type = AT_Character,
        .c = c
    };
    return a;
}

//----------------------------------------------------------------------------------------------------------------------

Atom NeMakeString(Nerd N, const char* str)
{
    return NeMakeStringRanged(N, str, str + (strlen(str)));
}

//----------------------------------------------------------------------------------------------------------------------

Atom NeMakeStringRanged(Nerd N, const char* start, const char* end)
{
    Range r = { .start = start, .end = end };
    StringObject* str = (StringObject *)NeObjectCreate(N, N->stringType, &r);
    return NeMakeObject(N, str);
}

//----------------------------------------------------------------------------------------------------------------------

Atom NeMakeObject(Nerd N, void* object)
{
    Atom a = {
        .type = AT_Object,
        .obj = (GcObj *)object - 1
    };
    return a;
}

//----------------------------------------------------------------------------------------------------------------------{PRINT}
//----------------------------------------------------------------------------------------------------------------------
// P R I N T I N G
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

#define NE_IS_WHITESPACE(c) (' ' == (c) || '\t' == (c) || '\n' == (c))
#define NE_IS_CLOSE_PAREN(c) (')' == (c) || ']' == (c) || '}' == (c))
#define NE_IS_TERMCHAR(c) (NE_IS_WHITESPACE(c) || NE_IS_CLOSE_PAREN(c) || ':' == (c) || '\\' == (c) || 0 == (c))

//----------------------------------------------------------------------------------------------------------------------
// This table defines the character tokens that are understood
static struct { const char* name; char ch; } gCharMap[] =
{
    { "5\\space", ' ' },
    { "9\\backspace", '\b' },
    { "3\\tab", '\t' },
    { "7\\newline", '\n' },
    { "6\\return", '\r' },
    { "4\\bell", '\a' },
    { "3\\esc", '\033' },
    { 0, 0 }
};

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
        NeScratchFormat(N, "nil");
        break;

    case AT_Integer:
        NeScratchFormat(N, "%lli", value.i);
        break;

    case AT_Boolean:
        NeScratchFormat(N, "%s", value.i ? "yes" : "no");
        break;

    case AT_Character:
        {
            int done = 0;
            char c = value.c;

            if (NSM_Normal != mode)
            {
                NeScratchFormat(N, "\\");

                // Check for long name version.
                if (c <= ' ' || c > 126)
                {
                    int i = 0;
                    for (; gCharMap[i].name; ++i)
                    {
                        if (c == gCharMap[i].ch)
                        {
                            NeScratchFormat(N, "%s", gCharMap[i].name + 2);
                            done = 1;
                            break;
                        }
                    }
                    if (!done && !gCharMap[i].name)
                    {
                        // Unknown characters, so show hex version.
                        NeScratchFormat(N, "#x%02x", (int)c);
                        done = 1;
                    }
                }

            }

            if (!done)
            {
                if (NE_IS_WHITESPACE(c) || '\r' == c || '\b' == c || '\033' == c ||
                    (c > ' ' && c < 127))
                {
                    NeScratchAdd(N, &c, &c + 1);
                }
                else
                {
                    NeScratchFormat(N, "?");
                }
            }
        }
        break;

    case AT_Object:
        {
            ObjectInfo* info = objectType(N, value.obj);
            if (info->toStringFn)
            {
                info->toStringFn(N, value.obj + 1, mode);
            }
            else
            {
                NeScratchAddChar(N, '<');
                if (info->name)
                {
                    NeScratchAdd(N, info->name, info->name + strlen(info->name));
                }
                else
                {
                    NeScratchFormat(N, "object");
                }
                NeScratchFormat(N, ":%x>", value.obj + 1);
            }
        }
        break;

    default:
        NeScratchFormat(N, "<invalid atom>");
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
        NeScratchFormatV(N, format, args);
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
    NeToken_Symbol,         // e.g. foo, bar, hello-world
    NeToken_Character,      // e.g. \c \space 
    NeToken_String,         // e.g. "Hello", "A new line\n"

    // Keywords
    NeToken_KEYWORDS,
    NeToken_Nil = NeToken_KEYWORDS,     // e.g. nil
    NeToken_Yes,                        // e.g. yes
    NeToken_No,                         // e.g. no

    NeToken_COUNT
}
NeToken;

//----------------------------------------------------------------------------------------------------------------------
// This table represents the validity of a name (symbol or keyword) character.
//
//      0 = Cannot be found within a name.
//      1 = Can be found within a name.
//      2 = Can be found within a name but not as the initial character.
//
static const char gNameChar[128] =
{
    //          00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f  // Characters
    /* 00 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 
    /* 10 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 
    /* 20 */    0, 1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, //  !"#$%&' ()*+,-./
    /* 30 */    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 1, 1, 1, 1, // 01234567 89:;<=>?
    /* 40 */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @ABCDEFG HIJKLMNO
    /* 50 */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, // PQRSTUVW XYZ[\]^_
    /* 60 */    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // `abcdefg hijklmno
    /* 70 */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, // pqrstuvw xyz{|}~
};

//----------------------------------------------------------------------------------------------------------------------

static const unsigned int gKeyWordHashes[] =
{
    /* 0 */     NeToken_Yes,
    /* 1 */     0,
    /* 2 */     0,
    /* 3 */     0,
    /* 4 */     0,
    /* 5 */     0,
    /* 6 */     0,
    /* 7 */     0,
    /* 8 */     0,
    /* 9 */     0,
    /* A */     NeToken_No,
    /* B */     0,
    /* C */     NeToken_Nil,
    /* D */     0,
    /* E */     0,
    /* F */     0,
};

//----------------------------------------------------------------------------------------------------------------------
// The order of this array MUST match the order of the enums after
// NeToken_KEYWORDS.
static const char* gKeywords[NeToken_COUNT - NeToken_KEYWORDS] =
{
    "3nil",
    "3yes",
    "2no",
};

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
    NeOut(N, "%s(%d): LEX ERROR: ", origin, L->line);

    const char* errorMsg = scratchStart(N);
    NeScratchFormatV(N, format, args);
    scratchEnd(N);

    NeOut(N, errorMsg);
    NeOut(N, "\n");

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
    // Check for keywords and symbols
    //------------------------------------------------------------------------------------------------------------------

    else if (gNameChar[c] == 1)
    {
        while (gNameChar[c]) c = nextChar(L);
        ungetChar(L);

        i64 sizeToken = L->cursor - s0;
        u64 h = hash(s0, L->cursor);

#if NE_DEBUG_SYMBOL_HASHES
        const char* msg = scratchStart(N);
        NeScratchFormat(N, "Hash: \"");
        NeScratchAdd(N, s0, L->cursor);
        NeScratchFormat(N, "\": %x\n", (int)h);
        scratchEnd(N);
        NeOut(N, msg);
#endif

        i64 tokens = gKeyWordHashes[h & 0xf];
        while (tokens != 0)
        {
            i64 index = (tokens & 0xff) - NeToken_KEYWORDS;
            tokens >>= 8;

            if ((i64)(*gKeywords[index] - '0') == sizeToken)
            {
                // The length of the tokens match with the keywords we're currently testing against.
                if (strncmp(s0, gKeywords[index] + 1, (size_t)sizeToken) == 0)
                {
                    // It is a keywords.
                    NeToken t = NeToken_KEYWORDS + index;
                    return lexBuild(N, info, s0, L->cursor, L->line, t, NeMakeNil());
                }
            }
        }

        // Must be a symbol!
        // #todo: symbols
        return lexError(N, L, origin, "Symbols not implemented yet!");
    }

    //------------------------------------------------------------------------------------------------------------------
    // Check for strings
    //------------------------------------------------------------------------------------------------------------------

    else if ('"' == c)
    {
        s0 = L->cursor;
        c = nextChar(L);
        while ((c != 0) && (c != '\n') && (c != '"'))
        {
            c = nextChar(L);
        }

        if (c == '"')
        {
            // End of string found
            return lexBuild(N, info, s0, L->cursor - 1, L->line, NeToken_String, NeMakeNil());
        }
        else
        {
            // Unterminated string
            return lexError(N, L, origin, "Unterminated string.");
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // Check for characters
    //------------------------------------------------------------------------------------------------------------------

    else if ('\\' == c)
    {
        // Fetch next character.
        c = nextChar(L);

        if (0 == c || NE_IS_WHITESPACE(c))
        {
            return lexError(N, L, origin, "Invalid character token.");
        }

        char ch = c;

        // Check for named characters or hashed characters (\#32 or \#x20
        if ('#' == c)
        {
            c = nextChar(L);
            if (NE_IS_TERMCHAR(c) || '#' == c)
            {
                ungetChar(L);
                return lexBuild(N, info, s0, L->cursor, L->line, NeToken_Character, NeMakeChar('#'));
            }
            else if ('x' == c)
            {
                // Possible hex character
                char ch = 0;
                int maxNumChars = sizeof(ch) * 2;
                while (((c = nextChar(L)) > '0' && c <= '9') ||
                    (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F'))
                {
                    ch <<= 4;
                    if (c >= '0' && c <= '9') ch += c - '0';
                    else if (c >= 'a' && c <= 'f') ch += c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') ch += c - 'A' + 10;
                    if (--maxNumChars < 0)
                    {
                        // Too many hex digits to hold a character.
                        return lexError(N, L, origin, "Unknown character token.");
                    }
                }
                if (!NE_IS_TERMCHAR(c)) return lexError(N, L, origin, "Unknown character token.");
                ungetChar(L);

                return lexBuild(N, info, s0, L->cursor, L->line, NeToken_Character, NeMakeChar(ch));
            }
            else if (c >= '0' && c <= '9')
            {
                char ch = c - '0';
                while ((c = nextChar(L)) >= '0' && c <= '9')
                {
                    ch *= 10;
                    ch += c - '0';
                }
                if (!NE_IS_TERMCHAR(c)) return lexError(N, L, origin, "Unknown character token.");
                ungetChar(L);
                return lexBuild(N, info, s0, L->cursor, L->line, NeToken_Character, NeMakeChar(ch));
            }
        }

        c = nextChar(L);
        if (NE_IS_TERMCHAR(c))
        {
            ungetChar(L);
            return lexBuild(N, info, s0, L->cursor, L->line, NeToken_Character, NeMakeChar(ch));
        }

        while (!NE_IS_TERMCHAR(c))
        {
            if (c < 'a' || c > 'z')
            {
                // This cannot be a long character description, so return error
                if (!NE_IS_TERMCHAR(c)) return lexError(N, L, origin, "Unknown character token.");
            }
            c = nextChar(L);
        }
        ungetChar(L);

        int tokenLen = (int)(L->cursor - s0);
        for (int i = 0; gCharMap[i].name; ++i)
        {
            int lenToken = (int)gCharMap[i].name[0] - '0' + 1;
            if (lenToken != tokenLen) continue;

            if (strncmp(gCharMap[i].name + 1, s0, (size_t)tokenLen) == 0)
            {
                // Found a match
                return lexBuild(N, info, s0, L->cursor, L->line, NeToken_Character, NeMakeChar(gCharMap[i].ch));
            }
        }

        return lexError(N, L, origin, "Unknown character token.");
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

static int lex(Nerd N, const char* origin, const char* start, const char* end, Arena* outArena)
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

    if (t == NeToken_Error)
    {
        arenaDone(N, &info);
        *outArena = info;
        return 0;
    }

    *outArena = info;
    return 1;
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
    case NeToken_Character:
        // The lexical analyser did the hard work for us!
        *outAtom = t->atom;
        break;

    case NeToken_Yes:
        *outAtom = NeMakeBool(1);
        break;

    case NeToken_No:
        *outAtom = NeMakeBool(0);
        break;

    case NeToken_String:
        *outAtom = NeMakeStringRanged(N, t->start, t->end);
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
    case AT_Boolean:
    case AT_Character:
        // These evaluate to themselves.
        *outResult = a;
        break;

    case AT_Object:
        return objectEval(N, a, outResult);

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
    *outResult = NeMakeNil();
    Arena tokenArena;
    if (!lex(N, origin, source, source + size, &tokenArena))
    {
        return 0;
    }
    const NeLexInfo* tokens = (NeLexInfo *)tokenArena.start;
    const NeLexInfo* endToken = tokens + (tokenArena.cursor / sizeof(NeLexInfo));

    int result = 1;

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
