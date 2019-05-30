#ifndef _STRING_H_
#define _STRING_H_

#include <stdlib.h> // size_t
#include <string.h> // memcmp

typedef struct mstring {
	char* base;
	size_t len;
} mstring;

/* because C sucks, const string str; str.base has type char* not const char*
	 and there's no way to conditionally make it const char*
 */
typedef struct string {
	const char* base;
	size_t len;
} string;

#define STRPRINT(str) fwrite((str).base,(str).len,1,stdout);

#define LITSIZ(a) (sizeof(a)-1)
#define LITLEN(a) a,LITSIZ(a)

#define CSTR(a) *((struct string*)&a)

#define ISLIT(s,a) ((strlen(s) == LITSIZ(a)) ? (0 == memcmp(s,a,LITSIZ(a))) : 0)

#endif /* _STRING_H_ */
