int main(int argc, char *argv[])
{
#include "o/foo.trie.h"
	enum foo foo = lookup_foo(argv[1]);
	switch(foo) {
	case foo_BAR:
		puts("foobar");
		break;
	case foo_BAZ:
		puts("bazz");
		break;
	case foo_UNKNOWN:
		printf("ehunno! what is %s?",argv[1]);
	default:
		printf("We should really handle %d(%s)\n",foo,argv[1]);
	};
	return 0;
}
