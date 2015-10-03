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
#include <time.h>
#include <sys/sem.h>
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
#include "../../sockets.h"

#define BACKLOG 5

typedef enum {
	INICIARPROCESO = 0,
	ENTRADASALIDA = 1,
	INICIOMEMORIA = 2,
	LEERMEMORIA = 3,
	ESCRIBIRMEMORIA = 4,
	FINALIZARPROCESO = 5,
	RAFAGAPROCESO = 6,
	PROCESOBLOQUEADO = 7
} operacion_t;

t_log* archivoLog;
int puertoEscucha;
char* algoritmo;
int quantum;
int listeningSocket;
int clienteCPU;

t_list* listaProcesos;

t_queue* queueReady;
t_queue* queueRunning;
t_queue* queueBlocked;
t_queue* queueCPU;
t_queue* queueCPULibre;

pthread_mutex_t mutex;
pthread_mutex_t mutexCrearPCB;
pthread_mutex_t mutexQueueReady;
pthread_mutex_t mutexQueueRunning;
pthread_mutex_t mutexQueueCPU;
pthread_mutex_t mutexQueueCPULibre;
pthread_mutex_t mutexQueueBlocked;

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
	int programCounter;
	char* path;
} pcb_t;

//Funciones de configuracion
void configurarPlanificador(char* config);
int configurarSocketServidor();

//Funciones de gestion
void manejoDeConsola();
void planificadorFIFO();
void planificadorRR();

//Funciones de comandos
void correrProceso(char* path);
void generarPCB(pcb_t* pcb);
void finalizarProceso(int pid);
void estadoProcesos();
void comandoCPU();

//Funciones de planificador
int buscarYEliminarEnCola(t_queue* cola, int pid);
void finalizarRafaga(pcb_t* pcb, t_queue* colaDestino);

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_Planificador", "Planificador", 0, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	configurarPlanificador(argv[1]);

	//Creacion de colas
	queueReady = queue_create();
	queueRunning = queue_create();
	queueBlocked = queue_create();
	queueCPU = queue_create();
	queueCPULibre = queue_create();

	//inicializo los semaforos
	pthread_mutex_init(&mutexQueueReady, NULL);
	pthread_mutex_init(&mutexCrearPCB, NULL);
	pthread_mutex_init(&mutexQueueRunning, NULL);
	pthread_mutex_init(&mutexQueueCPU, NULL);
	pthread_mutex_init(&mutexQueueCPULibre, NULL);
	pthread_mutex_init(&mutexQueueBlocked, NULL);
	pthread_mutex_init(&mutex, NULL);

	//Creacion de servidor
	configurarSocketServidor();

	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteCPU = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %d\n", clienteCPU);

	//Meto la cpu que se conecta a la cola de libres
	queue_push(queueCPULibre, &clienteCPU);

//TODO Hilos Control de tiempo e Hilo multiplexor
	//Comienza el thread de la consola
	pthread_t hiloConsola;
	pthread_create(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

	//Comienza el thread de control de tiempo
	//pthread_t hiloControlTiempo;
	//pthread_create(&hiloControlTiempo, NULL, (void *) controlTiempo, NULL);
	log_info(archivoLog, algoritmo);
	if(string_equals_ignore_case(algoritmo, "FIFO")){
		//Comienza el thread del planificadorFIFO
		pthread_t hiloPlanificadorFIFO;
		pthread_create(&hiloPlanificadorFIFO, NULL, (void *) planificadorFIFO, NULL);
		pthread_join(hiloPlanificadorFIFO, NULL);
	}else{
		//Comienza el thread del planificadorRR
		pthread_t hiloPlanificadorRR;
		pthread_create(&hiloPlanificadorRR, NULL, (void *) planificadorRR, NULL);
		pthread_join(hiloPlanificadorRR, NULL);
	}

	pthread_join(hiloConsola, NULL);
	return 0;
}

