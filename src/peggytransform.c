#include <ctype.h>

#include "peggy/utils.h"
#include "peggy.h"
#include "peggytransform.h"
#include "peggybuild.h"

PeggyString get_rule_pointer(PeggyParser * parser, PeggyString name) {
    PeggyProduction prod = {0};
    if (parser->productions._class->get(&parser->productions, name, &prod)) {
        LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_ERROR, "ERROR: %s - retrieval of production %.*s failed.\n", __func__, name.len, name.str);
    }
    
    PeggyString arg = {.len = 9 + prod.identifier.len};
    arg.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (arg.len + 1));
    memcpy((void *)(arg.str), "(Rule *)", 8);
    arg.str[8] = '&';
    memcpy((void*)(arg.str + 9), (void*)prod.identifier.str, prod.identifier.len);

    return arg;
}

unsigned char size_t_strlen(size_t val){
    char buffer[64]; // a buffer of 64 will cover any crazy sized architectures
    int length = sprintf(buffer, "%zu", val);
    if (length < 0 || length > 63) {
        printf("what the hell did you do\?\?\?!!!\n");
        exit(EXIT_FAILURE);
    }
    return (unsigned char) length;
}

void handle_terminal(PeggyParser * parser, ASTNode * node, const PeggyString parent_id) {
    if (node->rule->id == STRING_LITERAL) {
        handle_string_literal(parser, node->children[0], parent_id);
    } else {
        handle_regex_literal(parser, node->children[0], parent_id);
    }
}


void handle_lookahead_rule(PeggyParser * parser, ASTNode * node, PeggyString name) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling lookahead rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);
    
    PeggyProduction prod;
    production_init(parser, name, &prod);

    ASTNode * lookahead_type = node->children[0]->children[0];
    switch (lookahead_type->rule->id) {
        case AMPERSAND: {
            prod.type = PEGGY_POSITIVELOOKAHEAD;
            prod.identifier.len = parser->export.len + 1 + strlen("pos") + 1 + size_t_strlen(parser->productions.fill);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
            char * buffer = copy_export(parser, prod.identifier.str);
            size_t written = (size_t)(buffer - prod.identifier.str);
            written += sprintf(buffer, "_pos_%zu", parser->productions.fill);
            prod.identifier.str[written] = '\0';

            // add arguments
            break;
        }
        case EXCLAIM: { // repeat 0 or more
            prod.type = PEGGY_NEGATIVELOOKAHEAD;
            prod.identifier.len = parser->export.len + 1 + strlen("neg") + 1 + size_t_strlen(parser->productions.fill);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
            char * buffer = copy_export(parser, prod.identifier.str);
            size_t written = (size_t)(buffer - prod.identifier.str);
            written += sprintf(buffer, "_neg_%zu", parser->productions.fill);
            prod.identifier.str[written] = '\0';
            // add arguments
            break;
        }
        default: { // repeat m to n
            LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_ERROR, "ERROR: %s - syntax error. lookahead rule with leading rule %s not understood\n", __func__, lookahead_type->rule->name);
            return ;
        }
    }

    PeggyProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    ASTNode * child1 = node->children[1];

    PeggyString look_name = get_string_from_parser(parser, child1);
    handle_base_rule(parser, child1, NULL_STRING, look_name);
    prod.args._class->push(&prod.args, get_rule_pointer(parser, look_name));

    parser->productions._class->set(&parser->productions, prod.name, prod);
} 

void handle_list_rule(PeggyParser * parser, ASTNode * node, PeggyString name) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling list rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);
    PeggyProduction prod;
    production_init(parser, name, &prod);
    size_t nchildren = (node->nchildren + 1) / 2;
    prod.identifier.len = parser->export.len + 1 + strlen("list") + 1 + size_t_strlen((size_t)nchildren) + 1 + size_t_strlen(parser->productions.fill);
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
    char * buffer = copy_export(parser, prod.identifier.str);
    size_t written = (size_t)(buffer - prod.identifier.str);
    written += sprintf(buffer, "_list_%zu_%zu", nchildren, parser->productions.fill);
    prod.identifier.str[written] = '\0';

    prod.type = PEGGY_LIST;

    PeggyProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    for (size_t i = 0; i < node->nchildren; i += 2) {
        PeggyString name = get_string_from_parser(parser, node->children[i]);
        handle_simplified_rules(parser, node->children[i], NULL_STRING, name);
        prod.args._class->push(&prod.args, get_rule_pointer(parser, name));
    }
    
    parser->productions._class->set(&parser->productions, prod.name, prod);
}

