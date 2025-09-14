.POSIX:
# just in case FreeBSD's make implementation screws up the current directory
.OBJDIR: ./
CC = gcc
EXAMPLE_NAME = peg4c
PREFIX=.
BIN_DIR = ../../bin
GRAMMAR = $(EXAMPLE_NAME).grmr
ASTNODE_ADD_PARENT = 0
CFLAGS = -Wall -Werror -Wextra -pedantic -Wno-unused -Wno-unused-parameter -std=gnu99 -O2 -g -DMAX_LOGGING_LEVEL=$(BLD_LOG_LEVEL) -fPIC -DASTNODE_ADD_PARENT=$(ASTNODE_ADD_PARENT)
IFLAGS = -I../../include -I../../lib/lexre/include
LFLAGS = '-Wl,--rpath,$(PREFIX)/lib' -L$(PREFIX)/lib -lpeg4c

SRC_DIR = ../../src
INC_DIR = ../../include

INCS = $(INC_DIR)/peg4cbuild.h $(INC_DIR)/peg4cparser.h $(INC_DIR)/peg4cstring.h $(INC_DIR)/peg4ctransform.h $(INC_DIR)/peg4c.h
SRCS = $(SRC_DIR)/peg4cbuild.c $(SRC_DIR)/peg4cparser.c $(SRC_DIR)/peg4cstring.c $(SRC_DIR)/peg4ctransform.c $(SRC_DIR)/peg4c.c

EXE_SRCS = peg4cbuild.c peg4cparser.c peg4cstring.c peg4ctransform.c peg4c.c

all: $(BIN_DIR)/peg4c

# OpenBSD weirdness
.MAIN: all

$(BIN_DIR)/peg4c: $(GRAMMAR)
	@(echo "\nbuilding peg4c parser")
	@cp -f $(SRCS) $(INCS) .
	(if [ -n "$(SANITIZE)" ] ; then export DBGOPT="-fsanitize=address,undefined"; else export DBGOPT="-DNDEBUG"; fi ; \
	$(CC) $(CFLAGS) $$DBGOPT $(IFLAGS) $(EXE_SRCS) -o $@ $(LFLAGS))

rebuild:
	@cp -f $(SRCS) $(INCS) .
	(cd ../.. && bin/peg4c examples/peg4c/$(GRAMMAR) && mv -f peg4c.* examples/peg4c && \
	rm -f bin/peg4c)
	@(echo "\nbuilding peg4c parser")
	@(if [ -n "$(SANITIZE)" ] ; then export DBGOPT="-fsanitize=address,undefined"; else export DBGOPT="-DNDEBUG"; fi ; \
	$(CC) $(CFLAGS) $$DBGOPT $(IFLAGS) $(EXE_SRCS) -o $(BIN_DIR)/peg4c $(LFLAGS))

test: rebuild
	@(echo "running bootstrap tests")
	@mv -f peg4c.c peg4c_orig.c
	@mv -f peg4c.h peg4c_orig.h
	@(cd ../../ && bin/peg4c examples/peg4c/peg4c.grmr && mv peg4c.* examples/peg4c) || $(BIN_DIR)/peg4c peg4c.grmr 
	@(if [ -z "`comm -3 peg4c_orig.h peg4c.h`" ] && [ -z "`comm -3 peg4c_orig.c peg4c.c`" ] ; then echo "test peg4c bootstrap...passed" ; else echo "test peg4c bootstrap...failed" ; fi)

# be careful with this. will remove any version of peg4c that you have already built
clean:
	@rm -f *.c *.h peg4c *.o $(BIN_DIR)/peg4c
