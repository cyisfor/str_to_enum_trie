#include "mystring.h"

#include <sys/mman.h> // mmap
#include <sys/stat.h>
#include <stdlib.h> // size_t
#include <assert.h>
#include <string.h>
#include <ctype.h> // isspace
#include <error.h>
#include <unistd.h> // write
#include <stdbool.h>
#include <stdio.h> // snprintf
#include <fcntl.h> // open O_*
#include <libgen.h> // basename

string needenv(const char* name) {
	const char* val = getenv(name);
	if(val == NULL)
		error(1,0,"please specify %s=...",name);
	string r = {
		.base = val,
		.len = strlen(val)
	};
	return r;
}

/*
	enum $some_type { A, AB, ..., EHUNNO };

	enum $some_type $prefix_lookup(const char* s) {
	switch(s[0]) {
	case 'a':
	switch(s[1]) {
	case '\0': return A;
	...
	}
	...
	}

	switch($prefix_lookup(s)) {
	case A:
	return argTag("url","href");
	case AB:
	return dumbTag("quote");
	...
	default:
	error("unknown? %.*s",slen,s);
	};
*/

struct trie {
	char c;
	struct trie* subs;
	size_t nsubs;
};

// note, CANNOT sort by string size, since same prefix = different sizes
// would have to use "depth" parameter in traversal, to decide between
// little or big suffixes first...
int compare_nodes(struct trie* a, struct trie* b) {
	return a->c - b->c;
}
	
void sort_level(struct trie* cur) {
	if(cur->nsubs == 0) return;

	qsort(&cur->subs[0],cur->nsubs, sizeof(cur->subs[0]),(void*)compare_nodes);
	int i;
	for(i=0;i<cur->nsubs;++i) {
		sort_level(&cur->subs[i]);
	}
}

void insert(struct trie* root, const char* s, size_t len) {
	void visit(struct trie* cur, size_t off) {
		char c = (off == len) ? 0 : s[off];

		size_t i;
		// TODO: make subs sorted, and binary search to insert
		// that'd be faster for LOTS of strings, probably slower otherwise
		// because mergesort/indirection/etc
		for(i=0;i<cur->nsubs;++i) {
			struct trie* sub = &cur->subs[i];
			if(sub->c == c) {
				return visit(sub,off+1);
			}
		}

		cur->subs = realloc(cur->subs,sizeof(*cur->subs)*(cur->nsubs+1));
		cur = &cur->subs[cur->nsubs++];
		cur->c = c;

		// we don't need to traverse the subs we create. just finish the string here
		// as children.
		for(++off;off<=len;++off) {
			c = off == len ? 0 : s[off];
			cur->subs = malloc(sizeof(*cur->subs));
			cur->nsubs = 1;
			cur = &cur->subs[0];
			cur->c = c;
		}
		// final one, be sure to null it
		cur->subs = NULL;
		cur->nsubs = 0;
		return;
	}
	return visit(root, 0);
}

