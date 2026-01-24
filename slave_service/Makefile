CC = gcc
CFLAGS = -Wall -Iinc
SRC = main.c src/utils.c src/cJSON.c src/tuya_protocol.c src/log.c src/md5.c src/ota_handler.c
OBJ_DIR = obj
OBJ = $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRC))
TARGET = slave_service

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJ_DIR)

distclean: clean
	rm -f $(TARGET)

.PHONY: all clean distclean
