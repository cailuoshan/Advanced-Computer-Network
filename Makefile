all: http-server

http-server: http-server.c
	gcc -Wall -g http-server.c -o http-server -lssl -lcrypto -lpthread
vlc-http-server: http-server.c
	gcc -Wall -g vlc-http-server.c -o vlc-http-server -lssl -lcrypto -lpthread
clean:
	@rm http-server
