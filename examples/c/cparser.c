#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cparser.h"
#include "cstring.h"
#include "os.h"

#define BUFFER_SIZE 4096
#define BUFFER_SIZE_SCALE 2

#ifndef DEFAULT_LOG_LEVEL
	#define DEFAULT_LOG_LEVEL LOG_LEVEL_WARN
#endif

// add builtin types here so that the parsers work. The first two below are needed to parse the stand library with gcc and clang
CString BUILTIN_TYPES[] = {
    {.str = "__builtin_va_list", .len = 17},    // gcc and clang
    {.str = "_Float32", .len = 8},              // gcc
    {.str = "_Float32x", .len = 9},             // gcc
    {.str = "_Float64", .len = 8},              // gcc
    {.str = "_Float64x", .len = 9},             // gcc
    {.str = "_Float128", .len = 9},             // gcc
    {.str = NULL, .len = 0}                     // sentinel that must be last
};

#define KEY_TYPE CString
#define VALUE_TYPE pASTNode
#define KEY_COMP CString_comp
#define HASH_FUNC CString_hash
#include "peg4c/hash_map.h"

/* Scope definitions and handlers */
struct Scope {
    Scope * parent;
    HASH_MAP(CString, pASTNode) typedefs; 
};

// Forward Declarations
int CParser_parse(Parser * self, char const * string, size_t string_length);


Scope * Scope_new(Scope * parent) {
    Scope * new = malloc(sizeof(*new));
    if (!new) {
        return NULL;
    }
    new->parent = parent;
    HASH_MAP_INIT(CString, pASTNode)(&new->typedefs, 3);
    return new;
}
void Scope_add_typedef(Scope * self, CString typedef_, ASTNode * node) {
    self->typedefs._class->set(&self->typedefs, typedef_, node);
}
bool Scope_is_typedef(Scope * self, CString id) {
    if (!self) {
        return false;
    }
    ASTNode * node;
    if (!self->typedefs._class->get(&self->typedefs, id, &node)) {
        return true;
    }
    return Scope_is_typedef(self->parent, id);
}
Scope * Scope_dest(Scope * self) {
    if (!self) {
        return NULL;
    }
    Scope * parent = self->parent;
    self->typedefs._class->dest(&self->typedefs);
    free(self);
    return parent;
}

CParserConfig DEFAULT_CPARSERCONFIG = {
    .c = 'c'
};

struct ParserType CParser_class = {.parse = CParser_parse, .tokenize = Parser_tokenize};

void CParser_init(CParser * self, char const * path, char const * file, CParserConfig * config, CPPConfig * cpp_config) {
    if (!config) {
        config = &DEFAULT_CPARSERCONFIG;
    }
    self->scope = Scope_new(NULL); // a file-level scope
    Parser_init((Parser *)self, crules, C_NRULES, C_TOKEN, C_C, 0);
    ((Parser *)self)->_class = &CParser_class;

    // add built-in types to file scope
    ASTNode * builtin_type_node = Parser_add_node((Parser *)self, C_TOKEN, ((Parser *)self)->token_head, NULL, 1, 0, 0);
    size_t i = 0;
    while (BUILTIN_TYPES[i].len) {
        Scope_add_typedef(self->scope, BUILTIN_TYPES[i], builtin_type_node);
        i++;
    }

    self->path = path;
    self->file = file;
    self->mgr = MemPoolManager_new(4096, 1, 1);
    self->cpp = CPP_new(self->mgr, cpp_config);
}
void CParser_dest(CParser * self) {
    Parser_dest(&self->parser);
    while (self->scope) {
        self->scope = Scope_dest(self->scope);
    }
    CPP_del(self->cpp);
    if (self->mgr) {
        MemPoolManager_del(self->mgr);
    }
}

int CParser_parse(Parser * self, char const * string, size_t string_length) {
    CParser * cself = (CParser *)self;

    // storage for start and end tokens
    Token * start;
    Token * end;

    // tokenize the input string
    int error = self->_class->tokenize(self, string, string_length, 
        &start, &end);

    if (!error) {
        self->tokenizing = 0; // ensure tokenizing is off
        error = CPP_preprocess(((CParser *)self), &start, &end);
    } else {
        return error;
    }

    // if tokenizer succeeded (at least one token found), no error
    if (!error) {

        // reset fail node
        Parser_fail_node(self)->token_start = self->token_head;

        // insert Token * linked list into current position of Parser
        end->next = self->token_cur->next;
        end->next->prev =  end;
        self->token_cur->next = start;
        start->prev = self->token_cur;
        self->token_cur = start;

        // this is an ugly hack to ensure cache checking on the tail node works,
        // but it will only work so long as adding tokens to the stream only 
        // happens during
        self->token_tail->id = (self->token_tail->prev->id > end->id ? self->token_tail->prev->id : end->id) + 1;
        if (self->root_rule) {
            // initiae parse of the Token list
            self->ast = Rule_check(self->root_rule, self);
            if (!self->ast || Parser_is_fail_node(self, self->ast)) {
                return 1;
            }
        }
    } else {
        return error;
    }
    return error;
}

