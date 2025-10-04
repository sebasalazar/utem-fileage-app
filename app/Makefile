CXX = g++
CXXFLAGS = -g3 -Wall -Wextra -Wpedantic -std=c++17 -fopenmp -march=native
MKDIR = mkdir -p

LIBS = -lm -lboost_atomic -latomic -ltbb -lboost_thread -lboost_system

directorios:
	$(MKDIR) build dist

build/Edad.o: directorios Edad.cpp
	$(CXX) $(CXXFLAGS) -c Edad.cpp -o build/Edad.o

build/main.o: directorios main.cpp
	$(CXX) $(CXXFLAGS) -c main.cpp -o build/main.o

build/simple.o: directorios simple.cpp
	$(CXX) $(CXXFLAGS) -c simple.cpp -o build/simple.o

all: clean build/main.o build/simple.o build/Edad.o
	$(CXX) $(CXXFLAGS) -o dist/paralelo \
	build/main.o \
	build/Edad.o \
	$(LIBS)
	
	$(CXX) $(CXXFLAGS) -o dist/simple \
	build/simple.o \
	build/Edad.o \
	-lm
	rm -fr build

clean:
	rm -fr *.o a.out core dist build

.DEFAULT_GOAL := all
