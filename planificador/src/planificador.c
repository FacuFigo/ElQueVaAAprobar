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
	FALLOPROCESO = 7,
	PEDIDOMETRICA = 8
} operacion_t;
t_log* archivoLog;
t_log* archivoLogObligatorio;
t_log* archivoLogDebug;

int puertoEscucha;
char* algoritmo;
int quantum;
int listeningSocket;
int clienteCPUPadre;

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
pthread_mutex_t mutexPlanificador;
pthread_mutex_t mutexEntradaSalida;

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

typedef struct{
	int numeroCPU;
	int cliente;
} cpu_t;

typedef struct {
	pcb_t* pcb;
	int tiempoDormido;
} procesoBlocked_t;

typedef struct {
	pcb_t* pcb;
	cpu_t* clienteCPU;
} procesoCorriendo_t;

//Funciones de configuracion
void configurarPlanificador(char* config);
int configurarSocketServidor();

//Funciones de gestion
void manejoDeConsola();
void planificador();
void esperarConexiones();

//Funciones de comandos
void correrProceso(char* path);
void generarPCB(pcb_t* pcb);
void finalizarProceso(int pid);
void estadoProcesos();
void comandoCPU();

//Funciones de planificador
int buscarYEliminarEnCola(t_queue* cola, int pid);
//void finalizarRafaga(pcb_t* pcb, t_queue* colaDestino, int* tiempoBlocked);
void finalizarRafaga(pcb_t* pcb, int* tiempoBlocked);
void entradaSalida();
void procesoCorriendo(procesoCorriendo_t* proceso);
void logueoEstados(t_queue* cola);

int main(int argc, char** argv) {

	system("rm log_Debug");
	system("rm log_Planificador_Obligatorio");

	//Creo el archivo de logs
	archivoLogObligatorio = log_create("log_Planificador_Obligatorio", "Planificador", 0, LOG_LEVEL_TRACE);
	archivoLog = log_create("log_Planificador", "planificador", 0, LOG_LEVEL_TRACE);
	archivoLogDebug = log_create("log_Debug", "PLANIFICADOR", 1, LOG_LEVEL_DEBUG);

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
	pthread_mutex_init(&mutexPlanificador, NULL);
	pthread_mutex_init(&mutexEntradaSalida, NULL);

	esperarConexiones();

	//Comienza el thread de la consola
	pthread_t hiloConsola;
	pthread_create(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

	pthread_mutex_lock(&mutexEntradaSalida);
	pthread_t hiloEntradaSalida;
	pthread_create(&hiloEntradaSalida, NULL, (void *) entradaSalida, NULL);

	log_info(archivoLogObligatorio,"Comienza la ejecucion del algoritmo %s" , algoritmo);

	pthread_mutex_lock(&mutexPlanificador);
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

		char* comandoLeido = malloc(25);
		fgets(comandoLeido, 25, stdin);

		char** comandoSplit = string_n_split(comandoLeido, 2, " ");
		log_debug(archivoLogDebug, "Hace el split.");
		comando.comando = comandoSplit[0];

		if(comandoSplit[1] != NULL){
			comando.parametro = comandoSplit[1];
			log_debug(archivoLogDebug, "Hace las asignaciones.");

			char* nuevaLinea;
			nuevaLinea = strchr(comando.parametro, 10);
			log_debug(archivoLogDebug, "Hace strchr.");

			if (nuevaLinea != NULL)
				*nuevaLinea = '\0';

			log_debug(archivoLogDebug, "Cambia la nueva linea por vacio.");
		}

		if (string_starts_with(comando.comando, "correr")) {
				correrProceso(comando.parametro);
			}

		if(string_starts_with(comando.comando, "finalizar")){
				//Cambio el pid string que viene como parametro por un int
				int pidNumero = strtol(comando.parametro, NULL, 10);
				finalizarProceso(pidNumero);
			}

		if (string_starts_with(comando.comando, "ps"))
				estadoProcesos();

		if(string_starts_with(comando.comando, "cpu")){
			log_debug(archivoLogDebug, "entra a comando cpu");
				comandoCPU();
				log_debug(archivoLogDebug, "sali de comando cpu");
		}
		free(comando.comando);
		free(comando.parametro);
	}
}

