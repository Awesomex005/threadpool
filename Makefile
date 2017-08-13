
FLAGS=-std=c99
all: tp_test.o thread_pool.o
	cc $(FLAGS) -o tp_test tp_test.o thread_pool.o -lpthread

tp_test.o: tp_test.c
	cc $(FLAGS) -c tp_test.c

thread_pool.o: thread_pool.c
	cc $(FLAGS) -c -lpthread thread_pool.c
clean:
	rm -rf tp_test tp_queue *.o
