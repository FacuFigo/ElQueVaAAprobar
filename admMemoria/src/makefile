admMemoria: admMemoria.c

	gcc -I"/home/utnso/tp-2015-2c-elquevaaaprobar/sockets" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"admMemoria.d" -MT"admMemoria.d" -o ".o" "admMemoria.c"

	gcc -L"/home/utnso/tp-2015-2c-elquevaaaprobar/sockets/Debug" -o "admMemoria"  admMemoria.o   -lsockets -lcommons -lpthread

.PHONY: clean
clean:
	rm -rf  admMemoria.o  admMemoria.d  admMemoria


