all: filter store 

filter: filter.o rt-lib.o
	gcc -o filter filter.o rt-lib.o -lrt -pthread -lm

store: store.o 
	gcc -o store store.o -lrt -pthread

filter.o: filter.c rt-lib.h 
	gcc -Wall -Wextra -c filter.c

store.o: store.c
	gcc -Wall -Wextra -c store.c

rt-lib.o: rt-lib.c rt-lib.h
	gcc -Wall -c rt-lib.c

clean:
	rm -f *.o filter store
