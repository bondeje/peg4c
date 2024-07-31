#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <peggy/parser.h>
#include <peggy/packrat_cache.h>

#define Parser_NAME "Parser"

#define PARSER_NONE 0
#define PARSER_LAZY 1
#ifndef PARSER_LOGGER_BUFFER_SIZE
    #define PARSER_LOGGER_BUFFER_SIZE 1024
#endif
#define PARSER_PRINT_BUFFER_SIZE 4096
#ifndef PARSER_PRINT_TAB_SIZE 
    #define PARSER_PRINT_TAB_SIZE 4
#endif

struct ParserType Parser_class = {
    .type_name = Parser_NAME,
    .new = &Parser_new,
    .init = &Parser_init,
    .dest = &Parser_dest,
    .del = &Parser_del,
    .tell = &Parser_tell,
    .seek = &Parser_seek,
    .get_line_col_end = &Parser_get_line_col_end,
    //.gen_final_token_ = &Parser_gen_final_token_,
    .skip_token = &Parser_skip_token,
    .add_token = &Parser_add_token,
    .add_node = &Parser_add_node,
    .gen_next_token_ = &Parser_gen_next_token_,
    .parse = &Parser_parse,
    .tokenize = &Parser_tokenize,
    .check_cache = &Parser_check_cache,
    .cache_check = &Parser_cache_check,
    .traverse = &Parser_traverse,
    .print_ast = &Parser_print_ast
};

Parser * Parser_new(char const * name, size_t name_length, Rule * token_rule, 
                    Rule * root_rule, size_t nrules, unsigned int flags, 
                    char const * log_file, unsigned char log_level) {
    Parser * parser = malloc(sizeof(Parser));
    if (!parser) {
        return NULL;
    }
    if (Parser_init(parser, name, name_length, token_rule, root_rule, nrules, flags, log_file, log_level)) {
        free(parser);
        return NULL;
    }
    return parser;
}
err_type Parser_init(Parser * self, char const * name, size_t name_length,
                         Rule * token_rule, Rule * root_rule, size_t nrules, unsigned int flags, 
                         char const * log_file, unsigned char log_level) {
    self->_class = &Parser_class;
    self->name = name;
    self->name_length = name_length;
    self->token_rule = token_rule;
    self->root_rule = root_rule;

    if (log_file) {
        self->log_file = log_file;
    } else {
        self->log_file = "stdout";
    }

    LOG_INIT(&self->logger, self->log_file, NULL, 0, log_level, 0, NULL, name);
    
    LOG_EVENT(&self->logger, LOG_LEVEL_INFO, "INFO: %s - initialized logger. finalizing initialization of parser\n", __func__);
    
    err_type status = PackratCache_init(&self->cache, nrules, PACKRAT_DEFAULT); // PACKRAT_SPARSE probably not yet implemented
    if (status) {
        return status;
    }

    // initialize token manager
    self->token_mgr = MemPoolManager_new(PARSER_DEFAULT_NTOKENS, sizeof(Token), 8);
    self->token_head = MemPoolManager_next(self->token_mgr);
    *self->token_head = (Token){0};

    // initialize node manager
    self->node_mgr = MemPoolManager_new(PARSER_DEFAULT_NNODES, sizeof(ASTNode), 8);

    self->disable_cache_check = false;
    self->flags = flags;

    return PEGGY_SUCCESS;
}

void Parser_dest(Parser * self) {
    LOG_EVENT(&self->logger, LOG_LEVEL_INFO, "INFO: %s - destroying parser\n", __func__);
    
    /* clear out the ASTNodes */
    MemPoolManager_del(self->node_mgr);
    self->node_mgr = NULL;
    self->ast = NULL;
    
    LOG_DEST(&self->logger);

    // clear the cache
    self->cache._class->dest(&self->cache);

    /* clear the token list */
    self->token_head = NULL;
    self->token_cur = NULL;
    MemPoolManager_del(self->token_mgr);
    self->token_mgr = NULL;

}
void Parser_del(Parser * self) {
    Parser_dest(self);
    free(self);
}

