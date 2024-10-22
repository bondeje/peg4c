// This must occur before any inclusion of the standard libraries headers
#ifdef __linux__
// this is needed for the regex of glibc
#define _GNU_SOURCE
#endif

/* C standard library includes  */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h> // for unhandled error printfs
#include <assert.h>

/* POSIX includes */
#ifdef _GNU_SOURCE
#include <regex.h>
#endif

/* lib includes */
#ifndef _GNU_SOURCE
// basically anywhere that does not have _GNU_SOURCE, which is apparently everywhere other than non-Android Linux
// regex alternative
#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif
#include "pcre2.h"
#endif

/* peg4c includes */
#include "peg4c/utils.h"
#include "peg4c/rule.h"
#include "peg4c/parser.h"
#include "peg4c/token.h"
#include "peg4c/hash_map.h"

#define DEFAULT_RE_SYNTAX RE_SYNTAX_POSIX_EXTENDED | RE_BACKSLASH_ESCAPE_IN_LISTS | RE_DOT_NEWLINE

/* Rule implementations */

struct RuleType Rule_class = {
    .type = PEG_RULE,
    .new = &Rule_new,
    .init = &Rule_init,
    .dest = &Rule_dest, 
    .del = &Rule_del,
    .check_rule_ = &Rule_check_rule_,
};

/**
 * @brief allocate a new Rule
 * @param[in] id integer id for the instance of the Rule
 * @param[in] name a convenient name for printing the instance of the Rule
 * @returns pointer to the new Rule instance
 */
Rule * Rule_new(rule_id_type id, char const * name) {
    Rule * ret = malloc(sizeof(Rule));
    if (!ret) {
        return NULL;
    }
    if (Rule_init(ret, id, name)) {
        free(ret);
        return NULL;
    }
    return ret;
}

/**
 * @brief initialize a Rule
 * @param[in] self pointer to Rule
 * @param[in] id integer id for the instance of the Rule
 * @param[in] name a convenient name for printing the instance of the Rule
 * @returns non-zero on error, else 0
 */
