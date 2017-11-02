LINK=$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

all: main example

main: o/main.o src/mystring.h
	$(LINK)

o/%.o: src/%.c  | o
	$(CC) $(CFLAGS) -c -o $@ $<

o:
	mkdir $@
