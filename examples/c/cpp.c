#include "peg4c/hash_utils.h"

#include "c.h"
#include "cparser.h"
#include "cpp.h"
#include "cstring.h"
#include "config.h"
#include "os.h"
#include "common.h"

#define DEFAULT_MACRO_CAPACITY 7

#define MACRO 0
#define MACRO_FUNCTION 1
#define EXPECTS_VA_ARGS 1

typedef struct Macro {
    Token * id;
    Token * rep_start; // start of replacement tokens
    Token * rep_end; // end of replacement tokens (inclusive)
    // not going to distinguish between macros and macro functions
    unsigned short nparam;
    unsigned char type;
    unsigned char va_arg;
} Macro, *pMacro;

BUILD_ALIGNMENT_STRUCT(Macro)

#define KEY_TYPE pToken
#define VALUE_TYPE pMacro
#define KEY_COMP Token_scomp
#define HASH_FUNC Token_shash
#include "peg4c/hash_map.h"

#define KEY_TYPE pToken
#define VALUE_TYPE Include
#define KEY_COMP Token_scomp
#define HASH_FUNC Token_shash
#include "peg4c/hash_map.h"

struct CPreProcessor {
    HASH_MAP(pToken, pMacro) defines;   // cache macros from #define
    HASH_MAP(pToken, Include) includes; // cache location of includes
    MemPoolManager * macro_mgr;
    MemPoolManager * cstr_mgr; // should take from the parser
    CPPConfig * config;
};

BUILD_ALIGNMENT_STRUCT(CPP)

typedef struct TokenPair {
    Token * start;
    Token * end;
} TokenPair;

BUILD_ALIGNMENT_STRUCT(TokenPair)

// for creating mapping of identifier tokens to token stream of replacements
#define KEY_TYPE pToken
#define VALUE_TYPE TokenPair
#define KEY_COMP Token_scomp
#define HASH_FUNC Token_shash
#include "peg4c/hash_map.h"

// allocate CPP from a pool
// config should not be NULL
CPP * CPP_new(MemPoolManager * mgr, CPPConfig * config) {
    CPP * out = malloc(sizeof(CPP));
    if (!out) {
        return out;
    }
    *out = (CPP){.config = config, .macro_mgr = MemPoolManager_new(128, sizeof(Macro), _Alignof(Macro)), .cstr_mgr = mgr};    
    return out;
}

void CPP_del(CPP * self) {
    MemPoolManager_del(self->macro_mgr);
    if (self->defines.capacity) {
        self->defines._class->dest(&self->defines);
    }
    free(self);
}

void CPP_define(CPP * cpp, ASTNode * define) {
    if (!cpp->defines.capacity) {
        HASH_MAP_INIT(pToken, pMacro)(&cpp->defines, DEFAULT_MACRO_CAPACITY);
    }
    Macro * macro = MemPoolManager_next(cpp->macro_mgr);
    *macro = (Macro){
        .id = define->children[2]->token_start
    };

    if (define->nchildren > 5) {
        macro->type = MACRO_FUNCTION;
        ASTNode * id_list = NULL;
        if (C_ELLIPSIS == define->children[4]->rule) {
            macro->va_arg = EXPECTS_VA_ARGS;
        } else if (C_ELLIPSIS == define->children[5]->rule) {
            macro->va_arg = EXPECTS_VA_ARGS;
            id_list = define->children[4];
        } else if (define->children[4]->nchildren) { // no va_args
            id_list = define->children[4]->children[0];
        }
        if (id_list) {
            macro->nparam = (id_list->nchildren + 1) >> 1;
        }
    } else {
        macro->type = MACRO;
    }

    ASTNode * rep_list = define->children[define->nchildren - 2];
    if (rep_list->nchildren) {
        macro->rep_start = rep_list->token_start;
        macro->rep_end = rep_list->token_end;    
    }
    cpp->defines._class->set(&cpp->defines, macro->id, macro);
}

void CPP_undef(CPP * cpp, ASTNode * undef) {
    cpp->defines._class->remove(&cpp->defines, undef->children[2]->token_start);
}

void CPP_get_directive_line(ASTNode * line, Token ** start, Token ** end) {
    *start = line->token_start;
    Token * cur = (*start);
    while (cur->string[0] != '\n') {
        cur = cur->next;
    }
    *end = cur;
}