// This is a pretty garbage function that needs to get re-written
void handle_repeated_rule(PeggyParser * parser, ASTNode * node, PeggyString name) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling repeated rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);

    PeggyProduction prod;  
    production_init(parser, name, &prod);
    prod.type = PEGGY_REPEAT;

    ASTNode * repeat_type = node->children[1]->children[0];

    PeggyString m = {0}, n = {0};
    
    switch (repeat_type->rule->id) {
        case PLUS: { // repeat 1 or more
            //printf("one or more\n");
            m.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char));
            memcpy((void*)m.str, "1", 1);
            m.len = 1;

            prod.identifier.len = parser->export.len + 9 /* 1 + strlen("rep_1_0") + 1 */ + size_t_strlen(parser->productions.fill);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
            char * buffer = copy_export(parser, prod.identifier.str);
            size_t written = (size_t)(buffer - prod.identifier.str);
            written += sprintf(buffer, "_rep_1_0_%zu", parser->productions.fill);
            prod.identifier.str[written] = '\0';

            // add arguments
            break;
        }
        case ASTERISK: { // repeat 0 or more
            //printf("zero or more\n");
            prod.identifier.len = parser->export.len + 9 /* 1 + strlen("rep_0_0") + 1 */ + size_t_strlen(parser->productions.fill);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
            char * buffer = copy_export(parser, prod.identifier.str);
            size_t written = (size_t)(buffer - prod.identifier.str);
            written += sprintf(buffer, "_rep_0_0_%zu", parser->productions.fill);
            prod.identifier.str[written] = '\0';

            // add arguments
            break;
        }
        case QUESTION: { // repeat 0 or 1
            //printf("optional\n");
            m.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char));
            memcpy((void*)m.str, "0", 1);
            m.len = 1;
            n.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char));
            memcpy((void*)n.str, "1", 1);
            n.len = 1;

            prod.identifier.len = parser->export.len + 9 /* 1 + strlen("rep_0_1") + 1 */ + size_t_strlen(parser->productions.fill);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
            char * buffer = copy_export(parser, prod.identifier.str);
            size_t written = (size_t)(buffer - prod.identifier.str);
            written += sprintf(buffer, "_rep_0_1_%zu", parser->productions.fill);
            prod.identifier.str[written] = '\0';
            
            break;
        }
        default: { // repeat m to n // TODO: assess whether m and n are really necessary since get_string_from_parser now makes a new copy
            ASTNode ** children = repeat_type->children;
            PeggyString mstr = get_string_from_parser(parser, children[1]);
            PeggyString nstr = get_string_from_parser(parser, children[3]);
            //printf("mstr: %zu, %.*s, nstr: %zu, %.*s\n", mstr.len, (int)mstr.len, mstr.str, nstr.len, (int)nstr.len, nstr.str);
            if (mstr.len) {
                m.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * mstr.len);
                memcpy((void*)m.str, (void*)mstr.str, mstr.len);
                m.len = mstr.len;   
            } else {
                m.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char));
                m.str[0] = '0';
                m.len = 1;
            }

            if (nstr.len) {
                n.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * nstr.len);
                memcpy((void*)n.str, (void*)nstr.str, nstr.len);
                n.len = nstr.len;
            } else {
                if (!children[2]->nchildren) { // comma is omitted. n should be a copy of m
                    n.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * m.len);
                    n.len = m.len;
                    memcpy(n.str, m.str, n.len);
                } else {
                    n.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char));
                    n.str[0] = '0';
                    n.len = 1;
                }
            }

            prod.identifier.len = parser->export.len + 6 /* 1 + strlen("rep__") */ + mstr.len + nstr.len + 1 + size_t_strlen(parser->productions.fill);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
            char * buffer = copy_export(parser, prod.identifier.str);
            size_t written = (size_t)(buffer - prod.identifier.str);
            written += sprintf(buffer, "_rep_");
            if (m.len) {
                memcpy((void*)(prod.identifier.str + written), (void*)m.str, m.len);
            }
            written += m.len;
            prod.identifier.str[written++] = '_';
            if (n.len) {
                memcpy((void*)(prod.identifier.str + written), (void*)n.str, n.len);
            }
            written += n.len;
            written += sprintf((prod.identifier.str + written), "_%zu", parser->productions.fill);
            prod.identifier.str[written] = '\0';

            // add arguments
        }
    }

    PeggyProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    PeggyString rep_name = get_string_from_parser(parser, node->children[0]);
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

