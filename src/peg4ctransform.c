#include <ctype.h>

#include "peg4c/utils.h"
#include "peg4c.h"
#include "peg4ctransform.h"
#include "peg4cbuild.h"

P4CString get_rule_pointer(P4CParser * parser, P4CString name) {
    P4CProduction prod = {0};
    
    parser->productions._class->get(&parser->productions, name, &prod);

    P4CString arg = {.len = 9 + prod.identifier.len};
    arg.str = MemPoolManager_malloc(parser->str_mgr, (arg.len + 1));
    memcpy((void *)(arg.str), "(Rule *)", 8);
    arg.str[8] = '&';
    memcpy((void*)(arg.str + 9), (void*)prod.identifier.str, prod.identifier.len);

    return arg;
}

void handle_terminal(P4CParser * parser, ASTNode * node, const P4CString parent_id) {
    if (node->rule == PEG4C_STRING_LITERAL) {
        handle_string_literal(parser, node->children[0], parent_id);
    } else {
        handle_regex_literal(parser, node->children[0], parent_id);
    }
}


void handle_lookahead_rule(P4CParser * parser, ASTNode * node, P4CString name) {    
    P4CProduction prod;
    production_init(parser, name, &prod);

    ASTNode * lookahead_type = node->children[0]->children[0];
    switch (lookahead_type->rule) {
        case PEG4C_AMPERSAND: { // positive lookahead
            prod.type = PEG_POSITIVELOOKAHEAD;
            size_t buf_len = (parser->export.len + strlen("pos") + size_t_strlen(parser->productions.fill) + 3);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
            prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_pos_%zu", (int)parser->export.len, parser->export.str, parser->productions.fill);
            // add arguments
            break;
        }
        case PEG4C_EXCLAIM: { // negative lookahead
            prod.type = PEG_NEGATIVELOOKAHEAD;
            size_t buf_len = (parser->export.len + strlen("neg") + size_t_strlen(parser->productions.fill) + 3);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
            prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_neg_%zu", (int)parser->export.len, parser->export.str, parser->productions.fill);
            // add arguments
            break;
        }
    }

    P4CProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    ASTNode * child1 = node->children[1];

    P4CString look_name = get_string_from_parser(parser, child1);
    handle_base_rule(parser, child1, NULL_STRING, look_name);
    prod.args._class->push(&prod.args, get_rule_pointer(parser, look_name));

    parser->productions._class->set(&parser->productions, prod.name, prod);
} 

void handle_list_rule(P4CParser * parser, ASTNode * node, P4CString name) {
    P4CProduction prod;
    production_init(parser, name, &prod);
    size_t nchildren = (node->nchildren + 1) / 2;
    size_t buf_len = (parser->export.len + strlen("list") + size_t_strlen((size_t)nchildren) + size_t_strlen(parser->productions.fill) + 4);
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
    prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_list_%zu_%zu", (int)parser->export.len, parser->export.str, nchildren, parser->productions.fill);
    
    prod.type = PEG_LIST;

    P4CProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    for (size_t i = 0; i < node->nchildren; i += 2) {
        P4CString name = get_string_from_parser(parser, node->children[i]);
        handle_simplified_rules(parser, node->children[i], NULL_STRING, name);
        prod.args._class->push(&prod.args, get_rule_pointer(parser, name));
    }
    
    parser->productions._class->set(&parser->productions, prod.name, prod);
}

