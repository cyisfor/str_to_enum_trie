/* CRAP
I forgot that without sentinel endpoints there's no way to tell whether a trie terminates or not!

 */

#include "mystring.h"
#include "record.h"
#include "ensure.h"

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

struct slice {
	bstring* str;
	size_t start;
	size_t len;
};

const struct slice substringb(bstring* src, size_t start, size_t len) {
	return (struct slice) {
		.str = src,
			.start = start,
			.len = len
			};
}

const string needenv(const char* name) {
	const char* val = getenv(name);
	if(val == NULL)
		record(ERROR,"please specify %s=...",name);
	return strlenstr(val);
}

const string maybeenv(const char* name) {
	const char* val = getenv(name);
	if(val == NULL) {
		return (const string){};
	}
	return strlenstr(val);
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
	bool terminates;
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
#if DEBUGGING	
	record(INFO, "Checking %.*s", (int)len, s);
#endif
	void visit(struct trie* cur, size_t off) {
		assert(off != len);
		char c = s[off];

		size_t i;
		// TODO: make subs sorted, and binary search to insert
		// that'd be faster for LOTS of strings, probably slower otherwise
		// because mergesort/indirection/etc
		for(i=0;i<cur->nsubs;++i) {
			struct trie* sub = &cur->subs[i];
			if(sub->c == c) {
				if(off+1 == len) return;
				return visit(sub,off+1);
			}
		}

		cur->subs = realloc(cur->subs,sizeof(*cur->subs)*(cur->nsubs+1));
		cur = &cur->subs[cur->nsubs++];
		cur->c = c;

		// we don't need to traverse the subs we create. just finish the string here
		// as children.
		for(++off;off<len;++off) {
			c = s[off];
			cur->subs = malloc(sizeof(*cur->subs));
			cur->nsubs = 1;
			cur = &cur->subs[0];
			cur->c = c;
		}
		// final one, be sure to null it
		cur->terminates = true;
		cur->subs = NULL;
		cur->nsubs = 0;
		return;
	}
	return visit(root, 0);
}

struct options {
/* noupper keeps the generated names from having uppercase in them.
   Normally they have uppercase stuff as that's how enums are usually
   named. FOO_BAR instead of foo_bar
*/
	bool noupper; // = getenv("noupper")!=NULL;

	const string enum_prefix;
	const string prefix;
	bstring filename;

	bool nullterm;
	bool nocase;
};

struct output {
	int fd;
	int level;
	int index;
	bool neednewline;
	struct trie root;

	struct options options;
};

#if DEBUGGING	
void debugdest(struct output* out, bstring* dest) {
	size_t i;
	bstring res = {};
	for(i=0;i<dest->len;++i) {
		if(i == out->index) {
			straddn(&res, LITLEN("→"));
		}
		straddn(&res, &dest->base[i], 1);
	}	
	record(INFO, "Dest %.*s",
		   STRING_FOR_PRINTF(res));
	strclear(&res);
}
#endif

void inclevel(bstring* dest, struct trie* cur, struct output* out) {
	++out->level;
	++out->index;
	straddn(dest, &cur->c, 1);
#if DEBUGGING
	record(INFO, "GOing up to %d:%d added %c", out->index, dest->len, cur->c);
	debugdest(out, dest);
#endif
}
void declevel(bstring* dest, struct output* out) {
	assert(dest->len > 0);
	--dest->len;
	--out->level;
	--out->index;
#if DEBUGGING	
	record(INFO, "GOing down to %d:%d removed %c", out->index, dest->len, dest->base[dest->len]);
	debugdest(out, dest);
#endif
}

void writething(struct output* self, const char* buf, size_t n) {
	if(self->neednewline) {
		self->neednewline = false;
		ssize_t res = write(self->fd,"\n",1);
		if(self->level > 0) {
			char* buf = alloca((self->level)<<1);
			memset(buf,' ',(self->level)<<1);
			write(self->fd,buf,(self->level)<<1);
		}
	}
	ssize_t res = write(self->fd,buf,n);
	assert(res == n);
}

