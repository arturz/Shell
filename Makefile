default: shell

shell.o: shell.c
	gcc -c shell.c -o shell.o -Wall

shell: shell.o
	gcc shell.o -o shell -ltinfo -lncursesw -Wall

clean:
	-rm -f shell.o
	-rm -f shell