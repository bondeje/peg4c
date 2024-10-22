#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "cpp.h"
#include "common.h"

void CPPConfig_add_include(CPPConfig * cpp_config, char * inc_stmt, size_t length) {
    char const * start = strchr(inc_stmt, '=');
    char * end = inc_stmt + length;
    if (!start) {
        return;
    }

    start++;
    while (is_whitespace(*start)) {
        start++;
    }

    while (is_whitespace(*(end - 1))) {
        end--;
    }
    *end = '\0';

    IncludeDir * include_dir = malloc(sizeof(*include_dir));
    *include_dir = (IncludeDir){.path = start, .next = cpp_config->includes};
    cpp_config->includes = include_dir;
}
