/*
 ============================================================================
 Name        : admSwap.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <commons/config.h>
#include <commons/log.h>
#include <commons/process.h>
#include <commons/string.h>
#include <commons/collections/list.h>
#include <commons/collections/queue.h>

#define BACKLOG 5

t_log* archivoLog;
char* ipAdmMemoria;
char* puertoAdmMemoria;
char* puertoEscucha;
char* nombreSwap;
int cantidadPaginas;
int tamanioPagina;
unsigned retardoCompactacion; //son segundos sino lo cambio a int

void configurarAdmSwap(char* config);
int configurarSocketServidor(int listeningSocket);

int main(int argc, char** argv) {
	//Creo el archivo de logs
			archivoLog = log_create("log_SWAP", "SWAP", 1, 0);
			log_info(archivoLog, "Archivo de logs creado.\n");
			//Chequeo de argumentos
			if(argc < 1){
				log_error(archivoLog, "Falta el archivo de configuraciones.\n");
			}

		//TODO Leer archivo de configuracion y extraer variables
			configurarAdmSwap(argv[1]);





		return 0;

}

void configurarAdmSwap(char* config) {
	t_config* configurarAdmSwap = config_create(config);
	if (config_has_property(configurarAdmSwap, "NOMBRE_SWAP"))
		nombreSwap = config_get_string_value(configurarAdmSwap, "NOMBRE_SWAP");
	if (config_has_property(configurarAdmSwap, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_string_value(configurarAdmSwap,
				"PUERTO_ESCUCHA");
	if (config_has_property(configurarAdmSwap, "IP_MEMORIA"))
		ipAdmMemoria = config_get_string_value(configurarAdmSwap, "IP_MEMORIA");
	if (config_has_property(configurarAdmSwap, "PUERTO_MEMORIA"))
		puertoAdmMemoria = config_get_string_value(configurarAdmSwap, "PUERTO_MEMORIA");
	if (config_has_property(configurarAdmSwap, "CANTIDAD_PAGINAS"))
		cantidadPaginas = config_get_int_value(configurarAdmSwap,
				"CANTIDAD_PAGINAS");
	if (config_has_property(configurarAdmSwap, "TAMANIO_PAGINA"))
		tamanioPagina = config_get_int_value(configurarAdmSwap,
				"TAMANIO_PAGINA");
	if (config_has_property(configurarAdmSwap, "RETARDO_COMPACTACION"))
		retardoCompactacion = config_get_int_value(configurarAdmSwap,
				"RETARDO_COMPACTACION");

	config_destroy(configurarAdmSwap);
}

int configurarSocketServidor(int listeningSocket){
	int status;
	int yes = 1;
	struct addrinfo hints, *serverInfo, *p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if((status = getaddrinfo(NULL, puertoEscucha, &hints, &serverInfo)) == -1) {
	   return 0;
	}
	for(p = serverInfo; p != NULL; p = p->ai_next) {
	if((listeningSocket = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol)) == -1){
		return 0;
	}
	//fcntl(listeningSocket, F_SETFD, O_NONBLOCK);
	//No estoy seguro de esta funcion
	if (setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
	    return 0;
	}
	if((bind(listeningSocket, serverInfo->ai_addr, serverInfo->ai_addrlen)) == -1){
		return 0;
	}
	break;
	}

    if (p == NULL)  {
       	fprintf(stderr, "server: failed to bind\n");
    }

	freeaddrinfo(serverInfo);

	if ((listen(listeningSocket, BACKLOG)) == -1){
			log_error(archivoLog, "Error en el listen");
	}
	return 1;
}
