#ifndef CPP_H
#define CPP_H

#include <stddef.h>

#include "token_.h"
#include "config.h"

// there are 31 standard headers as of C23, use this as default initialization
// in hash_map of includes for CPP
#ifndef N_STDC_HDRS
#define N_STDC_HDRS 31
#endif

enum PPStatus {
    PP_OK
};

typedef struct Include {
    char const * path; // this is the path to the include
    char const * include; // this is the include itself
    char const * string; // this is the contents of the include. this is always malloc'd
    long size; // length of contents
} Include;

BUILD_ALIGNMENT_STRUCT(Include)

CPP * CPP_new(MemPoolManager * mgr, CPPConfig * config);

int CPP_directive(CParser * cparser, ASTNode * directive, int type);

int CPP_preprocess(CParser * cparser, Token ** start, Token ** end);

void CPP_del(CPP * cpp);

int is_cpp_line(ASTNode * node);

#endif
