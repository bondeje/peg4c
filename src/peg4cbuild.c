#include <stdio.h>

#include "peg4c/utils.h"
#include "peg4c/mempool.h"
#include "peg4cbuild.h"
#include "peg4cstring.h"
#include "peg4cparser.h"

P4CString get_rule_resolution(char const * type_name) {
    size_t N = strlen(type_name);
    char * first_period = strchr(type_name, '.');
    return (P4CString) {.len = N - (size_t)(first_period - type_name), .str = first_period};
}

int P4CProduction_write_arg(void * data, P4CString arg) {
    P4CParser * parser = (P4CParser *) data;
    fwrite(",\n\t", 1, 3, parser->source_file);
    P4CString_fwrite(arg, parser->source_file, PSFO_NONE);
    return 0;
}

// NOTE: will not handle octal, hex, or unicode escapes yet. ref: https://en.wikipedia.org/wiki/Escape_sequences_in_C
char * unescape_string(char const * buffer, size_t buffer_size, size_t * buf_out_size, MemPoolManager * str_mgr) {
    char * buf_out = MemPoolManager_malloc(str_mgr, buffer_size);
    size_t i = 0, j = 0;
    while (i < buffer_size - 1) {
        if (buffer[i] == '\\') {
            switch (buffer[i + 1]) {
                case 'a': {
                    buf_out[j++] = '\a';
                    i++;
                    break;
                }
                case 'b': {
                    buf_out[j++] = '\b';
                    i++;
                    break;
                }
                case 'f': {
                    buf_out[j++] = '\f';
                    i++;
                    break;
                }
                case 'n':  {
                    buf_out[j++] = '\n';
                    i++;
                    break;
                }
                case 'r': {
                    buf_out[j++] = '\r';
                    i++;
                    break;
                }
                case 't':  {
                    buf_out[j++] = '\t';
                    i++;
                    break;
                }
                case 'v':  {
                    buf_out[j++] = '\v';
                    i++;
                    break;
                }
                case '\'':  {
                    buf_out[j++] = '\'';
                    i++;
                    break;
                } 
                case '"': {
                    buf_out[j++] = '\"';
                    i++;
                    break;
                } 
                case '?': {
                    buf_out[j++] = '\?';
                    i++;
                    break;
                }
                case '\\': { // non-standard, but regex needs to keep the '\\' escaped
                    buf_out[j++] = buffer[i++];
                    break;
                }
                default: {
                    buf_out[j++] = buffer[i];
                }
            }
            i++;
        } else {
            buf_out[j++] = buffer[i++];
        }
    }
    if (i == buffer_size - 1) {
        buf_out[j++] = buffer[i++];
    }
    *buf_out_size = j;
    return buf_out;
}

// signature is so that it can be used by for_each in the hash map
int P4CProduction_define(void * parser_, P4CString name, P4CProduction prod) {
    P4CParser * parser = (P4CParser *)parser_;

    /* work here for generating static, compiled regex */

    char const * type_name_cstr = get_type_name(prod.type);
    P4CString type_main = {.len = strlen(type_name_cstr), .str = (char *)type_name_cstr};
    P4CString_fwrite(type_main, parser->source_file, PSFO_UPPER);

    fputc('(', parser->source_file);
    P4CString_fwrite(prod.identifier, parser->source_file, PSFO_NONE);
    fputc(',', parser->source_file);

    unsigned int offset = 0;
    
    P4CString_fwrite(prod.identifier, parser->source_file, PSFO_UPPER | PSFO_LOFFSET(offset));

    prod.args._class->for_each(&prod.args, &P4CProduction_write_arg, (void*)parser);

    fwrite(");\n", 1, strlen(");\n"), parser->source_file);

    return 0;
}

