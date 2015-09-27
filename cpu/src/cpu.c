/*
 ============================================================================
 Name        : cpu.c
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
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <commons/config.h>
#include <commons/log.h>
#include <commons/process.h>
#include <commons/string.h>
#include <commons/collections/list.h>

t_log* archivoLog;
char* ipPlanificador;
int puertoPlanificador;
char* ipMemoria;
int puertoMemoria;
int cantidadHilos;
int retardo;
int socketPlanificador;
int socketMemoria;
int programCounter;
pthread_t hilo1;

typedef struct _t_Package {
	char message[100];
	uint32_t message_long;
} t_Package;

typedef enum {
	iniciar, leer, escribir, entradaSalida, finalizar
} t_instruccion;

void configurarCPU(char* config);
int configurarSocketCliente(char* ip, int puerto, int*);
int recieve_and_deserialize(t_Package *package, int socketCliente);
void ejecutarmProc(char* ruta);
void iniciarmProc(char* comando); //desde aca a finalizarmProc,ver si son void
void leermProc(char* comando);
void escribirmProc(char* comando);
void entradaSalidamProc(char* comando);
void finalizarmProc();
void asignarmCodHilo(char* path, int programCounter);

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_CPU", "CPU", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	configurarCPU(argv[1]);

	//conexion con el planificador
	if (configurarSocketCliente(ipPlanificador, puertoPlanificador,
			&socketPlanificador))
		log_info(archivoLog, "Conectado al Planificador %i.\n",
				socketPlanificador);
	else
		log_error(archivoLog, "Error al conectar con Planificador. %s\n",
				ipPlanificador);

	if (configurarSocketCliente(ipMemoria, puertoMemoria, &socketMemoria))
		log_info(archivoLog, "Conectado a la Memoria %i.\n", socketMemoria);
	else
		log_error(archivoLog, "Error al conectar con Memoria. %s\n", ipMemoria);

	char* mCod = malloc(15);

	recv(socketPlanificador, mCod, 15, 0);
	log_info(archivoLog, "Recibi %s", mCod);

	send(socketMemoria, mCod, 15, 0);

	char* notificacion = malloc(11);
	recv(socketMemoria, notificacion, 11, 0);
	log_info(archivoLog, "%s", notificacion);

	send(socketPlanificador, notificacion, 11, 0);

	free(mCod);
	free(notificacion);

	// Por ahora pruebo con un solo hilo para el Checkpoint

	pthread_create(&hilo1, NULL, (void *) ejecutarmProc, mCod);

	/*
	 char* msg = "hola";
	 int tam = strlen(msg);
	 if (send(socketPlanificador, msg, tam, 0) == -1)
	 log_error(archivoLog, "Error en el send.\n");
	 else
	 log_info(archivoLog, "Mandé \"%s\" a planificador.\n", msg);
	 t_Package package;
	 int status = recieve_and_deserialize(&package, socketPlanificador);
	 if (status)
	 log_info(archivoLog, "Planificador says: %s", package.message);
	 //conexion con el adm de mem
	 if (configurarSocketCliente(ipMemoria, puertoMemoria, &socketMemoria))
	 log_info(archivoLog, "Conecté con el administrador de memoria %i.\n", socketMemoria);
	 else
	 log_error(archivoLog, "Error al conectar con la memoria. %s\n",	ipMemoria);

	 if (send(socketMemoria, msg, tam, 0) == -1)
	 log_error(archivoLog, "Error en el send.\n");
	 else
	 log_info(archivoLog, "Mandé \"%s\" a memoria.\n", msg);

	 */

	return 0;

}

void configurarCPU(char* config) {

	t_config* configCPU = config_create(config);
	if (config_has_property(configCPU, "IP_PLANIFICADOR"))
		ipPlanificador = string_duplicate(
				config_get_string_value(configCPU, "IP_PLANIFICADOR"));
	if (config_has_property(configCPU, "PUERTO_PLANIFICADOR"))
		puertoPlanificador = config_get_int_value(configCPU,
				"PUERTO_PLANIFICADOR");
	if (config_has_property(configCPU, "IP_MEMORIA"))
		ipMemoria = string_duplicate(
				config_get_string_value(configCPU, "IP_MEMORIA"));
	if (config_has_property(configCPU, "PUERTO_MEMORIA"))
		puertoMemoria = config_get_int_value(configCPU, "PUERTO_MEMORIA");
	if (config_has_property(configCPU, "CANTIDAD_HILOS"))
		cantidadHilos = config_get_int_value(configCPU, "CANTIDAD_HILOS");
	if (config_has_property(configCPU, "RETARDO"))
		retardo = config_get_int_value(configCPU, "RETARDO");

	config_destroy(configCPU);
}

