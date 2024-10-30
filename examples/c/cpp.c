#include "peg4c/hash_utils.h"

#include "c.h"
#include "cpp.h"
#include "cparser.h"
#include "cstring.h"
#include "config.h"
#include "os.h"

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
    _Bool disable_expand;
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

// Forward declarations
//Token * CPP_check_expand(CPP * cpp, Parser * parser, Token * next, Token * end);
int CPP_expand_next(CParser * cparser, Token ** cur, Token ** end);

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
        macro->rep_start->prev = NULL;
        macro->rep_end = rep_list->token_end;    
        macro->rep_end->next = NULL;
    }
    cpp->defines._class->set(&cpp->defines, macro->id, macro);
}

void CPP_undef(CPP * cpp, ASTNode * undef) {
    cpp->defines._class->remove(&cpp->defines, undef->children[2]->token_start);
}

// build null-terminated string for relative file
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
int CPP_include(Parser * parser, CPP * cpp, ASTNode * node, Token ** start, Token ** end) {

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

    return parser->_class->tokenize(parser, include.string, include.size, start, end);
}

struct CPPTrieNode {
    char const * chars;
    struct CPPTrieNode * children; // NULL terminated array
    int success; // non-zero on success
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
                                                {.success = C_DEFINE_KW}
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
                                    .success = C_ELIF_KW,
                                    .children = &(struct CPPTrieNode[]) {
                                        { // elifdef
                                            .chars = "e",
                                            .children = &(struct CPPTrieNode[]) {
                                                {
                                                    .chars = "f",
                                                    .children = &(struct CPPTrieNode[]) {
                                                        {.success = C_ELIFDEF_KW}
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
                                                                {.success = C_ELIFNDEF_KW}
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
                                {.success = C_ELSE_KW}
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
                                        {.success = C_EMBED_KW}
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
                                        {.success = C_ENDIF_KW}
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
                                        {.success = C_ERROR_KW}
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
                    .success = C_IF_KW,
                    .children = &(struct CPPTrieNode[]) {
                        { // ifdef
                            .chars = "e",
                            .children = &(struct CPPTrieNode[]) {
                                {
                                    .chars = "f",
                                    .children = &(struct CPPTrieNode[]) {
                                        {.success = C_IFDEF_KW}
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
                                                {.success = C_IFNDEF_KW}
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
                                                        {.success = C_INCLUDE_KW}
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
                                {.success = C_LINE_KW}
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
                                                {.success = C_PRAGMA_KW}
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
                                        {.success = C_UNDEF_KW}
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
                                                        {.success = C_WARNING_KW}
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

int cpp_type(struct CPPTrieNode * node, char const * str, char const * end) {
    char const * loc;
    while (str != end 
        && ' ' != *str 
        && '\t' != *str 
        && (loc = node->chars ? strchr(node->chars, (int)*str) : NULL)) {
        
        node = node->children + (loc - node->chars);
        str++;
    }
    return node->success;
}

int is_cpp_line(ASTNode * node) {
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
        return 0;
    }
    return cpp_type(&cpp_trie, str, end);
}

// returns pointer one after the end of line or end of string
char const * get_next_eol(char const * str, int * delta_line) {
    int dline = 0;
    while (*str) {
        if (*str == '\n') {
            if ('\\' == *(str - 1)) {
                str++;
                dline++;
            } else if ('\r' == *(str - 1)) {
                if ('\\' == *(str - 2)) {
                    str++;
                    dline++;
                } else {
                    str++;
                    break;
                }
            } else {
                str++;
                break;
            }
        } else {
            str++;
        }
    }
    *delta_line = dline;
    return str;
}

int CPP_directive(CParser * cparser, ASTNode * directive, int type) {
    CPP * cpp = cparser->cpp;
    int status = 0;
    size_t consumed = 0;

    char const * string = directive->token_start->string + 1;
    int dline;
    char const * next_line = get_next_eol(string, &dline);
    size_t length = (size_t) (next_line - string);
    consumed += length + 1;

    // add hashtag
    Token * hashtag = Parser_copy_token((Parser *)cparser, directive->token_start);
    hashtag->length = 1;
    hashtag->prev = NULL;
    hashtag->next = NULL;
    Token * start, * end;

    if (type == C_DEFINE_KW) {
        // disable expansions for tokenizing the #define statements
        _Bool disable_expand = cpp->disable_expand;
        cpp->disable_expand = true;

        ((Parser *)cparser)->_class->tokenize((Parser *)cparser, string, length - 1, &start, &end);

        cpp->disable_expand = disable_expand;
    } else {
        ((Parser *)cparser)->_class->tokenize((Parser *)cparser, string, length - 1, &start, &end);
    }

    Token_append(hashtag, start);

    // add newline
    Token * newline = Parser_copy_token((Parser *)cparser, hashtag);
    newline->length = 1;
    newline->string = "\n";
    newline->prev = NULL;
    newline->next = NULL;

    Token_append(end, newline);
    Token_append(newline, &(Token){.string = "", .id = SIZE_MAX}); // add a null terminator
    Token * cur = Parser_tell(cparser);
    Parser_seek(cparser, hashtag);

    // need to parse the directive, ensure tokenizing is disabled to allow cache to be used
    _Bool tokenizing = ((Parser *)cparser)->tokenizing;
    ((Parser *)cparser)->tokenizing = false;

    switch (type) {
        case C_DEFINE_KW: {
            // register define
            CPP_define(cpp, Rule_check(crules[C_CONTROL_LINE], (Parser *)cparser));
            break;
        }
        case C_UNDEF_KW: {
            CPP_undef(cpp, Rule_check(crules[C_CONTROL_LINE], (Parser *)cparser));
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
            Token * start;
            Token * end;
            int status = CPP_include((Parser *)cparser, cpp, Rule_check(crules[C_CONTROL_LINE], (Parser *)cparser), &start, &end);
            if (status) {
                fprintf(stderr, "failed to include file\n");
            }
            Token_insert_before(directive->token_start, start, end);
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

    ((Parser *)cparser)->tokenizing = tokenizing;
    Parser_seek(cparser, cur);

    directive->str_length = consumed;

    return 0;
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
    // TODO: doesn't handle va_arg yet
    if (start != end) {
        fprintf(stderr, "CPP_init_macro_arg_map: error in number of args and number of parameters for macro %.*s. # of parameters: %c\n", 
            (int)macro->id->length, macro->id->string, nparams);
    }
}

// returns the stringified token inserted into the list
Token * CPP_stringify(CParser * cparser, Token * start, Token * end) {
    
    size_t length = 2 + start->length; // +2 for the 2 double quotes
    size_t ntokens = 1;
    Token * cur = start;
    while (cur != end) {
        cur = cur->next;
        length += cur->length + 1; // + 1 for the space
        ntokens++;
    }

    char * string = MemPoolManager_malloc(cparser->cpp->cstr_mgr, length);
    Token * result = Parser_copy_token((Parser *)cparser, start);
    result->string = string;
    result->length = length;

    cur = start;
    *string = '"';
    string++;
    memcpy(string, cur->string, cur->length);
    string += cur->length;
    while (cur != end) {
        cur = cur->next;
        *string = ' ';
        string++;
        memcpy(string, cur->string, cur->length);
        string += cur->length;
    }
    *string = '"';
    string++;
    assert(length == string - result->string);

    return result;
}

// returns the concatenated token inserted into the list
Token * CPP_concatenate(CParser * cparser, Token * left, Token * right) {
    size_t length = right->length + left->length;

    char * string = MemPoolManager_malloc(cparser->cpp->cstr_mgr, length);
    Token * result = Parser_copy_token((Parser *)cparser, left);
    result->string = string;
    result->length = length;

    memcpy(string, left->string, left->length);
    memcpy(string + left->length, right->string, right->length);
    return result;
}

// start and end are the first and last tokens in the replacement list as input, as output, it is the 
// this is like CPP_preprocess but also copies tokens as it goes
void CPP_expand_macro_function(CParser * cparser, HASH_MAP(pToken, TokenPair) * argmap, Token ** start, Token ** end) {
    Token * term = *end;
    Token * cur = *start;
    // for output
    Token head = {0};
    Token * tail = &head;

    TokenPair pair;
    if (argmap->_class->get(argmap, cur, &pair)) { // the token is not a parameter
        Token * result = NULL;
        if (tail->string && '#' == tail->string[0]) {
            switch (tail->length) {
                case 1: {
                    result = CPP_stringify(cparser, cur, cur);
                    tail = tail->prev;
                    Token_remove_tokens(tail->next, tail->next);
                    break;
                }
                case 2: {
                    if ('#' == tail->string[1]) {
                        result = CPP_concatenate(cparser, tail->prev, cur);
                        tail = tail->prev->prev;
                        Token_remove_tokens(tail->next, tail->next->next);
                    }
                    break;
                }
            }
        }
        if (!result) { // simple copy
            result = Parser_copy_token((Parser *)cparser, cur);
        }

        // add resulting token
        Token_append(tail, result);
        tail = tail->next;
    } else {
        Token * result = NULL;
        if (tail->string && '#' == tail->string[0]) {
            switch (tail->length) {
                case 1: {
                    result = CPP_stringify(cparser, pair.start, pair.end);
                    pair.start = result;
                    pair.end = result;
                    tail = tail->prev;
                    Token_remove_tokens(tail->next, tail->next);
                    break;
                }
                case 2: {
                    if ('#' == tail->string[1]) {
                        result = CPP_concatenate(cparser, tail->prev, pair.start);
                        if (pair.start == pair.end) {
                            pair.start = result;
                            pair.end = result;
                        } else {
                            pair.start = pair.start->next;
                            Parser_copy_tokens((Parser *)cparser, &pair.start, &pair.end);
                            Token_append(result, pair.start);
                            pair.start = result;
                        }
                        tail = tail->prev->prev;
                        Token_remove_tokens(tail->next, tail->next->next);
                    }
                    break;
                }
            }
        }
        if (!result) {
            Parser_copy_tokens((Parser *)cparser, &pair.start, &pair.end);
        }
        Token_append(tail, pair.start);
        tail = pair.end;
    }
    if (cur != term) {
        do {
            cur = cur->next;
            if (argmap->_class->get(argmap, cur, &pair)) { // the token is not a parameter
                Token * result = NULL;
                if ('#' == tail->string[0]) {
                    switch (tail->length) {
                        case 1: {
                            result = CPP_stringify(cparser, cur, cur);
                            tail = tail->prev;
                            Token_remove_tokens(tail->next, tail->next);
                            break;
                        }
                        case 2: {
                            if ('#' == tail->string[1]) {
                                result = CPP_concatenate(cparser, tail->prev, cur);
                                tail = tail->prev->prev;
                                Token_remove_tokens(tail->next, tail->next->next);
                            }
                            break;
                        }
                    }
                }
                if (!result) { // simple copy
                    result = Parser_copy_token((Parser *)cparser, cur);
                }

                // add resulting token
                Token_append(tail, result);
                tail = tail->next;
            } else {
                Token * result = NULL;
                if ('#' == tail->string[0]) {
                    switch (tail->length) {
                        case 1: {
                            result = CPP_stringify(cparser, pair.start, pair.end);
                            pair.start = result;
                            pair.end = result;
                            tail = tail->prev;
                            Token_remove_tokens(tail->next, tail->next);
                            break;
                        }
                        case 2: {
                            if ('#' == tail->string[1]) {
                                result = CPP_concatenate(cparser, tail->prev, pair.start);
                                if (pair.start == pair.end) {
                                    pair.start = result;
                                    pair.end = result;
                                } else {
                                    pair.start = pair.start->next;
                                    Parser_copy_tokens((Parser *)cparser, &pair.start, &pair.end);
                                    Token_append(result, pair.start);
                                    pair.start = result;
                                }
                                tail = tail->prev->prev;
                                Token_remove_tokens(tail->next, tail->next->next);
                            }
                            break;
                        }
                    }
                }
                if (!result) {
                    Parser_copy_tokens((Parser *)cparser, &pair.start, &pair.end);
                }
                Token_append(tail, pair.start);
                tail = pair.end;
            }
        } while (cur != term);
    }
    *start = head.next;
    (*start)->prev = NULL;
    *end = tail;
}

// returns number of parameters, must be confirmed to parser a set of arguments prior call
int CPP_preprocess_args(CParser * cparser, Token ** start, Token ** end) {
    Token * term = *end;
    Token * cur = *start;
    int nparams = 0;
    int nopen = 1;

    assert('(' == cur->string[0] && 1 == cur->length);
    
    while (nopen && cur->next) {
        cur = cur->next;
        if (CPP_expand_next(cparser, &cur, &term)) {
            Token * final = term->next;
            while (cur != final) {
                if (1 == cur->length) {
                    switch (cur->string[0]) {
                        case '(': {
                            nopen++;
                            break;
                        }
                        case ')': {
                            nopen--;
                            break;
                        }
                        case ',': {
                            if (1 == nopen) {
                                nparams++;
                            }
                            break;
                        }
                    }
                }
                cur = cur->next;
            }
            cur = term;
            term = *end;
        } else if (1 == cur->length) {
            switch (cur->string[0]) {
                case '(': {
                    nopen++;
                    break;
                }
                case ')': {
                    nopen--;
                    break;
                }
                case ',': {
                    if (1 == nopen) {
                        nparams++;
                    }
                    break;
                }
            }
        }
    }

    if (cur != (*start)->next) {
        nparams++;
    }

    (*end) = cur;

    return nparams;
}

// returns 0 if no expansion occurred, else 1
int CPP_hashtag_expand(CParser * cparser, Token ** cur, Token ** end) {
    Token * hashtag = *cur;
    Token * result = NULL;
    Token * term = *end;
    if (hashtag->string && '#' == hashtag->string[0]) {
        switch (hashtag->length) {
            case 1: {
                Token * c = hashtag->next;
                if (!CPP_expand_next(cparser, &c, &term)) {
                    term = c;
                }
                result = CPP_stringify(cparser, c, term);
                break;
            }
            case 2: {
                if ('#' == hashtag->string[1]) {
                    Token * c = hashtag->next; // point to the Token after '##'
                    hashtag = hashtag->prev; // point to the Token before '##'
                    if (!CPP_expand_next(cparser, &c, &term)) {
                        term = c;
                    }
                    result = CPP_concatenate(cparser, hashtag, c); // only concatenate the token before and resulting one after '##'
                }
                break;
            }
        }
    }
    if (result) {
        Token_replace_tokens(hashtag, term, result, result);
        *cur = result;
        *end = result;
        return 1;
    }
    return 0;
}

int CPP_expand_macro(CParser * cparser, Token ** start, Token ** end) {
    Macro * macro;
    if (cparser->cpp->defines._class->get(&cparser->cpp->defines, *start, &macro)) {
        return 0;
    }

    if (MACRO == macro->type) {
        Token * s = macro->rep_start;
        Token * e = macro->rep_end;
        Parser_copy_tokens((Parser *)cparser, &s, &e);
        CPP_preprocess(cparser, &s, &e);
        Token_replace_tokens(*start, *start, s, e);
        *start = s;
        *end = e;
        return 1;
    }

    Token * left = (*start)->next;
    if (MACRO_FUNCTION == macro->type && left && 1 == left->length && '(' == left->string[0]) {
        Token * right = *end;
        int nparams = CPP_preprocess_args(cparser, &left, &right);

        if (nparams < 0) {
            fprintf(stderr, "CPP_preprocess_args returned -1\n");
            return 0; // This is really an error
        } else if (macro->va_arg) {
            if (nparams < macro->nparam) {
                fprintf(stderr, "insufficient arguments found for %.*s\n", (int)(*start)->length, (*start)->string);
                return 0; // This is really an error
            }
        } else if (nparams != macro->nparam) {
            fprintf(stderr, "incorrect number of arguments found for %.*s. found: %d, expected: %hu\n", (int)(*start)->length, (*start)->string, nparams, macro->nparam);
            return 0;
        }

        // macro seems redundant with rep_start and rep_end
        Token * s = macro->rep_start;
        Token * e = macro->rep_end;
        Parser_copy_tokens((Parser *)cparser, &s, &e);
        if (nparams > 0) {
            // create parameter_map
            HASH_MAP(pToken, TokenPair) arg_map;
            HASH_MAP_INIT(pToken, TokenPair)(&arg_map, next_prime(macro->nparam));

            CPP_init_macro_arg_map(&arg_map, macro, left, right);
            CPP_expand_macro_function(cparser, &arg_map, &s, &e);

            // cleanup
            arg_map._class->dest(&arg_map);
        } 
        // this preprocess step will technically double-check a bunch of tokens, but CPP_expand_macro_function is a bit of a mess already
        // TODO: clean this up
        CPP_preprocess(cparser, &s, &e);

        Token_replace_tokens(*start, right, s, e);
        *start = s;
        *end = e;

        return 1;
    }

    return 0;
}

// returns 0 if no expansion occurred else positive
int CPP_expand_next(CParser * cparser, Token ** cur, Token ** end) {
    if (CPP_hashtag_expand(cparser, cur, end) || CPP_expand_macro(cparser, cur, end)) {
        return 1;
    }
    return 0;
}

int CPP_preprocess(CParser * cparser, Token ** start, Token ** end) {
    if (!cparser->cpp->defines.capacity) {
        return 0;
    }
    Token * reset = Parser_tell(cparser);

    Token * term = *end;
    Token * cur = *start;
    Token * last = NULL;
    
    if (CPP_expand_next(cparser, &cur, &term)) {
        if (term->next) { // only reset term if it is in fact not the final token
            term = *end;
        }
        last = cur->prev;
        *start = cur;
    } else { // go to next token
        last = cur;
        *start = cur;
        cur = last->next;
    }
    while (cur) {
        if (CPP_expand_next(cparser, &cur, &term)) {
            if (term->next) { // only reset term if it is in fact not the final token
                term = *end;
            }
            last = cur->prev;
        } else { // go to next token
            last = cur;
            cur = last->next;
        }
    };

    (*end) = last;

    Parser_seek(cparser, reset);

    return 0;
}