int build_export_rules_resolved(void * parser_, P4CString name, P4CProduction prod) {
    P4CParser * parser = (P4CParser *) parser_;
	fwrite("[", 1, 1, parser->source_file);
	P4CString_fwrite(prod.identifier, parser->source_file, PSFO_UPPER);

    size_t buf_len = (prod.identifier.len + 14);
    P4CString arg = {.str = MemPoolManager_malloc(parser->str_mgr, buf_len)};
    arg.len = snprintf(arg.str, buf_len, "] = (Rule *)&%.*s", (int)prod.identifier.len, prod.identifier.str);

    P4CString_fwrite(arg, parser->source_file, PSFO_NONE);
    char * buffer = ",\n\t";
    fwrite(buffer, 1, strlen(buffer), parser->source_file);

    return 0;
}

void build_export_rules(P4CParser * parser) {
    char const * buffer = NULL;
    FILE * file = parser->source_file;
    buffer = "\n\nRule * ";
    fwrite(buffer, 1, strlen(buffer), file);
    P4CString_fwrite(parser->export, file, PSFO_NONE);

    buffer = "rules[";
    fwrite(buffer, 1, strlen(buffer), file);
    P4CString_fwrite(parser->export, file, PSFO_UPPER);

    buffer = "_NRULES + 1] = {\n\t";
    fwrite(buffer, 1, strlen(buffer), file);

    parser->productions._class->for_each(&parser->productions, &build_export_rules_resolved, (void*)parser);

	fwrite("[", 1, 1, file);
	P4CString_fwrite(parser->export, file, PSFO_UPPER);
    buffer = "_NRULES] = NULL\n};\n\n";
    fwrite(buffer, 1, strlen(buffer), file);
    
}

void build_destructor(P4CParser * parser) {
    char const * buffer = NULL;
    FILE * file = parser->source_file;
    buffer = "void ";
    fwrite(buffer, 1, strlen(buffer), file);
    P4CString_fwrite(parser->export, file, PSFO_NONE);

    buffer = "_dest(void) {\n\tint i = 0;\n\twhile (";
    fwrite(buffer, 1, strlen(buffer), file);
    P4CString_fwrite(parser->export, file, PSFO_NONE);

    buffer = "rules[i]) {\n\t\t";
    fwrite(buffer, 1, strlen(buffer), file);
    P4CString_fwrite(parser->export, file, PSFO_NONE);

    buffer = "rules[i]->_class->dest(";
    fwrite(buffer, 1, strlen(buffer), file);
    P4CString_fwrite(parser->export, file, PSFO_NONE);

    buffer = "rules[i]);\n\t\ti++;\n\t}\n}\n";
    fwrite(buffer, 1, strlen(buffer), file);
}

P4CProduction P4CProduction_build(P4CParser * parser, ASTNode * id, RuleTypeID type) {
    P4CProduction prod;
    STACK_INIT(P4CString)(&prod.args, 0);

    prod.name = get_string_from_parser(parser, id);
    size_t buf_len = (parser->export.len + prod.name.len + 2);
    prod.identifier.str = MemPoolManager_malloc(parser->str_mgr, buf_len); // +1 for underscore
    prod.identifier.len = snprintf(prod.identifier.str, buf_len, "%.*s_%.*s", (int)parser->export.len, parser->export.str, (int)prod.name.len, prod.name.str);

    if (type != PEG_NOTRULE) {
        prod.type = type;
    } else {
        prod.type = PEG_PRODUCTION;
    }    

    parser->productions._class->set(&parser->productions, prod.name, prod);

    return prod;
}

void P4CProduction_declare(P4CParser * parser, P4CProduction prod) {
    //printf("declaring type: %s\n", prod.type_name);

    char const * main_type_end = get_type_name(prod.type);
    P4CString type = {.len = strlen(main_type_end), .str = (char *)main_type_end};
    P4CString_fwrite(type, parser->source_file, PSFO_NONE);
    fputc(' ', parser->source_file);
    P4CString_fwrite(prod.identifier, parser->source_file, PSFO_NONE);
    // for testing
    fwrite("; // ", 1, strlen("; // "), parser->source_file);
    
    unsigned int offset = 0;

    P4CString_fwrite(prod.identifier, parser->source_file, PSFO_UPPER | PSFO_LOFFSET(offset));
    fputc('\n', parser->source_file);

    P4CString_fwrite(prod.identifier, parser->header_file, PSFO_UPPER | PSFO_LOFFSET(offset));
    fwrite(",\n\t", 1, strlen(",\n\t"), parser->header_file);
    fflush(parser->header_file);
    fflush(parser->source_file);
}

