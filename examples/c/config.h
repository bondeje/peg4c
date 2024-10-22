#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include "common.h"

typedef struct IncludeDir IncludeDir;
struct IncludeDir {
    char const * path; // must never be NULL
    IncludeDir * next;
};

BUILD_ALIGNMENT_STRUCT(IncludeDir)

typedef struct CPPConfig {
    IncludeDir * includes; // stack of includes
} CPPConfig;

BUILD_ALIGNMENT_STRUCT(CPPConfig)

typedef struct CParserConfig {
    char c; // dummy. not actually used
} CParserConfig;

BUILD_ALIGNMENT_STRUCT(CParserConfig)

void CPPConfig_add_include(CPPConfig * cpp_config, char * inc_stmt, size_t length);

#endif
