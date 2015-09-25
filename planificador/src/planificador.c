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

t_list* listaProcesos;

t_queue* queueReady;
t_queue* queueRunning;
t_queue* queueBlocked;
t_queue* queueCPU;
t_queue* queueCPULibre;


int pIDContador = 1;

//Estructuras
typedef enum {READY, RUNNING, BLOCKED} estados_t;
typedef enum {FINISH, QUANTUM} estadoCPU_t; //el cpu avisa si termino la rafaga (FIFO) o quantum(RR)

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

//Funciones de sockets
char* serializarOperandos(t_Package *package);
void fill_package(t_Package *package);

//Funciones de comandos
void correrProceso(char* path);
void generarPCB(pcb_t* pcb);
void finalizarProceso(char* pid);
void estadoProcesos();
void comandoCPU();

int buscarEnCola(t_queue* cola, char* pid);

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_Planificador", "Planificador", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	configurarPlanificador(argv[1]);

	//Creacion de servidor
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

	//Meto la cpu que se conecta a la cola de libres
	queue_push(queueCPULibre, &clienteCPU);

	//Creacion de colas
	queueReady = queue_create();
	queueRunning = queue_create();
	queueBlocked = queue_create();
	queueCPU = queue_create();
	queueCPULibre = queue_create();

//TODO Hilo multiplexor
	//Comienza el thread de la consola
	pthread_t hiloConsola;
	pthread_create(&hiloConsola, NULL, (void *) manejoDeConsola, NULL);

	//Comienza el thread del planificadorFIFO
	pthread_t hiloPlanificadorFIFO;
	pthread_create(&hiloPlanificadorFIFO, NULL, (void *) planificadorFIFO, NULL);

	//Comienza el thread del planificadorRR
	pthread_t hiloPlanificadorRR;
	pthread_create(&hiloPlanificadorRR, NULL, (void *) planificadorRR, NULL);

	pthread_join(hiloPlanificadorFIFO, NULL);
	pthread_join(hiloConsola, NULL);
	pthread_join(hiloPlanificadorRR, NULL);

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
		
		comando.comando = malloc(10);
		comando.parametro = malloc(50);
		
//TODO Cambiar scanf() por fgets()
		scanf("%s %s", comando.comando, comando.parametro);
		getchar();
		if (comando.parametro == NULL){
			if (string_equals_ignore_case(comando.comando, "correr")) 
				correrProceso(comando.parametro);
				//send(clienteCPU, comando.parametro, sizeof(comando.parametro), 0);
			 else 
				finalizarProceso(comando.parametro);
		} else {
			if (string_equals_ignore_case(comando.comando, "ps"))
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

void correrProceso(char* path) {
	
	pcb_t* pcbProc = malloc(sizeof(pcb_t));
	generarPCB(pcbProc);
	pcbProc->path = string_duplicate(path);
	//Agrego a la cola READY
	queue_push(queueReady, pcbProc);
}

void generarPCB(pcb_t* pcb){

	pcb->processID = pIDContador;
	pcb->programCounter = 0;
	//El estado se asigna a Ready
	pcb->estadoProceso = 0;

	pIDContador++;
	
	log_info(archivoLog, "Se genero el PCB del proceso %i.", pcb->processID);

}

void finalizarProceso(char* pid){

	if(buscarEnCola(queueReady, pid) == -1)
		if(buscarEnCola(queueRunning, pid) == -1)
			if(buscarEnCola(queueBlocked, pid) == -1)
				log_error(archivoLog, "No se pudo finalizar el proceso %s", pid);

}

//¿Una mejor forma seria que devuelva el pcb encontrado y lo elimino fuera?
int buscarEnCola(t_queue* cola, char* pid){

	pcb_t* pcb = malloc(sizeof(pcb_t));
	t_queue* queueAuxiliar = queue_create();
	int encontrado = 0;

	//Busco en la queue que viene por parametro, si se encuentra lo elimina y marca el flag como encontrado
	while(!queue_is_empty(cola)){

		pcb = queue_pop(cola);
		//TODO Encontrar la forma de comparar el int con el pID del comando
		if(pcb->processID == pid){
			//Enviar al CPU la peticion de eliminar
			//Recibir la confirmacion de que se pudo eliminar
			log_info(archivoLog, "Se elimina el proceso:%i", pcb->processID);
			free(pcb);
			encontrado++;
			break;
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

void planificadorFIFO() {

	log_info(archivoLog, "Empieza el thread planificador.\n");

	int* auxCPU = malloc(sizeof(int));

	while(1){

		if (! (queue_is_empty(queueCPULibre) && queue_is_empty(queueReady))){

			pcb_t* auxPCB = malloc(sizeof(pcb_t));
			auxPCB = queue_pop(queueReady);
			auxCPU = queue_pop(queueCPULibre);
			//Cambia el estado del proceso
			auxPCB->estadoProceso = 1;
			queue_push(queueCPU, auxCPU);
			queue_push(queueRunning, auxPCB);



//TODO mandarle al cpu el pid del proceso que va a correr
//switch enum me va a llegar notificacion del cpu de que termino y lo mando a block o finish

			finalizarProceso(auxPCB->processID);
			auxCPU	= queue_pop(queueCPU);
			queue_push(queueCPULibre, auxCPU);

		}
	}
	free(auxCPU);
}

void planificadorRR() {

/*	log_info(archivoLog, "Empieza el thread planificador.\n");
	//Meto la cpu que se conecta a la cola de libres
	queue_push(queueCPULibre, &clienteCPU);

	if (! queue_is_empty(queueCPULibre)){

		pcb_t *auxPCB = queue_pop(queueReady);
		int *auxCPU = queue_pop(queueCPULibre);
		queue_push(queueRunning, auxPCB);
		queue_push(queueCPU, auxCPU);

	}

	int* estadoCPU = malloc(sizeof(int));
	recv(clienteCPU, estadoCPU, sizeof(int), 0);

//switch enum me va a llegar notificacion del cpu de que termino y lo mando a block o finish

	switch(*estadoCPU){
		case FINISH:
			finalizarProceso(pid);

			queue_pop(queueRunning);
			int *auxCPU = queue_pop(queueCPU);
			queue_push(queueCPULibre, auxCPU);

			free(estadoCPU);
			break;
//TODO cuando va a block asignarle nuevo proceso a la cpu libre, lo comento porque en primer linea de quantum da error de label (?)
		case QUANTUM:
			bloquearProceso();

			free(estadoCPU);
			break;
	}
*/
}

void bloquearProceso(){
/*
	pcb_t *auxPCB = queue_pop(queueRunning);
				queue_push(queueBlocked, auxCPU );
				int *auxCPU = queue_pop(queueCPU);
				queue_push(queueCPULibre, auxCPU);
*/
}
