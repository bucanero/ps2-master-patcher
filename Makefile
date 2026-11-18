
SRC_PS2MDBP = ps2master-patcher.c

CC = gcc
CFLAGS = -Wall -Wextra

TARGET_PS2MDBP = ps2-master-patcher

INSTALL_DIR = /usr/local/bin

RM = rm
CP = cp

all: $(TARGET_PS2MDBP)

# VERSION=x.y.z (or vx.y.z, like a tag) overrides the version in the source
VERSION ?=
VERSION_DEF = $(if $(VERSION),-DPATCHER_VERSION=\"$(patsubst v%,%,$(VERSION))\")

$(TARGET_PS2MDBP): $(SRC_PS2MDBP) $(wildcard *.h)
	$(CC) $(CFLAGS) $(VERSION_DEF) $(SRC_PS2MDBP) -o $(TARGET_PS2MDBP)

install: $(TARGET_PS2MDBP)
	$(CP) $(TARGET_PS2MDBP) $(INSTALL_DIR)

clean:
	$(RM) -f $(TARGET_PS2MDBP)

.PHONY: all install clean
