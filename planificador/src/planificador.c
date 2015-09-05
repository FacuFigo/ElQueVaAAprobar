/*
 ============================================================================
 Name        : planificador.c
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
char* puertoEscucha;
char* algoritmo;
int quantum;

void configurarPlanificador(char* config);
int configurarSocketServidor(int listeningSocket);

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_Planificador", "Planificador", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");
	//Chequeo de argumentos
	if(argc < 1){
		log_error(archivoLog, "Falta el archivo de configuraciones.\n");
	}

//TODO Leer archivo de configuracion y extraer variables
	configurarPlanificador(argv[1]);

	return 0;

}

void configurarPlanificador(char* config){

	t_config* configPlanificador = config_create(config);
	if(config_has_property(configPlanificador,"PUERTO_ESCUCHA"))
		puertoEscucha = config_get_string_value(configPlanificador, "PUERTO_ESCUCHA");
	if(config_has_property(configPlanificador, "ALGORITMO_PLANIFICADOR"))
		algoritmo = config_get_string_value(configPlanificador, "ALGORITMO_PLANIFICADOR");
	if(config_has_property(configPlanificador, "QUANTUM"))
		quantum = config_get_int_value(configPlanificador, "QUANTUM");
	config_destroy(configPlanificador);
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