int configurarSocketCliente(char* ip, int puerto, int* s) {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr(ip);
	direccionServidor.sin_port = htons(puerto);

	*s = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(*s, (void*) &direccionServidor, sizeof(direccionServidor))
			== -1) {
		log_error(archivoLog, "No se pudo conectar");
		return 0;
	}

	return 1;
}

int recieve_and_deserialize(t_Package *package, int socketCliente) {
	int status;
	int buffer_size;
	char *buffer = malloc(buffer_size = sizeof(uint32_t));
	uint32_t message_long;
	status = recv(socketCliente, buffer, sizeof(package->message_long), 0);
	memcpy(&(message_long), buffer, buffer_size);
	log_info(archivoLog, "primer recv: %d", message_long);
	if (!status)
		return 0;
	status = recv(socketCliente, package->message, message_long, 0);
	log_info(archivoLog, "segundo recv: %s", package->message);
	if (!status)
		return 0;
	free(buffer);
	return status;
}

void ejecutarmProc(char* path) {
	FILE* mCod;
	char* resultadosTot = malloc(100);
	char* comando = malloc(20);
	char* instruccion = malloc(10);
	char** comandoSplit = malloc(sizeof(char*) * 3);

	mCod = fopen(path, "r");
	while (fgets(comando, 20, mCod) != NULL) {

		comandoSplit = string_split(comando, " ");
		instruccion = comandoSplit[0];

		switch (instruccion) {
		case iniciar:
			iniciarmProc(comando);
			char* resultado = malloc(20);
			recv(socketMemoria, resultado, 20, 0);
			string_append_with_format(&resultadosTot, "%s!", resultado);
			break;
		case leer:
			leermProc(comando);
			break;
		case escribir:
			escribirmProc(comando);
			break;
		case entradaSalida:
			entradaSalidamProc(comando);
			break;
		case finalizar:
			finalizarmProc();
			char* resultado = malloc(20);
			recv(socketMemoria, resultado, 20, 0);
			string_append_with_format(&resultadosTot, "%s!", resultado);
			break;
		}

		sleep(retardo);
	}

	void liberarPunterosDobles(comandoSplit, 3);
	free(comandoSplit);
	free(instruccion);
	free(comando);
	fclose(mCod);

	send(socketPlanificador, resultadosTot, 100, 0);
	free(resultadosTot);

}

void iniciarmProc(char* comando) {
	send(socketMemoria, comando, 20, 0);
}

void finalizarmProc() {
	char* avisoMemoria = malloc(sizeof(char) * 30);
	strcpy(avisoMemoria, "Finalizar proceso PID");
	send(socketMemoria, avisoMemoria, 30, 0);
	free(avisoMemoria);

}

void leermProc(char* comando) {
	send(socketMemoria, comando, 20, 0);

}

liberarPuntrosDobles(char** puntero, int ultimo) {
	int i;
	for (i = 0; i <= ultimo; i++)
		free(puntero[i]);
}

void asignarmCodHilo(char* path, int programCounter) {
	FILE* mCod;
	int pc = programCounter; //le llega de plani
	char* comandoLeido = malloc(30);
	char* instruccion = malloc(20);
	char** leidoSplit = malloc(sizeof(char*) * 3); //esta copiado de la func ejecutarmProc, hay que ver lo del malloc

	mCod = fopen(path, "r");
	fgets(comandoLeido, 30, mCod);

	leidoSplit = string_split(comandoLeido, " "); //separar string y asignarle la primer parte a comando --> separar con un espacio " "
	instruccion = leidoSplit[0];

	while (!string_equals_ignore_case(instruccion, "entrada-salida")) {
		pc += 1;
		if (string_equals_ignore_case(instruccion, "finalizar")) {
			//TODO finalizarProceso();
			break;
		} else {
			fgets(comandoLeido, 30, mCod); //Revisar aca ¡!¡!¡!
		}
	}

	if (string_equals_ignore_case(instruccion, "finalizar")) {
		//avisar finalizar al plani
	} else {
		//avisar interrupcion al plani
	}
}
