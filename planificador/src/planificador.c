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
#include <sockets.h>

#define BACKLOG 5

typedef enum {
	INICIARPROCESO = 0,
	ENTRADASALIDA = 1,
	INICIOMEMORIA = 2,
	LEERMEMORIA = 3,
	ESCRIBIRMEMORIA = 4,
	FINALIZARPROCESO = 5,
	RAFAGAPROCESO = 6,
	PROCESOBLOQUEADO = 7,
	FINALIZAPROCESO = 8
} operacion_t;

t_log* archivoLog;
t_log* archivoLogDebug;

int puertoEscucha;
char* algoritmo;
int quantum;
int listeningSocket;
int clienteCPUPadre;
int* clientesCPUs;

t_queue* queueReady;
t_queue* queueRunning;
t_queue* queueBlocked;
t_queue* queueCPU;
t_queue* queueCPULibre;

pthread_mutex_t mutexQueueReady;
pthread_mutex_t mutexQueueRunning;
pthread_mutex_t mutexQueueCPU;
pthread_mutex_t mutexQueueCPULibre;
pthread_mutex_t mutexQueueBlocked;
pthread_mutex_t mutexCliente;

int pIDContador = 1;

//Estructuras
typedef enum {READY, RUNNING, BLOCKED} estados_t;

#pragma pack(1)

typedef struct {
	char* comando;
	char* parametro;
} comando_t;

typedef struct {
	int processID;
	estados_t estadoProceso;
	int programCounter;
	char* path;
} pcb_t;

typedef struct {
	pcb_t* pcb;
	int tiempoDormido;
} procesoBlocked_t;

typedef struct {
	pcb_t* pcb;
	int clienteCPU;
} procesoCorriendo_t;

//Funciones de configuracion
void configurarPlanificador(char* config);
int configurarSocketServidor();

//Funciones de gestion
void manejoDeConsola();
void planificador();

//Funciones de comandos
void correrProceso(char* path);
void generarPCB(pcb_t* pcb);
void finalizarProceso(int pid);
void estadoProcesos();
void comandoCPU();

//Funciones de planificador
int buscarYEliminarEnCola(t_queue* cola, int pid);
void finalizarRafaga(pcb_t* pcb, t_queue* colaDestino, int* tiempoBlocked);
void entradaSalida();
void procesoCorriendo();

