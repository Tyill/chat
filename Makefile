CC = g++
CFLAGS = -c -Wall -g -I/usr/local/include
SOURCES = $(CURDIR)/main.cpp
OBJECTS = $(SOURCES:.cpp=.o)
EXECUTABLE = serg
   
all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf *.o