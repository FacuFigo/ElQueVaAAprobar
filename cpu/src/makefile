cpu: cpu.c

	gcc -I"/home/utnso/tp-2015-2c-elquevaaaprobar/sockets" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"cpu.d" -MT"cpu.d" -o "cpu.o" "cpu.c"

	gcc -L"/home/utnso/tp-2015-2c-elquevaaaprobar/sockets" -o "cpu" cpu.o   -lsockets -lcommons -lpthread

.PHONY: clean
clean:
	rm -rf  cpu.o  cpu.d  cpu