ASTNode * nc1_pass0(Production * rule, Parser * parser, ASTNode * node) {
    if (node->nchildren == 1) { // there is no binary operator. reduce to left-hand operand type
        return node->children[0];
    }
    return build_action_default(rule, parser, node);
}

ASTNode * c0nc0_pass1(Production * rule, Parser * parser, ASTNode * node) {
    if (!node->children[0]->nchildren) {
        return node->children[1];
    }
    return build_action_default(rule, parser, node);
}

// identical to nc1_pass0 but want a little more clarity
ASTNode * simplify_binary_op(Production * binary_op, Parser * parser, ASTNode * node) {
    if (node->nchildren == 1) { // there is no binary operator. reduce to left-hand operand type
        return node->children[0];
    }
    return build_action_default(binary_op, parser, node);
}

// open and close scopes in compound statement
ASTNode * _open_scope(Production * rule, Parser * parser, ASTNode * node) {
    CParser * self = (CParser *)parser;
    self->scope = Scope_new(self->scope);
    return node; // return node because it's just a punctuator that is unused
}
ASTNode * _close_scope(Production * rule, Parser * parser, ASTNode * node) {
    CParser * self = (CParser *)parser;
    self->scope = Scope_dest(self->scope);
    return node; // return node because it's just a punctuator that is unused
}

// extracts the identifier from the declarator
CString get_declarator_identifier(ASTNode * declrtr) {
    ASTNode * possible_id = declrtr->children[1]->children[0]->children[0];
    if (possible_id->rule == C_IDENTIFIER) {
        return (CString) {.str = possible_id->token_start->string, .len = possible_id->str_length};
    }
    return get_declarator_identifier(declrtr->children[1]->children[0]->children[1]);
}

// checks and eliminates ambiguities from typedef_name
ASTNode * c_process_declaration_specifiers(Production * decl_specs, Parser * parser, ASTNode * node) {
    _Bool has_type_spec = false;

    assert((node->children[0]->rule == C_DECLARATION_SPECIFIER) || !printf("%.*s is not a declaration specifier\n", (int)node->children[0]->token_start->length, node->children[0]->token_start->string));
    size_t nchildren = 0;
    for (size_t i = 0; i < node->nchildren; i++) {
        ASTNode * decl_spec = node->children[i];
        if (decl_spec->children[0]->rule == C_TYPE_SPECIFIER) {
            ASTNode * type_spec = decl_spec->children[0];
            if (type_spec->children[0]->rule == C_TYPEDEF_NAME && has_type_spec) {
                // strip declaration specifiers at this point
                ASTNode * decl_specs_end = node->children[i - 1];
                node->nchildren = nchildren;
                node->token_end = decl_specs_end->token_end;
                node->str_length = (size_t)(decl_specs_end->token_end->string - node->token_start->string) + decl_specs_end->token_end->length;
                return build_action_default(decl_specs, parser, node);            
            }
            has_type_spec = true;
        }
        nchildren++;
    }
    return build_action_default(decl_specs, parser, node);
}

