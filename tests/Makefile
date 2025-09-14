.POSIX:
.OBJDIR: ./
CC = gcc
NAME = test
BIN_DIR = ../bin
DBG_LOG_LEVEL=LOG_LEVEL_DEBUG
ASTNODE_ADD_PARENT = 0
CFLAGS = -Wall -Werror -Wextra -pedantic -Wno-unused -Wno-unused-parameter -std=gnu99 -O0 -g2 -DMAX_LOGGING_LEVEL=$(DBG_LOG_LEVEL) -DASTNODE_ADD_PARENT=$(ASTNODE_ADD_PARENT)
LFLAGS = '-Wl,--rpath,$(PREFIX)/lib' -L$(PREFIX)/lib -lpeg4cd
IFLAGS = -I../include -I../lib/TypeMemPools/include

EXE_SRCS = test_parser.c test_driver.c test_utils.c test_peg4c_utils.c test_rules.c 

all: clean $(BIN_DIR)/test

# OpenBSD weirdness
.MAIN: all

clean:
	@rm -f *.o test *.txt *.so $(BIN_DIR)/test

$(BIN_DIR)/test: $(EXE_SRCS)
	@(echo "\nbuilding tests")
	if [ -n "$(SANITIZE)" ] ; then export DBGOPT="-fsanitize=address,undefined"; else export DBGOPT="" ; fi ; \
	$(CC) $(CFLAGS) $$DBGOPT $(IFLAGS) $(EXE_SRCS) -o $@ $(LFLAGS)
