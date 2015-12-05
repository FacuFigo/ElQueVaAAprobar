planificador: planificador.c

	gcc -I"/home/utnso/tp-2015-2c-elquevaaaprobar/sockets" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"planificador.d" -MT"planificador.d" -o "planificador.o" "planificador.c"

	gcc -L"/home/utnso/tp-2015-2c-elquevaaaprobar/sockets" -o "planificador"  planificador.o   -lsockets -lcommons -lpthread

.PHONY: clean
clean:
	rm -rf  planificador.o  planificador.d  planificador


