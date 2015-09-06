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
int clienteCPU;

//Tipos de comandos
typedef enum {
	CORRER, FINALIZAR, PS, CPU
} comando_t;

//Funciones de configuracion
void configurarPlanificador(char* config);
int configurarSocketServidor();
//Funciones de gestion
void manejoDeConsola();

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

//Creacion de servidor
//TODO Preguntar si se termina el programa o hay que reintentar.
	configurarSocketServidor();
	/*	if (configurarSocketServidor())
	 log_info(archivoLog, "Servidor creado.\n");
	 else
	 log_error(archivoLog, "No se pudo crear el Servidor.\n");
	 */

//Prueba para testeo de sockets -SACAR-
	char* prueba = malloc(5);
	for (;;) {
		recv(listeningSocket, prueba, sizeof(5), 0);
	}

//TODO Esperar la conexion de CPUs -minimo 2-
//Lo más probable es que se cambie por un hilo que maneje las conexiones entrantes
	struct sockaddr_in direccionCliente;
	unsigned int len;
	int cantidadCPUs;
	while(cantidadCPUs < 2){
		clienteCPU = accept(listeningSocket, (void*) &direccionCliente, &len);
		log_info(archivoLog, "Se conecta el proceso CPU %.\n", clienteCPU);
	}
//TODO Levantar la consola
	pthread_t hiloConsola;
	pthread_create(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

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

	listeningSocket = socket(AF_INET, SOCK_STREAM, 0);

	int activado = 1;
	setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &activado,
			sizeof(activado));

	if (bind(listeningSocket, (void*) &direccionServidor,
			sizeof(direccionServidor)) != 0) {
		log_error(archivoLog, "Falló el bind");
		return 1;
	}

	listen(listeningSocket, (int) puertoEscucha);

	log_info(archivoLog, "Servidor creado.\n");

	return 1;
}

void manejoDeConsola() {
//TODO Cambiar el tamaño de comando
	char* directiva = malloc(100);
	comando_t comando;

	for (;;) {
		scanf("%s", directiva);

		if (string_starts_with(directiva, "correr") || string_starts_with(directiva, "finalizar")){
			char** directivaSplit = string_n_split(directiva, 2, " ");
			string_capitalized(directivaSplit[1]);
			comando = (comando_t) directivaSplit[1];
			switch (comando){
			//Correr
			case 0:
				break;
			//Finalizar
			case 1:
				break;
			}
		} else {
			string_capitalized(directiva);
			comando = (comando_t) directiva;
			switch (comando){
			//PS
			case 2:
				break;
			//CPU
			case 3:
				break;
			}

		}

	}
}

