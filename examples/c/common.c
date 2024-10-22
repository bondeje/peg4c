#include <string.h>
#include "common.h"

_Bool is_whitespace(char c) {
    static char const * ws = " \t\r\n\v\f";
    return ((void *)0) != strchr(ws, c);
}
