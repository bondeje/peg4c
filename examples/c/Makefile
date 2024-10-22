.POSIX:
# just in case FreeBSD's make implementation screws up the current directory
.OBJDIR: .
CC = gcc
EXAMPLE_NAME = c
BIN_DIR = ../../bin
STDC_DIALECT = -std=c99
GRAMMAR = $(EXAMPLE_NAME).grmr
DEFAULT_LOG_LEVEL = LOG_LEVEL_WARN
CFLAGS = -Wall -Werror -Wextra -pedantic -Wno-unused -Wno-unused-parameter $(STDC_DIALECT) -O0 -g -fPIC
IFLAGS = -I../../include -I../../lib/TypeMemPools/include
LFLAGS = '-Wl,-rpath,$$ORIGIN/.' -L$(BIN_DIR) -lpeg4c

EXE_SRCS = cparser.c c.c cpp.c cstring.c token_.c os.c config.c common.c

all: $(BIN_DIR)/c

.MAIN: all

$(BIN_DIR)/c: $(GRAMMAR)
	$(BIN_DIR)/peg4c $(GRAMMAR)
	if [ -n "$(SANITIZE)" ] ; then export DBGOPT="-fsanitize=address,undefined"; else export DBGOPT="-DNDEBUG"; fi ; \
	$(CC) $(CFLAGS) $$DBGOPT $(IFLAGS) $(EXE_SRCS) -o $@ $(LFLAGS)

test: $(BIN_DIR)/c
	@for file in sample_files/*.c; do echo $$file && $ $(BIN_DIR)/c $$file > $$file.ast; done

check_peg4c: $(BIN_DIR)/c
	@for file in ../../src/*.c; do echo $$file && $(CC) -E -I../../include $$file | $(BIN_DIR)/c > $$file.ast; done

check_libc: $(BIN_DIR)/c
	@echo using $(CC) with dialect $(STDC_DIALECT)
	@for file in sample_files/libc/*.c; do echo $$file && $(CC) $(STDC_DIALECT) -E $$file | $(BIN_DIR)/c > $$file.ast; done

clean:
	@rm -f c.h c.c sample_files/*.ast sample_files/libc/*.ast *.ast $(BIN_DIR)/c
