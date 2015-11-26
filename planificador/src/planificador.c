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
	PEDIDOMETRICA = 8
} operacion_t;

t_log* archivoLog;
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

typedef struct {
	int pid;
	estados_t estadoProceso;
	char* mcod;
} ps_t;

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
void finalizarRafaga(pcb_t* pcb, t_queue* colaDestino, int* tiempoBlocked);
void entradaSalida();
void procesoCorriendo(procesoCorriendo_t* proceso);

int main(int argc, char** argv) {

	system("rm log_Debug");

	//Creo el archivo de logs
	archivoLog = log_create("log_Planificador", "Planificador", 0, LOG_LEVEL_TRACE);
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

	esperarConexiones();

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

	pcb_t* pcb = malloc(sizeof(pcb_t));
	generarPCB(pcb);
	pcb->path = string_duplicate(path);

	//Agrego a la cola READY
	pthread_mutex_lock(&mutexQueueReady);
	queue_push(queueReady, pcb);
	pthread_mutex_unlock(&mutexQueueReady);

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
	//Este comando deberá escribir en la pantalla del Planificador el PID, el nombre del programa y el estado de cada proceso “mProc”.
	//El formato deberá ser: mProc PID: nombre -> estado, escribiendo en una línea diferente cada proceso “mProc”

		// creo un array para guardar los mproc que haya en las distintas colas queueReady, queueRunning y queueBlocked
		// guardo los PID asignandolos a una variable dinamica, guardo los nombres mcod en variable din, y lo mismo con el estado segun la cola que estén
		// logueo resultados

	ps_t vEstados[100];
	int i = 0;
	for (i = 0; i < 100; i++){
	// busco en las colas y asigno como vEstados.pid[i] = etc
	}
}

void comandoCPU(){
	//Envio la peticion a cpu de metrica, cantidad cpus ponerla global e ir mostrando. 1 for para send y recv
	int tamanioPaquete = sizeof(int);
	char* paquete = malloc(tamanioPaquete);

	serializarInt(paquete, PEDIDOMETRICA);

	send(clienteCPUPadre, paquete, tamanioPaquete, 0);

	free(paquete);
	//recv(); ver si se lo mando a cada hilo de cpu asi voy mostrando.
}

void planificador() {
	log_info(archivoLog, "Empieza el thread planificador.\n");
	log_debug(archivoLog, "Empieza el thread planificador.\n");

	cpu_t* cpu = malloc(sizeof(cpu_t));
	pcb_t* pcb = malloc(sizeof(pcb_t));

	while(1){

		if (! (queue_is_empty(queueCPULibre) || queue_is_empty(queueReady))){

			pthread_mutex_lock(&mutexQueueReady);
			pcb = queue_pop(queueReady);
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

			log_debug(archivoLogDebug, "Proceso ejecuta en CPU N°: %i", cpu->numeroCPU);

			procesoCorriendo_t* proceso = malloc(sizeof(procesoCorriendo_t));
			proceso->pcb = pcb;
			proceso->clienteCPU = cpu;

			log_debug(archivoLogDebug, "Struct proceso: pcb = %i, CPU: %i", proceso->pcb->processID, proceso->clienteCPU->cliente);

			//Comienza un thread para mantener el proceso corriendo y seguirlo
			pthread_t threadProceso;
			pthread_create(&threadProceso, NULL, (void *) procesoCorriendo, proceso);
		}
	}
	free(pcb);
	free(cpu);
}

void finalizarRafaga(pcb_t* pcb, t_queue* colaDestino, int* tiempoBlocked){

	log_debug(archivoLogDebug, "Entré a finalizar rafaga");
	pcb_t* aux = malloc(sizeof(pcb_t));
	t_queue* queueAux;

	queueAux = queue_create();

	log_debug(archivoLogDebug, "pase los malloc ");
	while(!queue_is_empty(queueRunning)){
		log_debug(archivoLogDebug, "por sacar aux en running");
		aux = queue_pop(queueRunning);
		log_debug(archivoLogDebug,"Saco aux en running");
		if(pcb->processID == aux->processID){

			if(tiempoBlocked != NULL){
				log_debug(archivoLogDebug, "entre al if");
				procesoBlocked_t* proceso = malloc(sizeof(procesoBlocked_t));
				proceso->tiempoDormido = *tiempoBlocked;
				proceso->pcb = pcb;
				queue_push(colaDestino, proceso);
				free(proceso);
				break;
			}

			log_debug(archivoLogDebug, "sali del if");

			//Si la colaDestino es NULL significa que termina el proceso y lo saca de todas las queue
			if(colaDestino != NULL)
				queue_push(colaDestino, pcb);

			break;
		} else {
			log_debug(archivoLogDebug,"entre donde la cola destino no es null");
			queue_push(queueAux, aux);
			log_debug(archivoLogDebug,"meti aux en queueAux");
		}
	}

	log_debug(archivoLogDebug, "Tamaño de la cola: %i", queue_size(queueAux));
	while(!queue_is_empty(queueAux)){

		pthread_mutex_lock(&mutexQueueRunning);
		log_debug(archivoLogDebug, "la cola de Figo no esta vacia");
		aux = queue_pop(queueAux);
		log_debug(archivoLogDebug,"saque aux de queueAux");
		queue_push(queueRunning, aux);
		log_debug(archivoLogDebug, "Meti aux en queueRunning");
		pthread_mutex_unlock(&mutexQueueRunning);
	}

	queue_destroy(queueAux);
	free(aux);
}