Token * Parser_tokenize(Parser * self, char const * string, size_t string_length) {
    static const int REMAINING_TOKEN_MAX_SIZE = 16;
    LOG_EVENT(&self->logger, LOG_LEVEL_INFO, "INFO: %s - tokenizing string of length %zu: %.*s\n", __func__, string_length, string_length > REMAINING_TOKEN_MAX_SIZE ? REMAINING_TOKEN_MAX_SIZE : (int)string_length, string);
    if (!string_length) {
        return NULL;
    }
    Token * parser_token = Parser_tell(self);
    MemPoolManager * token_mgr = self->token_mgr;
    Token * cur = MemPoolManager_next(token_mgr);
    Token_init(cur, string, string_length, 0, 0);
    Parser_seek(self, cur);
    Token * start = cur;
    Rule * tokenizer = self->token_rule;

    while (cur && cur->length) {
        ASTNode * node = Rule_check(tokenizer, self);
        if (!node) {
            printf("failed node\n");
        }
        if (node == &ASTNode_fail) {
            LOG_EVENT(&self->logger, LOG_LEVEL_ERROR, "ERROR: %s - failed to tokenize string at line: %hu, col: %hu - %.*s\n", __func__, cur->coords.line, cur->coords.col, REMAINING_TOKEN_MAX_SIZE < cur->length ? REMAINING_TOKEN_MAX_SIZE : cur->length, cur->string);
        }
        cur = cur->next;
    }
    // reset Parser to current position
    Parser_seek(self, parser_token);
    return start;
}

Token * Parser_tell(Parser * self) {
    return self->token_cur;
}
void Parser_seek(Parser * self, Token * tok) {
    self->token_cur = tok;
}
void Parser_get_line_col_end(Parser * self, Token * tok, unsigned int * line_out, unsigned int * col_out) {
    char const * start = tok->string;
    char const * end = tok->string + tok->length;
    unsigned int line = tok->coords.line;
    char const * chr = strchr(start, '\n');
    while (chr && chr < end) {
        line++;
        start = chr + 1;
        chr = strchr(start, '\n');
    }
    unsigned int col = (unsigned int) (end - start);
    if (line == tok->coords.line) { /* no change */
        col += tok->coords.col;
    }
    if (!col) {
        col = 1;
    }
    *col_out = col;
    *line_out = line;
}

void Parser_add_string(Parser * self, Token * cur, char const * string, size_t string_length) {
    unsigned int line;
    unsigned int col;
    Parser_get_line_col_end(self, cur, &line, &col);
    Token * new_token = MemPoolManager_next(self->token_mgr);
    if (!new_token) {
        // TODO: error code
    }
    Token_init(new_token, string, string_length, line, col);
    new_token->next = cur->next;
    cur->next = new_token;
    new_token->prev = cur;
}

// should return an error code
void Parser_generate_new_token(Parser * self, size_t token_length, Token * cur) {
    if (!cur) {
        cur = self->token_cur;
    }
    size_t length = cur->length - token_length;
    cur->length = token_length;
    LOG_EVENT(&self->logger, LOG_LEVEL_TRACE, "TRACE: %s - adding token at line: %hu, col: %hu - %.*s\n", __func__, cur->coords.line, cur->coords.col, (int)cur->length, cur->string);
    Parser_add_string(self, cur, cur->string + token_length, length);
}


