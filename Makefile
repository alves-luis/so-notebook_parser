src = $(wildcard *.c)
obj = $(src:.c=.o)

CC = gcc 
CFLAGS = -Wall -std=c11 -g 

program: $(obj)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(obj) program