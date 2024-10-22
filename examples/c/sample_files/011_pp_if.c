///*
#define DECL_INT_ARRAY(a) int a[2]
#define INIT_DECLTR(a, b) = { \
    a, \
    b, \
}
#define SEMICOLON ;

DECL_INT_ARRAY(array) INIT_DECLTR(-1, 1) SEMICOLON
//*/
///*
#define PLUS +
#define ADD_ONE(a) a PLUS 1

int b = ADD_ONE(ADD_ONE(ADD_ONE(2)));
//*/