void configurarPlanificador(char* config) {

	t_config* configPlanificador = config_create(config);

	if (config_has_property(configPlanificador, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_int_value(configPlanificador, "PUERTO_ESCUCHA");
	if (config_has_property(configPlanificador, "ALGORITMO_PLANIFICACION"))
		algoritmo = string_duplicate(config_get_string_value(configPlanificador, "ALGORITMO_PLANIFICACION"));
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
		if (comando.parametro != NULL){
			if (string_equals_ignore_case(comando.comando, "correr")) {
				correrProceso(comando.parametro);
			}
			else{
				//Cambio el pid string que viene como parametro por un int
				int pidNumero = strtol(comando.parametro, NULL, 10);
				finalizarProceso(pidNumero);
			}
		} else {
			if (string_equals_ignore_case(comando.comando, "ps"))
				estadoProcesos();
			else 
				comandoCPU();
		}

		
		free(comando.comando);
		free(comando.parametro);
	}
}

void correrProceso(char* path) {

	pthread_mutex_lock(&mutexCrearPCB);

	pcb_t* pcbProc = malloc(sizeof(pcb_t));
	generarPCB(pcbProc);
	pcbProc->path = string_duplicate(path);

	pthread_mutex_unlock(&mutexCrearPCB);

	//Agrego a la cola READY
	pthread_mutex_lock(&mutexQueueReady);

	queue_push(queueReady, pcbProc);

	pthread_mutex_unlock(&mutexQueueReady);

}

void generarPCB(pcb_t* pcb){

	pcb->processID = pIDContador;
	pcb->programCounter = 0;
	//El estado se asigna a Ready
	pcb->estadoProceso = 0;

	pIDContador++;
	
	log_info(archivoLog, "Se genero el PCB del proceso %i.", pcb->processID);

}

void finalizarProceso(int pid){

	if(buscarYEliminarEnCola(queueReady, pid) == -1){
		if(buscarYEliminarEnCola(queueRunning, pid) == -1)
			if(buscarYEliminarEnCola(queueBlocked, pid) == -1)
				log_error(archivoLog, "No se pudo finalizar el proceso %s", pid);
			else
				log_info(archivoLog, "Finaliza el proceso %i.\n", pid);
		else
			log_info(archivoLog, "Finaliza el proceso %i.\n", pid);
	}else{
		log_info(archivoLog, "Finaliza el proceso %i.\n", pid);
	}
}

int buscarYEliminarEnCola(t_queue* cola, int pid){

	pcb_t* pcb = malloc(sizeof(pcb_t));
	t_queue* queueAuxiliar = queue_create();
	int encontrado = 0;

	//Busco en la queue que viene por parametro, si se encuentra lo elimina y marca el flag como encontrado
	while(!queue_is_empty(cola)){

		pthread_mutex_lock(&mutex);

		pcb = queue_pop(cola);

		pthread_mutex_unlock(&mutex);

		if(pcb->processID == pid){

			encontrado++;

			int tamanioPaquete = sizeof(int) * 2;
			char* paquete = malloc(tamanioPaquete);
			serializarInt(serializarInt(paquete, FINALIZARPROCESO), pid);

			send(clienteCPU, paquete, tamanioPaquete, 0);

			free(paquete);

			int* notificacion = malloc(sizeof(int));
			recibirYDeserializarInt(notificacion, clienteCPU);

			if(*notificacion != -1){
				log_info(archivoLog, "Se elimina el proceso:%i", pcb->processID);
				free(pcb);
				break;
			}else{
				log_error(archivoLog, "No se pudo eliminar el proceso %i.\n", pid);
			}

			free(notificacion);
			free(paquete);

		}else{
			pthread_mutex_lock(&mutex);

			queue_push(queueAuxiliar, pcb);

			pthread_mutex_unlock(&mutex);
		}

	}

	while(!queue_is_empty(queueAuxiliar)){
		pthread_mutex_lock(&mutex);

		pcb = queue_pop(queueAuxiliar);
		queue_push(cola, pcb);

		pthread_mutex_unlock(&mutex);
	}

	if(encontrado)
		return 0;
	else
		return -1;
}

void estadoProcesos(){
	
}

void comandoCPU(){

}

//TODO COLOCAR LOS SEMAFOROS PARA CUIDAR LAS QUEUE
void planificadorFIFO() {

	log_info(archivoLog, "Empieza el thread planificador.\n");

	int* auxCPU = malloc(sizeof(int));

	int tamanioPaquete;

	while(1){

		if (! (queue_is_empty(queueCPULibre) || queue_is_empty(queueReady))){

			// ASQUEROSO MEMORY LEAK
			//pcb_t* auxPCB = malloc(sizeof(pcb_t));
			pthread_mutex_lock(&mutexQueueReady);
			pcb_t* auxPCB = queue_pop(queueReady);
			log_info(archivoLog, "Proceso a Ejecutar: %i", auxPCB->processID);

			pthread_mutex_unlock(&mutexQueueReady);

			pthread_mutex_lock(&mutexQueueCPULibre);

			auxCPU = queue_pop(queueCPULibre);

			pthread_mutex_unlock(&mutexQueueCPULibre);

			//Cambia el estado del proceso (Acá tambien mutex para cuidar el acceso a estado proceso?)
			auxPCB->estadoProceso = 1;

			pthread_mutex_lock(&mutexQueueCPU);

			queue_push(queueCPU, auxCPU);

			pthread_mutex_unlock(&mutexQueueCPU);

			pthread_mutex_lock(&mutexQueueRunning);

			queue_push(queueRunning, auxPCB);

			pthread_mutex_unlock(&mutexQueueRunning);

			log_info(archivoLog, "Empieza la ejecución de ");

			//Envio el proceso que va a correr despues
			tamanioPaquete = sizeof(int) * 3 + strlen(auxPCB->path) + 1;
			char* paquete = malloc(tamanioPaquete);

			serializarChar(serializarInt(serializarInt(serializarInt(serializarInt(paquete, 1), auxPCB->processID), auxPCB->programCounter), strlen(auxPCB->path)),
							auxPCB->path);

			send(clienteCPU, paquete, tamanioPaquete, 0);

			free(paquete);

			int* procedimiento = malloc(sizeof(int));
			recibirYDeserializarInt(procedimiento, clienteCPU);

			if(*procedimiento == FINALIZARPROCESO){
				int* formaFinalizacion = malloc(sizeof(int));
				recibirYDeserializarInt(formaFinalizacion, clienteCPU);

				switch(*formaFinalizacion){
					case RAFAGAPROCESO:
						finalizarRafaga(auxPCB, queueReady);

						log_info(archivoLog, "Se acabo la rafaga de %i.\n", auxPCB->processID);

						recibirYDeserializarInt(&tamanioPaquete, clienteCPU);
						char* resultadoTotal = malloc(tamanioPaquete);
						log_info(archivoLog, resultadoTotal);
						free(resultadoTotal);

						break;
					case PROCESOBLOQUEADO:
						finalizarRafaga(auxPCB, queueBlocked);
						log_info(archivoLog, "Se bloquea el proceso %i.\n", auxPCB->processID);
						break;
				}
				free(formaFinalizacion);
			} else {
				finalizarProceso(auxPCB->processID);
			}

			//Libero la CPU
			pthread_mutex_lock(&mutexQueueCPULibre);

			auxCPU	= queue_pop(queueCPU);
			queue_push(queueCPULibre, auxCPU);

			pthread_mutex_unlock(&mutexQueueCPULibre);

			free(procedimiento);
		}
	}
	free(auxCPU);
}

//Al finalizar la rafaga de ejecucion se pone en la colaDestino dependiendo del estado del proceso en ese momento
void finalizarRafaga(pcb_t* pcb, t_queue* colaDestino){

	pcb_t* aux = malloc(sizeof(pcb_t));
	t_queue* queueAux = malloc(sizeof(t_queue));

	while(!queue_is_empty(queueRunning)){
		aux = queue_pop(queueRunning);
		if(pcb->processID == aux->processID){

			pthread_mutex_lock(&mutex);

			queue_push(colaDestino, pcb);

			pthread_mutex_unlock(&mutex);
			break;
		} else {
			pthread_mutex_lock(&mutex);

			queue_push(queueAux, aux);

			pthread_mutex_unlock(&mutex);
		}
	}

	while(!queue_is_empty(queueAux)){

		pthread_mutex_lock(&mutex);
		pthread_mutex_lock(&mutexQueueRunning);

		aux = queue_pop(queueAux);
		queue_push(queueRunning, aux);

		pthread_mutex_unlock(&mutex);
		pthread_mutex_unlock(&mutexQueueRunning);
	}

	free(queueAux);
	free(aux);
}

void planificadorRR() {

}

void controlarTiempoBlock(){

}
