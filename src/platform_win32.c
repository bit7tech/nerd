// Nerd - Win32 platform layer
// Copyright (C)2018 Matt Davies, all rights reserved.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <nerd.h>

//----------------------------------------------------------------------------------------------------------------------
// Returns the path of the executable.
// Free the return value with free().
//----------------------------------------------------------------------------------------------------------------------

char* getExePathName()
{
    int len = MAX_PATH;
    for(;;)
    {
        char* buf = malloc(len);
        if (!buf) return 0;
        DWORD pathLen = GetModuleFileName(0, buf, len);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            // Not enough memory!
            len = 2 * len;
            free(buf);
            continue;
        }

        while (buf[pathLen] != '\\') --pathLen;
        buf[pathLen] = 0;
        return buf;
    }


}

//----------------------------------------------------------------------------------------------------------------------
// Entry point
//----------------------------------------------------------------------------------------------------------------------

extern size_t getline(char **lineptr, size_t *n, FILE *stream);

void SigHandler(int sig)
{

}

int main(int argc, char** argv)
{
    char* exePath = getExePathName();

    signal(SIGINT, &SigHandler);

    int cont = 1;
    
    while(cont)
    {
        printf("Nerd REPL (V0.0)\n");
        printf("PWD: %s\n", exePath);
        printf("\nEnter ,q to quit.\n\n");
        cont = 0;

        NeConfig config;
        NeDefaultConfig(&config);
        Nerd N = NeOpen(&config);
        if (N)
        {
            for (;;)
            {
                char* input = 0;
                size_t size = 0;
                size_t result = 0;

                printf("> ");
                result = getline(&input, &size, stdin);
                if ((-1 != result) && (*input == ','))
                {
                    switch (input[1])
                    {
                    case 'q':
                        result = -1;
                        break;

                    case 'r':
                        result = -1;
                        cont = 1;
                        break;
                    }
                }

                if (-1 == result)
                {
                    free(input);
                    break;
                }

                if (size)
                {
                    Atom result = { AT_Nil };
                    int success = NeRun(N, "<stdin>", input, size, &result);

                    NeString resultString = NeToString(N, result, success ? NSM_REPL : NSM_Normal);

                    printf(success ? "==> %s\n" : "ERROR: %s\n", resultString);
                }
            }
        }
        NeClose(N);
    }

    free(exePath);
}

