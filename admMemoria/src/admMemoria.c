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
#include <arpa/inet.h>
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
#include "../../sockets.h"

#define BACKLOG 5

t_log* archivoLog;

typedef enum {
	INICIARPROCESO,
	ENTRADASALIDA,
	INICIOMEMORIA,
	LEERMEMORIA,
	ESCRIBIRMEMORIA,
	FINALIZARPROCESO
} operacion_t;

int puertoEscucha;
char* ipSwap;
int puertoSwap;
int maximoMarcosPorProceso;
int cantidadMarcos;
int listeningSocket;
int tamanioMarco;
int entradasTLB;
char* TLBHabilitada;
int retardoMemoria;
int socketSwap;
int clienteCPU;

typedef enum{iniciar, leer, escribir, entradaSalida, finalizar} t_instruccion;

void configurarAdmMemoria(char* config);
int configurarSocketCliente(char* ip, int puerto, int*);
int configurarSocketServidor();

int main(int argc, char** argv) {
	//Creo el archivo de logs
	archivoLog = log_create("log_AdmMemoria", "AdmMemoria", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	//TODO Leer archivo de configuracion y extraer variables
	configurarAdmMemoria(argv[1]);

	if (configurarSocketCliente(ipSwap, puertoSwap,	&socketSwap))
		log_info(archivoLog, "Conectado al Administrador de Swap %i.\n", socketSwap);
	else
		log_error(archivoLog, "Error al conectar en el Administrador de Swap. %s %i \n", ipSwap, puertoSwap);

	configurarSocketServidor();

	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteCPU = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %.\n", clienteCPU);

	pthread_t admDeMemoria;
	pthread_create(&admDeMemoria, NULL, (void *)admDeMemoria, NULL);

	pthread_join(admDeMemoria, NULL);

	return 0;
}

void configurarAdmMemoria(char* config) {

	t_config* configurarAdmMemoria = config_create(config);
	if (config_has_property(configurarAdmMemoria, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_int_value(configurarAdmMemoria, "PUERTO_ESCUCHA");
	if (config_has_property(configurarAdmMemoria, "IP_SWAP"))
		ipSwap = string_duplicate(config_get_string_value(configurarAdmMemoria, "IP_SWAP"));
	if (config_has_property(configurarAdmMemoria, "PUERTO_SWAP"))
		puertoSwap = config_get_int_value(configurarAdmMemoria, "PUERTO_SWAP");
	if (config_has_property(configurarAdmMemoria, "MAXIMO_MARCOS_POR_PROCESO"))
		maximoMarcosPorProceso = config_get_int_value(configurarAdmMemoria, "MAXIMO_MARCOS_POR_PROCESO");
	if (config_has_property(configurarAdmMemoria, "CANTIDAD_MARCOS"))
		cantidadMarcos = config_get_int_value(configurarAdmMemoria,	"CANTIDAD_MARCOS");
	if (config_has_property(configurarAdmMemoria, "TAMANIO_MARCO"))
		tamanioMarco = config_get_int_value(configurarAdmMemoria, "TAMANIO_MARCO");
	if (config_has_property(configurarAdmMemoria, "ENTRADAS_TLB"))
		entradasTLB = config_get_int_value(configurarAdmMemoria, "ENTRADAS_TLB");
	if (config_has_property(configurarAdmMemoria, "TLB_HABILITADA"))
		TLBHabilitada = string_duplicate(config_get_string_value(configurarAdmMemoria, "TLB_HABILITADA"));
	if (config_has_property(configurarAdmMemoria, "RETARDO_MEMORIA"))
		retardoMemoria = config_get_int_value(configurarAdmMemoria, "RETARDO_MEMORIA");

	config_destroy(configurarAdmMemoria);
}

int configurarSocketCliente(char* ip, int puerto, int* s) {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr(ip);
	direccionServidor.sin_port = htons(puerto);

	*s = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(*s, (void*) &direccionServidor, sizeof(direccionServidor)) == -1) {
		log_error(archivoLog, "No se pudo conectar");
		return 0;
	}

	return 1;
}

int configurarSocketServidor() {

	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = INADDR_ANY;
	direccionServidor.sin_port = htons(puertoEscucha);

	listeningSocket = socket(AF_INET, SOCK_STREAM, 0);

	int activado = 1;
	setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	if (bind(listeningSocket, (void*) &direccionServidor, sizeof(direccionServidor)) != 0) {
		log_error(archivoLog, "Falló el bind");
		return 1;
	}

	listen(listeningSocket, BACKLOG);


	log_info(archivoLog, "Servidor creado. %i\n", listeningSocket);

	return 1;
}

void admDeMemoria(){

	while(1){
		int instruccion;
		recibirYDeserializarInt(&instruccion, clienteCPU);

		switch(instruccion){
		case INICIOMEMORIA:{

			int* pid = malloc(sizeof(int));
			int* cantPaginas = malloc(sizeof(int));

			recibirYDeserializarInt(pid, clienteCPU);
			recibirYDeserializarInt(cantPaginas, clienteCPU);

			int* tamanioPaquete = malloc(sizeof(int));
			*tamanioPaquete = sizeof(int) * 3;
			char* paquete = malloc(*tamanioPaquete);

			//TODO Corregir warning
			serializarInt(serializarInt(serializarInt(paquete, INICIOMEMORIA), pid), cantPaginas);

			send(clienteCPU, paquete, *tamanioPaquete, 0);

			free(tamanioPaquete);
			free(paquete);

			break;
		}
		case LEERMEMORIA:

			break;
		case FINALIZARPROCESO:

			break;
		}

	}
}
