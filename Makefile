
SRC_PS2MDBP = ps2master-patcher.c

CC = gcc
CFLAGS = -Wall -Wextra

TARGET_PS2MDBP = ps2-master-patcher

INSTALL_DIR = /usr/local/bin

RM = rm
CP = cp

all: ps2-master-patcher

ps2-master-patcher:
	$(CC) $(CFLAGS) $(SRC_PS2MDBP) -o $(TARGET_PS2MDBP)

install: ps2-master-patcher
	$(CP) $(TARGET_PS2MDBP) $(INSTALL_DIR)

clean:
	$(RM) $(TARGET_PS2MDBP)
