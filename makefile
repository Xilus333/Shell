all: shell

shell: shell.c
	gcc -pedantic -Wall -g -o xish xish.c