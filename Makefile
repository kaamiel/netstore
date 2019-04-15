TARGET: netstore-server netstore-client

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pedantic
LFLAGS = -Wall -Wextra -O2 -pedantic

netstore-server.o netstore-client.o: common.h err.h

netstore-server: netstore-server.o err.o
	$(CC) $(LFLAGS) $^ -o $@

netstore-client: netstore-client.o err.o
	$(CC) $(LFLAGS) $^ -o $@


.PHONY: clean TARGET
clean:
	rm -f netstore-server netstore-client *.o

