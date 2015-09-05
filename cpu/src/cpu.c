/*
 ============================================================================
 Name        : cpu.c
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

t_log* archivoLog;
char* ipPlanificador;
char* puertoPlanificador;
char* ipMemoria;
char* puertoMemoria;
int cantidadHilos;
int retardo;

void configurarCPU(char* config);
int configurarSocketCliente(int s, char* ip, char* puerto);

int main(int argc, char** argv) {

	//Creo el archivo de logs
		archivoLog = log_create("log_CPU", "CPU", 1, 0);
		log_info(archivoLog, "Archivo de logs creado.\n");
		//Chequeo de argumentos
		if(argc < 1){
			log_error(archivoLog, "Falta el archivo de configuraciones.\n");
		}

	//TODO Leer archivo de configuracion y extraer variables
		configurarCPU(argv[1]);





	return 0;

}

void configurarCPU (char* config){

	t_config* configCPU = config_create(config);
	if(config_has_property(configCPU,"IP_PLANIFICADOR"))
		ipPlanificador = config_get_string_value(configCPU, "IP_PLANIFICADOR");
	if(config_has_property(configCPU, "PUERTO_PLANIFICADOR"))
		puertoPlanificador = config_get_string_value(configCPU, "PUERTO_PLANIFICADOR");
	if(config_has_property(configCPU, "IP_MEMORIA"))
		ipMemoria = config_get_string_value(configCPU, "IP_MEMORIA");
	if(config_has_property(configCPU, "PUERTO_MEMORIA"))
			puertoPlanificador = config_get_string_value(configCPU, "PUERTO_MEMORIA");
	if(config_has_property(configCPU, "CANTIDAD_HILOS"))
			cantidadHilos = config_get_int_value(configCPU, "CANTIDAD_HILOS");
	if(config_has_property(configCPU, "RETARDO"))
				retardo = config_get_int_value(configCPU, "RETARDO");


	config_destroy(configCPU);
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
