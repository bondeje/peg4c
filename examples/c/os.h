#ifndef C_OS_H
#define C_OS_H

#include "common.h"

extern char const PATH_SEP;
_Bool is_dir(char const * path);
_Bool is_file(char const * path);
void list_dir(char const * path, _Bool use_full_path);
_Bool file_in_dir(char const * path, char const * file);
char * get_path(char const * filepath);

#endif
