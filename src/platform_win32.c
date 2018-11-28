// Nerd - Win32 platform layer
// Copyright (C)2018 Matt Davies, all rights reserved.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char** argv)
{
    char* exePath = getExePathName();
    printf("%s\n", exePath);
    free(exePath);
}