// try to remove this, replace with Parser_generate_new_token
/*
Token Parser_gen_final_token_(Parser * self, ASTNode * node) {
    // TODO: need to rework this
    Token * final = self->tokens.bins + self->tokens.fill - 1;
    size_t length = final->length - node->str_length;
    final->length = node->str_length;
    char const * string = final->string + final->length;
    unsigned int line;
    unsigned int col;

    // initialize tok 
    self->_class->get_line_col_end(self, final, &line, &col); // get token coordinates
    // override const in initializing "tok" 
    //return final->_class->new(final->string, start, end, line, col);
    Token new_tok = Token_DEFAULT_INIT;
    new_tok._class->init(&new_tok, string, length, line, col);
    return new_tok;
}
*/
err_type Parser_skip_token(Parser * self, ASTNode * node) {
    Token * skipped = node->token_start;
    LOG_EVENT(&self->logger, LOG_LEVEL_DEBUG, "DEBUG: %s - skipping token at line %u, col %u of length %zu: %.*s\n", __func__, skipped->coords.line, skipped->coords.col, node->str_length, (int)node->str_length, skipped->string);
    Parser_generate_new_token(self, node->str_length, skipped);
    if (skipped->prev) {
        skipped->prev->next = skipped->next;
    }    
    if (skipped->next) {
        skipped->next->prev = skipped->prev;
    }
    self->token_cur = self->token_cur->prev->next;
    return PEGGY_SUCCESS;
}

err_type Parser_add_token(Parser * self, ASTNode * node) {
    LOG_EVENT(&self->logger, LOG_LEVEL_DEBUG, "DEBUG: %s - adding new token at line %u, col %u of length %zu: %.*s\n", __func__, node->token_start->coords.line, node->token_start->coords.col, node->str_length, (int)node->str_length, node->token_start->string);
    
    Parser_generate_new_token(self, node->str_length, node->token_start);
    self->token_cur = node->token_start->next;
    return PEGGY_SUCCESS;
}


ASTNode * Parser_add_node(Parser * self, Rule * rule, Token * start, Token * end, size_t str_length, size_t nchildren, ASTNode * child, size_t size) {
    LOG_EVENT(&self->logger, LOG_LEVEL_TRACE, "TRACE: %s - adding node with str_length %zu, nchildren %zu (%p) at line: %hu, col: %hu", __func__, str_length, nchildren, (void*)child, start->coords.line, start->coords.col);
    if (!size) {
        size = sizeof(ASTNode);
    }
    ASTNode * new_node = MemPoolManager_malloc(self->node_mgr, size); // probably have to worry about alignment. Could just require that the object has the same alignment as ASTNode
    ASTNode_init(new_node, rule, start, end, str_length, nchildren, child);
    return new_node;
}

//ASTNode * Parser_extend_node(Parser * self, ASTNode * node)
//void Parser_move_node(Parser * self, ASTNode * node)


bool Parser_gen_next_token_(Parser * self) {
    LOG_EVENT(&self->logger, LOG_LEVEL_TRACE, "TRACE: %s - generating next token\n", __func__);

    self->disable_cache_check = true;
    ASTNode * result = self->token_rule->_class->check(self->token_rule, self);
    self->disable_cache_check = false;
    
    return result != &ASTNode_fail;
}
void Parser_parse(Parser * self, char const * string, size_t string_length) {
    LOG_EVENT(&self->logger, LOG_LEVEL_INFO, "INFO: %s - initiating parse\n", __func__);
    Token * tok = self->_class->tokenize(self, string, string_length);
    if (tok) {
        self->token_cur->next = tok;
        tok->prev = self->token_cur;
        self->token_cur = tok;
        if (self->root_rule) {
            self->ast = self->root_rule->_class->check(self->root_rule, self);
        }
    }
    
}
// the token_key isn't strictly necessary, but should be used for safety
ASTNode * Parser_check_cache(Parser * self, rule_id_type rule_id, Token * tok) {
    LOG_EVENT(&self->logger, LOG_LEVEL_TRACE, "TRACE: %s - retrieving cache result of rule id %d at line: %hu, col: %hu, cache_check: %s\n", __func__, rule_id, Parser_tell(self)->coords.line, Parser_tell(self)->coords.col, self->disable_cache_check ? "disabled" : "enabled");
    if (self->disable_cache_check) {
        return NULL;
    }
    return self->cache._class->get(&self->cache, rule_id, Parser_tell(self));
}
void Parser_cache_check(Parser * self, rule_id_type rule_id, Token * tok, ASTNode * node) {
    LOG_EVENT(&self->logger, LOG_LEVEL_TRACE, "TRACE: %s - caching result of rule id %d at line: %hu, col: %hu: %p\n", __func__, rule_id, tok->coords.line, tok->coords.col, (void*)node);
    self->cache._class->set(&self->cache, self, rule_id, tok, node);
}
err_type Parser_traverse(Parser * self, void (*traverse_action)(void * ctxt, ASTNode * node), void * ctxt) {
    /* TODO */
    /* requires a stack implementation */
    return PEGGY_NOT_IMPLEMENTED;
}
typedef struct ASTNodeSize {
    ASTNode * node;
    size_t size;
} ASTNodeSize;
#define ELEMENT_TYPE ASTNodeSize
#include <peggy/stack.h>

