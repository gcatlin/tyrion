CFLAGS=-std=c11 -Wall -Werror -pedantic

.PHONY: build clean expand format release run

build:
	$(CC) $(CFLAGS) -g main.c

disasm:
	$(CC) $(CFLAGS) -s main.c

clean:
	$(RM) -rf a.out.dSYM/ a.out main.s

expand:
	$(CC) $(CFLAGS) -E main.c

format:
	clang-format -i main.c

release:
	$(CC) $(CFLAGS) -O2 main.c

run: build
	./a.out