void correrProceso(char* path) {

	pcb_t* pcb = malloc(sizeof(pcb_t));
	generarPCB(pcb);
	pcb->path = string_duplicate(path);

	//Agrego a la cola READY
	pthread_mutex_lock(&mutexQueueReady);
	queue_push(queueReady, pcb);
	pthread_mutex_unlock(&mutexQueueReady);

	pthread_mutex_unlock(&mutexPlanificador);

	log_debug(archivoLogDebug, "Se genero el PCB del proceso %i.", pcb->processID);
}

void generarPCB(pcb_t* pcb){

	pcb->processID = pIDContador;
	pcb->programCounter = 0;
	//El estado se asigna a Ready
	pcb->estadoProceso = 0;

	pIDContador++;

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

	log_debug(archivoLogDebug, "Entré a estadoProcesos");



	// Me voy fijando si las colas no son vacias, saco a variable aux y logueo
	if(!queue_is_empty(queueReady)){
		pthread_mutex_lock(&mutexQueueReady);
		logueoEstados(queueReady);
		pthread_mutex_unlock(&mutexQueueReady);
	}
	if(!queue_is_empty(queueBlocked)){
		pthread_mutex_lock(&mutexQueueBlocked);
		logueoEstados(queueBlocked);
		pthread_mutex_unlock(&mutexQueueBlocked);
	}
	if(!queue_is_empty(queueRunning)){
		pthread_mutex_lock(&mutexQueueRunning);
		logueoEstados(queueRunning);
		pthread_mutex_unlock(&mutexQueueRunning);
	}
	log_debug(archivoLogDebug, "Estados logueados, saliendo de ps");
}

void logueoEstados(t_queue* cola){

	pcb_t* pcb;
	t_queue* queueAux;
	queueAux = queue_create();

	while(!queue_is_empty(cola)){
		pcb = queue_pop(cola);
		switch (pcb->estadoProceso){

			case READY:{
				log_debug(archivoLogDebug, "mProc %i: %s -> Listo", pcb->processID, pcb->path);
				queue_push(queueAux, pcb);
				break;
			}

			case RUNNING:{
				log_debug(archivoLogDebug, "mProc %i: %s -> Ejecutando", pcb->processID, pcb->path);
				queue_push(queueAux, pcb);
				break;
			}

			case BLOCKED:{
				log_debug(archivoLogDebug, "mProc %i: %s -> Bloqueado", pcb->processID, pcb->path);
				queue_push(queueAux, pcb);
				break;
			}

			default:{
				log_debug(archivoLogDebug, "El proceso ya finalizó. \n");
				break;
			}


		}
	}

	while(!queue_is_empty(queueAux)){
		pcb = queue_pop(queueAux);
		queue_push(cola, pcb);
	}
	queue_destroy(queueAux);
}

void comandoCPU(){
	t_queue* queueAux;
	cpu_t* cpu;
	int tamanioPaquete = sizeof(int);
	char* paquete = malloc(tamanioPaquete);
	queueAux = queue_create();
	serializarInt(paquete, PEDIDOMETRICA);

	log_debug(archivoLogDebug, "entre a la funcion");
	pthread_mutex_lock(&mutexQueueCPU);
	while(!queue_is_empty(queueCPU)){
		cpu = queue_pop(queueCPU);
		send(cpu->cliente, paquete, tamanioPaquete, 0);
		log_debug(archivoLogDebug, "la cpu conectada es :%i", cpu->cliente);
		queue_push(queueAux, cpu);
	}

	free(paquete);

	while(!queue_is_empty(queueAux)){
		cpu = queue_pop(queueAux);
		queue_push(queueCPU, cpu);
	}

	queue_destroy(queueAux);
	pthread_mutex_unlock(&mutexQueueCPU);

	int paqueteMetrica;
	recibirYDeserializarInt(&paqueteMetrica, cpu->cliente);
	log_debug(archivoLogDebug, "CPU %i: %i%", cpu->cliente, paqueteMetrica);


}