// this is unused and probably should be unused. delete
size_t ast_depth(ASTNode * root) {
    if (!root) {
        return 0;
    }
    size_t depth = 1;
    STACK(ASTNodeSize) st;
    STACK_INIT(ASTNodeSize)(&st, 0);
    st._class->push(&st, (ASTNodeSize) {.node = root, .size = 1});
    while (st.fill) {
        ASTNodeSize nodesize;
        st._class->pop(&st, &nodesize);
        ASTNode * node = nodesize.node;        
        size_t new_depth = nodesize.size + 1;
        depth = depth * (new_depth <= depth) + new_depth * (new_depth > depth);
        node = node->child;
        if (node) { // has children
            // move to right-most child
            while (node->next) {
                node = node->next;
            }
            while (node) { // backtrack adding all valid nodes to stack
                st._class->push(&st, (ASTNodeSize) {.node = node, .size = new_depth});
                node = node->prev;
            }
        }
    }
    return depth;
}

err_type Parser_print_ast(Parser * self, FILE * stream) {
    LOG_EVENT(&self->logger, LOG_LEVEL_DEBUG, "DEBUG: %s - starting ast print %p\n", __func__, (void*)self->ast);
    if (self->ast == &ASTNode_fail) {
        LOG_EVENT(&self->logger, LOG_LEVEL_DEBUG, "DEBUG: %s - invalid AST for printing\n", __func__);
        return PEGGY_SUCCESS;
    }
    if (!stream) {
        stream = stdout;
    }
    char print_buffer[PARSER_PRINT_BUFFER_SIZE];
    char * buffer = &print_buffer[0];
    int buffer_size = PARSER_PRINT_BUFFER_SIZE;
    STACK(ASTNodeSize) st;
    STACK_INIT(ASTNodeSize)(&st, 0); // this is huge. should just get depth of ast

    ASTNodeSize pair;
    Token * toks = NULL;
    st._class->push(&st, (ASTNodeSize){.node = self->ast, .size = 0});
    err_type status = PEGGY_SUCCESS;
    int snp_size = 0;
    while (st.fill) {
        //printf("starting ast traversal loop with %zu elements in stack\n", st.fill);
        if ((status = st._class->pop(&st, &pair))) {
            fprintf(stream, "%.*s\nERROR: status %d retrieving node...aborting Parser_print_ast\n", PARSER_PRINT_BUFFER_SIZE - buffer_size, print_buffer, status);
            fflush(stream);
            goto print_ast_fail;
        }
        // print the information to the buffer
        // TODO: should really only print if the rule is an instance of LiteralRule
        if (!pair.node->nchildren) { // the node is a token leaf in the AST tree. Print the node and token into the buffer
            Token * tok = pair.node->token_start;
            snp_size = snprintf(buffer, buffer_size, "%*s%s: rule id: %d, nchildren: %zu, token: %.*s\n", (int)pair.size * PARSER_PRINT_TAB_SIZE, "", pair.node->_class->type_name, pair.node->rule->id, pair.node->nchildren, (int)toks->length, toks->string);
        } else { // the node is not a leaf. Print the node into the buffer
            //printf("printing branch to buffer\n");
            snp_size = snprintf(buffer, buffer_size, "%*s%s: rule id: %d, nchildren: %zu\n", (int)pair.size * PARSER_PRINT_TAB_SIZE, "", pair.node->_class->type_name, pair.node->rule->id, pair.node->nchildren);
            // increment the number of tabs and add the children in reverse order (pre-order traversal)
            pair.size++;
            size_t i = pair.node->nchildren;
            ASTNode * node = pair.node->child;
            if (node) {
                while (node->next) {
                    node = node->next;
                }
                while (node) {
                    pair.node = node;
                    st._class->push(&st, pair);
                }
            }
        }

        // update the buffer data based on how many characters were printed to the buffer
        if (snp_size >= 0) {
            if (snp_size < buffer_size) {
                buffer_size -= snp_size;
                buffer += snp_size;
                //printf("successfully updated buffer");
            } else if (snp_size < PARSER_PRINT_BUFFER_SIZE) {
                fprintf(stream, "%.*s", PARSER_PRINT_BUFFER_SIZE - buffer_size, print_buffer);
                fflush(stream);
                buffer = &print_buffer[0];
                buffer_size = PARSER_PRINT_BUFFER_SIZE;
            } else {
                fprintf(stream, "%.*sERROR: static buffer of insufficient size %d, need %d\n", PARSER_PRINT_BUFFER_SIZE - buffer_size, print_buffer, PARSER_PRINT_BUFFER_SIZE, snp_size);
                fflush(stream);
                buffer = &print_buffer[0];
                buffer_size = PARSER_PRINT_BUFFER_SIZE;
            }
        } else {
            fprintf(stream, "%.*s\nERROR: status %d retrieving node...aborting Parser_print_ast\n", PARSER_PRINT_BUFFER_SIZE - buffer_size, print_buffer, status);
            fflush(stream);
            goto print_ast_fail;
        }
        //printf("buffer contents: %.*s\n", PARSER_PRINT_BUFFER_SIZE - buffer_size, print_buffer);
    }

    // print remainder of buffer to stream
    fprintf(stream, "%.*s", PARSER_PRINT_BUFFER_SIZE - buffer_size, print_buffer);
    fflush(stream);

print_ast_fail:
    st._class->dest(&st);
    return status;
}

