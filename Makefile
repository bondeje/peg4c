.POSIX:
# just in case FreeBSD's make implementation screws up the current directory
.OBJDIR: ./
.SUFFIXES:
CC = gcc
NAME = peg4c
ASTNODE_ADD_PARENT = 0

PREFIX=$$(pwd)

SUB_MAKE_ARGS = CC=$(CC) SANITIZE=$(SANITIZE) ASTNODE_ADD_PARENT=$(ASTNODE_ADD_PARENT) PREFIX=$(PREFIX)

# for use in specifying a PCRE2 path and (for linux) overriding GNU_regex
PCRE2 = 
COMMON_CFLAGS = -Wall -Werror -Wextra -pedantic -Wno-unused -Wno-unused-parameter -std=gnu99 -fPIC -g3 -DASTNODE_ADD_PARENT=$(ASTNODE_ADD_PARENT)
DBG_CFLAGS = $(COMMON_CFLAGS) -O0
CFLAGS = $(COMMON_CFLAGS) -O2 -DNDEBUG
COMMON_IFLAGS = -Iinclude -Ilib/TypeMemPools/include/
DBG_IFLAGS = $(COMMON_IFLAGS)
IFLAGS = $(COMMON_IFLAGS)
COMMON_LFLAGS = -Llib '-Wl,--rpath,$(PREFIX)/lib'
LIB_LFLAGS = $(COMMON_LFLAGS) -L/usr/local/lib
DBG_LFLAGS = $(COMMON_LFLAGS) -lpeg4cd
LFLAGS = $(COMMON_LFLAGS) -lpeg4c

EXT_LIB_OBJS = lib/TypeMemPools/src/mempool.o
DBG_EXT_LIB_OBJS = lib/TypeMemPools/src/mempool.do
LIB_OBJS = src/astnode.o src/hash_utils.o src/packrat_cache.o src/parser.o src/rule.o src/token.o src/utils.o
DBG_LIB_OBJS = src/astnode.do src/hash_utils.do src/packrat_cache.do src/parser.do src/rule.do src/token.do src/utils.do
EXE_OBJS = src/peg4c.o src/peg4cparser.o src/peg4cstring.o src/peg4cbuild.o src/peg4ctransform.o

all: build_paths lib/lib$(NAME).so lib/lib$(NAME)d.so bin/$(NAME) bin/test

# OpenBSD weirdness
.MAIN: all

install: all
	install bin/$(NAME) $(PREFIX)/bin
	install lib/lib$(NAME).so $(PREFIX)/lib
	install include/peg4c/* $(PREFIX)/include/peg4c

bin/test: lib/lib$(NAME)d.so
	@touch examples/peg4c/peg4c.grmr
	@(cd tests && unset MAKELEVEL && make $(SUB_MAKE_ARGS))
	@(echo "running tests")
	$@ --verbose
	@(cd examples/peg4c && touch peg4c.grmr && unset MAKELEVEL && make $(SUB_MAKE_ARGS) test)

ext_libs: $(EXT_LIB_OBJS) $(DBG_EXT_LIB_OBJS)
	@(cd lib/TypeMemPools && unset MAKELEVEL && make $(SUB_MAKE_ARGS))
	@cp lib/TypeMemPools/include/* include/peg4c/

clean:
	@rm -f src/*.o src/*.do src/*.ast include/peg4c/mempool.h
	@rm -f lib/*.so
	@rm -rf bin
	@(cd tests && unset MAKELEVEL && make clean)
	@(cd lib/TypeMemPools && unset MAKELEVEL && make clean)
	@(cd examples/peg4c && unset MAKELEVEL && make clean)

build_paths:
	@mkdir -p bin

lib/lib$(NAME).so: build_paths ext_libs $(LIB_OBJS)
	@echo building library $@
	@if [ -n "$$USE_PCRE2"] || [ $$(uname -s) != "Linux" ] ; then export USE_PCRE2="-lpcre2-8" ; else export USE_PCRE2="" ; fi ; \
	$(CC) -shared -fPIC $(LIB_OBJS) $(EXT_LIB_OBJS) -o $@ $(LIB_LFLAGS) $$USE_PCRE2

lib/lib$(NAME)d.so: build_paths ext_libs $(DBG_LIB_OBJS)
	@echo building debug library $@
	@if [ $$(uname -s) != "Linux" ] ; then export USE_PCRE2="-lpcre2-8" ; else export USE_PCRE2="" ; fi ; \
	$(CC) -shared -fPIC $(DBG_LIB_OBJS) $(DBG_EXT_LIB_OBJS) -o $@ $(LIB_LFLAGS) $$USE_PCRE2

bin/$(NAME): build_paths lib/lib$(NAME).so $(EXE_OBJS)
	@echo building peg4c executable $@
	$(CC) $(CFLAGS) $(IFLAGS) $(EXE_OBJS) -o $@ $(LFLAGS)

.SUFFIXES: .c .o .do
# should build in a compile flag to use PCRE2 if USE_PCRE2 is set
.c.o:
	@$(CC) $(CFLAGS) $(IFLAGS) -c $< -o $@

.c.do:
	@if [ -n "$(SANITIZE)" ] ; then export DBGOPT="-fsanitize=address,undefined"; else export DBGOPT="" ; fi ; \
	$(CC) $(DBG_CFLAGS) $$DBGOPT $(DBG_IFLAGS) -c $< -o $@
