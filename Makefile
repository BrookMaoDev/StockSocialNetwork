#
# Compiler
#
#
CXX = g++

#
# Compiler flags
#
#
CXXFLAGS = -std=c++20 -Wall -O3

#
# Library directories
#
#
# Link pqxx statically. If linked dynamically, both our program and the shared
# library define the global `std::string const type_name`, and both resolve to
# the same object in memory. Each side destroys it at program exit, so it gets
# freed twice (double free).
LIBS = -l:libpqxx.a -lpq

#
# Source files
#
#
SRCS = $(wildcard src/*.cpp)

#
# Output executable
#
#
TARGET = stocksocial

#
# Default target
#
#
all: $(TARGET)

#
# Rule to build the target executable
#
#
$(TARGET): $(SRCS) $(wildcard src/*.hpp) sql/schema.sql
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SRCS) $(LIBS)

#
# Clean rule to remove the compiled executable
#
#
clean:
	rm -f $(TARGET)