void newline(struct output* self) {
	self->neednewline = true;
}

#define NL() newline(out)

void writei(struct output* out, int i) {
	char buf[0x100];
	writething(out, buf, snprintf(buf,0x100,"%d",i));
}


#define WRITE(a,len) writething(out, a, len)
#define WRITELIT(a) WRITE(a,sizeof(a)-1)
#define WRITESTR(ss) WRITE((ss).base,(ss).len)
#define WRITESLICE(ss) WRITE((ss).str->base + (ss).start, (ss).len)
#define WRITEI(num) writei(out, num);

char toident(char c) {
	/* only alnum allowed in identifiers */
	if(!isalnum(c)) return '_';
	return c;
}

char mytoupper(struct output* out, char c) {
	c = toident(c);
	if(out->options.noupper) return c;
	return toupper(c);
}
#define TOUPPER(c) mytoupper(out, c)

/* write an enum value like "bar_FOO" */
void write_enum_value(struct output* out, const string name) {
	if(out->options.enum_prefix.len > 0) {
		WRITESTR(out->options.enum_prefix);
		write(out->fd, "_", 1);
	}
	int i;
	char buf[name.len];
	for(i=0;i<name.len;++i) {
		buf[i] = TOUPPER(name.base[i]);
	}
	write(out->fd, buf, name.len);
}

void write_return_enum_value(struct output* out, const string name) {
	WRITELIT("return ");
	write_enum_value(out, name);
	WRITELIT(";");
	NL();
}

void write_unknown(struct output* out) {
	write_enum_value(out, LITSTR("UNKNOWN"));
}

void write_return_unknown(struct output* out) {
	WRITELIT("return ");
	write_unknown(out);
	WRITELIT(";");
	NL();
}

/* dump all enum values comma separated for in the definition of the enum */
void write_enum_values(struct output* out, struct trie* root) {
	bstring dest = {};
	void onelevel(struct trie* cur) {
		int i = 0;
		for(;i<cur->nsubs;++i) {
			char c = cur->subs[i].c;
			strreserve(&dest, 1);
			dest.base[dest.len] = TOUPPER(c);
			++dest.len;
			if(cur->subs[i].terminates) {
				WRITELIT(",\n\t");
				write_enum_value(out, STRING(dest));
			}
			if(cur->subs[i].nsubs) {
				++out->level;
				/* writing ALL values so don't return here. */
				onelevel(&cur->subs[i]);
				--out->level;
			} 
			--dest.len;
		}
	}
	onelevel(root);
}

void end_bracket(struct output* out) {
	NL();
	--out->level;
	WRITELIT("}");
	NL();
}

void if_length(struct output* out, const string op, int level) {
	WRITELIT("if(length ");
	WRITESTR(op);
	WRITELIT(" ");
	WRITEI(level);
	WRITELIT(")");
}

void begin_bracket(struct output* out) {
	WRITELIT("{");
	NL();
	++out->level;
}	

// no branches, so just memcmp
void if_memcmp(struct output* out, struct slice dest) {
	if(!(out->options.nocase || out->options.nullterm)) {
		// can use memcmp yay
		WRITELIT("if(0==memcmp(&s[");
	} else {
		WRITELIT("if(0==strn");
		if(out->options.nocase)
			WRITELIT("case");
		// start at the address of character 'level'
		WRITELIT("cmp(&s[");
	}
	WRITEI(out->index);
	WRITELIT("],\"");

	WRITESLICE(dest);
	WRITELIT("\", ");
	// only strcmp up to num characters
	WRITEI(dest.len);
	WRITELIT("))");
}

bool nobranches(struct trie* cur, int* len) {
	while(cur) {
		if(cur->nsubs > 1) return false;
		if(cur->nsubs == 0) return true;
		++*len;
		cur = &cur->subs[0];
	}
}

