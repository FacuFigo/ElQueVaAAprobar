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
#include "../../sockets.h"

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


void configurarCPU(char* config);
int configurarSocketCliente(char* ip, int puerto, int*);
void ejecutarmProc(char* ruta, int contador);
void iniciarmProc(char* comando,int cantPaginas);
void leermProc(char* comando, int nroPagina);
void finalizarmProc();
void escribirmProc();
void entradaSalidamProc();

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

//	if (configurarSocketCliente(ipMemoria, puertoMemoria, &socketMemoria))
//		log_info(archivoLog, "Conectado a la Memoria %i.\n", socketMemoria);
//	else
//		log_error(archivoLog, "Error al conectar con Memoria. %s\n", ipMemoria);

	char* mCod = malloc(15);
	int tamanioCadena,operacion;
	//recv(socketPlanificador, mCod, 8, 0);
	//memcpy(&operacion,mCod,4);
	//memcpy(&tamanioCadena,mCod+4,4);
	recibirYDeserializarInt(&operacion,socketPlanificador);
	log_info(archivoLog, "Operacion %d", operacion);
	//log_info(archivoLog, "Tamaño cadena %d", tamanioCadena);
	//recv(socketPlanificador, mCod, 5, 0);
	recibirYDeserializarChar(&mCod,socketPlanificador);
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



void iniciarmProc(char* comando, int cantPaginas) {
	int tamPaquete= sizeof(char)*7 + sizeof(int);
	char* paquete=malloc(tamPaquete);
	serializarChar(&paquete, comando);
	serializarInt(&paquete, cantPaginas);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);
}

void finalizarmProc() {
    char* tamPaquete= sizeof(char) * 30;
	char* avisoMemoria = malloc(tamPaquete);
	char* paquete= malloc(tamPaquete);
	strcpy(avisoMemoria, "Finalizar proceso PID");
	serializarChar(&paquete, avisoMemoria);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(avisoMemoria);
	free(paquete);

}

void leermProc(char* comando, int nroPagina) {
	int tamPaquete= sizeof(char)*4 + sizeof(int);
    char* paquete=malloc(tamPaquete);
    serializarChar(&paquete, comando);
    serializarInt(&paquete, nroPagina);
    send(socketMemoria, paquete, tamPaquete, 0);
    free(paquete);
}

void ejecutarmProc(char* path, int programCounter) {
	FILE* mCod;
	int pc = programCounter; //le llega de plani
	char* comandoLeido = malloc(sizeof(char)*14 + sizeof(int));
	char* instruccion = malloc(sizeof(char)*14);
	char** leidoSplit = malloc(sizeof(char)*3);
	leidoSplit[0]=malloc(sizeof(char)*14);
	leidoSplit[1]=malloc(sizeof(int));
	char* resultadosTot = malloc(sizeof(char)*100);  //ver calloc
	char* paqueteRafaga=malloc(sizeof(char)*100);

	mCod = fopen(path, "r");
	fgets(comandoLeido, 30, mCod);

	leidoSplit = string_split(comandoLeido, " ");
	instruccion = leidoSplit[0];

	while (!feof(mCod)) {
		pc += 1;

		if (string_equals_ignore_case(instruccion, "iniciar")) {
			iniciarmProc(leidoSplit[0], leidoSplit[1]);
			char* resultado = malloc(sizeof(char)*25);
			recibirYDeserializarChar(&resultado, socketMemoria);
			string_append_with_format(&resultadosTot, "%s!", resultado);
			log_info(archivoLog, "Instruccion ejecutada:iniciar %d Proceso:PID Resultado: %s.", leidoSplit[1], resultado);
			free(resultado);
		}
		if (string_equals_ignore_case(instruccion, "leer")) {
			leermProc(leidoSplit[0], leidoSplit[1]);
			log_info(archivoLog, "Instruccion ejecutada:leer %d", leidoSplit[1]);
		}
		if (string_equals_ignore_case(instruccion, "escribir")) { //proximamente, solo en sisop
			escribirmProc();
		}
		if (string_equals_ignore_case(instruccion, "entrada-salida")) { //proximamente, solo en sisop
			entradaSalidamProc();
		}
		if (string_equals_ignore_case(instruccion, "finalizar")) {
			finalizarmProc();
			char* resultado = malloc(sizeof(char)*25);
			recibirYdeserializarChar(&resultado, socketMemoria);
			string_append_with_format(&resultadosTot, "%s!", resultado);
			log_info(archivoLog, "Instruccion ejecutada:finalizar Resultado:%s", resultado);
			free(resultado);
		}

		sleep(retardo);
		fgets(comandoLeido, 30, mCod);

	}

	log_info(archivoLog, "Ejecucion de rafaga concluida. Proceso: PID");
	free(comandoLeido);
	free(instruccion);
	free(leidoSplit);
	fclose(mCod);

	serializarChar(&paqueteRafaga, resultadosTot);
	send(socketPlanificador, paqueteRafaga, 100, 0);
	free(resultadosTot);
	free(paqueteRafaga);

}



void escribirmProc(){

}


void entradaSalidamProc(){

}