void handle_sequence(PeggyParser * parser, ASTNode * node, PeggyString name) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling sequence rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);
    PeggyProduction prod;
    production_init(parser, name, &prod);
    size_t nchildren = (node->nchildren + 1) / 2;
    prod.identifier.len = parser->export.len + 1 + strlen("seq") + 1 + size_t_strlen((size_t)nchildren) + 1 + size_t_strlen(parser->productions.fill);
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
    char * buffer = copy_export(parser, prod.identifier.str);
    size_t written = (size_t)(buffer - prod.identifier.str);
    written += sprintf(buffer, "_seq_%zu_%zu", nchildren, parser->productions.fill);
    prod.identifier.str[written] = '\0';
    prod.type = PEGGY_SEQUENCE;

    PeggyProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);


    for (size_t i = 0; i < node->nchildren; i += 2) {
        PeggyString name = get_string_from_parser(parser, node->children[i]);
        handle_simplified_rules(parser, node->children[i], NULL_STRING, name);
        prod.args._class->push(&prod.args, get_rule_pointer(parser, name));
    }

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_choice(PeggyParser * parser, ASTNode * node, PeggyString name) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling choice rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);
    PeggyProduction prod;
    production_init(parser, name, &prod);
    size_t nchildren = (node->nchildren + 1) / 2;
    prod.identifier.len = parser->export.len + 1 + strlen("choice") + 1 + size_t_strlen((size_t)nchildren) + 1 + size_t_strlen(parser->productions.fill);
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
    char * buffer = copy_export(parser, prod.identifier.str);
    size_t written = (size_t)(buffer - prod.identifier.str);
    written += sprintf(buffer, "_choice_%zu_%zu", nchildren, parser->productions.fill);
    prod.identifier.str[written] = '\0';
    prod.type = PEGGY_CHOICE;

    PeggyProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);


    for (size_t i = 0; i < node->nchildren; i += 2) {
        PeggyString name = get_string_from_parser(parser, node->children[i]);
        handle_simplified_rules(parser, node->children[i], NULL_STRING, name);
        prod.args._class->push(&prod.args, get_rule_pointer(parser, name));
    }

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_base_rule(PeggyParser * parser, ASTNode * node, const PeggyString parent_id, PeggyString name) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling base rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);
    switch (node->children[0]->rule->id) {
        case TERMINAL: {
            // terminals are only built if parent_id is not empty
            if (parent_id.len) {
                handle_terminal(parser, node->children[0], parent_id);
            }
            break;
        }
        case NONTERMINAL: { // do nothing. Don't make a new production
            /* forward declare the nonterminal */
            PeggyString name = get_string_from_parser(parser, node->children[0]);
            
            if (!parser->productions._class->in(&parser->productions, name)) {
                if (strncmp(name.str, "punctuator", name.len) && strncmp(name.str, "keyword", name.len)) {
                    PeggyProduction_build(parser, node->children[0], PEGGY_PRODUCTION);
                } else {
                    PeggyProduction_build(parser, node->children[0], PEGGY_LITERAL);
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
void handle_simplified_rules(PeggyParser * parser, ASTNode * node, const PeggyString parent_id, PeggyString name) {
    
    // This check is very critical
    // If the name already exists (which is any rule other than production definitions themselves), 
    // this will leak memory, cause double-frees and all sorts of havoc that is hard to troubleshoot
    PeggyProduction prod; // may not be used
    if (!parser->productions._class->get(&parser->productions, name, &prod)) {
        LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - repeated rule id %s with name %.*s already exists, skipping\n", __func__, node->rule->name, (int)name.len, name.str);
        return;
    }

    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling simplified rule rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);
    switch (node->rule->id) {
        case CHOICE: {
            handle_choice(parser, node, name);
            break;
        }
        case SEQUENCE: {
            handle_sequence(parser, node, name);
            break;
        }
        case REPEATED_RULE: {
            handle_repeated_rule(parser, node, name);
            break;
        }
        case LIST_RULE: {
            handle_list_rule(parser, node, name);
            break;
        }
        case LOOKAHEAD_RULE: {
            handle_lookahead_rule(parser, node, name);
            break;
        }
        case BASE_RULE: { // BASE_RULE
            handle_base_rule(parser, node, parent_id, name);
            break;
        }
        default: {
            LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_ERROR, "ERROR: %s - trying to simplify an unknown rule id %s with type %s from line %u, col %u\n", __func__, node->rule->name, get_type_name(node->rule->_class->type), node->token_start->coords.line, node->token_start->coords.col);
        }
    }
    
}

#define HANDLE_PRODUCTION_NONE 0
#define HANDLE_PRODUCTION_TOKEN 1
void handle_production_(PeggyParser * parser, ASTNode * id, ASTNode * transforms, ASTNode * rule_def, unsigned int flags) {
    PeggyProduction prod;
    PeggyString prod_name = get_string_from_parser(parser, id);
    if (parser->productions._class->get(&parser->productions, prod_name, &prod)) {
        // if retrieval fails, build a new one
        prod = PeggyProduction_build(parser, id, PEGGY_PRODUCTION);
    }
    PeggyProduction_declare(parser, prod);
    
    PeggyString name = get_string_from_parser(parser, rule_def);
    handle_simplified_rules(parser, rule_def, prod.identifier, get_string_from_parser(parser, rule_def));
    prod.args._class->push(&prod.args, get_rule_pointer(parser, name));

    if (flags & HANDLE_PRODUCTION_TOKEN) {
        char const * action = "token_action";
        size_t action_length = strlen(action);
        char * buffer = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (action_length + 1));
        memcpy((void *)buffer, action, action_length);
        prod.args._class->push(&prod.args, (PeggyString){.str = buffer, .len = action_length});
    } else {
        if (transforms) {
            for (size_t i = 0; i < transforms->nchildren; i += 2) {
                PeggyString transform_name = get_string_from_parser(parser, transforms->children[i]);
                
                char * buffer = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (transform_name.len + 1));
                memcpy((void *)buffer, transform_name.str, transform_name.len);
                prod.args._class->push(&prod.args, (PeggyString){.str = buffer, .len = transform_name.len});
            }
        }
    }

    // since productions are not allocated objects on the hash map, have to push to map again
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - adding production rule id %s from line %u, col %u to production map\n", __func__, id->rule->name, id->token_start->coords.line, id->token_start->coords.col);
    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_production(PeggyParser * parser, ASTNode * node) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling production rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);
    handle_production_(parser, node->children[0], node->children[1]->nchildren ? node->children[1]->children[0]->children[1] : NULL, node->children[3], HANDLE_PRODUCTION_NONE);
}

