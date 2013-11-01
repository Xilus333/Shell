all: shell

shell: shell.c
	gcc -pedantic -Wall -g -o shell shell.c