// takes a declaration and if it detects a typedef, registers it in the current scope
ASTNode * c_process_declaration(Production * decl, Parser * parser, ASTNode * node) {
    if (node->children[0]->rule == C_STATIC_ASSERT_DECLARATION || node->children[0]->children[0]->rule == C_ATTRIBUTE_SPECIFIER) {
        return build_action_default(decl, parser, node);
    }
    ASTNode * decl_specs = node->children[0]->children[0];
    ASTNode * init_declarators = node->children[0]->children[1]->nchildren ? node->children[0]->children[1]->children[0] : NULL;
    // check declaration specifiers for typedef_kw
    if (decl_specs->rule != C_DECLARATION_SPECIFIERS) {
        decl_specs = node->children[0]->children[1];
        init_declarators = node->children[0]->children[2];
    }
    assert((decl_specs->rule == C_DECLARATION_SPECIFIERS) || !printf("c_process_declaration failed to find the declaration_specifiers: %s\n", crules[decl_specs->rule]->name));
    assert((init_declarators == NULL) || (init_declarators->children[0]->rule == C_INIT_DECLARATOR));
    for (size_t i = 0; i < decl_specs->nchildren; i++) {
        ASTNode * child = decl_specs->children[i];
        ASTNode * gchild = child->children[0];
        if (gchild->rule == C_STORAGE_CLASS_SPECIFIER && gchild->children[0]->rule == C_TYPEDEF_KW) {
            for (size_t i = 0; i < init_declarators->nchildren; i += 2) {
                ASTNode * init_declarator = init_declarators->children[i];
                CString new_typedef = get_declarator_identifier(init_declarator->children[0]);
                Scope_add_typedef(((CParser *)parser)->scope, new_typedef, node);
            }
            return build_action_default(decl, parser, node);
        }
    }
    return build_action_default(decl, parser, node);
}

// takes a typedef_name type_specifier and if the identifier is registered as a typedef in any of the open scopes, returns the node as-is else fails
ASTNode * c_check_typedef(Production * decl_specs, Parser * parser, ASTNode * node) {
    ASTNode * identifier = node->children[0];
    if (Scope_is_typedef(((CParser *)parser)->scope, (CString) {.str = identifier->token_start->string, .len = identifier->str_length})) {
        return build_action_default(decl_specs, parser, node); // ignore the negative lookahead from now on
    }
    return Parser_fail_node(parser);
}

// this is a hack to allow negative lookbehind in preprocessing
ASTNode * c_pp_lparen(Production * prod, Parser * parser, ASTNode * node) {
    Token * tok = node->token_start;
    // technically this could be UB if tok->string - 1 is not within the object, 
    // but the use of lparen should guarantee that the production can never 
    // succeed at the beginning of a string object
    if (is_whitespace(*(tok->string - 1))) {
        // if preceded by a whitespace, fail
        return Parser_fail_node(parser);
    }
    return build_action_default(prod, parser, node);
}

ASTNode * c_pp_line_expand(Production * prod, Parser * parser, ASTNode * node) {
    int type = is_cpp_line(node);
    if (!type) {
        return Parser_fail_node(parser);
    }
    /*
     * CPP_directive handles the directive including skipping string
     * stores the amount of string being tokenized in node->str_length
     * inserts any necessary tokens in place before node->token_start
     */
    if (CPP_directive((CParser *)parser, node, type)) {
        fprintf(stderr, "CPP_directive failure\n");
        exit(1);
    }
    return make_skip_node(node);
}
/*
ASTNode * c_pp_identifier(Production * prod, Parser * parser, ASTNode * node) {
    // if CPP_check_expand returns true, the token was replaced and expansion
    // must be put BEFORE node->token_start.
    // what this build action must do then depends on whether parser was
    // tokenizing or not at the time it was expanded
    if (CPP_check_expand(parser, ((CParser *)parser)->cpp, node)) {
        if (parser->tokenizing) {
            / *
             * Skip over the token. It was replaced, but as long as 
             * CPP_check_expand put the expansion before and sets
             * node->str_length to the amount of consumed string, accounting 
             * will be accurate
             * /
            return make_skip_node(node);
        } else {
            /// * 
             * the token no long exists, but cannot return a fail node as we do
             * not know if it should have failed or not for the parse. So 
             * return a re-check of the rule. By calling Rule_check itself, the
             * PackratCache will remain consistent as the cache entry for this 
             * call will point to a token that no longer exists and cannot be 
             * recalled but the child call below will still point to the 
             * correct node *IF*: CPP_check_expand in this case must set the 
             * current token to the first one in the expansion and remove the
             * expanded tokens from the stream
             * /
            return Rule_check((Rule *)prod, parser);
        }
    }
    // if the expansion failed, build the resulting node as normal
    return build_action_default(prod, parser, node);
}
*/

/*
ASTNode * c_pp_is_defined(Production * prod, Parser * parser, ASTNode * node) {
    return make_skip_node(node);
}
*/

