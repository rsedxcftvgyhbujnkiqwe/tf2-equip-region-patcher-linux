CC = gcc
CFLAGS = -Wall
TARGETS = equip-patcher class-patcher

all: $(TARGETS)

equip-patcher: tf2-patcher.c
	$(CC) $(CFLAGS) -o $@ $<

class-patcher: tf2-class-patcher.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
