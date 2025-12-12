CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./src -I./vendor/cJSON
SRC_DIR = src
TEST_DIR = tests
VENDOR_DIR = vendor/cJSON

SRC = $(SRC_DIR)/espresso.c $(SRC_DIR)/main.c $(VENDOR_DIR)/cJSON.c
OBJ = $(SRC:.c=.o)

LIB = libespresso.a
TARGET = main

UNIT_TEST_SRC = $(TEST_DIR)/unit-tests.c
UNIT_TEST_OBJ = $(UNIT_TEST_SRC:.c=.o)
UNIT_TEST_BIN = test_unit

INTEGRATION_TEST_SRC = $(TEST_DIR)/integration-tests.c
INTEGRATION_TEST_OBJ = $(INTEGRATION_TEST_SRC:.c=.o)
INTEGRATION_TEST_BIN = test_integration

ifeq ($(OS),Windows_NT)
    RM = del /Q
    CFLAGS += -I/mingw64/include
    LDFLAGS = -L/mingw64/lib -luv
    CHECK_CFLAGS = -I/mingw64/include
    CHECK_LDFLAGS = -L/mingw64/lib -lcheck
else
    RM = rm -f
    CFLAGS += $(shell pkg-config --cflags check libuv)
    LDFLAGS = $(shell pkg-config --libs libuv)
    CHECK_CFLAGS = $(shell pkg-config --cflags check)
    CHECK_LDFLAGS = $(shell pkg-config --libs check)
endif

.PHONY: all clean test_unit test_integration

all: $(LIB) $(TARGET)

$(LIB): $(OBJ)
	ar rcs $@ $^

$(TARGET): $(LIB) $(SRC_DIR)/main.o
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/main.o $(LIB) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test_unit: $(LIB) $(UNIT_TEST_OBJ)
ifeq ($(OS),Windows_NT)
	$(CC) $(CFLAGS) -o $(UNIT_TEST_BIN) $(UNIT_TEST_OBJ) $(LIB) $(LDFLAGS)
	$(UNIT_TEST_BIN)
else
	$(CC) $(CFLAGS) $(CHECK_CFLAGS) -o $(UNIT_TEST_BIN) $(UNIT_TEST_OBJ) $(LIB) $(CHECK_LDFLAGS) $(LDFLAGS)
	./$(UNIT_TEST_BIN)
endif

test_integration: $(LIB) $(INTEGRATION_TEST_OBJ)
ifeq ($(OS),Windows_NT)
	$(CC) $(CFLAGS) -o $(INTEGRATION_TEST_BIN) $(INTEGRATION_TEST_OBJ) $(LIB) $(LDFLAGS)
	$(INTEGRATION_TEST_BIN)
else
	$(CC) $(CFLAGS) $(CHECK_CFLAGS) -o $(INTEGRATION_TEST_BIN) $(INTEGRATION_TEST_OBJ) $(LIB) $(CHECK_LDFLAGS) $(LDFLAGS) -lpthread
	./$(INTEGRATION_TEST_BIN)
endif

clean:
ifeq ($(OS),Windows_NT)
	$(RM) $(OBJ) $(UNIT_TEST_OBJ) $(INTEGRATION_TEST_OBJ) $(LIB) $(TARGET) $(UNIT_TEST_BIN) $(INTEGRATION_TEST_BIN)
else
	$(RM) $(OBJ) $(UNIT_TEST_OBJ) $(INTEGRATION_TEST_OBJ) $(LIB) $(TARGET) $(UNIT_TEST_BIN) $(INTEGRATION_TEST_BIN)
endif
