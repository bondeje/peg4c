#ifdef __NetBSD__
// as suggested in github.com/wolfpython/apue.3e.netbsd since
// _SC_XOPEN_VERSION is not defined
#define _SC_XOPEN_VERSION 9
#endif

#define PATH_MAX_GUESS_ 1024

#ifdef PATH_MAX
static const long PATH_MAX_GUESS = PATH_MAX > PATH_MAX_GUESS_ ? PATH_MAX : PATH_MAX_GUESS_;
#else
static const long PATH_MAX_GUESS = PATH_MAX_GUESS_;
#define PATH_MAX PATH_MAX_GUESS_
#endif

// needed for realpath in stdlib.h
#define _XOPEN_SOURCE 600

#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>


char const PATH_SEP = '/';

long rel_path_alloc_size(char const * base) { /* also return allocated size, if nonnull */
    long pathmax;
	if ((pathmax = pathconf(base, _PC_PATH_MAX)) < 0) {
        if (errno == 0) {
            pathmax = PATH_MAX_GUESS; /* it's indeterminate */
        } else {
            fprintf(stderr, "pathconf error for _PC_PATH_MAX");
            exit(1);
        }
    }
	return ++pathmax; // add null terminator
}

_Bool is_dir(char const * path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

_Bool is_file(char const * path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

void iter_files(char const * path, int (*func)(char const * file, void * data), void * data, _Bool use_full_path) {
    struct dirent * dir = NULL;
    DIR * d = opendir(path);
    
    if (d) {
        if (use_full_path) {
            char * full_path = malloc(rel_path_alloc_size(path));
            if (!full_path) {
                fprintf(stderr, "malloc failure in listdir. requested size: %zu\n", rel_path_alloc_size(path));
                goto cleanup;
            }
            char * to_write = full_path + sprintf(full_path, "%s/", path);
            while (NULL != (dir = readdir(d))) {
                sprintf(to_write, "%s", dir->d_name);
                if (func(full_path, data)) {
                    break;
                }
            }
            free(full_path);
        } else {
            while (NULL != (dir = readdir(d))) {
                if (func(dir->d_name, data)) {
                    break;
                }
            }
        }
    } else {
        printf("invalid directory: %s\n", path);
    }
cleanup:
    if (d)
        closedir(d);
}

// why is this needed
/*
extern char *realpath (const char *__restrict __name,
    char *__restrict __resolved) __attribute__ ((__nothrow__, __leaf__));
*/
char * get_path(char const * filepath) {
    char base[PATH_MAX] = ".";
    char * last_path_sep = NULL;
    char * path_sep;
    char * path;
    char const * fp = filepath;
    while ((path_sep = strchr(fp, PATH_SEP))) {
        last_path_sep = path_sep;
        fp = path_sep + 1;
    }
    if (PATH_SEP == filepath[0]) {
        // at root, filepath is possibly relative to root, but is full path
        if (last_path_sep != filepath) {
            snprintf(base, PATH_MAX - 1, "%.*s", (int)(last_path_sep - filepath), filepath);
        } else {
            base[0] = PATH_SEP;
            base[1] = '\0';
        }
    }
    size_t length = rel_path_alloc_size(base);
    path = malloc(length);
    if (!path) {
        return NULL;
    }
    if (!realpath(filepath, path)) {
        free(path);
        return NULL;
    }
    while (length && PATH_SEP != path[length-1]) {
        length--;
    }
    length--;
    path[length] = '\0';

    return path;
}
