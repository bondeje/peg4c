#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "peg4c/parser.h"
#include "peg4c/packrat_cache.h"

#define PARSER_NONE 0
#ifndef PARSER_LOGGER_BUFFER_SIZE
    #define PARSER_LOGGER_BUFFER_SIZE 1024
#endif
#define PARSER_PRINT_BUFFER_SIZE 4096
#ifndef PARSER_PRINT_TAB_SIZE 
    #define PARSER_PRINT_TAB_SIZE 4
#endif

// Forward declaration
void Parser_generate_new_token(Parser * self, size_t token_length, Token * cur);

struct ParserType Parser_class = {
    .parse = &Parser_parse,
    .tokenize = &Parser_tokenize,
};

/**
 * @brief allocate a new Parser instance
 * @param[in] token_rule the "Rule *" instance that is called to initiate the 
 *      the tokenizer/lexer to split input string into tokens
 * @param[in] root_rule the "Rule *" instance that initiates the parse upon
 *      its success stores the final node in ast
 * @param[in] flags to be used to control parsing
 */
Parser * Parser_new(Rule * rules[], rule_id_type nrules, rule_id_type token_rule, 
    rule_id_type root_rule, unsigned int flags) {
        
    Parser * parser = malloc(sizeof(Parser));
    if (!parser) {
        return NULL;
    }
    if (Parser_init(parser, rules, nrules, token_rule, root_rule, flags)) {
        free(parser);
        return NULL;
    }
    return parser;
}

/**
 * @brief initialize a Parser instance. Only call once per instance
 * @param[in] token_rule the "Rule *" instance that is called to initiate the 
 *      the tokenizer/lexer to split input string into tokens
 * @param[in] root_rule the "Rule *" instance that initiates the parse upon
 *      its success stores the final node in ast
 * @param[in] flags to be used to control parsing
 */