void dumptrie(struct output* out, struct trie* cur) {
	void onelevel(struct trie* cur) {
		if(!cur) return;
		int len = 0;
		if(nobranches(cur, &len)) {
			writething(out, "", 0);
//		write(out->fd, "@", 1);
			for(;;) {
				write(out->fd, &cur->c, 1);
				if(cur->nsubs == 0) break;
				cur = &cur->subs[0];
			}
			if(cur->terminates) {
				writething(out, "$", 1);
			}
			NL();
			return;
		}
		if(cur->c)
			writething(out, &cur->c, 1);
		else
			WRITELIT("\\0");
		if(cur->terminates) {
			writething(out, "$", 1);
		}
		NL();
		int i;
		++out->level;
		for(i=0;i<cur->nsubs;++i) {
			onelevel(&cur->subs[i]);
		}
		--out->level;
	}
	onelevel(cur);
	writething(out,"",0);
}

struct trie* first_branch(struct output* out, bstring* dest, struct trie* cur, int* num) {
	if(!cur) return NULL;
	if(cur->nsubs == 0) return NULL;
	*num = 0;
	size_t oldlen = dest->len;
	while(cur->nsubs == 1) {
		cur = &cur->subs[0];
		straddn(dest, &cur->c, 1);
		++*num;
	}
#if DEBUGGING		
	record(INFO, "going up branch %d:%d added %d %.*s",
		   out->index,
		   dest->len,
		   *num,
		   (int)dest->len - oldlen,
		   dest->base + oldlen
		);
#endif	
	/* don't increase out->index yet... */
	return cur;
}

void inc_nonbranches(struct output* out, bstring* dest, int num) {
#if DEBUGGING		
	record(INFO, "finish branch GOing up %d", num);
#endif
	assert(out->index + num <= dest->len);
	out->index += num;
#if DEBUGGING		
	debugdest(out, dest);
#endif
}

void dec_nonbranches(struct output* out, bstring* dest, int num) {
#if DEBUGGING		
	record(INFO, "GOing down %d removing %.*s", num,
		   dest->len - (num ),
		   dest->base + (num - 1 )
		);
#endif
	assert(dest->len >= num);
	out->index -= num;
	/* this is gonna not work... out->index needs to be -1 here? */
	dest->len -= num;
#if DEBUGGING		
	debugdest(out, dest);
#endif
}


