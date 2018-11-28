//----------------------------------------------------------------------------------------------------------------------
// Implementation of Nerd
//----------------------------------------------------------------------------------------------------------------------

#include <nerd.h>
#include <stdlib.h>

//----------------------------------------------------------------------------------------------------------------------
// Index of code:
//
//      CONFIG      Handles default configuration.
//      EXEC        Execution of code.
//      LIFETIME    Lifetime management routines for the VM.
//      PRINT       Printing and conversions to strings.
//
//----------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------{CONFIG}
//----------------------------------------------------------------------------------------------------------------------
// C O N F I G U R A T I O N
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

static void* DefaultMemoryFunc(void* address, i64 oldSize, i64 newSize)
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

void NeDefaultConfig(NeConfig* config)
{
    config->memoryFunc = &DefaultMemoryFunc;
}

//----------------------------------------------------------------------------------------------------------------------{LIFETIME}
//----------------------------------------------------------------------------------------------------------------------
// L I F E T I M E   M A N A G E M E N T
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

Nerd NeOpen(NeConfig* config)
{
    return 0;
}

void NeClose(Nerd N)
{

}

//----------------------------------------------------------------------------------------------------------------------{PRINT}
//----------------------------------------------------------------------------------------------------------------------
// P R I N T I N G
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

NeString NeToString(Nerd N, Atom value, NeStringMode mode)
{
    return 0;
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
