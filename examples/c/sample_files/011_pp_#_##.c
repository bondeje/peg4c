#define CAT(a, b) a##b
#define CAT2(a, b) CAT(a, b)
#define STRINGIFY(x) #x
#define MODULE A

char * a = STRINGIFY(I am the very model of a modern major general);
struct CAT(MODULE, my_struct);
struct CAT2(MODULE, my_struct2);
