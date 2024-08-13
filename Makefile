ifneq ($(shell pkg-config --exists libconfig && echo 0), 0)
$(error "libconfig is not installed")
endif

ifneq ($(shell pkg-config --exists libdpdk && echo 0), 0)
CFLAGS = $(shell pkg-config --cflags libconfig)
LDFLAGS = $(shell pkg-config --libs-only-L libconfig)
LDLIBS = $(shell pkg-config --libs-only-l libconfig)
else
CFLAGS = $(shell pkg-config --cflags libconfig libdpdk)
LDFLAGS = $(shell pkg-config --libs-only-L libconfig libdpdk)
LDLIBS = $(shell pkg-config --libs-only-l libconfig libdpdk)
CFLAGS += -DUSE_RTE_MEMPOOL
endif


CFLAGS += -Wall -Werror  -O3
INCLUDES = -I./ -I./test/unity -I./perf
LDFLAGS += -libverbs
LIBS=-pthread


TEST_DIR=test
PERF_DIR=perf
UNITY_DIR=test/unity
BIN_DIR=bin
EXAMPLE_DIR=examples

SRC_FILES = $(wildcard *.c)
EXAMPLE_FILES = $(wildcard $(EXAMPLE_DIR)/*.c)
TEST_FILES = $(wildcard $(TEST_DIR)/*.c)
PERF_FILES = $(wildcard $(PERF_DIR)/*.c)
UNITY_FILES = $(UNITY_DIR)/unity.c

SRC_OBJS=$(SRC_FILES:.c=.o)
EXAMPLE_OBJS=$(EXAMPLE_FILES:.c=.o)
TEST_OBJS=$(TEST_FILES:.c=.o)
PERF_OBJS=$(PERF_FILES:.c=.o)
UNITY_OBJS=$(UNITY_FILES:.c=.o)

PROG=$(BIN_DIR)/rdma-bench $(BIN_DIR)/rc_connection
TEST_EXEC=$(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/%,$(TEST_FILES))


all: clean $(PROG) $(TEST_EXEC)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BIN_DIR)/rdma-bench: $(SRC_OBJS) $(PERF_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(PERF_DIR)/rdma-bench.o $(PERF_DIR)/rdma-bench_cfg.o $(PERF_DIR)/client.o $(PERF_DIR)/server.o $(PERF_DIR)/setup_ib.o $(SRC_OBJS) $(LDFLAGS) $(LIBS) $(LDLIBS)

$(BIN_DIR)/rc_connection: $(SRC_OBJS) $(EXAMPLE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(EXAMPLE_DIR)/rc_connection.o $(SRC_OBJS) $(LDFLAGS) $(LIBS) $(LDLIBS)

$(TEST_EXEC): $(filter-out main.o, $(SRC_OBJS)) $(TEST_OBJS) $(UNITY_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS) $(LIBS) $(LDLIBS)


.PHONY: clean format bear debug
clean:
	$(RM) *.o $(TEST_DIR)/*.o $(UNITY_DIR)/*.o $(PERF_DIR)/*.o $(EXAMPLE_DIR)/*.o *~ $(BIN_DIR)/* compile_commands.json *.log

format:
	@ clang-format -i $(TEST_DIR)/*.c  $(PERF_FILES) $(PERF_DIR)/*.h $(SRC_FILES) *.h $(EXAMPLE_FILES)

bear:
	@if command -v bear >/dev/null ; then \
		echo "Bear is installed, generating compile_commands.json"; \
		bear -- make debug; \
	else \
		echo "Bear is not installed, skipping generation of compile_commands.json"; \
	fi

debug: CFLAGS := -Wall -Werror -Wno-string-conversion -O0 -g -DDEBUG
debug: clean all
