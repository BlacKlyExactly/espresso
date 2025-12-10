CC = gcc
CFLAGS = -Wall -Wextra -O2 -I. -Ivendor/cJSON

BACKEND_SRC = espresso.c vendor/cJSON/cJSON.c
BACKEND_OBJ = $(BACKEND_SRC:.c=.o)

MAIN_SRC = main.c
MAIN_OBJ = $(MAIN_SRC:.c=.o)

LIBNAME = libespresso.a
TARGET = main

ifdef IS_CI_BUILD
  CI_LDFLAGS = -mwindows
else
  CI_LDFLAGS =
endif


ifeq ($(OS),Windows_NT)
  LDFLAGS = -lws2_32 $(CI_LDFLAGS)
  RM = del /Q
else
  LDFLAGS = -lpthread
  RM = rm -f
endif

.PHONY: all clean

all: $(LIBNAME) $(TARGET)

$(LIBNAME): $(BACKEND_OBJ)
	ar rcs $@ $^

$(TARGET): $(MAIN_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) -o $@ $(MAIN_OBJ) $(LIBNAME) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(BACKEND_OBJ) $(MAIN_OBJ) $(LIBNAME) $(TARGET)