void CPP_init_macro_arg_map(HASH_MAP(pToken, TokenPair) * arg_map, Macro * macro, Token * start, Token * end) {
    // start should initially be pointing at the '(' start of args list
    start = start->next;
    Token * id = macro->id->next->next; // point to the first parameter
    unsigned char nparams = 0;
    while (nparams < macro->nparam && start != end) {
        start->prev = NULL;
        Token * cur = start->next;
        while (cur != end && ',' != cur->string[0]) { // end should be equivalent to ')'
            cur = cur->next;
        }
        cur->prev->next = NULL;
        // add to argument map. cur current points to either ',' or ')'
        arg_map->_class->set(arg_map, id, (TokenPair){.start = start, .end = cur->prev});
        // advance start of next argument
        if (cur != end) {
            start = cur->next;
        } else {
            start = cur;
        }
        
        // advance to next parameter
        id = id->next->next;
        nparams++;
    }
    if (start != end) {
        fprintf(stderr, "CPP_init_macro_arg_map: error in number of args and number of parameters for macro %.*s. # of parameters: %c\n", 
            (int)macro->id->length, macro->id->string, nparams);
    }
}

void CPP_expand_macro_function(Parser * parser, HASH_MAP(pToken, TokenPair) * arg_map, Macro * macro, Token ** start_, Token ** end_) {
    Token * start = *start_, * end = (*end_)->next;
    Token * head = &(Token){0};
    Token * tail = head;

    // this is where '#' and "##" should be handled
    while (start != end) {
        TokenPair rep;
        if (arg_map->_class->get(arg_map, start, &rep)) {
            Token_append(tail, Parser_copy_token(parser, start));
            tail = tail->next;
        } else {
            Token * s = rep.start, * e = rep.end;
            Parser_copy_tokens(parser, &s, &e);
            Token_append(tail, s);
            tail = e;
        }
        start = start->next;
    }
    *start_ = head->next;
    *end_ = tail;
}

// TODO: add compatibility with macro functions
// state of parser must be tokenizing  when entering CPP_check
int CPP_check(Parser * parser, CPP * cpp, ASTNode * id_re) {
    Macro * macro = NULL;
    Token * start = id_re->token_start;
    // since this only occurs during tokenizing, the token->length is not reliable.
    // need to overwrite with node->str_length
    Token * key = &(Token){.string = start->string, .length = id_re->str_length};
    // retrieve the macro if present
    if (!cpp->defines.capacity || cpp->defines._class->get(&cpp->defines, key, &macro)) {
        return 1;
    }
    if (macro->rep_start && macro->rep_end) {
        Token * rep_start = macro->rep_start;
        Token * rep_end = macro->rep_end;
        
        // insert before because a successful macro replacement will have the macro skipped
        if (MACRO_FUNCTION == macro->type) {
            size_t length = 0;
            char const * c = start->string + id_re->str_length;
            int nopen = 0;
            while (*c != '(') {
                c++;
                length++;
            }
            c++;
            length++;
            nopen++;

            while (nopen) {
                switch (*c) {
                    case '(': {
                        nopen++;
                        break;
                    }
                    case ')': {
                        nopen--;
                        break;
                    }
                }
                c++;
                length++;
            }
            
            unsigned short nparam = macro->nparam;
            if (nparam) {
                Token * par_start = NULL;
                Token * par_end = NULL;
                // no need to set parser->tokenizing since that is a requirement to entering CPP_check
                int status = parser->_class->tokenize(parser, start->string + id_re->str_length, length, &par_start, &par_end);
                
                // create map
                HASH_MAP(pToken, TokenPair) arg_map;
                HASH_MAP_INIT(pToken, TokenPair)(&arg_map, next_prime(nparam));

                CPP_init_macro_arg_map(&arg_map, macro, par_start, par_end);

                CPP_expand_macro_function(parser, &arg_map, macro, &rep_start, &rep_end);

                // cleanup
                arg_map._class->dest(&arg_map);
            } else {
                Parser_copy_tokens(parser, &rep_start, &rep_end);
            }
            id_re->str_length += length; // so that we skip the whole macro function invocation
        } else {
            Parser_copy_tokens(parser, &rep_start, &rep_end); // non function macros simply copy the replacement list
        }
        Token_insert_before(start, rep_start, rep_end); 
    }    

    return 0;
}

