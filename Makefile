TARGET: netstore-server netstore-client

CC = gcc
CFLAGS = -Wall -Wextra -O2
LFLAGS = -Wall -Wextra

serwer.o klient.o: err.h

netstore-server: serwer.o err.o
	$(CC) $(LFLAGS) $^ -o $@

netstore-client: klient.o err.o
	$(CC) $(LFLAGS) $^ -o $@


.PHONY: clean TARGET
clean:
	rm -f *.o $(TARGET)

