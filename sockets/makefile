sockets: sockets.c

	gcc -O0 -g3 -Wall -c -fmessage-length=0 -fPIC -MMD -MP -MF"sockets.d" -MT"sockets.d" -o "sockets.o" "sockets.c"

	gcc -shared -o "libsockets.so"  ./sockets.o 

.PHONY: clean
clean:
	rm -rf  sockets.o  sockets.d  libsockets.so


