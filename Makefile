CC = gcc
CFLAGS = -Wall
TARGETS = equip-patcher

all: $(TARGETS)

equip-patcher: tf2-patcher.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
