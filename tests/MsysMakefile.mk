.POSIX:
.OBJDIR: ./
CC = gcc
NAME = test
CFLAGS = -Wall -Werror -Wextra -pedantic -Wno-unused -Wno-unused-parameter -std=gnu99 -O0 -g3 `if [ -n "$(SANITIZE)" ] ; then echo "-fsanitize=address,undefined"; fi`
LFLAGS = '-Wl,-rpath,$$ORIGIN/../bin' -L. -lpeggyd
IFLAGS = -I../include -I../lib/logger/include -I../lib/TypeMemPools/include

TEST_OBJS = test_parser.o test_driver.o test_utils.o test_rules.o test_peggy_utils.o

all: clean get_shared test.exe

clean:
	@rm -f *.o
	@rm -f test.exe
	@rm -f *.txt
	@rm -f *.log
	@rm -f *.dll

get_shared:
	@cp ../bin/libpeggyd.dll ./

test.exe: $(TEST_OBJS)
	@$(CC) $(CFLAGS) $(IFLAGS) $(TEST_OBJS) -o $@ $(LFLAGS)

.c.o:
	@$(CC) $(CFLAGS) $(IFLAGS) -c $< -o $@