
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
LDFLAGS = -pthread -lrt

all: producer consumer

producer: producer.cpp
	$(CXX) $(CXXFLAGS) producer.cpp -o producer $(LDFLAGS)

consumer: consumer.cpp
	$(CXX) $(CXXFLAGS) consumer.cpp -o consumer $(LDFLAGS)

clean:
	rm -f producer consumer
