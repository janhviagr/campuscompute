CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11 -g -Iinclude -pthread -MMD -MP
LDLIBS  = -pthread -lrt
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:src/%.c=build/%.o)
TARGET  = campuscompute

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build $(TARGET) campus.log

-include $(OBJ:.o=.d)

.PHONY: all clean
