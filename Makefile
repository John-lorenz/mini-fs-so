# Compilador e flags
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -pedantic
TARGET  = mini_fs

# Alvo padrão
all: $(TARGET)

$(TARGET): mini_fs.c
	$(CC) $(CFLAGS) -o $(TARGET) mini_fs.c

# Compilação com informações de debug (para gdb/valgrind)
debug: mini_fs.c
	$(CC) $(CFLAGS) -g -fsanitize=address -o $(TARGET)_debug mini_fs.c

clean:
	rm -f $(TARGET) $(TARGET)_debug

.PHONY: all debug clean
