CC = gcc
CFLAGS = -Wall -Iinc
LDFLAGS = -lm
TARGET = slave_service
SRCDIR = src

# Object files: main.o (from current dir) + others (from src dir)
OBJS = main.o $(patsubst $(SRCDIR)/%.c, %.o, $(wildcard $(SRCDIR)/*.c))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	rm -f $(OBJS)

# Compile main.c (in current directory)
main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile other sources (in src directory)
%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
