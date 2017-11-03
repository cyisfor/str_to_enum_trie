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
