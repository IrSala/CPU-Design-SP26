CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

TARGET  := assembler
SRCS    := main.cpp assembler.cpp
HEADERS := assembler.h isa.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(TARGET) *.bin

.PHONY: all clean
