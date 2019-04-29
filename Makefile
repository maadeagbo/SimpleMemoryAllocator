all:
	g++ -no-pie -Wall -rdynamic -ggdb -std=c++14 -o memalloc_test *.cpp *.c
	rm -rf *.o

clean:
	rm -rf *.o memalloc_test