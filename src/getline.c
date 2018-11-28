//----------------------------------------------------------------------------------------------------------------------
// Function to fetch a line from a file
//----------------------------------------------------------------------------------------------------------------------

#ifdef _WIN32

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef SIZE_MAX
# define SIZE_MAX ((size_t) -1)
#endif
#ifndef SSIZE_MAX
# define SSIZE_MAX ((size_t) (SIZE_MAX / 2))
#endif
#if !HAVE_FLOCKFILE
# undef flockfile
# define flockfile(x) ((void) 0)
#endif
#if !HAVE_FUNLOCKFILE
# undef funlockfile
# define funlockfile(x) ((void) 0)
#endif

size_t getdelim(char **lineptr, size_t *n, int delimiter, FILE *fp)
{
    size_t result;
    size_t cur_len = 0;

    if (lineptr == NULL || n == NULL || fp == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    flockfile(fp);

    if (*lineptr == NULL || *n == 0)
    {
        *n = 120;
        *lineptr = (char *)malloc(*n);
        if (*lineptr == NULL)
        {
            result = -1;
            goto unlock_return;
        }
    }

    for (;;)
    {
        int i;

        i = getc(fp);
        if (i == EOF)
        {
            result = -1;
            break;
        }

        /* Make enough space for len+1 (for final NUL) bytes.  */
        if (cur_len + 1 >= *n)
        {
            size_t needed_max =
                SSIZE_MAX < SIZE_MAX ? (size_t)SSIZE_MAX + 1 : SIZE_MAX;
            size_t needed = 2 * *n + 1;   /* Be generous. */
            char *new_lineptr;

            if (needed_max < needed)
                needed = needed_max;
            if (cur_len + 1 >= needed)
            {
                result = -1;
                goto unlock_return;
            }

            new_lineptr = (char *)realloc(*lineptr, needed);
            if (new_lineptr == NULL)
            {
                result = -1;
                goto unlock_return;
            }

            *lineptr = new_lineptr;
            *n = needed;
        }

        (*lineptr)[cur_len] = i;
        cur_len++;

        if (i == delimiter)
            break;
    }
    (*lineptr)[cur_len] = '\0';
    result = cur_len ? cur_len : result;

unlock_return:
    funlockfile(fp);
    return result;
}

size_t getline(char **lineptr, size_t *n, FILE *stream)
{
    return getdelim(lineptr, n, '\n', stream);
}

#endif
