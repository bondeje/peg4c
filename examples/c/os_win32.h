#ifdef __NetBSD__
// as suggested in github.com/wolfpython/apue.3e.netbsd since
// _SC_XOPEN_VERSION is not defined
#define _SC_XOPEN_VERSION 9
#endif

#ifdef PATH_MAX
static const long PATH_MAX_GUESS = PATH_MAX > PATH_MAX_GUESS_ ? PATH_MAX : PATH_MAX_GUESS_;
#else
static const long PATH_MAX_GUESS = PATH_MAX_GUESS_;
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <windows.h>

char const PATH_SEP = '\\';

_Bool is_dir(char const * path) {
    return GetFileAttributesA(path) & FILE_ATTRIBUTE_DIRECTORY;
}

_Bool is_file(char const * path) {
    return !(GetFileAttributesA(path) & FILE_ATTRIBUTE_DIRECTORY);
}

void iter_files(char const * path, int (*func)(char const * file, void * data), void * data, _Bool use_full_path) {
    // mostly following https://learn.microsoft.com/en-us/windows/win32/fileio/listing-the-files-in-a-directory ignoring unicode
    size_t str_len = strlen(path);
    WIN32_FIND_DATAA ffd;
    char search_path[MAX_PATH];
    
    HANDLE hFind = INVALID_HANDLE_VALUE;
    if (MAX_PATH < str_len + 3) { // + 3 for "\*" + NULL terminator
        fprintf(stderr, "input path is too long %d (max = %d)\n", str_len, MAX_PATH - 3);
        return;
    }

    hFind = FindFirstFileA(search_path, &ffd);
    if (INVALID_HANDLE_VALUE == hFind) {
        fprintf(stderr, "failure to open directory iterator for %s\n", search_path);
        return;
    }

    sprintf(search_path, "%s\\*", path);
    if (use_full_path) {
        char full_path[MAX_PATH];
        char * to_write = &full_path[0] + sprintf(full_path, "%s\\", path);
        do {
            sprintf(to_write, "%s", ffd.cFileName);
            func(ffd.cFileName, data);
        } while (0 != FindNextFile(hFind, &ffd));
    } else {
        do {
            func(ffd.cFileName, data);
        } while (0 != FindNextFile(hFind, &ffd));
    }
    
    if (ERROR_NO_MORE_FILES != GetLastError()) {
        printf("error in FindFirstFile\n");
    }
    FindClose(hFind);
}

char * get_path(char const * filepath) {
    DWORD length = GetFullPathNameA(filepath, 0, NULL, NULL);
    char * path = malloc(length);
    if (GetFullPathNameA(filepath, length, path, NULL)) {
        return path;
    }
    return NULL;
}
