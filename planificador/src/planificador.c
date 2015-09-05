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
#include <arpa/inet.h>
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
int listeningSocket;

void configurarPlanificador(char* config);
int configurarSocketServidor();
//int manejoDeConsola();

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_Planificador", "Planificador", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");
	//Chequeo de argumentos
	if (argc < 1) {
		log_error(archivoLog, "Falta el archivo de configuraciones.\n");
	}

//TODO Leer archivo de configuracion y extraer variables
	configurarPlanificador(argv[1]);

//TODO Preguntar si se termina el programa o hay que reintentar.
	if (configurarSocketServidor())
		log_info(archivoLog, "Servidor creado.\n");
	else
		log_error(archivoLog, "No se pudo crear el Servidor.\n");

	char* prueba = "\0";

	for(;;){
		recv(listeningSocket, prueba, sizeof(5), 0);
	}

//TODO Esperar la conexion de CPUs

//TODO Levantar la consola
	//pthread_t hiloConsola;

	//pthreadCreate(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

	return 0;

}

void configurarPlanificador(char* config) {

	t_config* configPlanificador = config_create(config);
	if (config_has_property(configPlanificador, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_string_value(configPlanificador,
				"PUERTO_ESCUCHA");
	if (config_has_property(configPlanificador, "ALGORITMO_PLANIFICADOR"))
		algoritmo = config_get_string_value(configPlanificador,
				"ALGORITMO_PLANIFICADOR");
	if (config_has_property(configPlanificador, "QUANTUM"))
		quantum = config_get_int_value(configPlanificador, "QUANTUM");
	config_destroy(configPlanificador);
}

int configurarSocketServidor() {

	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = INADDR_ANY;
	direccionServidor.sin_port = htons((int) puertoEscucha);

	int servidor = socket(AF_INET, SOCK_STREAM, 0);

	int activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	if (bind(servidor, (void*) &direccionServidor, sizeof(direccionServidor)) != 0){
		log_error(archivoLog, "Falló el bind");
		return 1;
	}

	listen(servidor, (int) puertoEscucha);

	struct sockaddr_in direccionCliente;
	unsigned int len;
	listeningSocket = accept(servidor, (void*) &direccionCliente, &len);

	return 1;
}

/*int manejoDeConsola(){
 char* comando;

 gets(comando);
 string_starts_with(comando,"correr");

 return 0;
 }
 */
