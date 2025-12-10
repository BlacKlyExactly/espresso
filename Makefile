CC = gcc
CFLAGS = -Wall -Wextra -O2 -I. -Ivendor/cJSON

BACKEND_SRC = espresso.c vendor/cJSON/cJSON.c
BACKEND_OBJ = $(BACKEND_SRC:.c=.o)

MAIN_SRC = main.c
MAIN_OBJ = $(MAIN_SRC:.c=.o)

LIBNAME = libespresso.a
TARGET = main

.PHONY: all clean

all: $(LIBNAME) $(TARGET)

$(LIBNAME): $(BACKEND_OBJ)
	ar rcs $@ $^

$(TARGET): $(MAIN_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(BACKEND_OBJ) $(MAIN_OBJ) $(LIBNAME) $(TARGET)
