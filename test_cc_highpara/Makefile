CPP = g++ -std=c++11 -O3 -lpthread -O2
INCLUDE = -I../include -I../lib/libcuckoo/src -I../lib/libcuckoo/cityhash-1.1.1/src
LIBS = ../src/libgalloc.a -libverbs -lpthread ../lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a -lboost_thread -lboost_system
CFLAGS += -g -rdynamic

build: benchmark

benchmark: benchmark.cc
	$(CPP) $(CFLAGS) $(INCLUDE) -o $@ $^ $(LIBS) 

clean:
	rm -rf benchmark