err_type Parser_init(Parser * self, Rule * rules[], rule_id_type nrules, 
    rule_id_type token_rule, rule_id_type root_rule, unsigned int flags) {

    self->_class = &Parser_class;
    self->rules = rules;
    if (-1 < token_rule && token_rule < nrules) {
        self->token_rule = rules[token_rule];
    } else {
        self->token_rule = NULL;
    }
    if (-1 < root_rule && root_rule < nrules) {
        self->root_rule = rules[root_rule];
    } else {
        self->root_rule = NULL;
    }  
    
    // initialize packrat memoization
    err_type status = PackratCache_init(&self->cache, nrules, PARSER_DEFAULT_NTOKENS); 
    if (status) {
        return status;
    }

    // initialize Token pool
    self->token_mgr = MemPoolManager_new(PARSER_DEFAULT_NTOKENS, sizeof(Token), 8);

    // generate sentinel head of Token list
    self->token_head = MemPoolManager_next(self->token_mgr);

    // generate sentinel tail for Token list. Never a valid token
    self->token_tail = MemPoolManager_next(self->token_mgr);

    // initialize token sentinels
    *self->token_head = (Token){.next = self->token_tail};
    *self->token_tail = (Token){
        .string = "", 
        .length = 0, 
        .prev = self->token_head, 
        .id = SIZE_MAX}; // this is a sentinel to allow tokenization
    self->token_cur = self->token_head;
    self->ntokens = 0;

    // initialize ASTNode pool
    self->node_mgr = MemPoolManager_new(PARSER_DEFAULT_NNODES, sizeof(ASTNode), 8);

    // initialize pool for ASTNode children arrays
    self->childarr_mgr = MemPoolManager_new(PARSER_DEFAULT_NNODES, sizeof(ASTNode*), 8);

    // generate and initialize fail node sentinel
    self->fail_node = MemPoolManager_next(self->node_mgr);
    *self->fail_node = (ASTNode) ASTNode_DEFAULT_INIT;
    self->fail_node->token_start = self->token_head;

    // ASTNode 
    self->ast = Parser_fail_node(self);

    // tokenizing and parsing flags
    self->tokenizing = false;
    self->flags = flags;

    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory in parser. Use directly if initialized 
 *      with Parser_inint
 */
void Parser_dest(Parser * self) {
    /* clear out the ASTNodes */
	if (self->node_mgr) {
	    MemPoolManager_del(self->node_mgr);
    	self->node_mgr = NULL;
	}
    self->ast = NULL;
    
    // clear the cache
    PackratCache_dest(&self->cache);

    /* clear the token list */
    self->token_head = NULL;
    self->token_cur = NULL;
    
    if (self->token_mgr) {
	    MemPoolManager_del(self->token_mgr);
    	self->token_mgr = NULL;
	}

    /* clear the child array manager */
	if (self->childarr_mgr) {
	    MemPoolManager_del(self->childarr_mgr);
    	self->childarr_mgr = NULL;
	}
}

/**
 * @brief destroy and delete a Parser created with Parser_new
 */
void Parser_del(Parser * self) {
    Parser_dest(self);
    free(self);
}

/**
 * @brief run the tokenizer on the input string. A linked list starting from 
 *      "start" to "end" (inclusive) is created
 * @param[in] self Parser instance
 * @param[in] string the input string to tokenize
 * @param[in] string_length the length of the input string
 * @param[out] start the resulting starting token
 * @param[out] end the resulting ending token (inclusive)
 * @returns 0 on success, 1 on failure
 */
int Parser_tokenize(Parser * self, char const * string, size_t string_length, 
    Token ** start, Token ** end) {

    int status = 0;

    static const unsigned int REMAINING_TOKEN_MAX_SIZE = 16;
    if (!string_length) {
        return status;
    }

    // set up current state memo for returning later
    bool tokenizing_orig = self->tokenizing;
    self->tokenizing = true; // assert in tokenizing start regardless of current state
    Token * reset = Parser_tell(self);

    // set up doubly-linked terminals
    Token * tail = &(Token){.string = "", .length = 0, .id = SIZE_MAX};
    Token * head = &(Token){.id = SIZE_MAX, .next = tail};
    tail->prev = head;
    head->next = tail;

    // generate a new Token encapsulating the new string and insert into doubly-linked-list
    Token * cur = MemPoolManager_next(self->token_mgr);
    Token_init(cur, self->ntokens, string, string_length, 1, 1);
    self->ntokens++; // this might be a bug. might have to reset the token count
    Token_insert_after(head, cur, cur);

    // seek to new token
    Parser_seek(self, cur);
    Rule * tokenizer = self->token_rule;
    *end = cur;
    /*
    Iterate until the current token has no string left to tokenize
    */
    while (cur && cur->length) {
        ASTNode * node = Rule_check(tokenizer, self);
        if (!node) { // this should never happen
            fprintf(stderr, "NULL node encountered in tokenizier\n");
            status = 1;
            break;
        }

        // check for failed node and handle
        if (Parser_is_fail_node(self, node)) {
            status = 1;
            break;
        }     

        // update current node for loop
        cur = Parser_tell(self);
        
    }
    cur = cur->prev;
    // cur points one after the last non-empty token

    // return to original state. Originally this was false, but it prevented 
    // re-entrant tokenization needed for preprocessing, e.g. C

    Parser_seek(self, reset);
    self->tokenizing = tokenizing_orig;
    *start = head->next;
    head->next->prev = NULL;
    //(*end)->next = NULL;
    *end = cur;
    cur->next = NULL;
    return status;
}

/**
 * @brief copy a token representation. Note that the id and links will still change they must be unique to a parser. 
 * @param[in] self Parser instance
 * @param[in] tok Token pointer that is to be copied
 * @returns a pointer to a new Token
 */ 
Token * Parser_copy_token(Parser * self, Token * tok) {
    Token * out = MemPoolManager_next(self->token_mgr);
    *out = *tok;
    out->prev = NULL;
    out->next = NULL;
    out->id = self->ntokens++;
    return out;
}

/**
 * @brief copy a token representation. Note that the id and links will still change they must be unique to a parser. 
 * @param[in] self Parser instance
 * @param[inout] start the start of the token chain to copy and the resulting copy
 * @param[inout] end the end of the token chain to copy and the resulting copy
 */ 
void Parser_copy_tokens(Parser * self, Token ** start, Token ** end) {
    Token * cur = *start;
    Token * s = Parser_copy_token(self, cur);
    Token * e = *end;
    Token * c = s;
    while (cur != e) {
        cur = cur->next;
        c->next = Parser_copy_token(self, cur);
        c->next->prev = c;
        c = c->next;
    }
    *start = s;
    *end = c;
}

/**
 * @brief wrapper to retrieve number of tokens in parser. This is a more 
 *      accurate measure of the current state of the parser tokens than 
 *      Parser.ntokens
 */
size_t Parser_get_ntokens(Parser * self) {
    Token * cur = self->token_head->next;
    size_t ntokens = 0;
    while (cur && cur != self->token_tail) {
        ntokens++;
        cur = cur->next;
    }
    return ntokens;
}

/**
 * @brief parse the raw Token string to determine the line and col at the end 
 *      of the Token. This is only used during tokenization
 * @param[in] self pointer to Parser
 * @param[in] tok pointer a token after which a string will be inserted
 * @param[out] line_out pointer to the line count at end of Token string
 * @param[out] col_out  pointer to the column count at end of token string
 */
void Parser_get_line_col_end(Parser * self, Token * tok, 
    unsigned int * line_out, unsigned int * col_out) {

    static const int newline = '\n';
    char const * start = tok->string;
    char const * end = tok->string + tok->length;
    unsigned int line = tok->coords.line;
    char const * chr = memchr(start, newline, tok->length);
    while (chr && chr < end) {
        // at every newline character, increment line count
        line++;
        start = chr + 1;
        chr = memchr(start, newline, 
            tok->length - (size_t)(start - tok->string));
    }
    // calculate the column count
    unsigned int col = (unsigned int) (end - start);
    if (line == tok->coords.line) {
        col += tok->coords.col;
    }
    if (!col) {
        col = 1;
    }
    *col_out = col;
    *line_out = line;
}

/**
 * @brief creates a new Token, initializes it with the input string, and 
 *      inserts it into the Token linked list
 * @param[in] self parser
 * @param[in] cur token after which the string should be inserted
 * @param[in] string the string to be added
 * @param[in] string_length the length of the string to be added
 */
void Parser_add_string(Parser * self, Token * cur, char const * string, 
    size_t string_length) {

    unsigned int line;
    unsigned int col;
    Parser_get_line_col_end(self, cur, &line, &col);

    // create and initialize a new token
    Token * new_token = MemPoolManager_next(self->token_mgr);
    if (!new_token) {
        // TODO: error code
    }
    Token_init(new_token, self->ntokens, string, string_length, line, col);
    self->ntokens++;

    // insert into linked list
    Token_insert_after(cur, new_token, new_token);
}

/**
 * @brief creates a new Token, initializes it with the input string, and 
 *      inserts it into the Token linked list. wraps Parser_add_string. 
 *      Used only during tokenization as "cur" contain the remainder of
 *      untokenized string
 * @param[in] self parser
 * @param[in] token_length the length of the token to be extracted from "cur"
 * @param[in] cur the token representing the remainder of untokenized string
 */
void Parser_generate_new_token(Parser * self, size_t token_length, Token * cur) {
    if (!cur) {
        cur = self->token_cur;
    }
    //assert(cur->length >= token_length)
    size_t length = cur->length - token_length;
    cur->length = token_length;
    // if there's any string left, add it as a new token to the end
    if (length) {    
        Parser_add_string(self, cur, cur->string + token_length, length);
    }
}

/**
 * @brief add a token for the given node. Only used during tokenization
 * @param[in] self Parser instance
 * @param[in] node node encapsulating the token to be added to the stream
 * @returns non-zero on error, else 0
 */
err_type Parser_add_token(Parser * self, ASTNode * node) {
    Parser_generate_new_token(self, node->str_length, node->token_start);
    Parser_seek(self, node->token_end->next);
    //self->token_tail->id = self->token_tail->prev->id + 1;
    return P4C_SUCCESS;
}

/**
 * @brief implements internal handling of skip_token functionality by
 *      registering the token and handling the string while NOT inserting it
 *      into the Token linked list for the parsing stage
 * @param[in] self parser
 * @param[in] node the node encompassing the token to be skipped. Must only 
 *      encompass one token.
 */
err_type Parser_skip_token(Parser * self, ASTNode * node) {
    if (node->str_length) {
        Token * skipped = node->token_start;
        Parser_generate_new_token(self, node->str_length, skipped);

        // extract the skipped token from the linked list of tokens
        if (skipped->prev) {
            skipped->prev->next = skipped->next;
        }    
        if (skipped->next) {
            skipped->next->prev = skipped->prev;
        }

        // reset parse to token after skipped
        Parser_seek(self, skipped->next);
    }
    return P4C_SUCCESS;
}

/**
 * @brief prints the tokens after tokenization in the parser
 */
void Parser_print_tokens(Parser * self, FILE * stream) {
    Token * tok = self->token_head->next;
    fprintf(stream, "ntokens: %zu - ", Parser_get_ntokens(self)/*self->ntokens*/);
    while (tok && tok->length) {
        // should replace with a print function
        fprintf(stream, "\"%.*s\" ", (int)tok->length, tok->string);
        tok = tok->next;
    }
    fprintf(stream, "\n");
    fflush(stream);
}

/**
 * @brief wrapper allocation of a new array of ASTNode * children for a new 
 *      ASTNode
 * @param[in] self the Parser
 * @param[in] nchildren the number of children to be in output array
 * @returns a pointer to ASTNode * that has at least as much space as 
 *      nchildren allocations of ASTNode *.
 */
/*
ASTNode ** Parser_make_child_array(Parser * self, size_t nchildren) {
    return MemPoolManager_malloc(self->childarr_mgr, 
        nchildren * sizeof(ASTNode *));
}
*/

/**
 * @brief initiate the parsing stage. wraps the tokenizing stage so a separate 
 *      call to Parser_tokenize is not necessary
 * @param[in] self Parser instance
 * @param[in] string the input string to parser
 * @param[in] string_length the length of the input string
 * @returns 0 on success, 1 on failure
 */
int Parser_parse(Parser * self, char const * string, size_t string_length) {
    // storage for start and end tokens
    Token * start;
    Token * end;

    // tokenize the input string
    int error = self->_class->tokenize(self, string, string_length, 
        &start, &end);

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

/**
 * @brief create a new node and add its chidlren to parse tree. For internal
 *      use or overriding purposes only.
 * @param[in] self Parser instance
 * @param[in] rule Rule * instance to associate to node for its creation
 * @param[in] start The start Token at which the Rule * or node succeeded
 * @param[in] end The end Token at which the Rule completes.
 *      For "nonempty" nodes that represent at least 1 Token, the end Token 
 *          should be reachable traversing to the "right"/"next" from the 
 *          start Token, but it can be equal to the start node
 *      For nodes that do not consume tokens (Lookahead rules or RepeatRule 
 *          with 0 minimum repeats), end should be equal to start->prev (so 
 *          that the next rule starts at end->next == start)
 * @param[in] str_length During tokenization, this represents how much of the
 *      input string is captured by the node. Otherwise it is used as a flag
 *      and may not have this meaning anymore. Best not to use it. If you 
 *      need to estimate how much of a string to allocate, follow the Token
 *      linked list through a node accountingfor the fact that 
 *      ASTNode.token_end may be equal to ASTNode.token_start or in the case
 *      of parsing, may actually be one before ASTNode.token_start
 * @param[in] nchildren the number of children for the node. The reason the 
 *      children are not passed in is frequently the Rule * is unaware of how
 *      many children the resulting node needs and therefore cannot allocate
 *      it. From my experience with the PackratCache, you must NOT try to work
 *      around this with a linked list; it will get corrupted. So the children
 *      for the new node are rebuilt using the cache and the "start" and "end"
 *      parameters above
 * @param[in] size This is used for the allocation size of the node. This 
 *      allows for the creation and memory management of custom nodes. To
 *      use the default ASTNode provided by peg4c, this value may be 0 or
 *      sizeof(ASTNode). For creating a custom node, the custom node MUST
 *      have an ASTNode instance as its firts member and pass the value
 *      sizeof(MyCustomASTNode). Any initializtion or overriding of variables
 *      must be done after this function call
 * @returns a new pointer to an ASTNode *.
 */
ASTNode * Parser_add_node(Parser * self, rule_id_type rule, Token * start, 
    Token * end, size_t str_length, size_t nchildren, size_t size) {

    if (!size) {
        size = sizeof(ASTNode);
    }
    // probably have to worry about alignment. Could just require that the 
    // object has the same alignment as ASTNode
    ASTNode * new_node = MemPoolManager_malloc(self->node_mgr, size); 
    // build children array
    ASTNode ** children = NULL;
    if (nchildren) {
        Token * tok = start;
        children = MemPoolManager_malloc(self->childarr_mgr, 
            nchildren * sizeof(ASTNode *));
        if (!children) {
            return NULL;
        }
        switch (self->rules[rule]->_class->type) {

            // build child array for ListRule
            case PEG_LIST: {
                rule_id_type id = ((DerivedRule *)self->rules[rule])->rule->id;
                rule_id_type delim_id = ((ListRule *)self->rules[rule])->delim->id;
                for (size_t i = 0; i < nchildren; i++) {
                    children[i] = Parser_check_cache(self, i & 1 ? delim_id : id, tok);
                    tok = children[i]->token_end->next;
                }
                break;
            }

            // build child array for SequenceRule
            case PEG_SEQUENCE: {
                Rule ** deps = ((ChainRule *)self->rules[rule])->deps;
                //assert(((ChainRule *)rule)->deps_size == nchildren);
                for (size_t i = 0; i < nchildren; i++) {
                    children[i] = Parser_check_cache(self, deps[i]->id, tok);
                    tok = children[i]->token_end->next;
                }
                break;
            }

            // build child array for Production, NegativeLookahead, 
            // PositiveLookahead, and RepeatRules (those that solely derive 
            // from DerivedRule)
            default: {
                rule_id_type id = ((DerivedRule *)self->rules[rule])->rule->id;
                for (size_t i = 0; i < nchildren; i++) {
                    children[i] = Parser_check_cache(self, id, tok);
                    tok = children[i]->token_end->next;
                }
            }
        }
        
    }
    
    ASTNode_init(new_node, rule, start, end, str_length, nchildren, children);
    return new_node;
}

/**
 * @brief check for a (Rule, Token) pair in the Parser's PackratCache
 * @param[in] self the Parser
 * @param[in] rule_id the integer ID of the Rule corresponding to the query
 *      usually this is Rule.id
 * @param[in] tok pointer to Token of the corresponding query
 * @returns The ASTNode at the (Rule, Token) key. NULL if not present
 */
ASTNode * Parser_check_cache(Parser * self, rule_id_type rule_id, Token * tok) {
    //if (tok == self->token_head || tok == self->token_tail) {
    if (tok == self->token_head) {
        return Parser_fail_node(self);
    }
    return PackratCache_get(&self->cache, rule_id, tok);
}

/**
 * @brief store a resulting ASTNode in the Parser's PackratCache
 * @param[in] self the Parser
 * @param[in] rule_id the integer ID of the Rule corresponding to the query
 *      usually this is Rule.id
 * @param[in] tok pointer to Token of the corresponding query
 * @param[in] node the ASTNode pointer to be stored
 */
void Parser_cache_check(Parser * self, rule_id_type rule_id, Token * tok, 
    ASTNode * node) {

    //if (tok != self->token_head && tok != self->token_tail) {
    if (tok != self->token_head) {
        PackratCache_set(&self->cache, self, rule_id, tok, node);
    }
    
}
err_type Parser_traverse(Parser * self, 
    void (*traverse_action)(void * ctxt, ASTNode * node), void * ctxt) {

    return P4C_NOT_IMPLEMENTED;
}
typedef struct ASTNodeSize {
    ASTNode * node;
    size_t index;
} ASTNodeSize;

#define ELEMENT_TYPE ASTNodeSize
#include "peg4c/stack.h"

/**
 * @brief print the available Abstract Syntax Tree to stream
 */
err_type Parser_print_ast(Parser * self, FILE * stream) {
    if (Parser_is_fail_node(self, self->ast)) {
        return P4C_SUCCESS;
    }
    if (!stream) {
        stream = stdout;
    }
    char print_buffer[PARSER_PRINT_BUFFER_SIZE];
    char * buffer = &print_buffer[0];
    int buffer_size = PARSER_PRINT_BUFFER_SIZE;

    // iterative inorder traversal of the AST
    STACK(ASTNodeSize) st;
    STACK_INIT(ASTNodeSize)(&st, 32);

    st._class->push(&st, (ASTNodeSize){.node = self->ast, .index = 0});
    err_type status = P4C_SUCCESS;
    while (st.fill) {  
        // pair is always the latest node put on the stack, unprocessed
        ASTNodeSize pair; 
        st._class->peek(&st, &pair);

        // the node is a token leaf in the AST tree. Print the node and token 
        // into the buffer
        if (!pair.node->nchildren && pair.node->str_length) { 
            Token * tok = pair.node->token_start;
            fprintf(stream, "%*s%s: rule id: %s, nchildren: %zu, token: %.*s\n", 
                (int)(st.fill - 1) * PARSER_PRINT_TAB_SIZE, "", 
                pair.node->_class->type_name, self->rules[pair.node->rule]->name, 
                pair.node->nchildren, (int)tok->length, tok->string);
        } else { // the node is not a leaf. Print the node into the buffer
            fprintf(stream, "%*s%s: rule id: %s, nchildren: %zu\n", 
                (int)(st.fill - 1) * PARSER_PRINT_TAB_SIZE, "", 
                pair.node->_class->type_name, self->rules[pair.node->rule]->name, 
                pair.node->nchildren);
        }

        while (st.fill && pair.index >= pair.node->nchildren) {
            st._class->pop(&st, NULL);
            st._class->peek(&st, &pair);
        }
        if (!st.fill) {
            break;
        }
        if (pair.index < pair.node->nchildren) {
            st.bins[st.fill - 1].index++; // yeah...need to do this
            st._class->push(&st, (ASTNodeSize){
                .index = 0, 
                .node = pair.node->children[pair.index]});
        }
    }

    fflush(stream);

print_ast_fail:
    st._class->dest(&st);
    return status;
}

/**
 * @brief get the status of a call to Parser_parse
 * @returns P4C_TOKENIZE_FAILURE, P4C_PARSER_FAILURE on corresponding errors
 *      or 0/P4C_SUCCESS on success
 */
err_type Parser_parse_status(Parser * parser) {
    return Parser_print_parse_status(parser, NULL);
}

/**
 * @brief get the status of a call to Parser_parse as well as print the AST and
 *      any error information to stream
 * @param[in] self the Parser
 * @param[in] stream the stream to print output to
 * @returns result of Parser_parse_status
 */
err_type Parser_print_parse_status(Parser * parser, FILE * stream) {
    ASTNode * root = parser->ast;
    if (!root) {
        fprintf(stream, "parser not run\n");
        return P4C_TOKENIZE_FAILURE;
    }
    // token at start of AST
    Token * start = root->token_start;
    // token at end of AST
    Token * end = root->token_end;

    // get the furthest token that where a rule failed
    Token * final = Parser_fail_node(parser)->token_start;

    // if an output is provided
    if (stream) {
        // print start token
        fprintf(stream, "parser captured from token: %.*s, line: %u, col: %u ",
            (int)start->length, start->string, start->coords.line, 
            start->coords.col);

        // if end token is available, print it
        if (end) {
            fprintf(stream, "to token: %.*s, line: %u, col: %u.\n",        
                (int)end->length, end->string, end->coords.line, end->coords.col);
        } else {
            fprintf(stream, "\n");
        }
    }
    
    // if final Token is valid, print it as where the likely failure occurred
    // checking for string excludes parser->token_head and possible NULL 
    // strings during parse
    if (final && final->string) { 
        if (stream) {
            fprintf(stream, 
                "Furthest failed rule %s starting at token: %.*s, line: %u, col: %u\n",
                parser->rules[Parser_fail_node(parser)->rule]->name,
                (int)final->length, final->string, final->coords.line, 
                final->coords.col);
        }
        return P4C_PARSE_FAILURE;
    }
    return P4C_SUCCESS;
}

/**
 * @brief the build action to be use by Rule instances that result in skipping 
 *      tokens during tokenization
 * @param[in] production The pointer to the Production instance that is 
 *      assigned skip_token build action
 * @param[in] parser the pointer to the running Parser instance
 * @param[in] node the resulting node of the production Rule's dependent rule
 * @returns On failure of the production or dependent rule, returns a node for 
 *      which Parser_is_fail_node(parser, return) evaluates to true or a node
 *      for which is_skip_token(return) evaluates to true. It should never 
 *      return NULL
 */
ASTNode * skip_token(Production * production, Parser * parser, ASTNode * node) {
    node = make_skip_node(node);
    //DEBUG_ASSERT(is_skip_node(node), 
    //    "ASSERT FAILURE: %s - node made skip node not registering as skip node\n", 
    //    __func__);
    return node;
}

/**
 * @brief the build action to be used only by the special "token" Production
 * @param[in] production The pointer to the Production instance that is 
 *      assigned skip_token build action
 * @param[in] parser the pointer to the running Parser instance
 * @param[in] node the resulting node of the production Rule's dependent rule
 * @returns On failure of the production or dependent rule, returns a node for 
 *      which Parser_is_fail_node(parser, return) evaluates to true or a valid
 *      node. It should never return NULL
 */
ASTNode * token_action(Production * token, Parser * parser, ASTNode * node) {
    if (!Parser_is_fail_node(parser, node)) {
        // if the token is a "skip node" skip the token
        if (is_skip_node(node)) {
            Parser_skip_token(parser, node);
        } else { // node is valid, create a new token in linked list
            Parser_add_token(parser, node);
        }        
    }
    return node; // return node as is
}

