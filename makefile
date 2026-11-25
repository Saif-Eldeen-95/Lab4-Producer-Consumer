
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
LDFLAGS = -pthread -lrt

all: producer consumer

producer: producer.cpp
	$(CXX) $(CXXFLAGS) producer.cpp -o producer $(LDFLAGS)

consumer: consumer.cpp
	$(CXX) $(CXXFLAGS) consumer.cpp -o consumer $(LDFLAGS)

cleanup:
	rm -f /dev/shm/SH_Mem
	rm -f /dev/shm/sem.mutex
	rm -f /dev/shm/sem.empty
	rm -f /dev/shm/sem.full
	rm -f /dev/shm/sem.init_lock
	
clean:
	rm -f producer consumer
