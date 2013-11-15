all: xish

xish: xish.c
	gcc -pedantic -Wall -g -o xish xish.c