Token * build_include(CPP * cpp, ASTNode * inc_cl) {
    ASTNode * hdr = inc_cl->children[2];
    if ('"' == hdr->token_start->string[0]) {
        Token * out = MemPoolManager_aligned_alloc(cpp->cstr_mgr, sizeof(Token) + hdr->token_start->length - 1, _Alignof(Token));
        *out = *hdr->token_start;

        char * str = ((char *)out) + sizeof(Token);
        sprintf(str, "%.*s", (int)out->length - 2, out->string + 1);
        out->string = str;
        out->length -= 2;
        return out;
    }

    size_t inc_len = 0;
    hdr = hdr->children[1]; // non_rangle+
    for (unsigned int i = 1; i < hdr->nchildren; i++) {
        inc_len += hdr->children[i]->token_start->length;
    }
    Token * out = MemPoolManager_aligned_alloc(cpp->cstr_mgr, sizeof(Token) * inc_len, _Alignof(Token));
    *out = *hdr->token_start;
    char * str = ((char *)out) + sizeof(Token);
    out->length = inc_len;
    inc_len = 0;
    for (unsigned int i = 1; i < hdr->nchildren; i++) {
        inc_len += sprintf(str + inc_len, "%.*s", (int)hdr->children[i]->token_start->length, hdr->children[i]->token_start->string);
    }
    out->string = str;
    return out;
}

int load_include(Include * include_info, CPPConfig * cpp_config, char const * include, char const * starting_path) {
    // always search in current directory first
    *include_info = (Include){0};
    IncludeDir * stack = &(IncludeDir){.path = starting_path, .next = cpp_config->includes};
    while (stack && !file_in_dir(stack->path, include)) {
        stack = stack->next;
    }
    if (!stack) {
        return 1; // returns 0s in include_info
    }

    char * file_path = malloc(strlen(include) + strlen(stack->path) + 2); // +2 for null terminator and PATH_SEP from os.h
    sprintf(file_path, "%s%c%s", stack->path, PATH_SEP, include);
    FILE * file = fopen(file_path, "rb");
    if (fseek(file, 0, SEEK_END)) {
        printf("failed to seek input file for input file <%s>\n", file_path);
        return 1;
    }
    long size = 0;
    if ((size = ftell(file)) < 0) {
        printf("failed to get file size for input file <%s>\n", file_path);
        return 2;
    }
    fseek(file, 0, SEEK_SET);
    char * string = malloc(size);
    if (!string) {
        printf("malloc failed for input file: <%s>\n", file_path);
        return 3;
    }
    size = fread(string, 1, size, file);

    *include_info = (Include){
        .include = include,
        .path = stack->path,
        .size = size,
        .string = string
    };

cleanup:
    fclose(file);
    free(file_path);
    return 0;
}

// node is the control line for the #include
int CPP_include(Parser * parser, CPP * cpp, ASTNode * node) {

    // need to first check if it has already been included and if so, use it
    Include include;
    Token * inc_str = build_include(cpp, node);
    if (!cpp->includes.capacity) {
        HASH_MAP_INIT(pToken, Include)(&cpp->includes, next_prime(N_STDC_HDRS));
    }
    if (cpp->includes._class->get(&cpp->includes, inc_str, &include)) {
        int status = 0;        
        if ((status = load_include(&include, cpp->config, inc_str->string, ((CParser *)parser)->path))) {
            return status;
        }
        cpp->includes._class->set(&cpp->includes, inc_str, include);
    }

    // handle including tokens by re-tokenizing the string in the current context
    // TODO: here
    Token * inc_start;
    Token * inc_end;
    int status = parser->_class->tokenize(parser, include.string, include.size, &inc_start, &inc_end);
    if (!status) {
        Token_insert_before(node->token_start, inc_start, inc_end);    
    }
    return status;
}

struct CPPTrieNode {
    char const * chars;
    struct CPPTrieNode * children; // NULL terminated array
    _Bool success; // true only if a fail at the particular location is still a word
};

