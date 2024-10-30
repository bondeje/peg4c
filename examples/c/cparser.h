#ifndef CPARSER_H
#define CPARSER_H 

#include "peg4c/astnode.h"
#include "peg4c/parser.h"
#include "peg4c/rule.h"

#include "c.h"
#include "config.h"
#include "cpp.h"

ASTNode * nc1_pass0(Production * rule, Parser * parser, ASTNode * node);
ASTNode * c0nc0_pass1(Production * rule, Parser * parser, ASTNode * node);
ASTNode * simplify_binary_op(Production * binary_op, Parser * parser, ASTNode * node);
//ASTNode * b_to_end_of_line(Production * rule, Parser * parser, ASTNode * node);
ASTNode * _open_scope(Production * rule, Parser * parser, ASTNode * node);
ASTNode * _close_scope(Production * rule, Parser * parser, ASTNode * node);
ASTNode * c_process_declaration(Production * decl, Parser * parser, ASTNode * node);
ASTNode * c_process_declaration_specifiers(Production * decl_specs, Parser * parser, ASTNode * node);
ASTNode * c_check_typedef(Production * decl_specs, Parser * parser, ASTNode * node);
ASTNode * c_pp_lparen(Production * prod, Parser * parser, ASTNode * node);
ASTNode * c_pp_line_expand(Production * prod, Parser * parser, ASTNode * node);
//ASTNode * c_pp_identifier(Production * prod, Parser * parser, ASTNode * node);
//ASTNode * c_pp_is_defined(Production * prod, Parser * parser, ASTNode * node);

#endif