void write_branch(struct output* out, bstring* dest, struct trie* cur) {
#if DEBUGGING		
	record(INFO, "Index dump branch %d %.*s", out->index,
		STRING_FOR_PRINTF(*dest));
#endif
	/* first, try to check branches with many common prefixes
	 i.e. aaaaone, aaaatwo, aaaathree etc*/
	int num = 0;
	/* inclevel several times... */
	cur = first_branch(out, dest, cur, &num);
	switch(num) {
	case 0:
		break;
	case 1:
	case 2:
	case 3:
	case 4: {
		/* we only need logic for the 1 non-branch */
		WRITELIT("if(");
		bool first = true;
		int i = 0;
		void writeone(int i, char c) {
			WRITELIT("s[");
			WRITEI(i);
			WRITELIT("] == '");
			write(out->fd, &c, 1);
			WRITELIT("'");
		}
		for(i=0;i<num;++i) {
			if(first) {
				first = false;
			} else {
				WRITELIT(" && ");
			}
			int offset = out->index+i;
			char c = dest->base[offset];
			if(out->options.nocase && c != toupper(c)) {
				WRITELIT("(");
				writeone(offset, c);
				WRITELIT(" || ");
				writeone(offset, toupper(c));
				WRITELIT(")");
			} else {
				writeone(offset, c);
			}
		}					

		WRITELIT(")");
		begin_bracket(out);
		if(cur->terminates) {
			if_length(out, LITSTR("=="), dest->len );
			++out->level;
			NL();
			write_return_enum_value(out, STRING(*dest));
			--out->level;
		}
		if(cur->nsubs == 0) {
			write_return_unknown(out);
			end_bracket(out);
			inc_nonbranches(out, dest, num);
			dec_nonbranches(out, dest, num);
			return;
		}
		if_length(out, LITSTR("<"), dest->len );
		++out->level;
		NL();		
		write_return_unknown(out);
		--out->level;
		inc_nonbranches(out, dest, num);
	}
		break;
	default: {
		// if(num > 3)
		int i;
		if_length(out, LITSTR("<"), dest->len );
		++out->level;
		NL();
		WRITELIT("return ");
		write_unknown(out);
		WRITELIT(";");
		NL();
		--out->level;

		assert(out->index <= dest->len );
		if_memcmp(out, substringb(dest, out->index, dest->len - out->index));
		begin_bracket(out);
		/* if(dest->len == num) ? */
		inc_nonbranches(out, dest, num);
	}
	};

	if(cur->nsubs > 0) {
		if(cur->terminates) {
			if_length(out, LITSTR("=="), dest->len );
			++out->level;
			write_return_enum_value(out, STRING(*dest));
			--out->level;
		}
	} else if(cur->nsubs == 0) {
		if(cur->terminates) {
			write_return_enum_value(out, STRING(*dest));
		} else {
			write_return_unknown(out);
		}
		end_bracket(out);
		dec_nonbranches(out, dest, num);
		return;
	}


	/*
	  We finished sneaking down the prefix non-branches, if possible.
	  Now we have to do branch logic, for each of the cur->nsubs
	 */

	assert(cur->nsubs >= 2);
	size_t i;
	WRITELIT("switch (s[");
	WRITEI(out->index);
	WRITELIT("]) {");
	NL();

	for(i=0;i<cur->nsubs;++i) {
		struct trie* sub = &cur->subs[i];

		// two cases for lower and upper sometimes
		void onecasederp(char c) {
			WRITELIT("case '");
			if(c) {
				WRITE(&c,1);
			} else {
				WRITELIT("\\0");
			}
			WRITELIT("':");
			NL();
		}
		void onecase(char c) {
			onecasederp(c);
			if(out->options.nocase) {
				/* XXX: TOUPPE R here?
				 no, because this is in '' quotes*/
				if (c != toupper(c)) {
					onecasederp(toupper(c));
				} else if(c != tolower(c)) {
					onecasederp(tolower(c));
				}
			}
		}
		onecase(sub->c);
		inclevel(dest, sub, out);

		if(sub->nsubs == 0) {
			if_length(out, LITSTR("!="), dest->len);
			++out->level;
			NL();
			write_return_unknown(out);
			--out->level;
			write_return_enum_value(out, STRING(*dest));
		} else {
			int len = 0;
			if (nobranches(sub,&len) && len == 0) {
				++out->level;
				if(sub->nsubs == 0) {
					write_return_enum_value(out, STRING(*dest));					
				} else {
					/* 									&cur->subs[i].subs[0], */
					WRITELIT("DERP");
				}
			} else {
				write_branch(out, dest, sub);
			}
		}
		NL();
		WRITELIT("break;");
		NL();
		declevel(dest, out);
	}
	WRITELIT("default:");
	NL();
	WRITELIT("\treturn ");
	write_unknown(out);
	WRITELIT(";");
	NL();
	WRITELIT("};");
	NL();
	if(num != 0) {
		end_bracket(out);
	}
	dec_nonbranches(out, dest, num);
}

void write_header(struct output* out) {
	WRITELIT("enum ");
	WRITESTR(out->options.prefix);
	WRITELIT (" {\n\t");
	write_unknown(out);
	write_enum_values(out, &out->root);
	WRITELIT("\n};\nenum ");
	WRITESTR(out->options.prefix);
	WRITELIT(" ");
	WRITELIT("lookup_");
	WRITESTR(out->options.prefix);
	WRITELIT("(const char* s");

	if(out->options.nullterm == false) {
		WRITELIT(", int length");
	}

	WRITELIT(");");
	NL();
}