#define REGEX_LIB_STRING_LEFT "\""
#define REGEX_LIB_OFFSET_LEFT 1
#define REGEX_LIB_STRING_RIGHT "\""
#define REGEX_LIB_OFFSET_RIGHT 1

// returns one after last written character or dest if error
PeggyString format_regex(PeggyParser * parser, char const * src, size_t src_length) {
    static char const * const escaped_chars = ".^$*+?()[{\\|";

    size_t buffer_size = 3 * (src_length + 1 + REGEX_LIB_OFFSET_LEFT + REGEX_LIB_OFFSET_RIGHT);
    char * buffer = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * buffer_size);
    size_t len = 0;

    buffer[len++] = '"';

    for (size_t i = 0; i < src_length; i++) {
        /*
        if (len >= buffer_size - 2) {
            buffer_size += 2 * (src_length - i) + 3;
            buffer = realloc(buffer, sizeof(char) * buffer_size);
        }
        */
        if (strchr(escaped_chars, *src)) {
            buffer[len++] = '\\';
            buffer[len++] = '\\';
        }
        buffer[len++] = *src;
        src++;
    }
    /*
    if (len > buffer_size - 3) {
        buffer_size = len + 3;
        buffer = realloc(buffer, sizeof(char) * buffer_size);
    }
    */

    buffer[len++] = '"';
    buffer[len] = '\0';

    return (PeggyString){.str = buffer, .len = len};
}