void planificador() {
	log_debug(archivoLog, "Empieza el thread planificador.\n");

	cpu_t* cpu = malloc(sizeof(cpu_t));
	pcb_t* pcb = malloc(sizeof(pcb_t));

	while(1){
		pthread_mutex_lock(&mutexPlanificador);
		if (! (queue_is_empty(queueCPULibre) || queue_is_empty(queueReady))){

			pthread_mutex_lock(&mutexQueueReady);
			pcb = queue_pop(queueReady);
			pthread_mutex_unlock(&mutexQueueReady);

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

			log_debug(archivoLogDebug, "Proceso ejecuta en CPU N°: %i", cpu->numeroCPU);

			procesoCorriendo_t* proceso = malloc(sizeof(procesoCorriendo_t));
			proceso->pcb = pcb;
			proceso->clienteCPU = cpu;

			log_debug(archivoLogDebug, "Struct proceso: pcb = %i, CPU: %i", proceso->pcb->processID, proceso->clienteCPU->cliente);

			//Comienza un thread para mantener el proceso corriendo y seguirlo
			pthread_t threadProceso;
			pthread_create(&threadProceso, NULL, (void *) procesoCorriendo, proceso);

			pthread_mutex_unlock(&mutexPlanificador);
		} else {
			pthread_mutex_unlock(&mutexPlanificador);
			pthread_mutex_lock(&mutexPlanificador);
		}
	}
	free(pcb);
	free(cpu);
}

void finalizarRafaga(pcb_t* pcb, int* tiempoBlocked){

	log_debug(archivoLogDebug, "Entré a finalizar rafaga");
	pcb_t* aux;
	t_queue* queueAux;

	queueAux = queue_create();

	while(!queue_is_empty(queueRunning)){
		aux = queue_pop(queueRunning);
		if(pcb->processID == aux->processID){

			if(tiempoBlocked != NULL){
				log_debug(archivoLogDebug, "El proceso %i se bloquea.", pcb->processID);
				procesoBlocked_t* proceso = malloc(sizeof(procesoBlocked_t));
				proceso->tiempoDormido = *tiempoBlocked;
				proceso->pcb = pcb;
				queue_push(queueBlocked, proceso);

				pthread_mutex_unlock(&mutexEntradaSalida);
				break;
			}else{
				log_debug(archivoLogDebug, "El proceso %i va a la cola Ready.", pcb->processID);
				queue_push(queueReady, pcb);
			}

			//Si la colaDestino es NULL significa que termina el proceso y lo saca de todas las queue
			/*if(colaDestino != NULL)
				queue_push(colaDestino, pcb);
*/
			break;
		} else {
			queue_push(queueAux, aux);
		}
	}

	log_debug(archivoLogDebug, "Tamaño de la cola: %i", queue_size(queueAux));
	while(!queue_is_empty(queueAux)){

		pthread_mutex_lock(&mutexQueueRunning);
		aux = queue_pop(queueAux);
		queue_push(queueRunning, aux);
		pthread_mutex_unlock(&mutexQueueRunning);
	}

	queue_destroy(queueAux);
}

