src = $(wildcard *.c)
obj = $(src:.c=.o)

CC = gcc
CFLAGS = -Wall -std=c11 -g

notebook: $(obj)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(obj) notebook