int main(int argc, char** argv) {

	system("rm log_Debug");

	//Creo el archivo de logs
	archivoLog = log_create("log_Planificador", "Planificador", 0, LOG_LEVEL_TRACE);
	archivoLogDebug = log_create("log_Debug", "PLANIFICADOR", 0, LOG_LEVEL_DEBUG);

	configurarPlanificador(argv[1]);

	//Creacion de colas
	queueReady = queue_create();
	queueRunning = queue_create();
	queueBlocked = queue_create();
	queueCPU = queue_create();
	queueCPULibre = queue_create();

	//inicializo los semaforos
	pthread_mutex_init(&mutexQueueReady, NULL);
	pthread_mutex_init(&mutexQueueRunning, NULL);
	pthread_mutex_init(&mutexQueueCPU, NULL);
	pthread_mutex_init(&mutexQueueCPULibre, NULL);
	pthread_mutex_init(&mutexQueueBlocked, NULL);
	pthread_mutex_init(&mutexCliente, NULL);

	//Configuracion del Servidor
	configurarSocketServidor();

	//En primera instancia se conecta el proceso CPU y manda la cantidad de CPUs que va a tener corriendo
	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteCPUPadre = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %d\n", clienteCPUPadre);
	log_debug(archivoLogDebug, "Se conecta el proceso CPU %d\n", clienteCPUPadre);

	//Envio el quantum a CPU
	//Envio el quantum a CPU
	 	char* paquete = malloc(sizeof(int));
	 if(string_equals_ignore_case(algoritmo, "FIFO"))
	 	serializarInt(paquete, -1);
	 else
	 	serializarInt(paquete, quantum);
	send(clienteCPUPadre, paquete, sizeof(int), 0);
	free(paquete);

	int* cantidadCPUs = malloc(sizeof(int));
	recibirYDeserializarInt(cantidadCPUs, clienteCPUPadre);

	clientesCPUs = malloc((*cantidadCPUs) * sizeof(int));

	//Por cada una de las CPUs se hace un accept para conectarse y se hace el push a la cola de libres
	int i;
	for(i = 1; i <= *cantidadCPUs; i++){
		struct sockaddr_storage direccionCliente;
		unsigned int len = sizeof(direccionCliente);
		clientesCPUs[i] = accept(listeningSocket, (void*) &direccionCliente, &len);
		log_info(archivoLog, "Se conecta el proceso CPU %d\n", clientesCPUs[i]);
		log_debug(archivoLogDebug, "Se conecta el proceso CPU %d\n", clientesCPUs[i]);

		//El cliente CPU va a ser la posición i del array de clientes, entonces lo podria usar desde el array mismo
		queue_push(queueCPULibre, &i);
	}

	free(cantidadCPUs);

	//Comienza el thread de la consola
	pthread_t hiloConsola;
	pthread_create(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

	pthread_t hiloEntradaSalida;
	pthread_create(&hiloEntradaSalida, NULL, (void *) entradaSalida, NULL);

	log_info(archivoLog, algoritmo);

	//Comienza el thread del planificador
	pthread_t hiloPlanificador;
	pthread_create(&hiloPlanificador, NULL, (void *) planificador, NULL);

	pthread_join(hiloPlanificador, NULL);

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
	setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	if (bind(listeningSocket, (void*) &direccionServidor, sizeof(direccionServidor)) != 0) {
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
		
		scanf("%s %s", comando.comando, comando.parametro);
		getchar();
		if (comando.parametro != NULL){
			if (string_equals_ignore_case(comando.comando, "correr")) {
				correrProceso(comando.parametro);
				log_debug(archivoLogDebug, "corriendo el %s", comando.parametro);
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

	log_debug(archivoLogDebug,"entra a correr proceso");
	pcb_t* pcbProc = malloc(sizeof(pcb_t));
	log_debug(archivoLogDebug,"hace malloc");
	generarPCB(pcbProc);
	log_debug(archivoLogDebug,"generó pcb");
	pcbProc->path = string_duplicate(path);
	log_debug(archivoLogDebug,"entra a path");


	//Agrego a la cola READY
	pthread_mutex_lock(&mutexQueueReady);
	queue_push(queueReady, pcbProc);
	pthread_mutex_unlock(&mutexQueueReady);

}

void generarPCB(pcb_t* pcb){

	log_debug(archivoLogDebug, "entra a generar pcb");
	pcb->processID = pIDContador;
	pcb->programCounter = 0;
	//El estado se asigna a Ready
	pcb->estadoProceso = 0;

	pIDContador++;
	
	log_info(archivoLog, "Se genero el PCB del proceso %i.", pcb->processID);
	log_debug(archivoLogDebug, "Se genero el PCB del proceso %i.", pcb->processID);

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

		pcb = queue_pop(cola);

		if(pcb->processID == pid){

			encontrado++;

			int tamanioPaquete = sizeof(int) * 2;
			char* paquete = malloc(tamanioPaquete);
			serializarInt(serializarInt(paquete, FINALIZARPROCESO), pid);

			send(clienteCPUPadre, paquete, tamanioPaquete, 0);

			free(paquete);

			int* notificacion = malloc(sizeof(int));
			recibirYDeserializarInt(notificacion, clienteCPUPadre);

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

			queue_push(queueAuxiliar, pcb);

		}

	}

	while(!queue_is_empty(queueAuxiliar)){

		pcb = queue_pop(queueAuxiliar);
		queue_push(cola, pcb);

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

void planificador() {
	log_info(archivoLog, "Empieza el thread planificador.\n");
	log_debug(archivoLogDebug, "Empieza el thread planificador.\n");

	int* cpu = malloc(sizeof(int));

	while(1){

		if (! (queue_is_empty(queueCPULibre) || queue_is_empty(queueReady))){

			pthread_mutex_lock(&mutexQueueReady);
			pcb_t* pcb = queue_pop(queueReady);
			pthread_mutex_unlock(&mutexQueueReady);

			log_info(archivoLog, "Proceso a Ejecutar: %i", pcb->processID);
			log_debug(archivoLogDebug, "Proceso a Ejecutar: %i", pcb->processID);

			pthread_mutex_lock(&mutexQueueCPULibre);
			cpu = queue_pop(queueCPULibre);
			pthread_mutex_unlock(&mutexQueueCPULibre);

			//Cambia el estado del proceso
			pcb->estadoProceso = RUNNING;

			pthread_mutex_lock(&mutexQueueCPU);
			queue_push(queueCPU, cpu);
			pthread_mutex_unlock(&mutexQueueCPU);

			pthread_mutex_lock(&mutexQueueRunning);
			queue_push(queueRunning, pcb);
			pthread_mutex_unlock(&mutexQueueRunning);

			log_info(archivoLog, "Empieza la ejecución de proceso:%i", pcb->processID);
			log_debug(archivoLogDebug, "Empieza la ejecución de proceso: %i", pcb->processID);

			procesoCorriendo_t* proceso = malloc(sizeof(procesoCorriendo_t));
			proceso->pcb = pcb;
			proceso->clienteCPU = clientesCPUs[*cpu];
			log_debug(archivoLogDebug, "la pcb es: %i y el cpu: %i", proceso->pcb, proceso->clienteCPU);

			//Comienza un thread para mantener el proceso corriendo y seguirlo
			pthread_t threadProceso;
			pthread_create(&threadProceso, NULL, (void *) procesoCorriendo, &procesoCorriendo);

			free(proceso);
		}
	}
}

void finalizarRafaga(pcb_t* pcb, t_queue* colaDestino, int* tiempoBlocked){

	pcb_t* aux = malloc(sizeof(pcb_t));
	t_queue* queueAux = malloc(sizeof(t_queue));

	while(!queue_is_empty(queueRunning)){
		aux = queue_pop(queueRunning);
		if(pcb->processID == aux->processID){

			if(tiempoBlocked != NULL){
				procesoBlocked_t* proceso = malloc(sizeof(procesoBlocked_t));
				proceso->tiempoDormido = *tiempoBlocked;
				proceso->pcb = pcb;
				queue_push(colaDestino, proceso);
				free(proceso);
				break;
			}

			//Si la colaDestino es NULL significa que termina el proceso y lo saca de todas las queue
			if(colaDestino != NULL)
				queue_push(colaDestino, pcb);

			break;
		} else {
			queue_push(queueAux, aux);
		}
	}

	while(!queue_is_empty(queueAux)){

		pthread_mutex_lock(&mutexQueueRunning);
		aux = queue_pop(queueAux);
		queue_push(queueRunning, aux);
		pthread_mutex_unlock(&mutexQueueRunning);
	}

	free(queueAux);
	free(aux);
}

//Al ser un KLT usar sleep y se clava solo el hilo
void entradaSalida(){
	procesoBlocked_t* proceso;

	while(1){

		if(!queue_is_empty(queueBlocked)){
			pthread_mutex_lock(&mutexQueueReady);
			proceso = queue_pop(queueBlocked);
			pthread_mutex_unlock(&mutexQueueReady);

			sleep(proceso->tiempoDormido);

			pthread_mutex_lock(&mutexQueueReady);
			queue_push(queueReady, proceso->pcb);
			pthread_mutex_unlock(&mutexQueueReady);
		}
	}

}

void procesoCorriendo(procesoCorriendo_t* proceso){

	log_debug(archivoLogDebug, "comienza el thread para seguir al proceso");
	pcb_t* pcb = malloc(sizeof(pcb_t));
	memcpy(pcb, proceso->pcb, sizeof(pcb_t));
	int* cpu = (int*) proceso->clienteCPU;
	memcpy(cpu, (int*) proceso->clienteCPU, sizeof(int));

	log_debug(archivoLogDebug, "el pcb es: %s y el cpu es :%s", pcb, cpu);

	//Envio el proceso que va a correr despues
	int tamanioPaquete = sizeof(int) * 3 + strlen(pcb->path) + 1;
	char* paquete = malloc(tamanioPaquete);

	serializarChar(serializarInt(serializarInt(serializarInt(paquete,INICIARPROCESO), pcb->processID), pcb->programCounter),pcb->path);

	send(cpu, paquete, tamanioPaquete, 0);

	free(paquete);
	
	int* formaFinalizacion = malloc(sizeof(int));
	recibirYDeserializarInt(formaFinalizacion, cpu);

	switch(*formaFinalizacion){

		case RAFAGAPROCESO:{

			int programCounter;
			recibirYDeserializarInt(&programCounter, cpu);

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu);

			log_info(archivoLog, "El Resultado de la rafaga fue: %i.\n",resultadoRafaga);

			free(resultadoRafaga);

			pthread_mutex_lock(&mutexQueueReady);
			finalizarRafaga(pcb, queueReady, NULL);
			pthread_mutex_unlock(&mutexQueueReady);

			pcb->estadoProceso = READY;
			pcb->programCounter = programCounter;

			log_debug(archivoLogDebug, "Se acabo la rafaga de %i.\n", pcb->processID);

			break;
		}
		case PROCESOBLOQUEADO:{

			int programCounter;
			recibirYDeserializarInt(&programCounter, cpu);

			int tiempoBloqueado;
			recibirYDeserializarInt(&tiempoBloqueado, cpu);

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu);

			log_info(archivoLog, "El Resultado de la rafaga fue: %i.\n",resultadoRafaga);
			log_debug(archivoLogDebug, "El Resultado de la rafaga fue: %i.\n",resultadoRafaga);

			free(resultadoRafaga);

			pthread_mutex_lock(&mutexQueueBlocked);

			finalizarRafaga(pcb, queueBlocked, &tiempoBloqueado);
			pthread_mutex_unlock(&mutexQueueBlocked);

			pcb->estadoProceso = BLOCKED;
			pcb->programCounter = programCounter;

			log_info(archivoLog, "Se bloquea el proceso %i.\n", pcb->processID);

			break;
		}
		case FINALIZAPROCESO:{

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu);

			log_info(archivoLog, "El Resultado de la rafaga fue: %i.\n",resultadoRafaga);
			log_debug(archivoLogDebug, "El Resultado de la rafaga fue: %i.\n",resultadoRafaga);

			free(resultadoRafaga);

			pthread_mutex_lock(&mutexQueueReady);
			finalizarRafaga(pcb, NULL, NULL);
			pthread_mutex_unlock(&mutexQueueReady);

			log_info(archivoLog, "Finaliza el proceso %i.\n", pcb->processID);

		}

		free(formaFinalizacion);
	}

	pthread_mutex_lock(&mutexQueueCPULibre);
	pthread_mutex_lock(&mutexQueueCPU);
	int* cpuAux;
	t_queue queueAux;

	while(!queue_is_empty(queueCPU)){
		cpuAux = queue_pop(queueCPU);
		if(*cpuAux == proceso->clienteCPU){
			queue_push(queueCPULibre, &proceso->clienteCPU);
		}else{
			queue_push(&queueAux, &cpuAux);
		}
	}

	while(!queue_is_empty(&queueAux)){
		cpuAux = queue_pop(&queueAux);
		queue_push(queueCPU, cpuAux);
	}
	pthread_mutex_unlock(&mutexQueueCPU);
	pthread_mutex_unlock(&mutexQueueCPULibre);

	free(pcb);
}