void write_code(struct output* out) {
	WRITELIT("#include \"");
	/* NOTE:  filename has not yet been changed to .c, still .h */
	string base = { .base = basename(out->options.filename.base) };
	base.len = strlen(base.base);
	WRITESTR(base);
	WRITELIT("\"");
	NL();
	WRITELIT("#include <string.h> // strncmp");
	NL();
	WRITELIT("enum ");
	WRITESTR(out->options.prefix);
	WRITELIT(" ");
	WRITELIT("lookup_");
	WRITESTR(out->options.prefix);
	WRITELIT("(const char* s");
	if(out->options.nullterm == false) {
		WRITELIT(", int length");
	}
	WRITELIT(") {");
	NL();

	++out->level;
	bstring dest = {};
	write_branch(out, &dest, &out->root);
	--out->level;
	WRITELIT("}\n");
}

int main(int argc, char *argv[])
{

	struct output out = {
		.fd = 2,
		.root = {},
		.options = {
			/* noupper keeps the generated names from having uppercase in them.
			   Normally they have uppercase stuff as that's how enums are usually
			   named. FOO_BAR instead of foo_bar
			*/
			.noupper = getenv("noupper")!=NULL,

			/* What to prefix to all names, as a leading namespace thingy?
			   BAR -> FOO_BAR
			*/
			.prefix = needenv("prefix"),
			/* Should the enum constants have a special prefix?
			   i.e. lookup_foo returns FOO_*
			*/
			.enum_prefix = maybeenv("enum"),

	/* null terminated strings don't have a length,
		 but are terminated with a null
		 that's... actually not all that inefficient
		 since we're checking each letter anyway */
			.nullterm = NULL!=getenv("null_terminated"),

	/* the check can be case sensitive, or looking up
	 "FOO" and "foo" and "Foo" and "fOO" will all yield the same number.
	*/
			.nocase = NULL!=getenv("nocase")
		}
	};

	{

		/* What file are we generating?
		   This is actually a template, where filename.c also produces
		   a file called filename.h
		*/
		const string filename = maybeenv("file");
		if(filename.base == NULL) {
			straddn(&out.options.filename,
					out.options.prefix.base,
					out.options.prefix.len);
			stradd(&out.options.filename,
				   ".gen.T");
		} else {
			straddn(&out.options.filename, filename.base, filename.len);
		}
	}

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
		insert(&out.root, cur,tail - cur + 1);
		if(nl == NULL) break;
		cur = nl+1;
		if(cur == src + winfo.st_size) {
			// trailing newline
			break;
		}
		while(isspace(*cur)) {
			if(++cur == src + winfo.st_size) {
				// trailing whitespace
				goto BREAK_FOR;
			}
		}
	}
BREAK_FOR:
	munmap(src,winfo.st_size);

	sort_level(&out.root);

	/* aab aac abc ->
		 a: (a1 b)
		 a1: (b1 c)
		 b: (c1)

		 output a, then recurse on a1, a again, then on b
		 output self, then child, self, then child
		 -> aabaacabc add separators if at top
	*/

#if 0
	out.fd = 2;
	dumptrie(&out, &out.root);
#endif


	char tname[] = "tmpXXXXXX";
	out.fd = open(tname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	assert(out.fd >= 0);
	write_header(&out);
	close(out.fd);

	out.options.filename.base[out.options.filename.len-1] = 'h'; // blah.gen.h
	rename(tname,out.options.filename.base);

	out.fd = open(tname,O_WRONLY|O_CREAT|O_TRUNC,0644);
	assert(out.fd >= 0);
	write_code(&out);
	close(out.fd);
	out.options.filename.base[out.options.filename.len-1] = 'c'; // blah.gen.c
	rename(tname,out.options.filename.base);
}
