source_files := $(shell find CSpydr/src -name *.c)

.PHONY: build
build:
	mkdir -p bin/ && \
	mkdir -p bin/Linux && \
	gcc -g -lm $(source_files) -o bin/Linux/CSpydr.out