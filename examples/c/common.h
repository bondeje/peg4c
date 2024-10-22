#ifndef CCOMMON_H
#define CCOMMON_H

#ifndef CAT
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#endif

#if __STDC_VERSION__ < 201112L
#ifndef _Alignof
	#define _Alignof(type) offsetof(struct CAT(type, _ALIGNMENT), b)
#endif
	#define BUILD_ALIGNMENT_STRUCT(type) \
	struct CAT(type, _ALIGNMENT) {\
		char a;\
		type b;\
	};
#else
	#define BUILD_ALIGNMENT_STRUCT(type)
#endif

#ifdef __unix__
#define POSIX 1
#endif

#ifdef _POSIX_VERSION
#ifndef POSIX
#define POSIX 1
#endif
#endif

_Bool is_whitespace(char c);

#endif

