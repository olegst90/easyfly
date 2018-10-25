all: transport.h utils.h
	gcc -I../esp8285 transport.c utils.c test.c -lrt -o test

test: ltest.c ../esp8285/ulist.h
	gcc -I../esp8285 ltest.c -o ltest   

csvdec: csvdec.c utils.c utils.h
	gcc -I../esp8285 utils.c csvdec.c -o csvdec
    
.PHONY: clean

clean:
	rm -f test ltest
