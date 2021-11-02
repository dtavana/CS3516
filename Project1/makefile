CC=gcc

all: server client outdir

outdir:
	mkdir out/

server: server.c
	 $(CC) -o server server.c

client: client.c
	 $(CC) -o client client.c

clean: 
	/bin/rm -f *.o server client && rmdir out/