int main(int argc, char *argv[])
{
	/* What to prefix to all names, as a leading namespace thingy?
		 BAR -> FOO_BAR
	*/
	const string prefix = needenv("prefix");
	/* Should the enum constants have a special prefix?
	 i.e. lookup_foo returns FOO_*
	*/
	string enum_prefix = {
		.base = getenv("enum"),
	};
	if(enum_prefix.base == NULL) {
		enum_prefix = prefix;
	} else {
		enum_prefix.len = strlen(enum_prefix.base);
	}
	/* What file are we generating?
	 This is actually a template, where filename.c also produces
	 a file called filename.h
	*/
	bstring filename = {
		.base = getenv("file")
	};
	if(filename.base == NULL) {
		char* buf = malloc(prefix.len + LITSIZ(".gen.T"));
		memcpy(buf,prefix.base,prefix.len);
		memcpy(buf+prefix.len,LITLEN(".gen.T"));
		filename.base = buf;
		filename.len = prefix.len + LITSIZ(".gen.T");
	} else {
		filename.len = strlen(filename.base);
	}

	/* null terminated strings don't have a length,
		 but are terminated with a null
		 that's... actually not all that inefficient
		 since we're checking each letter anyway */
	bool nullterm = NULL!=getenv("null_terminated");

	/* the check can be case sensitive, or looking up
	 "FOO" and "foo" and "Foo" and "fOO" will all yield the same number.
	*/
	bool nocase = NULL!=getenv("nocase");
	
	struct trie root = {};

	struct stat winfo;
	assert(0==fstat(0,&winfo));
	char* src = mmap(NULL,winfo.st_size,PROT_READ,MAP_PRIVATE,0,0);
	assert(src != MAP_FAILED);
	char* cur = src;
	while(isspace(*cur)) {
		if(++cur == src + winfo.st_size) {
			// whitespace file]
			error(23,0,"only whitespace?");
		}
	}
	for(;;) {
		// cur should always point to non-whitespace here.
		char* hash = memchr(cur, '#', winfo.st_size-(cur-src));
		char* nl = memchr(cur,'\n',winfo.st_size-(cur-src));
		char* tail;
		if(nl == NULL) {
			// no newline at the end of file
			if(hash) {
				tail = hash-1;
			} else {
				tail = src + winfo.st_size-1;
			}
		} else {
			if(hash && hash < nl) {
				tail = hash-1;
			} else {
				tail = nl-1;
			}
		}
		if(tail != cur) {
			while(isspace(*tail)) {
				while(isspace(*tail)) {
					if(--tail == cur) break;
				}
			}
		}
		insert(&root, cur,tail - cur + 1);
		if(nl == NULL) break;
		cur = nl+1;
		if(cur == src + winfo.st_size) {
			// trailing newline
			break;
		}
		while(isspace(*cur)) {
			if(++cur == src + winfo.st_size) {
				// trailing whitespace
				break;
			}
		}
	}
	munmap(src,winfo.st_size);

	sort_level(&root);

	/* aab aac abc ->
		 a: (a1 b)
		 a1: (b1 c)
		 b: (c1)

		 output a, then recurse on a1, a again, then on b
		 output self, then child, self, then child
		 -> aabaacabc add separators if at top
	*/

	int fd = -1;

	bool neednewline = false;

	void writething(const char* buf, size_t n, int level) {
		if(neednewline) {
			neednewline = false;
			ssize_t res = write(fd,"\n",1);
			if(level > 0) {
				char* buf = alloca(level);
				memset(buf,'\t',level);
				write(fd,buf,level);
			}
		}
		ssize_t res = write(fd,buf,n);
		assert(res == n);
	}

#define WRITE(a,len) writething(a, len, level)
#define WRITELIT(a) WRITE(a,sizeof(a)-1)
#define WRITESTR(ss) WRITE(ss.base,ss.len)
#define WRITE_ENUM(tail,tlen) WRITESTR(enum_prefix); if(enum_prefix.len > 0) WRITELIT("_"); WRITE(tail,tlen)
#define WRITE_UNKNOWN WRITESTR(enum_prefix); if(enum_prefix.len > 0) WRITELIT("_"); WRITELIT("UNKNOWN")
	char s[0x100]; // err... 0x100 should be safe-ish.
	void newline(void) {
		neednewline = true;
	}
	void writei(int i, int level) {
		char buf[0x100];
		WRITE(buf, snprintf(buf,0x100,"%d",i));
	}
	
	void dumptrie(struct trie* cur, int level) {
		if(!cur) return;
		if(cur->c)
			WRITE(&cur->c,1);
		else
			WRITELIT("\\0");
		newline();
		int i;
		for(i=0;i<cur->nsubs;++i) {
			dumptrie(&cur->subs[i],level+1);
		}
	}
	fd = 1;
	dumptrie(&root,0);
	

	/* noupper keeps the generated names from having uppercase in them.
		 Normally they have uppercase stuff as that's how enums are usually
		 named. FOO_BAR instead of foo_bar
	*/
	bool noupper = getenv("noupper")!=NULL;
	char TOUPPER(char c) {
		if(noupper) return c;
		return toupper(c);
	}

	// no branches, so just memcmp
	void dump_memcmp(char* dest, struct trie* cur, int level, int len) {
		if(cur->nsubs == 0) {
			WRITELIT("return ");
			WRITE_ENUM(s,dest-s);
			WRITELIT(";");
			newline();
			return;
		}
		if(nullterm == false) {
			WRITELIT("if(length != ");
			writei(len,level);
			WRITELIT(")");
			newline();
			WRITELIT("\treturn ");
			WRITE_UNKNOWN;
			WRITELIT(";");
			newline();
		}
		if(!(nocase || nullterm)) {
			// can use memcmp yay
			WRITELIT("if(0==memcmp(&s[");
		} else {
			WRITELIT("if(0==strn");
			if(nocase)
				WRITELIT("case");
			// start at the address of character 'level'
			WRITELIT("cmp(&s[");
		}
		writei(level-1, level);
		WRITELIT("],\"");
		int num = 0;
		// add each character to the string, increasing num
		while(cur && cur->c) {
			WRITE(&cur->c,1);
			*dest++ = TOUPPER(cur->c);
			++num;
			cur = &cur->subs[0];
		}
		WRITELIT("\", ");
		// only strcmp up to num characters
		writei(num, level);
		WRITELIT("))");
		newline();
		WRITELIT("\treturn ");
		WRITE_ENUM(s,dest-s);
		WRITELIT(";");
		newline();
		WRITELIT("return ");
		WRITE_UNKNOWN;
		WRITELIT(";");
		newline();
	}

	bool nobranches(struct trie* cur, int* len) {
		while(cur) {
			if(cur->nsubs > 1) return false;
			if(cur->nsubs == 0) return true;
			++*len;
			cur = &cur->subs[0];
		}
	}
	
	void dump_code(char* dest, struct trie* cur, int level) {
		size_t i;
		if(nullterm == false) {
			WRITELIT("if(length == ");
			writei(level-1,level);
			WRITELIT(")");
			newline();
			++level;

			bool terminated = false;
			bool nonterminated = false;
			/* don't output a switch statement if NONE of them are nonterminated */
			for(i=0;i<cur->nsubs;++i) {
				if(cur->subs[i].c == '\0') {
					terminated = true;
				} else {
					nonterminated = true;
				}
				if(terminated && nonterminated) break;
			}

			WRITELIT("return ");
			if(terminated) {
				WRITE_ENUM(s,dest-s);
			} else {
				WRITE_UNKNOWN;
			}
			WRITELIT(";");
			
			--level;
			newline();
						
			if(nonterminated == false) {
				WRITELIT("else");
				newline();
				++level;
				WRITELIT("return ");
				WRITE_UNKNOWN;
				WRITELIT(";");
				newline();
				--level;
				return;
			}
		}
		WRITELIT("switch (s[");
		writei(level-1, level);
		WRITELIT("]) {");
		newline();

		for(i=0;i<cur->nsubs;++i) {
			char c = cur->subs[i].c;
			*dest = TOUPPER(c);
			// two cases for lower and upper sometimes
			void onecase(char c) {
				WRITELIT("case '");
				if(c) {
					WRITE(&c,1);
				} else {
					WRITELIT("\\0");
				}
				WRITELIT("':");
				newline();
			}
			if(!c) {
				if(nullterm == true) {
					onecase(c);
					WRITELIT("\treturn ");
					WRITESTR(enum_prefix);
					WRITELIT("_");
					WRITE(s,dest-s);
					WRITELIT(";");
					newline();
				}
			} else {
				onecase(c);
				if(nocase) {
					if (c != toupper(c)) {
						onecase(toupper(c));
					} else if(c != tolower(c)) {
						onecase(tolower(c));
					}
				}
				if(cur->nsubs == 0 || cur->subs[i].nsubs == 0) {
					WRITELIT("ehunno");
					newline();
				} else {
					int len = 0;
					if (nobranches(&cur->subs[i],&len)) {
						if(len > 4) {
							*dest = TOUPPER(cur->subs[i].c);
							dump_memcmp(dest+1,&cur->subs[i].subs[0],level+1,len);
							continue;
						}
					}
					dump_code(dest+1, &cur->subs[i],level+1);
				}
			}
		}
		WRITELIT("default:");
		newline();
		WRITELIT("\treturn ");
		WRITE_UNKNOWN;
		WRITELIT(";");
		newline();
		WRITELIT("};");
		newline();
	}

	void dump_enum(char* dest, struct trie* cur, int level) {
		int i = 0;
		for(;i<cur->nsubs;++i) {
			char c = cur->subs[i].c;
			if(c) {
				*dest = TOUPPER(c);
				dump_enum(dest+1,&cur->subs[i],level+1);
			} else {
				WRITELIT(",\n\t");
				WRITE_ENUM(s,dest-s);
			}
		}
	}

	char tname[] = ".tmpXXXXXX";
	fd = mkstemp(tname);
	assert(fd >= 0);
	int level = 0;

	WRITELIT("enum ");
	WRITESTR(prefix);
	WRITELIT (" {\n\t");
	WRITE_UNKNOWN;
	dump_enum(s, &root, level);
	WRITELIT("\n};\nenum ");
	WRITESTR(prefix);
	WRITELIT(" ");
	WRITELIT("lookup_");
	WRITESTR(prefix);
	WRITELIT("(const char* s");

	if(nullterm == false) {
		WRITELIT(", int length");
	}

	WRITELIT(");");
	newline();
	
	close(fd);
	filename.base[filename.len-1] = 'h'; // blah.gen.h
	rename(tname,filename.base);
	fd = open(tname,O_WRONLY|O_CREAT|O_TRUNC,0644);
	assert(fd >= 0);
	WRITELIT("#include \"");
	string base = { .base = basename(filename.base) };
	base.len = strlen(base.base);
	WRITESTR(base);
	WRITELIT("\"");
	newline();
	WRITELIT("#include <string.h> // strncmp");
	newline();
	WRITELIT("enum ");
	WRITESTR(prefix);
	WRITELIT(" ");
	WRITELIT("lookup_");
	WRITESTR(prefix);
	WRITELIT("(const char* s");
	if(nullterm == false) {
		WRITELIT(", int length");
	}
	WRITELIT(") {");
	newline();

	dump_code(s, &root, level+1);
	WRITELIT("}\n");
	close(fd);
	filename.base[filename.len-1] = 'c'; // blah.gen.c
	rename(tname,filename.base);
}
