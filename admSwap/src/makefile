admSwap: admSwap.c

	gcc -I"/home/utnso/tp-2015-2c-elquevaaaprobar/sockets" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"admSwap.d" -MT"admSwap.d" -o "admSwap.o" "admSwap.c"

	gcc -L"/home/utnso/tp-2015-2c-elquevaaaprobar/sockets" -o "admSwap"  admSwap.o   -lsockets -lcommons -lpthread

.PHONY: clean
clean:
	rm -rf  admSwap.o  admSwap.d  admSwap