// This is a pretty garbage function that needs to get re-written
void handle_repeated_rule(P4CParser * parser, ASTNode * node, P4CString name) {

    P4CProduction prod;  
    production_init(parser, name, &prod);
    prod.type = PEG_REPEAT;

    ASTNode * repeat_type = node->children[1]->children[0];

    P4CString m = {0}, n = {0};
    
    switch (repeat_type->rule) {
        case PEG4C_PLUS: { // repeat 1 or more
            m.str = MemPoolManager_malloc(parser->str_mgr, 1);
            m.str[0] = '1';
            m.len = 1;

            size_t buf_len = (parser->export.len + size_t_strlen(parser->productions.fill) + 10);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
            prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_rep_1_0_%zu", (int)parser->export.len, parser->export.str, parser->productions.fill);
            
            // add arguments
            break;
        }
        case PEG4C_ASTERISK: { // repeat 0 or more
            size_t buf_len = (parser->export.len + size_t_strlen(parser->productions.fill) + 10);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
            prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_rep_0_0_%zu", (int)parser->export.len, parser->export.str, parser->productions.fill);

            // add arguments
            break;
        }
        case PEG4C_QUESTION: { // repeat 0 or 1
            m.str = MemPoolManager_malloc(parser->str_mgr, 1);
            m.str[0] = '0';
            m.len = 1;
            n.str = MemPoolManager_malloc(parser->str_mgr, 1);
            n.str[0] = '1';
            n.len = 1;

            size_t buf_len = (parser->export.len + size_t_strlen(parser->productions.fill) + 10);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
            prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_rep_0_1_%zu", (int)parser->export.len, parser->export.str, parser->productions.fill);
            break;
        }
        default: { // repeat m to n // TODO: assess whether m and n are really necessary since get_string_from_parser now makes a new copy

            // TODO: BUG: This assumes children[3] and children[1] are populated...they might not be
            ASTNode ** children = repeat_type->children;

            P4CString mstr = {0};
            if (node->children[1]->nchildren) {
                mstr = get_string_from_parser(parser, children[1]);
            }
            P4CString nstr = {0};
            if (node->children[3]->nchildren) {
                nstr = get_string_from_parser(parser, children[3]);
            }
            if (mstr.len) {
                m.str = MemPoolManager_malloc(parser->str_mgr, mstr.len);
                memcpy((void*)m.str, (void*)mstr.str, mstr.len);
                m.len = mstr.len;   
            } else {
                m.str = MemPoolManager_malloc(parser->str_mgr, 1);
                m.str[0] = '0';
                m.len = 1;
            }

            if (nstr.len) {
                n.str = MemPoolManager_malloc(parser->str_mgr, nstr.len);
                memcpy((void*)n.str, (void*)nstr.str, nstr.len);
                n.len = nstr.len;
            } else {
                if (!children[2]->nchildren) { // comma is omitted. n should be a copy of m
                    n.str = MemPoolManager_malloc(parser->str_mgr, m.len);
                    n.len = m.len;
                    memcpy(n.str, m.str, n.len);
                } else {
                    n.str = MemPoolManager_malloc(parser->str_mgr, 1);
                    n.str[0] = '0';
                    n.len = 1;
                }
            }

            size_t buf_len = (parser->export.len + m.len + n.len + size_t_strlen(parser->productions.fill) + 8);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
            prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_rep_%.*s_%.*s_%zu", (int)parser->export.len, parser->export.str, (int)m.len, m.str, (int)n.len, n.str, parser->productions.fill);
            // add arguments
        }
    }

    P4CProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    P4CString rep_name = get_string_from_parser(parser, node->children[0]);
    handle_simplified_rules(parser, node->children[0], NULL_STRING, rep_name);
    prod.args._class->push(&prod.args, get_rule_pointer(parser, rep_name));
    if (m.len) {
        prod.args._class->push(&prod.args, m);
        if (n.len) {
            prod.args._class->push(&prod.args, n);
        }
    }   

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_sequence(P4CParser * parser, ASTNode * node, P4CString name) {
    P4CProduction prod;
    production_init(parser, name, &prod);
    size_t nchildren = (node->nchildren + 1) / 2;
    size_t buf_len = (parser->export.len + strlen("seq") + size_t_strlen((size_t)nchildren) + size_t_strlen(parser->productions.fill) + 4);
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
    prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_seq_%zu_%zu", (int)parser->export.len, parser->export.str, nchildren, parser->productions.fill);
    
    prod.type = PEG_SEQUENCE;

    P4CProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);


    for (size_t i = 0; i < node->nchildren; i += 2) {
        P4CString name = get_string_from_parser(parser, node->children[i]);
        handle_simplified_rules(parser, node->children[i], NULL_STRING, name);
        prod.args._class->push(&prod.args, get_rule_pointer(parser, name));
    }

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_choice(P4CParser * parser, ASTNode * node, P4CString name) {
    P4CProduction prod;
    production_init(parser, name, &prod);
    size_t nchildren = (node->nchildren + 1) / 2;
    size_t buf_len = (parser->export.len + strlen("choice") + size_t_strlen((size_t)nchildren) + size_t_strlen(parser->productions.fill) + 4);
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len);
    prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_choice_%zu_%zu", (int)parser->export.len, parser->export.str, nchildren, parser->productions.fill);
    
    prod.type = PEG_CHOICE;

    P4CProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);


    for (size_t i = 0; i < node->nchildren; i += 2) {
        P4CString name = get_string_from_parser(parser, node->children[i]);
        handle_simplified_rules(parser, node->children[i], NULL_STRING, name);
        prod.args._class->push(&prod.args, get_rule_pointer(parser, name));
    }

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_base_rule(P4CParser * parser, ASTNode * node, const P4CString parent_id, P4CString name) {
    switch (node->children[0]->rule) {
        case PEG4C_TERMINAL: {
            // terminals are only built if parent_id is not empty
            if (parent_id.len) {
                handle_terminal(parser, node->children[0], parent_id);
            }
            break;
        }
        case PEG4C_NONTERMINAL: { // do nothing. Don't make a new production
            /* forward declare the nonterminal */
            P4CString name = get_string_from_parser(parser, node->children[0]);
            
            if (!parser->productions._class->in(&parser->productions, name)) {
                if (strncmp(name.str, "punctuator", name.len) && strncmp(name.str, "keyword", name.len)) {
                    P4CProduction_build(parser, node->children[0], PEG_PRODUCTION);
                } else {
                    P4CProduction_build(parser, node->children[0], PEG_LITERAL);
                }
            }
            
            break;
        }
        default: {// make a new production based on the choice expression
            handle_simplified_rules(parser, node->children[1], NULL_STRING, name);
        }
    }
}
// HERE I AM 8/8/2024
void handle_simplified_rules(P4CParser * parser, ASTNode * node, const P4CString parent_id, P4CString name) {
    
    // This check is very critical
    // If the name already exists (which is any rule other than production definitions themselves), 
    // this will leak memory, cause double-frees and all sorts of havoc that is hard to troubleshoot
    P4CProduction prod; // may not be used
    if (!parser->productions._class->get(&parser->productions, name, &prod)) {
        return;
    }

    switch (node->rule) {
        case PEG4C_CHOICE: {
            handle_choice(parser, node, name);
            break;
        }
        case PEG4C_SEQUENCE: {
            handle_sequence(parser, node, name);
            break;
        }
        case PEG4C_REPEATED_RULE: {
            handle_repeated_rule(parser, node, name);
            break;
        }
        case PEG4C_LIST_RULE: {
            handle_list_rule(parser, node, name);
            break;
        }
        case PEG4C_LOOKAHEAD_RULE: {
            handle_lookahead_rule(parser, node, name);
            break;
        }
        case PEG4C_BASE_RULE: { // BASE_RULE
            handle_base_rule(parser, node, parent_id, name);
            break;
        }
    }
    
}

