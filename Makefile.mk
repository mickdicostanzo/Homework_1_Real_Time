all: filter store

filter: filter.o rt-lib.o
	gcc -o filter filter.o rt-lib.o -lrt -pthread -lm

store: store.o 
	gcc -o store store.o -lrt -pthread

filter.o: filter.c rt-lib.h 
	gcc -c filter.c

store.o: store.c  
	gcc -c store.c

rt-lib.o: rt-lib.c rt-lib.h
	gcc -c rt-lib.c

clean:
	rm *.o filter store
