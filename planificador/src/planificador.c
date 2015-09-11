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
int puertoEscucha;
char* algoritmo;
int quantum;
int listeningSocket;
int clienteCPU;

//Tipos de comandos
typedef enum {
	correr, finalizar, ps, cpu
} comand;

typedef struct{
	char* comando;
	char* parametro;
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

//Prueba para testeo de sockets -SACAR-
	//struct sockaddr_in direccionCliente;
	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);

	clienteCPU = accept(listeningSocket, (struct sockaddr*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %i.\n", clienteCPU);

//TODO Esperar la conexion de CPUs
//Lo más probable es que se cambie por un hilo que maneje las conexiones entrantes - PREGUNTAR
	clienteCPU = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %.\n", clienteCPU);

/* PARA CHECKPOINT
	char* directiva = "\0";

	scanf("%s", directiva);
	char** directivaSplit = string_n_split(directiva, 2, " ");

	int* tamanio = malloc(sizeof(directivaSplit[2]));
	char* buffer = malloc(len);
	buffer = directivaSplit[2];
	send(clienteCPU, buffer, *tamanio, 0);

*/

//TODO Levantar la consola
	pthread_t hiloConsola;
	pthread_create(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

	return 0;

}

void configurarPlanificador(char* config) {

	t_config* configPlanificador = config_create(config);
	if (config_has_property(configPlanificador, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_int_value(configPlanificador, "PUERTO_ESCUCHA");
	if (config_has_property(configPlanificador, "ALGORITMO_PLANIFICADOR"))
		algoritmo = string_duplicate(config_get_string_value(configPlanificador, "ALGORITMO_PLANIFICADOR"));
	if (config_has_property(configPlanificador, "QUANTUM"))
		quantum = config_get_int_value(configPlanificador, "QUANTUM");
	config_destroy(configPlanificador);
}

int configurarSocketServidor() {

	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = INADDR_ANY;
	direccionServidor.sin_port = htons(puertoEscucha);

	listeningSocket = socket(AF_INET, SOCK_STREAM, 0);

	int activado = 1;
	setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &activado,
			sizeof(activado));

	if (bind(listeningSocket, (void*) &direccionServidor,
			sizeof(direccionServidor)) != 0) {
		log_error(archivoLog, "Falló el bind");
		return 1;
	}

	listen(listeningSocket, BACKLOG);


	log_info(archivoLog, "Servidor creado. %i\n", listeningSocket);

	return 1;
}

void manejoDeConsola() {
//TODO Cambiar el tamaño de comando
	char* directiva;
	comando_t comando;

	int mantenerConsola = 1;

	while (mantenerConsola) {

		scanf("%s", directiva);

		if (string_starts_with(directiva, "correr") || string_starts_with(directiva, "finalizar")){
			char** directivaSplit = string_n_split(directiva, 2, " ");
			comando.comando = directivaSplit[1];
			comando.parametro = directivaSplit[2];

			switch (comando->comando){
			//Correr
			case 0:
				correrPath();
				break;
			//Finalizar
			case 1:
				finalizarCPU();
				break;
			}
		} else {
			comando->comando = directiva;
			switch (comando->comando){
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

void correrPath(){

}

void finalizarCPU(){

}