struct CPPTrieNode cpp_trie = {
    .chars = "deilpuw",
    .children = &(struct CPPTrieNode[]) {
        { // define
            .chars = "e",
            .children = &(struct CPPTrieNode[]) {
                {
                    .chars = "f",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "i",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "n",
                                    .children = &(struct CPPTrieNode[]) {
                                        {
                                            .chars = "e",
                                            .children = &(struct CPPTrieNode[]) {
                                                {0}
                                            }[0]
                                        }
                                    }[0]
                                }
                            }[0]
                        }
                    }[0]
                }
            }[0]
        },
        { // elifdef, elifndef, elif, else, embed, endif, error
            .chars = "lmnr",
            .children = &(struct CPPTrieNode[]) {
                { // elifdef, elifndef, elif, else
                    .chars = "is",
                    .children = &(struct CPPTrieNode[]) {
                        { // elifdef, elifndef, elif
                            .chars = "f",
                            .children = &(struct CPPTrieNode[]) {
                                { // elifdef, elifndef, elif
                                    .chars = "dn",
                                    .success = 1,
                                    .children = &(struct CPPTrieNode[]) {
                                        { // elifdef
                                            .chars = "e",
                                            .children = &(struct CPPTrieNode[]) {
                                                {
                                                    .chars = "f",
                                                    .children = &(struct CPPTrieNode[]) {
                                                        {0}
                                                    }[0]
                                                }
                                            }[0]
                                        },
                                        { // elifndef
                                            .chars = "d",
                                            .children = &(struct CPPTrieNode[]) {
                                                {
                                                    .chars = "e",
                                                    .children = &(struct CPPTrieNode[]) {
                                                        {
                                                            .chars = "f",
                                                            .children = &(struct CPPTrieNode[]) {
                                                                {0}
                                                            }[0]
                                                        }
                                                    }[0]
                                                }
                                            }[0]
                                        }
                                    }[0]
                                }
                            }[0]
                        },
                        { // else
                            .chars = "e",
                            .children = &(struct CPPTrieNode[]) {
                                {0}
                            }[0]
                        }
                    }[0]
                },
                { // embed
                    .chars = "b",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "e",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "d",
                                    .children = &(struct CPPTrieNode[]) {
                                        {0}
                                    }[0]
                                }
                            }[0]
                        }
                    }[0]
                },
                { // endif
                    .chars = "d",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "i",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "f",
                                    .children = &(struct CPPTrieNode[]) {
                                        {0}
                                    }[0]
                                }
                            }[0]
                        }
                    }[0]
                },
                { // error
                    .chars = "r",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "o",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "r",
                                    .children = &(struct CPPTrieNode[]) {
                                        {0}
                                    }[0]
                                }
                            }[0]
                        }
                    }[0]
                }
            }[0]
        },
        { // ifdef, ifndef, if, include
            .chars = "fn",
            .children = &(struct CPPTrieNode[]) {
                { // ifdef, ifndef, if
                    .chars = "dn",
                    .success = 1,
                    .children = &(struct CPPTrieNode[]) {
                        { // ifdef
                            .chars = "e",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "f",
                                    .children = &(struct CPPTrieNode[]) {
                                        {0}
                                    }[0]
                                }
                            }[0]
                        },
                        { // ifndef
                            .chars = "d",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "e",
                                    .children = &(struct CPPTrieNode[]) {
                                        {
                                            .chars = "f",
                                            .children = &(struct CPPTrieNode[]) {
                                                {0}
                                            }[0]
                                        }
                                    }[0]
                                },
                            }[0]
                        }
                    }[0]
                },
                { // include
                    .chars = "c",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "l",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "u",
                                    .children = &(struct CPPTrieNode[]) {
                                        {
                                            .chars = "d",
                                            .children = &(struct CPPTrieNode[]) {
                                                {
                                                    .chars = "e",
                                                    .children = &(struct CPPTrieNode[]) {
                                                        {0}
                                                    }[0]
                                                }
                                            }[0]
                                        }
                                    }[0]
                                }
                            }[0]
                        }
                    }[0]
                }
            }[0]
        },
        { // line
            .chars = "i",
            .children = &(struct CPPTrieNode[]) {
                {
                    .chars = "n",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "e",
                            .children = &(struct CPPTrieNode[]) {
                                {0}
                            }[0]
                        }
                    }[0]
                }
            }[0]
        },
        { // pragma
            .chars = "r",
            .children = &(struct CPPTrieNode[]) {
                {
                    .chars = "a",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "g",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "m",
                                    .children = &(struct CPPTrieNode[]) {
                                        {
                                            .chars = "a",
                                            .children = &(struct CPPTrieNode[]) {
                                                {0}
                                            }[0]
                                        }
                                    }[0]
                                }
                            }[0]
                        }
                    }[0]
                }
            }[0]
        },
        { // undef
            .chars = "n",
            .children = &(struct CPPTrieNode[]) {
                {
                    .chars = "d",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "e",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "f",
                                    .children = &(struct CPPTrieNode[]) {
                                        {0}
                                    }[0]
                                }
                            }[0]
                        }
                    }[0]
                }
            }[0]
        },
        { // warning
            .chars = "a",
            .children = &(struct CPPTrieNode[]) {
                {
                    .chars = "r",
                    .children = &(struct CPPTrieNode[]) {
                        {
                            .chars = "n",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "i",
                                    .children = &(struct CPPTrieNode[]) {
                                        {
                                            .chars = "n",
                                            .children = &(struct CPPTrieNode[]) {
                                                {
                                                    .chars = "g",
                                                    .children = &(struct CPPTrieNode[]) {
                                                        {0}
                                                    }[0]
                                                }
                                            }[0]
                                        }
                                    }[0]
                                }
                            }[0]
                        }
                    }[0]
                }
            }[0]
        }
    }[0]
};

