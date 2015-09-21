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
t_queue* queueReady;
t_queue* queueRunning;
t_queue* queueBlocked;
t_queue* queueCPU;
t_queue* queueCPULibre;


int pIDContador = 1;

//Estructuras
typedef enum {READY, RUNNING, BLOCKED} estados_t;

typedef struct {
	char* comando;
	char* parametro;
} comando_t;

typedef struct _t_Package {
	char* message;
	uint32_t message_long;
} t_Package;

typedef struct {
	int processID;
	estados_t estadoProceso;
	int pCrogramCounter;
	char path[50];
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
void generarPCB(pcb_t* pcb);
void finalizarProceso(char* pid);
void estadoProcesos();
void comandoCPU();

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

	//Creacion de colas
	queueReady = queue_create();
	queueRunning = queue_create();
	queueBlocked = queue_create();

//TODO Hilo multiplexor
	//Comienza el thread de la consola
	pthread_t hiloConsola;
	pthread_create(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

	//Comienza el thread del planificador
	pthread_t hiloPlanificador;
	pthread_create(&hiloPlanificador, NULL, (void *) planificador, NULL);
	//Meto la cpu que se conecta a la cola de libres
	queue_push(queueCPULibre, &clienteCPU);

	if (! queue_is_empty(queueCPULibre)){

		pcb_t *auxPCB = queue_pop(queueReady);
		int *auxCPU = queue_pop(queueCPULibre);
		queue_push(queueRunning, auxPCB);
		queue_push(queueCPU, auxCPU);

	}
//switch enum me va a llegar notificacion del cpu de que termino y lo mando a block o finish

	pthread_join(hiloConsola, NULL);
	pthread_join(hiloPlanificador, NULL);

	return 0;

}

void configurarPlanificador(char* config) {

	t_config* configPlanificador = config_create(config);
	if (config_has_property(configPlanificador, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_int_value(configPlanificador,
				"PUERTO_ESCUCHA");
	if (config_has_property(configPlanificador, "ALGORITMO_PLANIFICADOR"))
		algoritmo = string_duplicate(
				config_get_string_value(configPlanificador,
						"ALGORITMO_PLANIFICADOR"));
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
		
		comando.comando = malloc(10);
		comando.parametro = malloc(50);
		
//TODO Cambiar scanf() por fgets()
		scanf("%s %s", comando.comando, comando.parametro);
		getchar();
		if (comando.parametro == NULL){
			if (string_equals_ignore_case(comando.comando, "correr")) 
				//correrProceso(comando.parametro);
				send(clienteCPU, comando.parametro, sizeof(comando.parametro), 0);
			 else 
				finalizarProceso(comando.parametro);
		} else {
			if (string_equals_ignore_case(comando.comando, "ps")
				estadoProcesos();
			else 
				comandoCPU();
		}

		//Recibo notificacion del cpu para saber como termino la operacion
		char* notificacion = malloc(11);
		recv(clienteCPU, notificacion, 11, 0);
		log_info(archivoLog, "%s", notificacion);
		
		//Libero todas las variables dinamicas
		free(notificacion);
		free(comando.comando);
		free(comando.parametro);
	}
}

//TODO No envia bien el mensaje - ARREGLAR -
void correrProceso(char* path) {
	
	//TODO Ver una mejor forma de crear el PCB 
	//o crear una estructura para control que no sean las queue del planificador
	pcb_t* pcbProc = malloc(sizeof(pcb_t));
	generarPCB(pcbProc);
	pcbProc->path = string_duplicate(path);
	send(clienteCPU, path, sizeof(path), 0);

}


void generarPCB(pcb_t* pcb){

	pcb->processID = pIDContador;
	pcb->pCrogramCounter = 0;
	//El estado se asigna a Ready
	pcb->estadoProceso = 0;

	//Agrego a la cola READY
	queue_push(queueReady, pcb);

	pIDContador++;
	
	log_info(archivoLog, "Se genero el PCB del proceso %i", pcb->processID);

}

void finalizarProceso(char* pid){

}

void estadoProcesos(){
	
}

void comandoCPU(){

}

void fill_package(t_Package *package) {
	/* Me guardo los datos del usuario y el mensaje que manda */
	scanf("%s", package->message);
	getchar();
	//(package->message)[strlen(package->message)] = '\0';
	package->message_long = strlen(package->message) + 1; // Me guardo lugar para el \0
}

char* serializarOperandos(t_Package *package) {
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

void planificador() {

}
