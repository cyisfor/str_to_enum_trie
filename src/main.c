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

string needenv(const char* name) {
	const char* val = getenv(name);
	if(val == NULL)
		error(1,0,"please specify %s=...",name);
	string r = {
		.s = val,
		.l = strlen(val)
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

	const string prefix = needenv("prefix");
	string enum_prefix = {
		.s = getenv("enum"),
	};
	if(enum_prefix.s == NULL) {
		enum_prefix = prefix;
	} else {
		enum_prefix.l = strlen(enum_prefix.s);
	}
	mstring filename = {
		.s = getenv("file")
	};
	if(filename.s == NULL) {
		char* buf = malloc(prefix.l + LITSIZ(".gen.T"));
		memcpy(buf,prefix.s,prefix.l);
		memcpy(buf+prefix.l,LITLEN(".gen.T"));
		filename.s = buf;
		filename.l = prefix.l + LITSIZ(".gen.T");
	} else {
		filename.l = strlen(filename.s);
	}

	bool nocase = NULL==getenv("nocase");
	
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
		char* nl = memchr(cur,'\n',winfo.st_size-(cur-src));
		char* tail;
		if(nl == NULL) {
			// no newline at the end of file
			tail = src + winfo.st_size-1;
		} else {
			tail = nl-1;
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

	void WRITE(const char* buf, size_t n) {
		ssize_t res = write(fd,buf,n);
		assert(res == n);
	}

#define WRITELIT(a) WRITE(LITLEN(a))
#define WRITESTR(ss) WRITE(ss.s,ss.l)
#define WRITE_ENUM(tail,tlen) WRITESTR(enum_prefix); if(enum_prefix.l > 0) WRITELIT("_"); WRITE(tail,tlen)
#define WRITE_UNKNOWN WRITESTR(enum_prefix); if(enum_prefix.l > 0) WRITELIT("_"); WRITELIT("UNKNOWN")
	char s[0x100]; // err... 0x100 should be safe-ish.
	void indent(int level) {
		int i=0;
		char* buf = alloca(level);
		for(i=0;i<=level;++i) {
			buf[i] = '\t';
		}
		WRITE(buf,level+1);
	}
	void writei(int i) {
		char buf[0x100];
		WRITE(buf, snprintf(buf,0x100,"%d",i));
	}

	void dumptrie(struct trie* cur, int level) {
		if(!cur) return;
		indent(level);
		if(cur->c)
			WRITE(&cur->c,1);
		else
			WRITELIT("\\0");
		WRITE("\n",1);
		int i;
		for(i=0;i<cur->nsubs;++i) {
			dumptrie(&cur->subs[i],level+1);
		}
	}
	//dumptrie(&root,0);

	bool noupper = getenv("noupper")!=NULL;
	char TOUPPER(char c) {
		if(noupper) return c;
		return toupper(c);
	}

	void dump_memcmp(char* dest, struct trie* cur, int level, int len) {
		if(cur->nsubs == 0) {
			indent(level);
			WRITELIT("return ");
			WRITE_ENUM(s,dest-s);
			WRITELIT(";\n");
			return;
		}
		indent(level);
		WRITELIT("if(");
		struct trie* place = cur;
		int pos = level;
		void oneshortcut_onecase(char c, const char* prefix, size_t plen) {
			if(plen) WRITE(prefix,plen);
			WRITELIT("s[");
			writei(pos);
			WRITELIT("] == '");
			WRITE(&c,1);
			*dest = TOUPPER(place->c);
			WRITELIT("'");
		}
		void oneshortcut(const char* prefix, size_t plen) {
			oneshortcut_onecase(place->c, prefix, plen);
			if(nocase) {
				char c = place->c;
				if(c != tolower(c))
					oneshortcut_onecase(tolower(c)," && ",4);
				else if(c != toupper(c))
					oneshortcut_onecase(toupper(c)," && ",4);
			}
			++pos; ++dest;
			place = &place->subs[0];
		}
		switch(len) {
		case 2:
			oneshortcut(NULL,0);
			WRITELIT(")\n");
			break;
		case 3:
			oneshortcut(NULL,0);
			oneshortcut(LITLEN(" && "));
			WRITELIT(")\n");
			break;
		case 4:
			oneshortcut(NULL,0);
			oneshortcut(LITLEN(" && "));
			oneshortcut(LITLEN(" && "));
			WRITELIT(")\n");
			break;
		default:
			WRITELIT("0==strn");
			if(nocase)
				WRITELIT("case");
			WRITELIT("cmp(&s[");
			writei(level);
			WRITELIT("],\"");
			int num = 0;
			while(cur && cur->c) {
				WRITE(&cur->c,1);
				*dest++ = TOUPPER(cur->c);
				++num;
				cur = &cur->subs[0];
			}
			WRITELIT("\", ");
			writei(num);
			WRITELIT("))\n");
		};
		indent(level+1);
		WRITELIT("return ");
		WRITE_ENUM(s,dest-s);
		WRITELIT(";\n");
		indent(level);
		WRITELIT("return ");
		WRITE_UNKNOWN;
		WRITELIT(";\n");
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
		indent(level);
		WRITELIT("switch (s[");
		writei(level);
		WRITELIT("]) {\n");

		for(i=0;i<cur->nsubs;++i) {
			char c = cur->subs[i].c;
			*dest = TOUPPER(c);
			indent(level);
			// two cases for lower and upper sometimes
			void onecase(char c) {
				WRITELIT("case '");
				if(c) {
					WRITE(&c,1);
				} else {
					WRITELIT("\\0");
				}
				WRITELIT("':\n");
			}
			onecase(c);
			if(!c) {
				indent(level+1);
				WRITELIT("return ");
				WRITESTR(enum_prefix);
				WRITELIT("_");
				WRITE(s,dest-s);
				WRITELIT(";\n");
			} else {
				if(nocase) {
					if (c != toupper(c)) {
						onecase(toupper(c));
					} else if(c != tolower(c)) {
						onecase(tolower(c));
					}
				}
				if(cur->nsubs == 0 || cur->subs[i].nsubs == 0) {
				WRITELIT("ehunno\n");
			} else {
				int len = 0;
				if (nobranches(&cur->subs[i],&len)) {
					*dest = TOUPPER(cur->subs[i].c);
					dump_memcmp(dest+1,&cur->subs[i].subs[0],level+1,len);
				} else {
					dump_code(dest+1, &cur->subs[i],level+1);
				}
			}
		}
		indent(level);
		WRITELIT("default:\n");
		indent(level+1);
		WRITELIT("return ");
		WRITE_UNKNOWN;
		WRITELIT(";\n");
		indent(level);
		WRITELIT("};\n");
	}

	void dump_enum(char* dest, struct trie* cur) {
		int i = 0;
		for(;i<cur->nsubs;++i) {
			char c = cur->subs[i].c;
			if(c) {
				*dest = TOUPPER(c);
				dump_enum(dest+1,&cur->subs[i]);
			} else {
				WRITELIT(",\n\t");
				WRITE_ENUM(s,dest-s);
			}
		}
	}

	char tname[] = ".tmpXXXXXX";
	fd = mkstemp(tname);
	assert(fd >= 0);

	WRITELIT("enum ");
	WRITESTR(prefix);
	WRITELIT (" {\n\t");
	WRITE_UNKNOWN;
	dump_enum(s, &root);
	WRITELIT("\n};\nenum ");
	WRITESTR(prefix);
	WRITELIT(" ");
	WRITELIT("lookup_");
	WRITESTR(prefix);
	WRITELIT("(const char* s);\n");
	
	close(fd);
	filename.s[filename.l-1] = 'h'; // blah.gen.h
	rename(tname,filename.s);
	fd = open(tname,O_WRONLY|O_CREAT|O_TRUNC,0644);
	assert(fd >= 0);
	WRITELIT("#include \"");
	WRITESTR(filename);
	WRITELIT("\"\n");
	WRITELIT("#include <string.h> // strncmp\n");
	/*	WRITELIT("enum wanted_tags lookup_wanted(const char* tag) {\n"); */
	WRITELIT("enum ");
	WRITESTR(prefix);
	WRITELIT(" ");
	WRITELIT("lookup_");
	WRITESTR(prefix);
	WRITELIT("(const char* s) {\n");

	dump_code(s, &root, 0);
	WRITELIT("}\n");
	close(fd);
	filename.s[filename.l-1] = 'c'; // blah.gen.c
	rename(tname,filename.s);
}