_Bool in_cpp_trie(struct CPPTrieNode * node, char const * str, char const * end) {
    if (str == end || ' ' == *str || '\t' == *str) {
        return (!node->chars || node->success);
    }
    char * loc = node->chars ? strchr(node->chars, (int)*str) : NULL;
    if (!loc) {
        return false;
    }
    return in_cpp_trie(node->children + (loc - node->chars), str + 1, end);
}


_Bool is_cpp_line(ASTNode * node) {
    char const * str = node->token_start->string;
    if (*str != '#') {
        return 0;
    }
    str++;
    if (*str == '#') {
        return false;
    }
    char const * end = node->token_start->string + node->token_start->length;
    while (str != end && (' ' == *str || '\t' == *str)) {
        str++;
    }
    if (str == end) {
        return false;
    }
    return in_cpp_trie(&cpp_trie, str, end);
}

int CPP_directive(Parser * parser, CPP * cpp) {
    int status = 0;
    bool tokenizing_ref = parser->tokenizing;
    parser->tokenizing = false;

    ASTNode * pp_key = Rule_check(crules[C_PP_CHECK], parser);
    if (!pp_key) {
        return 1;
    }
    ASTNode * key = pp_key->children[1]->children[0];

    Parser_seek(parser, pp_key->token_start);
    switch (key->rule) {
        case C_DEFINE_KW: {
            // parse
            ASTNode * define = Rule_check(crules[C_CONTROL_LINE], parser);

            // register define
            CPP_define(cpp, define);

            // remove directive line
            Token_remove_tokens(define->token_start, define->token_end);
            break;
        }
        case C_UNDEF_KW: {
            CPP_undef(cpp, Rule_check(crules[C_CONTROL_LINE], parser));
            break;
        }
        case C_IF_KW:
        case C_IFDEF_KW:
        case C_IFNDEF_KW: {
            break;
        }
        case C_ELIF_KW: {
            break;
        }
        case C_ELIFDEF_KW: {
            break;
        }
        case C_ELIFNDEF_KW: {
            break;
        }
        case C_ELSE_KW: {
            break;
        }
        case C_ENDIF_KW: {
            break;
        }
        case C_INCLUDE_KW: {
            ASTNode * include = Rule_check(crules[C_CONTROL_LINE], parser);

            status = CPP_include(parser, cpp, include);

            Token_remove_tokens(include->token_start, include->token_end);
            break;
        }
        case C_EMBED_KW: {
            break;
        }
        case C_LINE_KW: {
            break;
        }
        case C_ERROR_KW: {
            break;
        }
        case C_WARNING_KW: {
            break;
        }
        case C_PRAGMA_KW: {
            break;
        }
        case C__PRAGMA_KW: {
            break;
        }
        default: {
            return 1;
        }
    }

    parser->tokenizing = tokenizing_ref;

    return status;
}
