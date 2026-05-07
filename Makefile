CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

TARGET  := assembler_bin
SRCS    := main.cpp assembler/assembler.cpp
HEADERS := assembler/assembler.h isa/isa.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(TARGET) *.bin

.PHONY: all clean
