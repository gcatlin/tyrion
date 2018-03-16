all: build

build:
	@cc -g -std=c99 main.c

run: build
	@./a.out

