#include <stdio.h>
#include "o/foo.trie.h"

int main(int argc, char *argv[])
{
	enum foo foo = lookup_foo(argv[1]);
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