void handle_string_literal(PeggyParser * parser, ASTNode * node, const PeggyString parent_id) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling string literal from line %u, col %u\n", __func__, node->token_start->coords.line, node->token_start->coords.col);
    PeggyProduction prod;
    STACK_INIT(PeggyString)(&prod.args, 0);
    
    prod.name = get_string_from_parser(parser, node);
    
    if (!strncmp("punctuator", parent_id.str, parent_id.len)) { // this is punctuation
        char * lookup_result = punctuator_lookup(prod.name.str + 1, prod.name.len - 2);
        //printf("%.*s -> %s\n", (int)prod.name.len, prod.name.str, lookup_result ? lookup_result : "(not found)");
        if (lookup_result) {
            prod.identifier.len = strlen(lookup_result);
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
            memcpy((void*)prod.identifier.str, (void*)lookup_result, prod.identifier.len);    
        } else {
            prod.identifier.len = (prod.name.len - 2)*4 + 5;
            prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
            prod.identifier.str[0] = 'p';
            prod.identifier.str[1] = 'u';
            prod.identifier.str[2] = 'n';
            prod.identifier.str[3] = 'c';
            size_t j = 4; // target index in prod.identifier.str
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
        prod.identifier.len = (prod.name.len - 2) + 3;
        prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
        memcpy((void*)prod.identifier.str, (void*)(prod.name.str + 1), prod.name.len-2);
        prod.identifier.str[prod.identifier.len-3] = '_';
        prod.identifier.str[prod.identifier.len-2] = 'k';
        prod.identifier.str[prod.identifier.len-1] = 'w';
    } else {
        prod.identifier.len = parent_id.len + 3;
        prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
        memcpy((void*)prod.identifier.str, (void*)parent_id.str, parent_id.len);
        prod.identifier.str[parent_id.len] = '_';
        prod.identifier.str[parent_id.len+1] = 'r';
        prod.identifier.str[parent_id.len+2] = 'e';
    } 
    
    prod.type = PEGGY_LITERAL;

    // don't need to declare LiteralRules
    PeggyProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    PeggyString arg = format_regex(parser, prod.name.str + 1, prod.name.len - 2);

    // assign args
    prod.args._class->push(&prod.args, arg);

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_regex_literal(PeggyParser * parser, ASTNode * node, const PeggyString parent_id) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling regex from line %u, col %u\n", __func__, node->token_start->coords.line, node->token_start->coords.col);
    PeggyProduction prod;
    STACK_INIT(PeggyString)(&prod.args, 0);
    
    prod.name = get_string_from_parser(parser, node);
    
    prod.identifier.len = parent_id.len + 3;
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1));
    memcpy((void*)prod.identifier.str, (void*)parent_id.str, parent_id.len);
    prod.identifier.str[parent_id.len] = '_';
    prod.identifier.str[parent_id.len+1] = 'r';
    prod.identifier.str[parent_id.len+2] = 'e';

    prod.type = PEGGY_LITERAL;

    PeggyProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    PeggyString arg = {.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.name.len  - 1 + REGEX_LIB_OFFSET_LEFT + REGEX_LIB_OFFSET_RIGHT)), .len = 0};
    memcpy((void*)arg.str, REGEX_LIB_STRING_LEFT, REGEX_LIB_OFFSET_LEFT);
    arg.len += REGEX_LIB_OFFSET_LEFT;
    memcpy((void*)(arg.str + arg.len), (void*)(prod.name.str + 1), prod.name.len - 2);
    arg.len += prod.name.len - 2;
    memcpy((void*)(arg.str + arg.len), REGEX_LIB_STRING_RIGHT, REGEX_LIB_OFFSET_RIGHT + 1);
    arg.len += REGEX_LIB_OFFSET_RIGHT;

    // assign args
    prod.args._class->push(&prod.args, arg);

    parser->productions._class->set(&parser->productions, prod.name, prod);
}