#define HANDLE_PRODUCTION_NONE 0
#define HANDLE_PRODUCTION_TOKEN 1
void handle_production_(P4CParser * parser, ASTNode * id, ASTNode * transforms, ASTNode * rule_def, unsigned int flags) {
    P4CProduction prod;
    P4CString prod_name = get_string_from_parser(parser, id);
    if (parser->productions._class->get(&parser->productions, prod_name, &prod)) {
        // if retrieval fails, build a new one
        prod = P4CProduction_build(parser, id, PEG_PRODUCTION);
    }
    P4CProduction_declare(parser, prod);
    
    P4CString name = get_string_from_parser(parser, rule_def);
    handle_simplified_rules(parser, rule_def, prod.identifier, get_string_from_parser(parser, rule_def));
    prod.args._class->push(&prod.args, get_rule_pointer(parser, name));

    if (flags & HANDLE_PRODUCTION_TOKEN) {
        char const * action = "token_action";
        size_t action_length = strlen(action);
        char * buffer = MemPoolManager_malloc(parser->str_mgr, (action_length + 1));
        memcpy((void *)buffer, action, action_length);
        prod.args._class->push(&prod.args, (P4CString){.str = buffer, .len = action_length});
    } else {
        if (transforms) {
            for (size_t i = 0; i < transforms->nchildren; i += 2) {
                P4CString transform_name = get_string_from_parser(parser, transforms->children[i]);
                
                char * buffer = MemPoolManager_malloc(parser->str_mgr, (transform_name.len + 1));
                memcpy((void *)buffer, transform_name.str, transform_name.len);
                prod.args._class->push(&prod.args, (P4CString){.str = buffer, .len = transform_name.len});
            }
        }
    }

    // since productions are not allocated objects on the hash map, have to push to map again
    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_production(P4CParser * parser, ASTNode * node) {
    handle_production_(parser, node->children[0], node->children[1]->nchildren ? node->children[1]->children[0]->children[1] : NULL, node->children[3], HANDLE_PRODUCTION_NONE);
}

#define REGEX_LIB_STRING_LEFT "\""
#define REGEX_LIB_OFFSET_LEFT 1
#define REGEX_LIB_STRING_RIGHT "\""
#define REGEX_LIB_OFFSET_RIGHT 1

// returns one after last written character or dest if error
P4CString format_regex(P4CParser * parser, char const * src, size_t src_length) {
    static char const * const escaped_chars = ".^$*+?()[{\\|";

    size_t buffer_size = 3 * (src_length + 1 + REGEX_LIB_OFFSET_LEFT + REGEX_LIB_OFFSET_RIGHT);
    char * buffer = MemPoolManager_malloc(parser->str_mgr, buffer_size);
    size_t len = 0;

    buffer[len++] = '"';

    for (size_t i = 0; i < src_length; i++) {
        if (strchr(escaped_chars, *src)) {
            buffer[len++] = '\\';
            buffer[len++] = '\\';
        }
        buffer[len++] = *src;
        src++;
    }

    buffer[len++] = '"';
    buffer[len] = '\0';

    return (P4CString){.str = buffer, .len = len};
}

void handle_string_literal(P4CParser * parser, ASTNode * node, const P4CString parent_id) {
    P4CProduction prod;
    STACK_INIT(P4CString)(&prod.args, 0);
    
    prod.name = get_string_from_parser(parser, node);
    
    if (!strncmp("punctuator", parent_id.str, parent_id.len)) { // this is punctuation
        char * lookup_result = punctuator_lookup(prod.name.str + 1, prod.name.len - 2);
        if (lookup_result) {
            prod.identifier.len = strlen(lookup_result) + parser->export.len + 1;
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, (prod.identifier.len) + 1);
			snprintf(prod.identifier.str, prod.identifier.len + 1, "%.*s_%s",
				(int)parser->export.len, parser->export.str, lookup_result);
        } else {
            prod.identifier.len = (prod.name.len - 2)*4 + 6 + parser->export.len;
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, (prod.identifier.len + 1));
			snprintf(prod.identifier.str, prod.identifier.len + 1, "%.*s_punc",
				(int)parser->export.len, parser->export.str);
            size_t j = 5 + parser->export.len; // target index in prod.identifier.str
            for (size_t i = 1; i < prod.name.len - 1; i++) {
                prod.identifier.str[j++] = '_';
                if (isalnum((unsigned char)prod.name.str[i]) || prod.name.str[i] == '_') {
                    prod.identifier.str[j++] = prod.name.str[i];
                } else {
                    sprintf(prod.identifier.str + j, "%.3d", prod.name.str[i]);
                    j += 3;
                }
            }
            prod.identifier.len = j;
        }
        
    } else if (!strncmp("keyword", parent_id.str, parent_id.len)) {
        prod.identifier.len = (prod.name.len - 2) + 4 + parser->export.len;
        prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, (prod.identifier.len) + 1);
		snprintf(prod.identifier.str, prod.identifier.len + 1, "%.*s_%.*s_kw", 
			(int)parser->export.len, parser->export.str, (int)(prod.name.len - 2), prod.name.str + 1);
    } else {
        prod.identifier.len = parent_id.len + 3;
        prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, (prod.identifier.len) + 1);
		snprintf(prod.identifier.str, prod.identifier.len + 1, "%.*s_re",
			(int)parent_id.len, parent_id.str);
    } 
    
    prod.type = PEG_LITERAL;

    // don't need to declare LiteralRules
    P4CProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    P4CString arg = format_regex(parser, prod.name.str + 1, prod.name.len - 2);

    // assign args
    prod.args._class->push(&prod.args, arg);

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_regex_literal(P4CParser * parser, ASTNode * node, const P4CString parent_id) {
    P4CProduction prod;
    STACK_INIT(P4CString)(&prod.args, 0);
    
    prod.name = get_string_from_parser(parser, node);
    
    size_t buf_len = (parent_id.len + 3);
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len + 1);
    prod.identifier.len = snprintf(prod.identifier.str, buf_len + 1, "%.*s_re", 
		(int)parent_id.len, parent_id.str);
    
    prod.type = PEG_LITERAL;

    P4CProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    buf_len = (prod.name.len  - 1 + REGEX_LIB_OFFSET_LEFT + REGEX_LIB_OFFSET_RIGHT);
    P4CString arg = {.str = MemPoolManager_malloc(parser->str_mgr, buf_len), .len = 0};

    arg.len += snprintf(arg.str + arg.len, buf_len - arg.len, "\"%.*s\"", (int)(prod.name.len - 2), prod.name.str + 1);

    // assign args
    prod.args._class->push(&prod.args, arg);

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_punctuator_keyword(P4CParser * parser, ASTNode * node) {
    P4CProduction prod;
    STACK_INIT(P4CString)(&prod.args, 0);
    P4CString parent_id = {.str = (node->children[0]->rule == PEG4C_PUNCTUATOR_KW) ? "punctuator" : "keyword"};
    parent_id.len = strlen(parent_id.str);

    prod.name = get_string_from_parser(parser, node->children[0]);

    size_t buf_len = (parser->export.len + prod.name.len + 2);
    
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len); // +1 for underscore
    prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_%.*s", (int)parser->export.len, parser->export.str, (int)prod.name.len, prod.name.str);

    prod.type = PEG_LITERAL;

    P4CProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    P4CString arg;
    ASTNode * punc = node->children[2];
    Token * cur = punc->token_start;
    Token * end = punc->token_end;
    arg.len = cur->length;
    while (cur != end) {
        cur = cur->next;
        arg.len += cur->length;
    }
    arg.len = 4 * arg.len + 1 + REGEX_LIB_OFFSET_LEFT + REGEX_LIB_OFFSET_RIGHT; 
    arg.str = MemPoolManager_malloc(parser->str_mgr, (arg.len + 1)); 
    size_t written = 0;
    arg.str[written++] = '"';

    ASTNode * child = node->children[2];

    size_t N = child->nchildren;
    size_t i = 0;

    for (size_t i = 0; i < N; i += 2) {
        handle_string_literal(parser, child->children[i], parent_id);

        P4CString str_lit_name = get_string_from_parser(parser, child->children[i]);
        P4CProduction str_lit_prod;
        parser->productions._class->get(&parser->productions, str_lit_name, &str_lit_prod);

        char const * re = str_lit_prod.args.bins[0].str + 1; // remove starting "^
        size_t length = str_lit_prod.args.bins[0].len - 2; // remove starting "^ and ending "

        memcpy((void*)(arg.str + written), (void*)re, length);
        written += length;
        if (N > 1 && i < N - 2) {
            arg.str[written++] = '|';
        }
    }
    arg.str[written++] = '"';
    arg.len = written;
    arg.str[written] = '\0';

    // make args
    prod.args._class->push(&prod.args, arg);

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_special_production(P4CParser * parser, ASTNode * node) {
    switch (node->children[0]->rule) {
        case PEG4C_TOKEN_KW: {
            handle_production_(parser, node->children[0], NULL, node->children[2], HANDLE_PRODUCTION_TOKEN);
            break;
        }
        case PEG4C_PUNCTUATOR_KW: { }
           FALLTHROUGH // otherwise gcc -pedantic complains about fallthrough
        case PEG4C_KEYWORD_KW: {
            handle_punctuator_keyword(parser, node);
            break;
        }
    }
}

