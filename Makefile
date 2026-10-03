CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
INCLUDE := -Iinclude

SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
BIN := bin/puzzle

TEST_CLI_BIN := bin/test_cli
TEST_CLI_SRC := tests/test_cli.c src/cli.c

TEST_PUZZLE_BIN := bin/test_puzzle
TEST_PUZZLE_SRC := tests/test_puzzle.c src/puzzle.c src/heap.c src/state_table.c src/pool.c

TEST_POOL_BIN := bin/test_pool
TEST_POOL_SRC := tests/test_pool.c src/pool.c

TEST_HEAP_BIN := bin/test_heap
TEST_HEAP_SRC := tests/test_heap.c src/heap.c

TEST_STATE_TABLE_BIN := bin/test_state_table
TEST_STATE_TABLE_SRC := tests/test_state_table.c src/state_table.c

TEST_ASTAR_BIN := bin/test_astar
TEST_ASTAR_SRC := tests/test_astar.c src/puzzle.c src/heap.c src/state_table.c src/pool.c

.PHONY: all test test-cli test-puzzle test-pool test-heap test-state-table test-astar clean

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OBJ) -o $@

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

$(TEST_CLI_BIN): $(TEST_CLI_SRC) include/cli.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) $(TEST_CLI_SRC) -o $@

test-cli: $(TEST_CLI_BIN)
	./$(TEST_CLI_BIN)

$(TEST_PUZZLE_BIN): $(TEST_PUZZLE_SRC) include/puzzle.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) $(TEST_PUZZLE_SRC) -o $@

test-puzzle: $(TEST_PUZZLE_BIN)
	./$(TEST_PUZZLE_BIN)

$(TEST_POOL_BIN): $(TEST_POOL_SRC) include/pool.h include/node.h tests/test_util.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) $(TEST_POOL_SRC) -o $@

test-pool: $(TEST_POOL_BIN)
	./$(TEST_POOL_BIN)

$(TEST_HEAP_BIN): $(TEST_HEAP_SRC) include/heap.h include/node.h tests/test_util.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) $(TEST_HEAP_SRC) -o $@

test-heap: $(TEST_HEAP_BIN)
	./$(TEST_HEAP_BIN)

$(TEST_STATE_TABLE_BIN): $(TEST_STATE_TABLE_SRC) include/state_table.h include/node.h tests/test_util.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) $(TEST_STATE_TABLE_SRC) -o $@

test-state-table: $(TEST_STATE_TABLE_BIN)
	./$(TEST_STATE_TABLE_BIN)

$(TEST_ASTAR_BIN): $(TEST_ASTAR_SRC) include/puzzle.h include/heap.h include/state_table.h include/pool.h include/node.h tests/test_util.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) $(TEST_ASTAR_SRC) -o $@

test-astar: $(TEST_ASTAR_BIN)
	./$(TEST_ASTAR_BIN)

test: test-cli test-puzzle test-pool test-heap test-state-table test-astar $(BIN)
	./tests/run_tests.sh

clean:
	rm -rf build bin
