CC = gcc
CFLAGS = -Wall -I. -Iinc
LDFLAGS = -lm
BUILD_DIR = build
TARGET = $(BUILD_DIR)/slave_service
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/tuya_protocol.o $(BUILD_DIR)/cJSON.o

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
