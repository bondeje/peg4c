#ifndef TYPES_H
#define TYPES_H

#include "peg4c/parser.h"
#include "peg4c/mempool.h"

typedef struct Scope Scope;
typedef struct CPreProcessor CPP;
typedef struct CParserConfig CParserConfig;

/* Thin wrapper around the default parser that adds scope context for parsing/disambiguating typedefs from other identifiers */
typedef struct CParser {
    Parser parser;
    CPP * cpp;
    Scope * scope;
    MemPoolManager * mgr; // manages generic memory allocations attached to the parsing process
    CParserConfig * config;
    char const * path;
    char const * file;
} CParser;

#endif
