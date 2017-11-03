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

o:
	mkdir $@

o/%.gen.o: o/%.gen.c
	$(COMPILE)

define TRIE
export nocase noupper prefix enum
o/$(name).trie.c: o/$(name).trie.h $(source) main
	file=$@ ./main
undefine nocase noupper prefix enum name source
endef

name=foo
source=./test.example
noupper=1
nocase=1
prefix=foo
enum=bar
$(TRIE)

o/%.gen.c: o/%.gen.h %
	file=$@ prefix=

example: o/example.o o/foo.gen.o