void handle_import(P4CParser * parser, ASTNode * node) {
    P4CString import = get_string_from_parser(parser, node);
    parser->imports._class->push(&parser->imports, import);

    if (!parser->source_file) { // export config not found
        open_output_files(parser);
        prep_output_files(parser);
    }

    char * buffer = "#include \"";
    fwrite(buffer, 1, strlen(buffer), parser->source_file);
    P4CString_fwrite(import, parser->source_file, PSFO_NONE);

    buffer = ".h\"\n";
    fwrite(buffer, 1, strlen(buffer), parser->source_file);
    fflush(parser->source_file);
}

void handle_export(P4CParser * parser, ASTNode * node) {
    ParserType * parser_class = parser->Parser._class;
    // node is a nonws_printable production
    if (parser->productions.fill || parser->source_file){ // || parser->keywords.fill || parser->punctuators.fill) {
        return;
    }
    if (parser->export_found) {
        return;
    }
    parser->export = (P4CString){.str = (char *)node->token_start->string, .len = node->token_start->length};

    open_output_files(parser);
    prep_output_files(parser);
}

/*
Should probably include a hash map of handlers mapping P4CString identifier -> void handle_[identifier](P4CParser * parser, ASTNode *)
*/
void handle_config(P4CParser * parser, ASTNode * node) {
    Token * tok = node->children[0]->token_start;
    if (!strncmp("import", tok->string, tok->length)) {
        handle_import(parser, node->children[2]);
    } else if (!strncmp("export", tok->string, tok->length)) {
        handle_export(parser, node->children[2]);
    }
}