void handle_punctuator_keyword(PeggyParser * parser, ASTNode * node) {
    PeggyProduction prod;
    STACK_INIT(PeggyString)(&prod.args, 0);
    PeggyString parent_id = {.str = (node->children[0]->rule->id == PUNCTUATOR_KW) ? "punctuator" : "keyword"};
    parent_id.len = strlen(parent_id.str);
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling %s from line %u, col %u\n", __func__, parent_id.str, node->token_start->coords.line, node->token_start->coords.col);

    prod.name = get_string_from_parser(parser, node->children[0]);

    prod.identifier.len = parser->export.len + 1 + prod.name.len;
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (prod.identifier.len + 1)); // +1 for underscore
    char * buffer = copy_export(parser, prod.identifier.str);
    buffer[0] = '_';
    buffer++;
    memcpy(buffer, prod.name.str, prod.name.len);
    prod.type = PEGGY_LITERAL;

    PeggyProduction_declare(parser, prod);
    parser->productions._class->set(&parser->productions, prod.name, prod);

    PeggyString arg;
    ASTNode * punc = node->children[2];
    Token * cur = punc->token_start;
    Token * end = punc->token_end;
    arg.len = cur->length;
    while (cur != end) {
        cur = cur->next;
        arg.len += cur->length;
    }
    arg.len = 4 * arg.len + 1 + REGEX_LIB_OFFSET_LEFT + REGEX_LIB_OFFSET_RIGHT; 
    arg.str = MemPoolManager_malloc(parser->str_mgr, sizeof(char) * (arg.len + 1)); 
    size_t written = 0;
    arg.str[written++] = '"';

    ASTNode * child = node->children[2];

    size_t N = child->nchildren;
    size_t i = 0;

    for (size_t i = 0; i < N; i += 2) {
        handle_string_literal(parser, child->children[i], parent_id);

        PeggyString str_lit_name = get_string_from_parser(parser, child->children[i]);
        PeggyProduction str_lit_prod;
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

void handle_special_production(PeggyParser * parser, ASTNode * node) {
    switch (node->children[0]->rule->id) {
        case TOKEN_KW: {
            handle_production_(parser, node->children[0], NULL, node->children[2], HANDLE_PRODUCTION_TOKEN);
            break;
        }
        case PUNCTUATOR_KW: { }
           FALLTHROUGH // otherwise gcc -pedantic complains about fallthrough
        case KEYWORD_KW: {
            handle_punctuator_keyword(parser, node);
            break;
        }
        default: {
            printf("special prodution case of %s not yet implemented\n", node->children[0]->rule->name);
        }
    }
}

void handle_import(PeggyParser * parser, ASTNode * node) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling import from line %u, col %u\n", __func__, node->token_start->coords.line, node->token_start->coords.col);
    PeggyString import = get_string_from_parser(parser, node);
    //printf("import: %.*s\n", (int)import.len, import.str);
    parser->imports._class->push(&parser->imports, import);

    if (!parser->source_file) { // export config not found
        open_output_files(parser);
        prep_output_files(parser);
    }

    char * buffer = "#include \"";
    fwrite(buffer, sizeof(char), strlen(buffer), parser->source_file);
    PeggyString_fwrite(import, parser->source_file, PSFO_NONE);

    buffer = ".h\"\n";
    fwrite(buffer, sizeof(char), strlen(buffer), parser->source_file);
    fflush(parser->source_file);
}

void handle_export(PeggyParser * parser, ASTNode * node) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling export from line %u, col %u\n", __func__, node->token_start->coords.line, node->token_start->coords.col);
    ParserType * parser_class = parser->Parser._class;
    // node is a nonws_printable production
    if (parser->productions.fill || parser->source_file){ // || parser->keywords.fill || parser->punctuators.fill) {
        LOG_EVENT(&((Parser *)parser)->logger, LOG_LEVEL_WARN, "WARN: %s - cannot change export name after any productions have been defined or modules imported: line %u, col %u...ignoring\n", __func__, node->token_start->coords.line, node->token_start->coords.col);
        return;
    }
    if (parser->export_found) {
        LOG_EVENT(&((Parser *)parser)->logger, LOG_LEVEL_WARN, "WARN: %s - cannot have multiple exports in a grammar specification: line %u, col %u...ignoring\n", __func__, node->token_start->coords.line, node->token_start->coords.col);
        return;
    }
    parser->export = (PeggyString){.str = (char *)node->token_start->string, .len = node->token_start->length};

    open_output_files(parser);
    prep_output_files(parser);
}


