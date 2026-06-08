CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O2 -D_FILE_OFFSET_BITS=64
INCLUDES = -Iinclude
SRC = src/zar_header.cpp src/zar_utils.cpp src/zar_store.cpp
OBJ = $(SRC:.cpp=.o)
BIN_DIR = bin

all: $(BIN_DIR)/example_write $(BIN_DIR)/example_read $(BIN_DIR)/zar_endian_convert

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/example_write: examples/example_write.cpp $(SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

$(BIN_DIR)/example_read: examples/example_read.cpp $(SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

$(BIN_DIR)/zar_endian_convert: tools/zar_endian_convert.cpp $(SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

clean:
	rm -rf $(BIN_DIR) $(OBJ) example.zar out_dir

.PHONY: all clean