//Al ser un KLT usar sleep y se clava solo el hilo
void entradaSalida(){
	procesoBlocked_t* proceso;

	log_debug(archivoLogDebug, "Empieza el hilo de Entrada/Salida.");

	while(1){

		if(!queue_is_empty(queueBlocked)){
			pthread_mutex_lock(&mutexQueueReady);
			proceso = queue_pop(queueBlocked);
			pthread_mutex_unlock(&mutexQueueReady);

			log_debug(archivoLogDebug, "Entra al thread bloqueado, proceso: %i tiempo: %i", proceso->pcb->processID, proceso->tiempoDormido);
			sleep(proceso->tiempoDormido);

			pthread_mutex_lock(&mutexQueueReady);
			queue_push(queueReady, proceso->pcb);
			pthread_mutex_unlock(&mutexQueueReady);
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

	log_debug(archivoLogDebug, "Pase el malloc de paquete");
	serializarChar(serializarInt(serializarInt(serializarInt(paquete,INICIARPROCESO), pcb->processID), pcb->programCounter),pcb->path);

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

			log_debug(archivoLogDebug, "El Resultado de la rafaga fue: %i.\n",resultadoRafaga);

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

			log_info(archivoLog, "Entré al case PROCESOBLOQUEADO");
			log_debug(archivoLogDebug, "Entré al case PROCESOBLOQUEADO");
			int programCounter;
			recibirYDeserializarInt(&programCounter, cpu->cliente);

			int tiempoBloqueado;
			recibirYDeserializarInt(&tiempoBloqueado, cpu->cliente);

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu->cliente);

			log_debug(archivoLogDebug, "El Resultado de la rafaga fue: %s.",resultadoRafaga);

			free(resultadoRafaga);

			pcb->estadoProceso = BLOCKED;
			pcb->programCounter = programCounter;

			pthread_mutex_lock(&mutexQueueBlocked);
			finalizarRafaga(pcb, queueBlocked, &tiempoBloqueado);
			pthread_mutex_unlock(&mutexQueueBlocked);

			log_info(archivoLog, "Se bloquea el proceso %i.\n", pcb->processID);

			break;
		}
		case FINALIZARPROCESO:{
			log_info(archivoLog, "entre al case FINALIZARPROCESO");
			log_debug(archivoLogDebug, "entre al case FINALIZARPROCESO");

			char* resultadoRafaga;
			recibirYDeserializarChar(&resultadoRafaga, cpu->cliente);

			log_info(archivoLog, "El Resultado de la rafaga fue: %s.\n",resultadoRafaga);
			log_debug(archivoLog, "El Resultado de la rafaga fue: %s.\n",resultadoRafaga);

			free(resultadoRafaga);

			int* numeroProceso = malloc(sizeof(int));
			*numeroProceso = pcb->processID;

			pthread_mutex_lock(&mutexQueueReady);
			log_debug(archivoLogDebug, "por finalizar rafaga");
			finalizarRafaga(pcb, NULL, NULL);
			log_debug(archivoLogDebug, "Despúes de finalizasizar rafaga");
			pthread_mutex_unlock(&mutexQueueReady);

			log_debug(archivoLogDebug, "Finaliza el proceso %i.", *numeroProceso);

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
}

void esperarConexiones(){
	int cantidadCPUs;

	//Configuracion del Servidor
	configurarSocketServidor();

	//En primera instancia se conecta el proceso CPU y manda la cantidad de CPUs que va a tener corriendo
	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteCPUPadre = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %d\n", clienteCPUPadre);

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

		//El cliente CPU va a ser la posición i del array de clientes, entonces lo podria usar desde el array mismo
		queue_push(queueCPULibre, cpu);
	}

}