ASTNode * handle_peg4c(Production * peg4c_prod, Parser * parser_, ASTNode * node) {
    P4CParser * parser = (P4CParser *)parser_;
    parser_->ast = node;
    
    // node is a peg4c document production
    for (size_t i = 0; i < node->nchildren; i++) {
        switch (node->children[i]->rule) {
            case PEG4C_CONFIG: {
                handle_config(parser, node->children[i]);
                break;
            }
            case PEG4C_SPECIAL_PRODUCTION: {
                handle_special_production(parser, node->children[i]);
                break;
            }
            case PEG4C_PRODUCTION: {
                handle_production(parser, node->children[i]);
                break;
            }
        }
        
    }

    cleanup_header_file(parser);

    parser->productions._class->for_each(&parser->productions, &P4CProduction_define, (void*)parser);

    build_export_rules(parser);

    build_destructor(parser);
    
    return node;
}

ASTNode * simplify_rule(Production * simplifiable_rule, Parser * parser, ASTNode * node) {
    rule_id_type cur_rule = ((Rule *)simplifiable_rule)->id;
    switch (cur_rule) {
        case PEG4C_LOOKAHEAD_RULE: {
            if (!(node->children[0]->nchildren)) {
                return node->children[1];
                
            }
            break;
        }
        case PEG4C_REPEATED_RULE: {
            if (!node->children[1]->nchildren) {
                return node->children[0];
            }
            break;
        }
        case PEG4C_LIST_RULE: {
        }
            FALLTHROUGH // otherwise gcc -pedantic complains about fallthrough
        case PEG4C_SEQUENCE: {
        }
            FALLTHROUGH // otherwise gcc -pedantic complains about fallthrough
        case PEG4C_CHOICE: {
            if (node->nchildren == 1) {
                return node->children[0];
            }
            break;
        }
        default: {
            return node;
        }
    }    

    node->rule = ((Rule *)simplifiable_rule)->id;
    return node;

}

