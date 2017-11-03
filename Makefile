CFLAGS+=-ggdb
LINK=$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
ifeq ($(PREPROC),)
COMPILE=$(CC) $(CFLAGS) -c -o $@ $<
else
COMPILE=cpp $(CFLAGS) -o $@.cpp $<
endif
all: main example

main: o/main.o src/mystring.h
	$(LINK)

o/%.o: src/%.c  | o
	$(COMPILE)

o/%.o: o/%.c  | o
	$(COMPILE)

o:
	mkdir $@


example: o/example.o o/foo.trie.o
	$(LINK)

o/example.o: o/foo.trie.c
o/example.o: CFLAGS+=-I. # to include o/...

define TRIE
export nocase noupper prefix enum

o/$(name).trie.c: o/$(name).trie.h $(source) main | o
	(cd o && file=$(name).trie.c ../main) <$(source)
o/$(name).trie.h: ;

undefine nocase noupper prefix enum name source
endef

name=foo
source=./test.example
noupper=1
nocase=1
prefix=foo
enum=bar
undefine null_terminated
$(eval $(TRIE))
