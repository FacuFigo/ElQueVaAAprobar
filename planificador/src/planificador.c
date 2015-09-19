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

int pIDContador = 1;

//Estructuras
typedef enum {READY, RUNNING, BLOCKED} estados_t;

typedef struct{
	char comando[10];
	char parametro[50];
} comando_t;

typedef struct _t_Package {
	char* message;
	uint32_t message_long;
} t_Package;

typedef struct {
	int processID;
	estados_t estadoProceso;
	int pCrogramCounter;
} pcb_t;

//Funciones de configuracion
void configurarPlanificador(char* config);
int configurarSocketServidor();

//Funciones de gestion
void manejoDeConsola();
void planificador();

//Funciones de sockets
char* serializarOperandos(t_Package *package);
void fill_package(t_Package *package);

//Funciones de comandos
void correrProceso(char* path);
void finalizarProceso(char* pid);
void generarPCB(pcb_t* pcb);

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_Planificador", "Planificador", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	configurarPlanificador(argv[1]);

//Creacion de servidor
//TODO Preguntar si se termina el programa o hay que reintentar.
	configurarSocketServidor();

/*
//Prueba para testeo de sockets con serializacion -SACAR-
	//struct sockaddr_in direccionCliente;
	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	t_Package package;
	package.message = malloc(100);
	char *serializedPackage;
	clienteCPU = accept(listeningSocket, (struct sockaddr*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %i.\n", clienteCPU);

	puts("Escriba un texto para mandar");
	scanf("%s",package.message);
	getchar();
	int size = strlen(package.message);
	package.message_long=size+ 1;
	serializedPackage = serializarOperandos(&package);

	//int recibido = recv(clienteCPU, prueba, sizeof(prueba), 0);
	//log_info(archivoLog, "Recibi %i %s ", recibido,prueba);
	if (send(clienteCPU, serializedPackage, (sizeof(package.message_long)+package.message_long), 0) == -1)
			log_error(archivoLog, "Error en el send.\n");
	else
			log_info(archivoLog, "Mandé \"%s\" a memoria.\n", package.message);
	free(serializedPackage);
	close(clienteCPU);
	*/

//Esperar la conexion de CPUs
//Lo más probable es que se cambie por un hilo que maneje las conexiones - PREGUNTAR
	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteCPU = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %.\n", clienteCPU);

//Creo las estructuras de planificación


//TODO Hilo multiplexor
//Comienza el thread de la consola
	pthread_t hiloConsola;
	pthread_create(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

	pthread_t hiloPlanificador;
	pthread_create(&hiloPlanificador, NULL, (void *) planificador, NULL);

	pthread_join(hiloConsola, NULL);
	pthread_join(hiloPlanificador, NULL);

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

	printf("Inicio Consola.\n");

	int mantenerConsola = 1;

	while (mantenerConsola) {

		comando_t comando;
//TODO CAMBIAR POR FGETS
		scanf("%s %s", comando.comando, comando.parametro);
		getchar();
		if(string_equals_ignore_case(comando.comando,"correr")){
//			correrProceso((char *) comando.parametro);
			send(clienteCPU, comando.parametro, sizeof(comando.parametro), 0);
		}else{

		}

/*
		char* notificacion = malloc(11);
		recv(clienteCPU, notificacion, 11, 0);
		log_info(archivoLog, "%s", notificacion);
		free(notificacion);
*/
	}
}

//TODO No envia bien el mensaje - ARREGLAR -
void correrProceso(char* path){

	pcb_t* pcbProc = malloc(sizeof(pcb_t));
	generarPCB(pcbProc);
	send(clienteCPU, path, sizeof(path), 0);

}

//Devolvera la estructura completa?
void generarPCB(pcb_t* pcb){

//TODO Agregar un semaforo para cuidar el PID
	pcb->processID = pIDContador;
	pcb->pCrogramCounter = 0;
	pcb->estadoProceso = 0;

//TODO Agrego a la cola READY
//	queue_push(&colaReady, &pcb->processID);

	pIDContador++;
}

void finalizarProceso(char* pid){



}

void fill_package(t_Package *package){
	/* Me guardo los datos del usuario y el mensaje que manda */
	scanf("%s",package->message);
	getchar();
	//(package->message)[strlen(package->message)] = '\0';
	package->message_long = strlen(package->message)+1; // Me guardo lugar para el \0
}

char* serializarOperandos(t_Package *package){
	int total = sizeof(package->message_long) + package->message_long;
	char *serializedPackage = malloc(total);
	int offset = 0;
	int size_to_send;
	size_to_send = sizeof(package->message_long);
	memcpy(serializedPackage + offset, &(package->message_long), size_to_send);
	offset += size_to_send;
	size_to_send = package->message_long;
	memcpy(serializedPackage + offset, package->message, size_to_send);
	return serializedPackage;
}

void planificador(){

}