void production_init(P4CParser * parser, P4CString name, P4CProduction * prod) {
    STACK_INIT(P4CString)(&prod->args, 0);
    prod->name = name;
}



#define PREP_OUTPUT_VAR_BUFFER_SIZE 256
void prep_output_files(P4CParser * parser) {
    char var_buffer[PREP_OUTPUT_VAR_BUFFER_SIZE];
    char const * buffer = NULL;
    int nbytes = 0;

    // write header in .h file
    buffer = "/* this file is auto-generated, do not modify */\n#ifndef ";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);
    
    P4CString_fwrite(parser->export, parser->header_file, PSFO_UPPER);
    buffer = "_H\n#define ";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);
    
    P4CString_fwrite(parser->export, parser->header_file, PSFO_UPPER);
    buffer = "_H\n\n#include \"peg4c/utils.h\"\n#include \"peg4c/rule.h\"\n#include \"peg4c/parser.h\"\n\ntypedef enum ";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);
    P4CString_fwrite(parser->export, parser->header_file, PSFO_NONE);
    buffer = "rule {\n\t";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);

    // write header in .c file

    nbytes = snprintf(var_buffer, PREP_OUTPUT_VAR_BUFFER_SIZE, "/* this file is auto-generated, do not modify */\n#include \"peg4c/parser_gen.h\"\n#include \"%s\"\n", parser->header_name);
    fwrite(var_buffer, 1, nbytes, parser->source_file);
    fflush(parser->header_file);
}

err_type open_output_files(P4CParser * parser) {
    size_t name_length = parser->export.len;
    size_t buf_len = 2* (sizeof(*parser->header_name) * (name_length + 3));
    parser->header_name = MemPoolManager_malloc(parser->str_mgr, buf_len);
    if (!parser->header_name) {
        return P4C_MALLOC_FAILURE;
    }
    
    // +1 to go after the null-terminator
    parser->source_name = parser->header_name + 1 + snprintf(parser->header_name, buf_len, "%.*s.h", (int)name_length, parser->export.str);
    buf_len -= (size_t)(parser->source_name - parser->header_name);
    snprintf(parser->source_name, buf_len, "%.*s.c", (int)name_length, parser->export.str);

    if (!(parser->header_file = fopen(parser->header_name, "w"))) {
        return P4C_FILE_IO_ERROR;
    }
    if (!(parser->source_file = fopen(parser->source_name, "w"))) {
        return P4C_FILE_IO_ERROR;
    }
    return P4C_SUCCESS;
}

void cleanup_header_file(P4CParser * parser) {
    P4CString_fwrite(parser->export, parser->header_file, PSFO_UPPER);
    char * buffer = "_NRULES\n} ";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);

    P4CString_fwrite(parser->export, parser->header_file, PSFO_NONE);
    buffer = "rule;";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);

	buffer = "\n\nextern Rule * ";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);
    P4CString_fwrite(parser->export, parser->header_file, PSFO_NONE);

	buffer = "rules[";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);
    P4CString_fwrite(parser->export, parser->header_file, PSFO_UPPER);

    buffer = "_NRULES + 1];\n\nvoid ";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);
    P4CString_fwrite(parser->export, parser->header_file, PSFO_NONE);

    buffer = "_dest(void);\n\n#endif //";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);
    P4CString_fwrite(parser->export, parser->header_file, PSFO_UPPER);

    buffer = "_H\n";
    fwrite(buffer, 1, strlen(buffer), parser->header_file);

    fflush(parser->header_file);
}

int P4CProduction_cleanup(void * data, P4CString name, P4CProduction prod) {
    prod.args._class->dest(&prod.args);
    return 0;
}

