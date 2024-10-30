#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // for Token_print only

#include "peg4c/utils.h"
//#include <peggy/type.h>
#include "peg4c/token.h"

void Token_init(Token * self, size_t id, char const * string, size_t length, unsigned int line, unsigned int col) {
    self->coords.col = col;
    self->coords.line = line;
    self->string = string;
    self->length = length;
    self->id = id;
}

// all tokens must be non-null, but some may be repeated
// note that if dstart is the head or dend is the tail, caller must handle them appropriately
// if dstart == dend->next, this works as an insert before dstart/after dend
void Token_replace_tokens(Token * dstart, Token * dend, Token * sstart, Token * send) {
    Token * tok = dstart->prev;
    sstart->prev = tok;
    if (tok) {
        tok->next = sstart;
    }

    tok = dend->next;
    send->next = tok;
    if (tok) {
        tok->prev = send;
    }
}

void Token_insert_before(Token * tok, Token * sstart, Token * send) {
    tok->prev->next = sstart;
    sstart->prev = tok->prev;
    tok->prev = send;
    send->next = tok;
}

// tokens must not be NULL
// if start or end are the head or tail, the caller must handle them appropriately
void Token_remove_tokens(Token * start, Token * end) {
    if (start->prev) {
        start->prev->next = end->next;
    }
    
    if (end->next) {
        end->next->prev = start->prev;
    }    
}

void Token_append(Token * tail, Token * next) {
    tail->next = next;
    next->prev = tail;
}
