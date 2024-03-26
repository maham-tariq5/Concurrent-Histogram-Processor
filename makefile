CFLAGS = -Wall -std=c11 -g 

all: parallel 

parallel: parallel.c 
	gcc $(CFLAGS) parallel.c -o parallel

clean:
	rm parallel