// use in e.g. WHITESPACE or comments
ASTNode * skip_token(Production * production, Parser * parser, ASTNode * node) {
    LOG_EVENT(&parser->logger, LOG_LEVEL_DEBUG, "DEBUG: %s - skipping node at line %u, col %u of length %zu\n", __func__, node->token_start->coords.line, node->token_start->coords.col, node->str_length);
    node = make_skip_node(node);
    DEBUG_ASSERT(is_skip_node(node), "ASSERT FAILURE: %s - node made skip node not registering as skip node\n", __func__);
    //printf("is_skip_node: %s\n", is_skip_node(node) ? "True" : "False");
    return node;
}

ASTNode * token_action(Production * token, Parser * parser, ASTNode * node) {
    if (node != &ASTNode_fail) {
        if (is_skip_node(node)) {
            LOG_EVENT(&parser->logger, LOG_LEVEL_TRACE, "TRACE: %s - skipping token generated at line %u, col %u of length %zu\n", __func__, node->token_start->coords.line, node->token_start->coords.col, node->str_length);
            parser->_class->skip_token(parser, node);
        } else {
            LOG_EVENT(&parser->logger, LOG_LEVEL_TRACE, "TRACE: %s - adding token generated at line %u, col %u of length %zu\n", __func__, node->token_start->coords.line, node->token_start->coords.col, node->str_length);
            parser->_class->add_token(parser, node);
        }
        // need to back up one token in order to ensure not erroneously moving the parser location forward
        //parser->_class->seek(parser, node->token_start);
        
    }
    return node;
}