char * process_config_file(char const * config_file, CParserConfig * cparser_config, CPPConfig * cpp_config) {
    FILE * cfg = fopen(config_file, "rb");
    if (!cfg) {
        return NULL;
    }
    char * string = NULL;
    size_t nbytes = 0;
    if (fseek(cfg, 0, SEEK_END)) {
        fprintf(stderr, "failed to seek %s file <%s>\n", "config", config_file);
        goto exit;
    }
    long file_size = 0;
    if ((file_size = ftell(cfg)) < 0) {
        fprintf(stderr, "failed to get file size for %s file <%s>\n", "config", config_file);
        goto exit;
    }
    fseek(cfg, 0, SEEK_SET);
    string = malloc((file_size + 1) * sizeof(*string));
    if (!string) {
        fprintf(stderr, "malloc failed for %s file: <%s>\n", "config", config_file);
        goto exit;
    }
    nbytes = fread(string, 1, file_size, cfg);
    string[nbytes] = '\0';
    
    char * const end = string + nbytes;
    char * line_end = NULL;
    while (string != end && is_whitespace(*string)) {
        string++;
    }
    while (string != end && (line_end = strchr(string, '\n'))) {
        size_t length = (size_t)(line_end - string) + 1;

        if ('#' != *string) {
            if (!strncmp("include", string, 7)) {
                *line_end = '\0';
                CPPConfig_add_include(cpp_config, string, length);
                length++;
                goto next;
            }
        }
next:
        string += length;
        while (string != end && is_whitespace(*string)) {
            string++;
        }
    }
exit:
    fclose(cfg);
    return string;
}

int main(int narg, char ** args) {
    char * string = NULL;
    size_t nbytes = 0;
    CPPConfig * cpp_config = &(CPPConfig) {0};
    CParserConfig * cparser_config = &(CParserConfig) {0};
    char * config_string = NULL;
    // must be a 2-part string with the first representing the path (null-terminated) followed by the file (null-terminated)
    // represents the stdin input
    char default_filepath[] = {'.', '\0', '\0'};
    char * path;
    char * file;
    if (narg >= 2) {
        //printf("arg %d: %s\n", i, args[i]);
        FILE * input_file = fopen(args[1], "rb");
        if (fseek(input_file, 0, SEEK_END)) {
            printf("failed to seek input file for input file <%s>\n", args[1]);
            return 1;
        }
        long file_size = 0;
        if ((file_size = ftell(input_file)) < 0) {
            printf("failed to get file size for input file <%s>\n", args[1]);
            return 2;
        }
        fseek(input_file, 0, SEEK_SET);
        string = malloc(file_size * sizeof(*string));
        if (!string) {
            printf("malloc failed for input file: <%s>\n", args[1]);
            return 3;
        }
        nbytes = fread(string, 1, file_size, input_file);
        fclose(input_file);
        if (narg >= 3) { // check for args
            if (!strncmp("--config=", args[2], 9)) {
                config_string = process_config_file(args[2] + 9, cparser_config, cpp_config);
            }
        }
        
        path = NULL;
        file = args[1];
        while ((path = strchr(file, PATH_SEP))) {
            file = path + 1;
        }
        path = get_path(args[1]); // if path non-null, needs to be freed
    } else {
        path = &default_filepath[0];
        file = strlen(default_filepath) + &default_filepath[0] + 1;
        int c = getc(stdin);
        if (c != EOF) {
            ungetc(c, stdin);
            size_t nbuffered = 0;
            while ((c = getc(stdin)) != EOF) {
                ungetc(c, stdin);
                char * new_string = realloc(string, nbuffered + BUFFER_SIZE);
                if (!new_string) {
                    free(string);
                    return 4;
                }
                string = new_string;
                nbuffered += BUFFER_SIZE;
                nbytes += fread(string + nbytes, sizeof(*string), nbuffered - nbytes, stdin);
            }
            printf("string from stdin (length %zu): %.*s\n", nbytes, (int)nbytes, string);
        } else {
            printf("parse string not found found in file or stdin\n");
            return 5;
        }
    }
    if (string) {
        CParser parser;
        CParser_init(&parser, path, file, cparser_config, cpp_config);
        CParser_parse((Parser *)&parser, string, nbytes);
        Parser_print_tokens((Parser *)&parser, stdout);
        if (!parser.parser.ast || Parser_is_fail_node((Parser *)&parser, parser.parser.ast) || parser.parser.token_cur->length) {
            err_type status = Parser_print_parse_status((Parser *)&parser, stdout);
        }
        Parser_print_ast((Parser *)&parser, stdout);
        CParser_dest(&parser);
        free(string);
    }
    if (narg >= 2) {
        if (path) {
            free(path);
        }
    }
    return 0;
}

