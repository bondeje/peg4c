/* this file is auto-generated, do not modify */
#ifndef PEG4C_H
#define PEG4C_H

#include "peg4c/utils.h"
#include "peg4c/rule.h"
#include "peg4c/parser.h"

typedef enum peg4crule {
	PEG4C_WHITESPACE,
	PEG4C_WHITESPACE_RE,
	PEG4C_STRING_LITERAL,
	PEG4C_STRING_LITERAL_RE,
	PEG4C_REGEX_LITERAL,
	PEG4C_REGEX_LITERAL_RE,
	PEG4C_PUNCTUATOR,
	PEG4C_EXCLAIM,
	PEG4C_VBAR,
	PEG4C_COMMA,
	PEG4C_QUESTION,
	PEG4C_PERIOD,
	PEG4C_AMPERSAND,
	PEG4C_COLON,
	PEG4C_PLUS,
	PEG4C_ASTERISK,
	PEG4C_LPAREN,
	PEG4C_RPAREN,
	PEG4C_LBRACE,
	PEG4C_RBRACE,
	PEG4C_EQUALS,
	PEG4C_DIGIT_SEQ,
	PEG4C_DIGIT_SEQ_RE,
	PEG4C_KEYWORD,
	PEG4C_PUNCTUATOR_KW,
	PEG4C_KEYWORD_KW,
	PEG4C_TOKEN_KW,
	PEG4C_IDENTIFIER,
	PEG4C_IDENTIFIER_RE,
	PEG4C_TOKEN,
	PEG4C_CHOICE_7_30,
	PEG4C_NONWS_PRINTABLE,
	PEG4C_NONWS_PRINTABLE_RE,
	PEG4C_NONTERMINAL,
	PEG4C_TERMINAL,
	PEG4C_CHOICE_2_35,
	PEG4C_BASE_RULE,
	PEG4C_CHOICE_3_37,
	PEG4C_SEQ_3_38,
	PEG4C_LOOKAHEAD_RULE,
	PEG4C_SEQ_2_41,
	PEG4C_REP_0_1_42,
	PEG4C_CHOICE_2_43,
	PEG4C_LIST_RULE,
	PEG4C_LIST_2_45,
	PEG4C_REPEATED_RULE,
	PEG4C_SEQ_2_47,
	PEG4C_REP_0_1_48,
	PEG4C_CHOICE_4_49,
	PEG4C_SEQ_5_50,
	PEG4C_REP_0_1_51,
	PEG4C_REP_0_1_52,
	PEG4C_SEQUENCE,
	PEG4C_LIST_2_54,
	PEG4C_CHOICE,
	PEG4C_LIST_2_55,
	PEG4C_SPECIAL_PRODUCTION,
	PEG4C_CHOICE_2_57,
	PEG4C_SEQ_3_58,
	PEG4C_SEQ_3_59,
	PEG4C_CHOICE_2_60,
	PEG4C_LIST_2_61,
	PEG4C_TRANSFORM_FUNCTIONS,
	PEG4C_LIST_2_63,
	PEG4C_PRODUCTION,
	PEG4C_SEQ_4_65,
	PEG4C_REP_0_1_66,
	PEG4C_SEQ_3_67,
	PEG4C_CONFIG,
	PEG4C_SEQ_3_69,
	PEG4C_PEG4C,
	PEG4C_REP_1_0_71,
	PEG4C_CHOICE_3_72,
	PEG4C_NRULES
} peg4crule;

extern Rule * peg4crules[PEG4C_NRULES + 1];

void peg4c_dest(void);

#endif //PEG4C_H