#include "o/foo.trie.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
	if(argc != 2) {
		puts("please supply an argument.");
		return 1;
	}
	enum foo foo = lookup_foo(argv[1],strlen(argv[1]));
	switch(foo) {
	case bar_bar:
		puts("foobar");
		break;
	case bar_baz:
		puts("bazz");
		break;
	case bar_UNKNOWN:
		printf("ehunno! what is %s?\n",argv[1]);
		break;
	default:
		printf("We should really handle %d(%s)\n",foo,argv[1]);
	};
	return 0;
}
