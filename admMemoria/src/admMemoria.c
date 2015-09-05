/*
 ============================================================================
 Name        : admMemoria.c
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
char* ipSwap;
char* puertoSwap;
int maximoMarcosPorProceso;
int cantidadMarcos;
int tamanioMarco;
int entradasTLB;
char* TLBHabilitada;
int retardoMemoria;

void configurarAdmMemoria(char* config);
int configurarSocketCliente(int s, char* ip, char* puerto);
int configurarSocketServidor(int listeningSocket);


int main(int argc, char** argv) {
	//Creo el archivo de logs
	archivoLog = log_create("log_AdmMemoria", "AdmMemoria", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");
	//Chequeo de argumentos
	if (argc < 1) {
		log_error(archivoLog, "Falta el archivo de configuraciones.\n");
	}

	//TODO Leer archivo de configuracion y extraer variables
	configurarAdmMemoria(argv[1]);

	return 0;
}

void configurarAdmMemoria(char* config) {

	t_config* configurarAdmMemoria = config_create(config);
	if (config_has_property(configurarAdmMemoria, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_string_value(configurarAdmMemoria,
				"PUERTO_ESCUCHA");
	if (config_has_property(configurarAdmMemoria, "IP_SWAP"))
		ipSwap = config_get_string_value(configurarAdmMemoria, "IP_SWAP");
	if (config_has_property(configurarAdmMemoria, "PUERTO_SWAP"))
		puertoSwap = config_get_string_value(configurarAdmMemoria, "PUERTO_SWAP");
	if (config_has_property(configurarAdmMemoria, "MAXIMO_MARCOS_POR_PROCESO"))
		maximoMarcosPorProceso = config_get_int_value(configurarAdmMemoria,
				"MAXIMO_MARCOS_POR_PROCESO");
	if (config_has_property(configurarAdmMemoria, "CANTIDAD_MARCOS"))
		cantidadMarcos = config_get_int_value(configurarAdmMemoria,
				"CANTIDAD_MARCOS");
	if (config_has_property(configurarAdmMemoria, "TAMANIO_MARCO"))
		tamanioMarco = config_get_int_value(configurarAdmMemoria,
				"TAMANIO_MARCO");
	if (config_has_property(configurarAdmMemoria, "ENTRADAS_TLB"))
		entradasTLB = config_get_int_value(configurarAdmMemoria,
				"ENTRADAS_TLB");
	if (config_has_property(configurarAdmMemoria, "TLB_HABILITADA"))
		TLBHabilitada = config_get_string_value(configurarAdmMemoria,
				"TLB_HABILITADA");
	if (config_has_property(configurarAdmMemoria, "RETARDO_MEMORIA"))
		retardoMemoria = config_get_int_value(configurarAdmMemoria,
				"RETARDO_MEMORIA");
	config_destroy(configurarAdmMemoria);
}


int configurarSocketCliente(int s, char* ip, char* puerto){
	int status;
	struct addrinfo hints, *serverInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;     	//Setea el tipo de IP
	hints.ai_socktype = SOCK_STREAM; 	// TCP stream sockets

	if((status = getaddrinfo(ip, puerto, &hints, &serverInfo)) == -1){
		return 0;
	}

	s = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);
	if((connect(s, serverInfo->ai_addr, serverInfo->ai_addrlen)) == -1){
		return 0;
	}

	freeaddrinfo(serverInfo);
	return 1;
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