err_type Rule_init(Rule * self, rule_id_type id, char const * name) {
    self->_class = &Rule_class;
    self->id = id;
    self->name = name;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the Rule instance
 */
void Rule_dest(Rule * self) {
    /* no-op */
}

/**
 * @brief destroy and delete the Rule instance
 */
void Rule_del(Rule * self) {
    Rule_dest(self);
    free(self);
}

/**
 * @brief dummy function. do not implement
 */
ASTNode * Rule_check_rule_(Rule * self, Parser * parser) {
    return NULL;
}

/**
 * @brief The master function that evaluates any Rule *
 * @param[in] self the Rule to be evaluated
 * @param[in] parser the Parser being executed
 * @returns On failure returns a node where Parser_is_fail_node(parser, return)
 *      evaluates to true else a valid ASTNode
 */
ASTNode * Rule_check(Rule * self, Parser * parser) {
    // save reference to current position in Token linked list
    Token * tok = Parser_tell(parser);
    // check for a bad Token
    if (!tok) {
        return Parser_fail_node(parser);
    }
    ASTNode * res = NULL;

    // if lexer is active disable cache checking...it will always fail
    if (!parser->tokenizing) {
        res = Parser_check_cache(parser, self->id, tok);
    }

    // if check cache succeeds
    if (res) {
        // if node is not a failure
        if (!Parser_is_fail_node(parser, res)) {
            // seek ahead to the next unparsed Token
            Parser_seek(parser, res->token_end->next);
        }
        return res;
    }

    // evaluate the current rule
    res = self->_class->check_rule_(self, parser);

    // if rule failed..
    if (Parser_is_fail_node(parser, res)) {        
        // ensure we reset to the Token at the start of the evaluation
        Parser_seek(parser, tok);

        // log a failure to the parser
        Parser_fail(parser, self->id);
    }

    // record the result in the cache
    /*
     * DO NOT change self->id to res->rule. The whole point of the packrat is 
     * to be able to pull/push to cache for a single production so that every 
     * time it comes up, you are not recomputing. You may want to allow a 
     * check_rule_ to create ASTNode's for rules that were not executed to 
     * allow more freedom, but then you completely loose the cache performance 
     * as the next call to the self->id rule will have to recomput the result, 
     * recreate it, and will again store it where it doesn't belong. You cannot 
     * access the cache because it would have to know you are creating a rule 
     * different from the currently executing one. DO NOT DO THIS
    */
    Parser_cache_check(parser, self->id, tok, res);

    return res;
}

/* ChainRule implementations */

#define ChainRule_NAME "ChainRule.Rule"

struct ChainRuleType ChainRule_class = {
    .Rule_class = {
        .type = PEG_CHAIN,
        .new = &Rule_new,
        .init = &Rule_init,
        .dest = &Rule_dest,
        .del = &ChainRule_as_Rule_del,
        .check_rule_ = &Rule_check_rule_,
    },
    .new = &ChainRule_new,
    .init = &ChainRule_init,
    .dest = &ChainRule_dest,
    .del = &ChainRule_del
};


/**
 * @brief allocate a new ChainRule
 * @param[in] id integer id for the instance of the ChainRule
 * @param[in] name a convenient name for printing the instance of the ChainRule
 * @param[in] deps_size the number of subrule dependencies of the ChainRule
 * @param[in] deps array of Rule * of length "deps_size"
 * @returns pointer to the new ChainRule instance
 */
ChainRule * ChainRule_new(rule_id_type id, char const * name, size_t deps_size, 
    Rule ** deps) {

    ChainRule * ret = malloc(sizeof(ChainRule));
    if (!ret) {
        return NULL;
    }
    if (ChainRule_init(ret, id, name, deps_size, deps)) {
        free(ret);
        return NULL;
    }
    return ret;
}

/**
 * @brief initialize a new ChainRule
 * @param[in] self the ChainRule to initialize
 * @param[in] id integer id for the instance of the ChainRule
 * @param[in] name a convenient name for printing the instance of the ChainRule
 * @param[in] deps_size the number of subrule dependencies of the ChainRule
 * @param[in] deps array of Rule * of length "deps_size"
 * @returns non-zero on failure else 0
 */
err_type ChainRule_init(ChainRule * self, rule_id_type id, char const * name, 
    size_t deps_size, Rule ** deps) {

    Rule_init((Rule *)self, id, name);
    ((Rule *)self)->_class = (RuleType *)&ChainRule_class;
    self->_class = &ChainRule_class;
    self->deps_size = deps_size;
    self->deps = deps;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the ChainRule instance
 */
void ChainRule_dest(ChainRule * self) {
    Rule_dest(&(self->Rule));
}

/**
 * @brief destroy and delete the ChainRule instance
 */
void ChainRule_del(ChainRule * self) {
    ChainRule_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the ChainRule instance passed as Rule *
 */
void ChainRule_as_Rule_del(Rule * chain_rule) {
    ChainRule_del((ChainRule *)chain_rule);
}

/* SequenceRule implementations */

#define SequenceRule_NAME "SequenceRule.ChainRule.Rule"

struct SequenceRuleType SequenceRule_class = {
    .ChainRule_class = {
        .Rule_class = {
            .type = PEG_SEQUENCE,
            .new = &Rule_new,
            .init = &Rule_init,
            .dest = &Rule_dest,
            .del = &SequenceRule_as_Rule_del,
            .check_rule_ = &SequenceRule_check_rule_,
        },
        .new = &ChainRule_new,
        .init = &ChainRule_init,
        .dest = &ChainRule_dest,
        .del = &SequenceRule_as_ChainRule_del},
    .new = &SequenceRule_new,
    .init = &SequenceRule_init,
    .dest = &SequenceRule_dest,
    .del = &SequenceRule_del
};

/**
 * @brief allocate a new SequenceRule
 * @param[in] id integer id for the instance of the SequenceRule
 * @param[in] name a convenient name for printing the instance of the 
 *      SequenceRule
 * @param[in] deps_size the number of subrule dependencies of the SequenceRule
 * @param[in] deps array of Rule * of length "deps_size"
 * @returns pointer to the new SequenceRule instance
 */
SequenceRule * SequenceRule_new(rule_id_type id, char const * name, 
    size_t deps_size, Rule ** deps) {

    SequenceRule * ret = malloc(sizeof(SequenceRule));
    if (!ret) {
        return NULL;
    }
    if (SequenceRule_init(ret, id, name, deps_size, deps)) {
        free(ret);
        return NULL;
    }
    return ret;
}

/**
 * @brief initialize a new SequenceRule
 * @param[in] self the SequenceRule to initialize
 * @param[in] id integer id for the instance of the SequenceRule
 * @param[in] name a convenient name for printing the instance of the 
 *      SequenceRule
 * @param[in] deps_size the number of subrule dependencies of the SequenceRule
 * @param[in] deps array of Rule * of length "deps_size"
 * @returns non-zero on failure else 0
 */
err_type SequenceRule_init(SequenceRule * self, rule_id_type id, 
    char const * name, size_t deps_size, Rule ** deps) {

    ChainRule_init((ChainRule *)self, id, name, deps_size, deps);
    ((Rule *)self)->_class = (RuleType *)&SequenceRule_class;
    self->_class = &SequenceRule_class;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the SequenceRule instance
 */
void SequenceRule_dest(SequenceRule * self) {
    ChainRule_dest(&(self->ChainRule));
}

/**
 * @brief destroy and delete the SequenceRule instance
 */
void SequenceRule_del(SequenceRule * self) {
    SequenceRule_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the SequenceRule instance passed as ChainRule
 */
void SequenceRule_as_ChainRule_del(ChainRule * sequence_rule) {
    SequenceRule_del((SequenceRule *)sequence_rule);
}

/**
 * @brief destroy and delete the SequenceRule instance passed as Rule
 */
void SequenceRule_as_Rule_del(Rule * sequence_rule) {
    SequenceRule_del((SequenceRule *)sequence_rule);
}

/**
 * @brief evaluates the specified SequenceRule
 */
ASTNode * SequenceRule_check_rule_(Rule * sequence_rule, Parser * parser) {
    // mark starting Token in case we have to seek back
    Token * start = Parser_tell(parser);
    if (!start->length) {
        return Parser_fail_node(parser);
    }
    SequenceRule * self = (SequenceRule *)sequence_rule;

    // iterate through the dependent subrules
    size_t i = 0;
    for (; i < self->ChainRule.deps_size; i++) {
        ASTNode * child_res = Rule_check(self->ChainRule.deps[i], parser);
        // if any subrules fail, return the fail node
        if (Parser_is_fail_node(parser, child_res)) {
            return child_res;
        }
    }

    Token * end = Parser_tell(parser);

    // create a successful new node
    return Parser_add_node(parser, sequence_rule->id, start, end->prev, 
        (size_t)((char *)end->string - (char *)start->string), i, 0);
}

/* ChoiceRule implementations */

#define ChoiceRule_NAME "ChoiceRule.ChainRule.Rule"

struct ChoiceRuleType ChoiceRule_class = {
    .ChainRule_class = {
        .Rule_class = {
            .type = PEG_CHOICE,
            .new = &Rule_new,
            .init = &Rule_init,
            .dest = &Rule_dest,
            .del = &ChoiceRule_as_Rule_del,
            .check_rule_ = &ChoiceRule_check_rule_,
        },
        .new = &ChainRule_new,
        .init = &ChainRule_init,
        .dest = &ChainRule_dest,
        .del = &ChoiceRule_as_ChainRule_del
    },
    .new = &ChoiceRule_new,
    .init = &ChoiceRule_init,
    .dest = &ChoiceRule_dest,
    .del = &ChoiceRule_del
};

/**
 * @brief allocate a new ChoiceRule
 * @param[in] id integer id for the instance of the ChoiceRule
 * @param[in] name a convenient name for printing the instance of the 
 *      ChoiceRule
 * @param[in] deps_size the number of subrule dependencies of the ChoiceRule
 * @param[in] deps array of Rule * of length "deps_size"
 * @returns pointer to the new ChoiceRule instance
 */
ChoiceRule * ChoiceRule_new(rule_id_type id, char const * name, 
    size_t deps_size, Rule ** deps) {

    ChoiceRule * ret = malloc(sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    if (ChoiceRule_init(ret, id, name, deps_size, deps)) {
        free(ret);
        return NULL;
    }
    return ret;
}

/**
 * @brief initialize a new ChoiceRule
 * @param[in] self the ChoiceRule to initialize
 * @param[in] id integer id for the instance of the ChoiceRule
 * @param[in] name a convenient name for printing the instance of the 
 *      ChoiceRule
 * @param[in] deps_size the number of subrule dependencies of the ChoiceRule
 * @param[in] deps array of Rule * of length "deps_size"
 * @returns non-zero on failure else 0
 */
err_type ChoiceRule_init(ChoiceRule * self, rule_id_type id, char const * name, 
    size_t deps_size, Rule ** deps) {

    ChainRule_init((ChainRule *)self, id, name, deps_size, deps);
    ((Rule *)self)->_class = (RuleType *)&ChoiceRule_class;
    self->_class = &ChoiceRule_class;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the ChoiceRule instance
 */
void ChoiceRule_dest(ChoiceRule * self) {
    ChainRule_dest(&(self->ChainRule));
}

/**
 * @brief destroy and delete the ChoiceRule instance
 */
void ChoiceRule_del(ChoiceRule * self) {
    ChoiceRule_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the ChoiceRule instance passed as ChainRule
 */
void ChoiceRule_as_ChainRule_del(ChainRule * choice_rule) {
    ChoiceRule_del((ChoiceRule *)choice_rule);
}

/**
 * @brief destroy and delete the ChoiceRule instance passed as Rule
 */
void ChoiceRule_as_Rule_del(Rule * choice_rule) {
    ChoiceRule_del((ChoiceRule *)choice_rule);
}

/**
 * @brief evaluates the specified ChoiceRule
 */
ASTNode * ChoiceRule_check_rule_(Rule * choice_rule, Parser * parser) {
    if (!Parser_tell(parser)->length) {
        return Parser_fail_node(parser);
    }
    ChoiceRule * self = (ChoiceRule *)choice_rule;
    for (size_t i = 0; i < self->ChainRule.deps_size; i++) {
        ASTNode * child_res = Rule_check(self->ChainRule.deps[i], parser);
        if (!Parser_is_fail_node(parser, child_res)) {
            return child_res;
        }
    }
    return Parser_fail_node(parser);
}

/* LiteralRule implementations */

#define LiteralRule_NAME "LiteralRule.Rule"

struct LiteralRuleType LiteralRule_class = {
    .Rule_class = {
        .type = PEG_LITERAL,
        .new = &Rule_new,
        .init = &Rule_init,
        .dest = &LiteralRule_as_Rule_dest,
        .del = &LiteralRule_as_Rule_del,
        .check_rule_ = &LiteralRule_check_rule_,
    },
    .new = &LiteralRule_new,
    .init = &LiteralRule_init,
    .dest = &LiteralRule_dest,
    .del = &LiteralRule_del,
    .compile = &LiteralRule_compile_regex
};

/**
 * @brief allocate a new LiteralRule
 * @param[in] id integer id for the instance of the LiteralRule
 * @param[in] name a convenient name for printing the instance of the 
 *      LiteralRule
 * @param[in] regex_s the regex string to be applied
 * @returns pointer to the new LiteralRule instance
 */
LiteralRule * LiteralRule_new(rule_id_type id, char const * name, 
    char const * regex_s) {

    LiteralRule * ret = malloc(sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    if (LiteralRule_init(ret, id, name, regex_s)) {
        free(ret);
        ret = NULL;
    }
    return ret;
}

#ifndef _GNU_SOURCE
PCRE2_SIZE pcre2_err_offset;
int pcre2_err_code;
#endif

/**
 * @brief compile the underlying regex for the LiteralRule
 */
err_type LiteralRule_compile_regex(LiteralRule * self) {
#ifdef _GNU_SOURCE
    re_set_syntax(DEFAULT_RE_SYNTAX);
    char const * err_message = re_compile_pattern(self->regex_s, 
        strlen(self->regex_s), &self->regex);
    if (err_message) {
        return P4C_REGEX_FAILURE;
    }
#else 
    self->regex = pcre2_compile(self->regex_s, 
        PCRE2_ZERO_TERMINATED,
        PCRE2_ANCHORED /* must search only at start of string */ 
            | PCRE2_DOTALL /* .* matches newlines */,
        &pcre2_err_code,    /* for error number */
        &pcre2_err_offset,  /* for error offset */
        NULL);              /* use default compile context */
    if (!self->regex) {
        return P4C_REGEX_FAILURE;
    }
    // only include the first match
    self->match = pcre2_match_data_create(1, NULL); 
#endif    
    self->compiled = true;
    return P4C_SUCCESS;
}

/**
 * @brief initialize a LiteralRule
 * @param[in] self pointer to LiteralRule
 * @param[in] id integer id for the instance of the LiteralRule
 * @param[in] name a convenient name for printing the instance of the 
 *      LiteralRule
 * @param[in] regex_s the regex string to be applied
 * @returns non-zero on error, else 0
 */
err_type LiteralRule_init(LiteralRule * self, rule_id_type id, 
    char const * name, char const * regex_s) {

    Rule_init((Rule *)self, id, name);
    ((Rule *)self)->_class = (RuleType *)&LiteralRule_class;
    self->_class = &LiteralRule_class;
#ifndef _GNU_SOURCE
    self->regex_s = (PCRE2_SPTR)regex_s;
#else
    self->regex_s = regex_s;
#endif
    self->compiled = false;
    if (regex_s) {
        LiteralRule_compile_regex(self);
    }
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the LiteralRule instance
 */
void LiteralRule_dest(LiteralRule * self) {
#ifdef _GNU_SOURCE
    regfree(&(self->regex));
#else
    pcre2_code_free(self->regex);
    pcre2_match_data_free(self->match);
#endif
    self->compiled = false;
    Rule_dest(&(self->Rule));
}

/**
 * @brief destroy and delete the LiteralRule instance
 */
void LiteralRule_del(LiteralRule * self) {
    LiteralRule_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the LiteralRule instance passed as Rule *
 */
void LiteralRule_as_Rule_del(Rule * literal_rule) {
    LiteralRule_del((LiteralRule *)literal_rule);
}

void LiteralRule_as_Rule_dest(Rule * literal_rule) {
    LiteralRule_dest((LiteralRule *)literal_rule);
}

/**
 * @brief evaluates the specified LiteralRule
 */
ASTNode * LiteralRule_check_rule_(Rule * literal_rule, Parser * parser) {
    // mark the Token at the current position in case reset is needed
    Token * tok = Parser_tell(parser);

    // don't even bother if length is 0
    if (!tok->length) {
        return Parser_fail_node(parser);
    }
    LiteralRule * self = (LiteralRule *)literal_rule;
    
    err_type status = P4C_SUCCESS;

    // compile the LiteralRule if it is not yet
    if (!self->compiled) {
        if ((status = LiteralRule_compile_regex(self))) {
            // failed to compile the regular expression
            return Parser_fail_node(parser); 
        }
    }
    
    int length = -1;
#ifdef _GNU_SOURCE
    length = re_match(&self->regex,
          tok->string, tok->length, 
          0, NULL);
#else 
    int rc = pcre2_match(
        self->regex,                /* the compiled pattern */
        (PCRE2_SPTR)tok->string,    /* the subject string */
        (PCRE2_SIZE)tok->length,    /* the length of the subject */
        0,                    /* start at offset 0 in the subject */
        0,                    /* default options. Don't need to enforce 
                                 PCRE2_ANCHORED when pattern was compiled as 
                                 such */
        self->match,          /* block for storing the result */
        NULL                  /* use default match context */
        );
    /*
    I think this means successful return. The official documentation does not
    say anything along the lines of "returns X on success, Y on failure", but 
    pcre2demo handles errors when rc < 0
    */
    if (rc >= 0) { 
        PCRE2_SIZE * result = pcre2_get_ovector_pointer(self->match);
        length = (result[1] - result[0]);
    }
#endif
    if (length > 0 && (parser->tokenizing || (size_t)length == tok->length)) {
        Parser_seek(parser, tok->next);

        return Parser_add_node(parser, literal_rule->id, tok, tok, length, 
            0, 0);
    }
    return Parser_fail_node(parser);
}

/* DerivedRule implementations */

#define DerivedRule_NAME "Rule.DerivedRule"

struct DerivedRuleType DerivedRule_class = {  
    .Rule_class = {
        .type = PEG_DERIVED,
        .new = &Rule_new,
        .init = &Rule_init,
        .dest = &Rule_dest,
        .del = &DerivedRule_as_Rule_del,
        .check_rule_ = &Rule_check_rule_,
    },
    .new = &DerivedRule_new,
    .init = &DerivedRule_init,
    .dest = &DerivedRule_dest,
    .del = &DerivedRule_del
};

/**
 * @brief allocate a new DerivedRule
 * @param[in] id integer id for the instance of the DerivedRule
 * @param[in] name a convenient name for printing the instance of the 
 *      DerivedRule
 * @param[in] rule the subrule dependency
 * @returns pointer to the new DerivedRule instance
 */
DerivedRule * DerivedRule_new(rule_id_type id, char const * name, Rule * rule) {
    DerivedRule * ret = malloc(sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    if (DerivedRule_init(ret, id, name, rule)) {
        free(ret);
        ret = NULL;
    }
    return ret;
}

/**
 * @brief initialize a new DerivedRule
 * @param[in] self the DerivedRule to initialize
 * @param[in] id integer id for the instance of the DerivedRule
 * @param[in] name a convenient name for printing the instance of the 
 *      DerivedRule
 * @param[in] rule the subrule dependency
 * @returns non-zero on failure else 0
 */
err_type DerivedRule_init(DerivedRule * self, rule_id_type id, char const * name, Rule * rule) {
    Rule_init((Rule *)self, id, name);
    ((Rule *)self)->_class = (RuleType *)&ListRule_class;
    self->_class = &DerivedRule_class;
    self->rule = rule;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the DerivedRule instance
 */
void DerivedRule_dest(DerivedRule * self) {
    Rule_dest((Rule *)self);
}

/**
 * @brief destroy and delete the DerivedRule instance
 */
void DerivedRule_del(DerivedRule * self) {
    DerivedRule_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the DerivedRule instance passed as Rule *
 */
void DerivedRule_as_Rule_del(Rule * derived_rule) {
    DerivedRule_del((DerivedRule *)derived_rule);
}

/* ListRule implementations */

#define ListRule_NAME "ListRule.DerivedRule.Rule"

struct ListRuleType ListRule_class = { 
    .DerivedRule_class = {
        .Rule_class = {
            .type = PEG_LIST,
            .new = &Rule_new,
            .init = &Rule_init,
            .dest = &Rule_dest,
            .del = &ListRule_as_Rule_del,
            .check_rule_ = &ListRule_check_rule_,
            },
        .new = &DerivedRule_new,
        .init = &DerivedRule_init,
        .dest = &DerivedRule_dest,
        .del = &ListRule_as_DerivedRule_del
    },
    .new = &ListRule_new,
    .init = &ListRule_init,
    .dest = &ListRule_dest,
    .del = &ListRule_del
};

/**
 * @brief allocate a new ListRule
 * @param[in] id integer id for the instance of the ListRule
 * @param[in] name a convenient name for printing the instance of the 
 *      ListRule
 * @param[in] rule the base rule which must succeed at least once for a ListRule
 * @param[in] delim the rule separating each successful evaluation of rule
 * @returns pointer to the new ListRule instance
 */
ListRule * ListRule_new(rule_id_type id, char const * name, Rule * rule, Rule * delim) {
    ListRule * ret = malloc(sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    if (ListRule_init(ret, id, name, rule, delim)) {
        free(ret);
        ret = NULL;
    }
    return ret;
}

/**
 * @brief allocate a new ListRule
 * @param[in] self ListRule pointer
 * @param[in] id integer id for the instance of the ListRule
 * @param[in] name a convenient name for printing the instance of the 
 *      ListRule
 * @param[in] rule the base rule which must succeed at least once for a 
 *      ListRule
 * @param[in] delim the rule separating each successful evaluation of rule
 * @returns non-zero on failure else 0
 */
err_type ListRule_init(ListRule * self, rule_id_type id, char const * name, 
    Rule * rule, Rule * delim) {
        
    DerivedRule_init((DerivedRule *)self, id, name, rule);
    ((Rule *)self)->_class = (RuleType *)&ListRule_class;
    ((DerivedRule *)self)->_class = (DerivedRuleType *)&ListRule_class;
    self->_class = &ListRule_class;
    self->delim = delim;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the ListRule instance
 */
void ListRule_dest(ListRule * self) {
    DerivedRule_dest(&(self->DerivedRule));
}

/**
 * @brief destroy and delete the ListRule instance
 */
void ListRule_del(ListRule * self) {
    ListRule_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the ListRule instance passed as DerivedRule
 */
void ListRule_as_DerivedRule_del(DerivedRule * list_rule) {
    ListRule_del((ListRule *)list_rule);
}

/**
 * @brief destroy and delete the ListRule instance passed as Rule
 */
void ListRule_as_Rule_del(Rule * list_rule) {
    ListRule_del((ListRule *)list_rule);
}

/**
 * @brief evaluates the specified ListRule
 */
ASTNode * ListRule_check_rule_(Rule * list_rule, Parser * parser) {

    // mark the current Token of the parser in case we have to reset
    Token * start = Parser_tell(parser);
    if (!start->length) {
        return Parser_fail_node(parser);
    }
    ListRule * self = (ListRule *) list_rule;

    // call alternating checks for the delimiter and base rule to count the 
    // number of children
    size_t nchildren = 0;
    Rule * derived_rule = ((DerivedRule *)self)->rule;
    Rule * delim_rule = self->delim;
    
    // check first derived_rule first
    ASTNode * node = Rule_check(derived_rule, parser);
    ASTNode * delim = Parser_fail_node(parser);

    // while node does not fail
    while (!Parser_is_fail_node(parser, node)) {
        // increment the number of children being track
        nchildren++;

        // check for delimiter
        delim = Rule_check(delim_rule, parser);
        if (Parser_is_fail_node(parser, delim)) {
            // if delimiter failed set node so that it exits loop
            node = Parser_fail_node(parser);
        } else {
            // if delimiter succeeded, increment number of children and check 
            // next node
            nchildren++;
            node = Rule_check(derived_rule, parser);
        }
    }
    // ListRule only fails if no children (initial node evaluation failed)
    if (!nchildren) {
        return Parser_fail_node(parser);
    }
    if (!Parser_is_fail_node(parser, delim)) {
        // if last delimiter check succeeded (but node failed so loop exited)
        // back out the delimiter token and decrement the number of children
        Parser_seek(parser, delim->token_start);
        nchildren--; 
    }

    Token * end = Parser_tell(parser);
    return Parser_add_node(parser, list_rule->id, start, end->prev, 
        (size_t)((char *)end->string - (char *)start->string), nchildren, 0);
}

/* RepeatRule implementations */

#define RepeatRule_NAME "RepeatRule.DerivedRule.Rule"

struct RepeatRuleType RepeatRule_class = {
    .DerivedRule_class = {
        .Rule_class = {
            .type = PEG_REPEAT,
            .new = &Rule_new,
            .init = &Rule_init,
            .dest = &Rule_dest,
            .del = &RepeatRule_as_Rule_del,
            .check_rule_ = &RepeatRule_check_rule_,
        },
        .new = &DerivedRule_new,
        .init = &DerivedRule_init,
        .dest = &DerivedRule_dest,
        .del = &RepeatRule_as_DerivedRule_del
    },
    .new = &RepeatRule_new,
    .init = &RepeatRule_init,
    .dest = &RepeatRule_dest,
    .del = &RepeatRule_del
};

/**
 * @brief allocate a new RepeatRule
 * @param[in] id integer id for the instance of the RepeatRule
 * @param[in] name a convenient name for printing the instance of the 
 *      RepeatRule
 * @param[in] rule the base rule to be repeated
 * @param[in] min_rep the minimum number of repetitions required for success
 * @param[in] max_rep the maximum or infinite(0) number of repetions to capture
 * @returns pointer to the new RepeatRule instance
 */
RepeatRule * RepeatRule_new(rule_id_type id, char const * name, Rule * rule, 
    size_t min_rep, size_t max_rep) {

    RepeatRule * ret = malloc(sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    if (RepeatRule_init(ret, id, name, rule, min_rep, max_rep)) {
        free(ret);
        ret = NULL;
    }
    return ret;
}

/**
 * @brief allocate a new RepeatRule
 * @param[in] id integer id for the instance of the RepeatRule
 * @param[in] name a convenient name for printing the instance of the 
 *      RepeatRule
 * @param[in] rule the base rule to be repeated
 * @param[in] min_rep the minimum number of repetitions required for success
 * @param[in] max_rep the maximum or infinite(0) number of repetions to capture
 * @returns non-zero on failure else 0
 */
err_type RepeatRule_init(RepeatRule * self, rule_id_type id, char const * name, 
    Rule * rule, size_t min_rep, size_t max_rep) {

    DerivedRule_init((DerivedRule *)self, id, name, rule);
    ((Rule *)self)->_class = (RuleType *)&RepeatRule_class;
    ((DerivedRule *)self)->_class = (DerivedRuleType *)&RepeatRule_class;
    self->_class = &RepeatRule_class;
    self->min_rep = min_rep;
    self->max_rep = max_rep;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the RepeatRule instance
 */
void RepeatRule_dest(RepeatRule * self) {
    DerivedRule_dest(&(self->DerivedRule));
}

/**
 * @brief destroy and delete the RepeatRule instance
 */
void RepeatRule_del(RepeatRule * self) {
    RepeatRule_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the RepeatRule instance passed as DerivedRule
 */
void RepeatRule_as_DerivedRule_del(DerivedRule * repeat_rule) {
    RepeatRule_del((RepeatRule *)repeat_rule);
}

/**
 * @brief destroy and delete the RepeatRule instance passed as Rule
 */
void RepeatRule_as_Rule_del(Rule * repeat_rule) {
    RepeatRule_del((RepeatRule *)repeat_rule);
}

/**
 * @brief evaluates the specified RepeatRule
 */
ASTNode * RepeatRule_check_rule_(Rule * repeat_rule, Parser * parser) {
    // mark first token in case we have to reset
    Token * start = Parser_tell(parser);
    RepeatRule * self = (RepeatRule *)repeat_rule;

    // short-circuit if current Token is empty
    if (!start->length) {
        // if min_rep == 0, make a new node even if token is empty
        if (self->min_rep) {
            return Parser_fail_node(parser);
        } else {
            return Parser_add_node(parser, repeat_rule->id, start, 
                start->prev, 0, 0, 0);
        }
    }

    size_t nchildren = 0;
    Rule * derived_rule = ((DerivedRule *)self)->rule;

    // check first repetition
    ASTNode * node = Rule_check(derived_rule, parser);

    // while node is not a failure
    while (!Parser_is_fail_node(parser, node)) {
        nchildren++;

        // if we reach the max repetitions just exit (consumes them successfully)
        if (self->max_rep && nchildren == self->max_rep) {
            break;
        }
        node = Rule_check(derived_rule, parser);
    }

    // if outside of range [min_rep, max_rep] and max_rep is not infinite, fail
    if (nchildren < self->min_rep 
        || (self->max_rep && nchildren > self->max_rep)) {

        return Parser_fail_node(parser);
    }

    Token * end = Parser_tell(parser);
    // Success! create a new node
    return Parser_add_node(parser, repeat_rule->id, start, end->prev, 
        (size_t)((char *)end->string - (char *)start->string), nchildren, 0);
}

/* NegativeLookahead implementations */

#define NegativeLookahead_NAME "NegativeLookahead.DerivedRule.Rule"

struct NegativeLookaheadType NegativeLookahead_class = {
    .DerivedRule_class = {
        .Rule_class = {
            .type = PEG_NEGATIVELOOKAHEAD,
            .new = &Rule_new,
            .init = &Rule_init,
            .dest = &Rule_dest,
            .del = &NegativeLookahead_as_Rule_del,
            .check_rule_ = &NegativeLookahead_check_rule_,
        },
        .new = &DerivedRule_new,
        .init = &DerivedRule_init,
        .dest = &DerivedRule_dest,
        .del = &NegativeLookahead_as_DerivedRule_del
    },
    .new = &NegativeLookahead_new,
    .init = &NegativeLookahead_init,
    .dest = &NegativeLookahead_dest,
    .del = &NegativeLookahead_del
};

/**
 * @brief allocate a new NegativeLookahead
 * @param[in] id integer id for the instance of the NegativeLookahead
 * @param[in] name a convenient name for printing the instance of the 
 *      NegativeLookahead
 * @param[in] rule the base rule to look ahead for
 * @returns pointer to the new NegativeLookahead instance
 */
NegativeLookahead * NegativeLookahead_new(rule_id_type id, char const * name, 
    Rule * rule) {

    NegativeLookahead * ret = malloc(sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    if (NegativeLookahead_init(ret, id, name, rule)) {
        free(ret);
        ret = NULL;
    }
    return ret;
}

/**
 * @brief allocate a new NegativeLookahead
 * @param[in] self the NegativeLookahead rule
 * @param[in] id integer id for the instance of the NegativeLookahead
 * @param[in] name a convenient name for printing the instance of the 
 *      NegativeLookahead
 * @param[in] rule the base rule to look ahead for
 * @returns non-zero on failure, else 0
 */
err_type NegativeLookahead_init(NegativeLookahead * self, rule_id_type id, 
    char const * name, Rule * rule) {

    DerivedRule_init((DerivedRule *)self, id, name, rule);
    ((Rule *)self)->_class = (RuleType *)&NegativeLookahead_class;
    ((DerivedRule *)self)->_class = (DerivedRuleType *)&NegativeLookahead_class;
    self->_class = &NegativeLookahead_class;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the NegativeLookahead instance
 */
void NegativeLookahead_dest(NegativeLookahead * self) {
    DerivedRule_dest(&(self->DerivedRule));
}

/**
 * @brief destroy and delete the NegativeLookahead instance
 */
void NegativeLookahead_del(NegativeLookahead * self) {
    NegativeLookahead_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the NegativeLookahead instance passed as DerivedRule
 */
void NegativeLookahead_as_DerivedRule_del(DerivedRule * negative_lookahead) {
    NegativeLookahead_del((NegativeLookahead *)negative_lookahead);
}

/**
 * @brief destroy and delete the NegativeLookahead instance passed as Rule
 */
void NegativeLookahead_as_Rule_del(Rule * negative_lookahead) {
    NegativeLookahead_del((NegativeLookahead *)negative_lookahead);
}

/**
 * @brief evaluates the specified NegativeLookahead
 */
ASTNode * NegativeLookahead_check_rule_(Rule * negative_lookahead, 
    Parser * parser) {

    // mark first Token in case we have to reset
    Token * tok = Parser_tell(parser);
    NegativeLookahead * self = (NegativeLookahead *)negative_lookahead;
    
    // if no string in Token, succeed
    if (!tok->length) {
        return Parser_add_node(parser, negative_lookahead->id, tok, tok->prev, 0, 
            0, 0);
    }
    
    ASTNode * node = Rule_check(self->DerivedRule.rule, parser);
    // if node fails, reset and create new node
    if (Parser_is_fail_node(parser, node)) {
        Parser_seek(parser, tok);
        return Parser_add_node(parser, negative_lookahead->id, tok, tok->prev, 0, 
            0, 0);
    }

    // if node is not failure, return a failed node. Don't have to reset 
    // because the failure from Rule_check will have reset already
    return Parser_fail_node(parser);
}
/* PositiveLookahead implementations */

#define PositiveLookahead_NAME "PositiveLookahead.DerivedRule.Rule"

struct PositiveLookaheadType PositiveLookahead_class = {
    .DerivedRule_class = {
        .Rule_class = {
            .type = PEG_POSITIVELOOKAHEAD,
            .new = &Rule_new,
            .init = &Rule_init,
            .dest = &Rule_dest,
            .del = &PositiveLookahead_as_Rule_del,
            .check_rule_ = &PositiveLookahead_check_rule_,
        },
        .new = &DerivedRule_new,
        .init = &DerivedRule_init,
        .dest = &DerivedRule_dest,
        .del = &PositiveLookahead_as_DerivedRule_del
    },
    .new = &PositiveLookahead_new,
    .init = &PositiveLookahead_init,
    .dest = &PositiveLookahead_dest,
    .del = &PositiveLookahead_del
};

/**
 * @brief allocate a new PositiveLookahead
 * @param[in] id integer id for the instance of the PositiveLookahead
 * @param[in] name a convenient name for printing the instance of the 
 *      PositiveLookahead
 * @param[in] rule the base rule to look ahead for
 * @returns pointer to the new PositiveLookahead instance
 */
PositiveLookahead * PositiveLookahead_new(rule_id_type id, char const * name, 
    Rule * rule) {

    PositiveLookahead * ret = malloc(sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    if (PositiveLookahead_init(ret, id, name, rule)) {
        free(ret);
        ret = NULL;
    }
    return ret;
}

/**
 * @brief allocate a new PositiveLookahead
 * @param[in] self the PositiveLookahead rule
 * @param[in] id integer id for the instance of the PositiveLookahead
 * @param[in] name a convenient name for printing the instance of the 
 *      PositiveLookahead
 * @param[in] rule the base rule to look ahead for
 * @returns non-zero on failure, else 0
 */
err_type PositiveLookahead_init(PositiveLookahead * self, rule_id_type id, 
    char const * name, Rule * rule) {

    DerivedRule_init((DerivedRule *)self, id, name, rule);
    ((Rule *)self)->_class = (RuleType *)&PositiveLookahead_class;
    ((DerivedRule *)self)->_class = (DerivedRuleType *)&PositiveLookahead_class;
    self->_class = &PositiveLookahead_class;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the PositiveLookahead instance
 */
void PositiveLookahead_dest(PositiveLookahead * self) {
    DerivedRule_dest(&(self->DerivedRule));
}

/**
 * @brief destroy and delete the PositiveLookahead instance
 */
void PositiveLookahead_del(PositiveLookahead * self) {
    PositiveLookahead_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the PositiveLookahead instance passed as DerivedRule
 */
void PositiveLookahead_as_DerivedRule_del(DerivedRule * positive_lookahead) {
    PositiveLookahead_del((PositiveLookahead *)positive_lookahead);
}

/**
 * @brief destroy and delete the PositiveLookahead instance passed as Rule
 */
void PositiveLookahead_as_Rule_del(Rule * positive_lookahead) {
    PositiveLookahead_del((PositiveLookahead *)positive_lookahead);
}

/**
 * @brief evaluates the specified PositiveLookahead
 */
ASTNode * PositiveLookahead_check_rule_(Rule * positive_lookahead, 
    Parser * parser) {

    // mark the current token for reset
    Token * tok = Parser_tell(parser);
    PositiveLookahead * self = (PositiveLookahead *)positive_lookahead;

    // check DerivedRule.rule
    ASTNode * node = Rule_check(self->DerivedRule.rule, parser);
    if (!Parser_is_fail_node(parser, node)) {
        // if node is success, reset the parser to tok and create a node
        Parser_seek(parser, tok);
        return Parser_add_node(parser, positive_lookahead->id, tok->prev, tok->prev, 
            0, 0, 0);
    }
    return node;
}

/* Production implementations */

#define Production_NAME "Production.DerivedRule.Rule"

RuleTypeID const PRODUCTION_UNESCAPED_RULES[3] = {
    PEG_PRODUCTION,
    PEG_LITERAL,
    PEG_NOTRULE
};

ASTNode * build_action_default(Production * production, Parser * parser, 
    ASTNode * node) {

    // check for skip node (do nothing)
    if (!is_skip_node(node)) {
         
        if (!isinstance(parser->rules[node->rule]->_class->type, PRODUCTION_UNESCAPED_RULES)) {
            node->rule = ((Rule *)production)->id;
        } else {
            ASTNode * child = node;
            node = Parser_add_node(parser, ((Rule *)production)->id, node->token_start, node->token_end, node->str_length, 1, 0);
            // assign original node as child. This should be unnecessary and might be a bug
            node->children[0] = child;
        }
    }
    return node;
}

struct ProductionType Production_class = {  
    .DerivedRule_class = {
        .Rule_class = {
            .type = PEG_PRODUCTION,
            .new = &Rule_new,
            .init = &Rule_init,
            .dest = &Rule_dest,
            .del = &Production_as_Rule_del,
            .check_rule_ = &Production_check_rule_,
            },
        .new = &DerivedRule_new,
        .init = &DerivedRule_init,
        .dest = &DerivedRule_dest,
        .del = &Production_as_DerivedRule_del
    },
    .new = &Production_new,
    .init = &Production_init,
    .dest = &Production_dest,
    .del = &Production_del
};

/**
 * @brief allocate a new Production
 * @param[in] id integer id for the instance of the Production
 * @param[in] name a convenient name for printing the instance of the 
 *      Production
 * @param[in] rule the base rule to look ahead for
 * @param[in] build_action the build action function for the production
 *      @brief the default build action function for productions
 *      @param production the Production rule whose node is being built
 *      @param parser the executing parser
 *      @param node the dependent node of the production node being built
 *      @returns the newly built ASTNode
 * @returns pointer to the new Production instance
 */
Production * Production_new(rule_id_type id, char const * name, Rule * rule, build_action_ftype build_action) {
    Production * ret = malloc(sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    if (Production_init(ret, id, name, rule, build_action)) {
        free(ret);
        ret = NULL;
    }
    return ret;
}

/**
 * @brief intialize a new Production
 * @param[in] self the Production rule
 * @param[in] id integer id for the instance of the Production
 * @param[in] name a convenient name for printing the instance of the 
 *      Production
 * @param[in] rule the base rule to look ahead for
 * @param[in] build_action the build action function for the production
 *      @brief the default build action function for productions
 *      @param production the Production rule whose node is being built
 *      @param parser the executing parser
 *      @param node the dependent node of the production node being built
 *      @returns the newly built ASTNode
 * @returns non-zero on failure else 0
 */
err_type Production_init(Production * self, rule_id_type id, char const * name, Rule * rule, build_action_ftype build_action) {
    DerivedRule_init((DerivedRule *)self, id, name, rule);
    ((Rule *)self)->_class = (RuleType *)&Production_class;
    ((DerivedRule *)self)->_class = (DerivedRuleType *)&Production_class;
    self->_class = &Production_class;
    self->build_action = build_action;
    return P4C_SUCCESS;
}

/**
 * @brief destroy and reclaim memory of the Production instance
 */
void Production_dest(Production * self) {
    DerivedRule_dest((DerivedRule *)self);
}

/**
 * @brief destroy and delete the Production instance
 */
void Production_del(Production * self) {
    Production_dest(self);
    free(self);
}

/**
 * @brief destroy and delete the Production instance passed as DerivedRule
 */
void Production_as_DerivedRule_del(DerivedRule * production) {
    Production_del((Production *)production);
}

/**
 * @brief destroy and delete the Production instance passed as Rule
 */
void Production_as_Rule_del(Rule * production) {
    Production_del((Production *)production);
}

/**
 * @brief evaluates the specified Production. Does not advance parser any more
 *      than the subrule does
 */
ASTNode * Production_check_rule_(Rule * production, Parser * parser) {
    Production * self = (Production *)production;
    // check subrule
    ASTNode * node = Rule_check(self->DerivedRule.rule, parser);
    if (!Parser_is_fail_node(parser, node)) {
        /*
        only execute build_action if non-failure
        a build action can take a successful node and cause failure but not the
        other way around
        */
        return self->build_action(self, parser, node);
    }
    return node;
}

