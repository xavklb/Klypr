CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Iinclude
LDFLAGS ?= -mwindows -luser32 -lgdi32 -lshell32

SRC = src/main.c \
      src/app.c \
      src/config.c \
      src/event.c \
      src/input.c \
      src/launcher.c \
      src/layout.c \
      src/window.c

OBJ = $(SRC:.c=.o)
TARGET = klypr.exe

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJ) $(TARGET) 2>/dev/null || del /q /f src\*.o $(TARGET) 2>nul

.PHONY: all clean