//Al ser un KLT usar sleep y se clava solo el hilo
void entradaSalida(){
	procesoBlocked_t* proceso;

	log_debug(archivoLogDebug, "Empieza el hilo de Entrada/Salida.");

	while(1){

		pthread_mutex_lock(&mutexEntradaSalida);
		if(!queue_is_empty(queueBlocked)){
			int tiempoDormido;
			pcb_t* pcbAux;
			t_queue* queueAux = queue_create();
			procesoBlocked_t* procesoAux;

			pthread_mutex_lock(&mutexQueueBlocked);
			proceso = queue_pop(queueBlocked);
			tiempoDormido = proceso->tiempoDormido;
			pcbAux = proceso->pcb;
			queue_push(queueBlocked, proceso);
			pthread_mutex_unlock(&mutexQueueBlocked);

			log_debug(archivoLogDebug, "Entra al thread bloqueado, proceso: %i tiempo: %i", proceso->pcb->processID, proceso->tiempoDormido);
			sleep(tiempoDormido);

			pthread_mutex_lock(&mutexQueueBlocked);
			while(!queue_is_empty(queueBlocked)){
				proceso = queue_pop(queueBlocked);
				if(proceso->pcb->processID == pcbAux->processID){
					pthread_mutex_lock(&mutexQueueReady);
					queue_push(queueReady, proceso->pcb);
					pthread_mutex_unlock(&mutexQueueReady);
				}else{
					queue_push(queueAux, proceso);
				}
			}
			while(!queue_is_empty(queueAux)){
				procesoAux = queue_pop(queueAux);
			}
			pthread_mutex_unlock(&mutexQueueBlocked);

			pthread_mutex_lock(&mutexQueueReady);
			queue_push(queueReady, proceso->pcb);
			pthread_mutex_unlock(&mutexQueueReady);
			pthread_mutex_unlock(&mutexPlanificador);
			pthread_mutex_unlock(&mutexEntradaSalida);
		} else{
			pthread_mutex_unlock(&mutexEntradaSalida);
			pthread_mutex_lock(&mutexEntradaSalida);
		}
	}

}

void procesoCorriendo(procesoCorriendo_t* proceso){

	pcb_t* pcb =  proceso->pcb;
	cpu_t* cpu =  proceso->clienteCPU;

	log_debug(archivoLogDebug, "Empieza el hilo de proceso corriendo, proceso: %i.", pcb->processID);

	//Envio el proceso que va a correr despues
	int tamanioPaquete = sizeof(int) * 4 + strlen(pcb->path) + 1;
	char* paquete = malloc(tamanioPaquete);

	serializarChar(serializarInt(serializarInt(serializarInt(paquete,INICIARPROCESO), pcb->processID), pcb->programCounter),pcb->path);

	log_info(archivoLogObligatorio, "Comienza la ejecución del proceso %i: %s", pcb->processID, pcb->path);
	send(cpu->cliente, paquete, tamanioPaquete, 0);

	free(paquete);
	
	int formaFinalizacion;
	recibirYDeserializarInt(&formaFinalizacion, cpu->cliente);
	log_debug(archivoLogDebug,"forma de finalizacion:%d", formaFinalizacion);


	switch(formaFinalizacion){

		case RAFAGAPROCESO:{

			int programCounter;
			recibirYDeserializarInt(&programCounter, cpu->cliente);

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu->cliente);

			log_debug(archivoLogDebug, "El Resultado de la rafaga fue: %s.\n",resultadoRafaga);

			pcb->estadoProceso = READY;
			pcb->programCounter = programCounter;

			pthread_mutex_lock(&mutexQueueReady);
			log_info(archivoLogObligatorio, "Rafaga de cpu %i completada para el proceso mProc %i", cpu->numeroCPU, pcb->processID);
			finalizarRafaga(pcb, NULL);
			pthread_mutex_unlock(&mutexQueueReady);

			log_debug(archivoLogDebug, "Se acabo la rafaga de %i.\n", pcb->processID);

			break;
		}
		case ENTRADASALIDA:{

			log_debug(archivoLogDebug, "Entré al case ENTRADASALIDA");

			int programCounter;
			recibirYDeserializarInt(&programCounter, cpu->cliente);
			log_debug(archivoLogDebug, "program counter:%i", programCounter);

			int tiempoBloqueado;
			recibirYDeserializarInt(&tiempoBloqueado, cpu->cliente);
			log_debug(archivoLogDebug, "tiempo bloqueado:%i", tiempoBloqueado);

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu->cliente);

			log_debug(archivoLogDebug, "El Resultado de la rafaga fue: %s.",resultadoRafaga);

			pcb->estadoProceso = BLOCKED;
			pcb->programCounter = programCounter;

			log_info(archivoLogObligatorio, "Comienza la entrada-salida del proceso %i: %s", pcb->processID, pcb->path);

			pthread_mutex_lock(&mutexQueueBlocked);
			finalizarRafaga(pcb, &tiempoBloqueado);
			pthread_mutex_unlock(&mutexQueueBlocked);

			log_info(archivoLogDebug, "Se bloquea el proceso %i.\n", pcb->processID);

			break;
		}
		case FINALIZARPROCESO:{
			log_info(archivoLog, "entre al case FINALIZARPROCESO");
			log_debug(archivoLogDebug, "entre al case FINALIZARPROCESO");

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu->cliente);

			log_info(archivoLog, "El Resultado de la rafaga fue: %s.\n",resultadoRafaga);
			log_debug(archivoLogDebug, "El Resultado de la rafaga fue: %s.\n",resultadoRafaga);

			int* numeroProceso = malloc(sizeof(int));
			*numeroProceso = pcb->processID;

			log_info(archivoLogObligatorio, "Finaliza el proceso %i: %s.\n ", *numeroProceso, pcb->path);
			log_debug(archivoLogDebug, "Finaliza el proceso %i.", pcb->processID);

			free(pcb);
			break;
		}
		case FALLOPROCESO:{
			log_debug(archivoLogDebug, "entre al case FALLOPROCESO");

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu->cliente);

			int* numeroProceso = malloc(sizeof(int));
			*numeroProceso = pcb->processID;

			log_debug (archivoLogDebug, "El resultado de la rafaga fue :%s.\n", resultadoRafaga);

			free(pcb);
						break;
		}
	}

	pthread_mutex_lock(&mutexQueueCPULibre);
	pthread_mutex_lock(&mutexQueueCPU);
	cpu_t* cpuAux;
	t_queue* queueAux;

	queueAux = queue_create();

	while(!queue_is_empty(queueCPU)){
		cpuAux = queue_pop(queueCPU);
		if(cpuAux->numeroCPU == cpu->numeroCPU){
			queue_push(queueCPULibre, cpuAux);
		}else{
			queue_push(queueAux, cpuAux);
		}
	}

	while(!queue_is_empty(queueAux)){
		cpuAux = queue_pop(queueAux);
		queue_push(queueCPU, cpuAux);
	}
	pthread_mutex_unlock(&mutexQueueCPU);
	pthread_mutex_unlock(&mutexQueueCPULibre);

	queue_destroy(queueAux);
	pthread_mutex_unlock(&mutexPlanificador);
}