/*
Should probably include a hash map of handlers mapping PeggyString identifier -> void handle_[identifier](PeggyParser * parser, ASTNode *)
*/
void handle_config(PeggyParser * parser, ASTNode * node) {
    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_DEBUG, "DEBUG: %s - handling config at line %u, col %u\n", __func__, node->token_start->coords.line, node->token_start->coords.col);
    Token * tok = node->children[0]->token_start;
    if (!tok) {
        LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_ERROR, "ERROR: %s - failed to retrieve config name for rule id: %s\n", __func__, node->rule->name);
    }
    if (!strncmp("import", tok->string, tok->length)) {
        handle_import(parser, node->children[2]);
    } else if (!strncmp("export", tok->string, tok->length)) {
        handle_export(parser, node->children[2]);
    } else {
        LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_ERROR, "ERROR: %s - config type not understood with rule id: %s\n", __func__, node->rule->name);
    }
}

ASTNode * handle_peggy(Production * peggy_prod, Parser * parser_, ASTNode * node) {
    LOG_EVENT(&parser_->logger, LOG_LEVEL_DEBUG, "DEBUG: %s - Found %zu document elements\n", __func__, node->nchildren);
    PeggyParser * parser = (PeggyParser *)parser_;
    parser_->ast = node;
    
    // node is a peggy document production
    for (size_t i = 0; i < node->nchildren; i++) {
        switch (node->children[i]->rule->id) {
            case CONFIG: {
                handle_config(parser, node->children[i]);
                break;
            }
            case SPECIAL_PRODUCTION: {
                handle_special_production(parser, node->children[i]);
                break;
            }
            case PRODUCTION: {
                handle_production(parser, node->children[i]);
                break;
            }
            default: {
                LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_ERROR, "ERROR: %s - document element not understood: %s. Expected a config or production.\n", __func__, node->children[i]->rule->name);
            }
        }
        
    }

    cleanup_header_file(parser);

    LOG_EVENT(&parser->Parser.logger, LOG_LEVEL_INFO, "INFO: %s - building definitions\n", __func__);
    parser->productions._class->for_each(&parser->productions, &PeggyProduction_define, (void*)parser);

    build_export_rules(parser);

    build_destructor(parser);
    
    return node;
}

ASTNode * simplify_rule(Production * simplifiable_rule, Parser * parser, ASTNode * node) {
    LOG_EVENT(&parser->logger, LOG_LEVEL_DEBUG, "DEBUG: %s - trying to simplifying rule id %s from line %u, col %u\n", __func__, node->rule->name, node->token_start->coords.line, node->token_start->coords.col);
    rule_id_type cur_rule = ((Rule *)simplifiable_rule)->id;
    switch (cur_rule) {
        case LOOKAHEAD_RULE: {
            if (!(node->children[0]->nchildren)) {
                return node->children[1];
                
            }
            break;
        }
        case REPEATED_RULE: {
            //printf("repeated rule found with %zu children. is actually repeated rule? %s\n", node->nchildren, node->children[1]->nchildren ? "yes": "no");
            if (!node->children[1]->nchildren) {
                return node->children[0];
            }
            break;
        }
        case LIST_RULE: {
        }
            FALLTHROUGH // otherwise gcc -pedantic complains about fallthrough
        case SEQUENCE: {
        }
            FALLTHROUGH // otherwise gcc -pedantic complains about fallthrough
        case CHOICE: {
            if (node->nchildren == 1) {
                return node->children[0];
            }
            break;
        }
        default: {
            LOG_EVENT(&parser->logger, LOG_LEVEL_DEBUG, "DEBUG: %s - rule id %s was not simplified\n", __func__, node->rule->name);
            return node;
        }
    }    

    LOG_EVENT(&parser->logger, LOG_LEVEL_TRACE, "TRACE: %s - re-initializing node rule from id %s to id %s; no node generated\n", __func__, node->rule->name, ((Rule *)simplifiable_rule)->name);
    node->rule = (Rule *)simplifiable_rule;
    //node->_class->init(node, (Rule *)simplifiable_rule, node->token_key, node->ntokens, node->str_length, node->nchildren, node->children);
    return node;

}

