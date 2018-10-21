all: transport.h
	gcc -I../esp8285 transport.c test.c -lrt -o test

test: ltest.c ../esp8285/ulist.h
	gcc -I../esp8285 ltest.c -o ltest   

.PHONY: clean

clean:
	rm -f test ltest