void esperarConexiones(){
	int cantidadCPUs;

	//Configuracion del Servidor
	configurarSocketServidor();

	//En primera instancia se conecta el proceso CPU y manda la cantidad de CPUs que va a tener corriendo
	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteCPUPadre = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLogObligatorio, "Se conecta el proceso CPU %d\n", clienteCPUPadre);

	//Envio el quantum a CPU
	char* paquete = malloc(sizeof(int));
	if(string_equals_ignore_case(algoritmo, "FIFO"))
		serializarInt(paquete, -1);
	else
		serializarInt(paquete, quantum);
	send(clienteCPUPadre, paquete, sizeof(int), 0);
	free(paquete);

	recibirYDeserializarInt(&cantidadCPUs, clienteCPUPadre);
	log_debug(archivoLogDebug, "Cantidad de CPUs: %i.", cantidadCPUs);

	//Por cada una de las CPUs se hace un accept para conectarse y se hace el push a la cola de libres
	int i;
	for(i = 1; i <= cantidadCPUs; i++){
		cpu_t* cpu = malloc(sizeof(cpu_t));

		struct sockaddr_storage direccionCliente;
		unsigned int len = sizeof(direccionCliente);
		cpu->cliente = accept(listeningSocket, (void*) &direccionCliente, &len);
		cpu->numeroCPU = i;
		log_debug(archivoLogDebug, "Se conecta el thread numero %i CPU: %i.", cpu->numeroCPU, cpu->cliente);
		log_info(archivoLogObligatorio, "Se conecta el thread numero %i CPU: %i.", cpu->numeroCPU, cpu->cliente);

		//El cliente CPU va a ser la posición i del array de clientes, entonces lo podria usar desde el array mismo
		queue_push(queueCPULibre, cpu);
	}

}
