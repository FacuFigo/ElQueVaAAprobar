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

typedef enum {
	INICIARPROCESO,
	ENTRADASALIDA,
	INICIOMEMORIA,
	LEERMEMORIA,
	ESCRIBIRMEMORIA,
	FINALIZARPROCESO
} operacion_t;

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
void ejecutarmProc();
void iniciarmProc(int pID, char* parametro);
void leermProc(int pID, char* parametro);
void finalizarmProc(int pID);
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

	if (configurarSocketCliente(ipMemoria, puertoMemoria, &socketMemoria))
		log_info(archivoLog, "Conectado a la Memoria %i.\n", socketMemoria);
	else
		log_error(archivoLog, "Error al conectar con Memoria. %s\n", ipMemoria);

	// Por ahora pruebo con un solo hilo para el Checkpoint

	pthread_create(&hilo1, NULL, (void *) ejecutarmProc, NULL);
	pthread_join(hilo1, NULL);

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
	if (connect(*s, (void*) &direccionServidor, sizeof(direccionServidor))	== -1) {
		log_error(archivoLog, "No se pudo conectar");
		return 0;
	}

	return 1;
}

void iniciarmProc(int pID, char* parametro) {
	int tamPaquete = sizeof(int) * 3;
	char* paquete = malloc(tamPaquete);
	int cantPaginas = strtol(parametro, NULL, 10);
	serializarInt(serializarInt(serializarInt(paquete, INICIOMEMORIA), pID),cantPaginas);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);
}

void finalizarmProc(int pID) {
	int tamPaquete = sizeof(int) * 2;
	char* paquete = malloc(tamPaquete);
	serializarInt(serializarInt(paquete, FINALIZARPROCESO), pID);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);

}

void leermProc(int pID, char* parametro) {
	int tamPaquete = sizeof(int) * 3;
	char* paquete = malloc(tamPaquete);
	int nroPagina = strtol(parametro, NULL, 10);
	serializarInt(serializarInt(serializarInt(paquete, LEERMEMORIA), pID), nroPagina);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);
}

void ejecutarmProc() {
	FILE* mCod;
	int pID;
	int programCounter;
	int tamanioPaquete;
	char* path;
	char* comandoLeido = malloc(14); //a cambiar en posteriores checkpoints
	char* instruccion;
	char* resultadosTot = string_new();
	char* paqueteRafaga;
	recibirYDeserializarChar(&path, socketPlanificador);
	recibirYDeserializarInt(&programCounter, socketPlanificador);
	recibirYDeserializarInt(&pID, socketPlanificador);

	mCod = fopen(path, "r");
	fgets(comandoLeido, 14, mCod);

	char** leidoSplit = string_split(comandoLeido, " ");
	instruccion = leidoSplit[0];

	while (!feof(mCod)) {
		programCounter++;

		if (string_equals_ignore_case(instruccion, "iniciar")) {
			iniciarmProc(pID, leidoSplit[1]);
			int verificador;
			recibirYDeserializarInt(&verificador, socketMemoria);
			if (verificador != -1) {
				log_info(archivoLog,"Instruccion ejecutada:%s Proceso:%d iniciado",	comandoLeido, pID);
				char* aux = string_from_format("mProc %d - Iniciado", pID);
				string_append(&resultadosTot, aux);
				free(aux);
			} else {
				log_info(archivoLog,"Instruccion ejecutada:%s Proceso:%d. FALLO!",comandoLeido, pID);
				char* aux = string_from_format("mProc %d - Fallo", pID);
				string_append(&resultadosTot, aux);
				free(aux);
			}
		}

		if (string_equals_ignore_case(instruccion, "leer")) {
			leermProc(pID, leidoSplit[1]);
			int verificador;
			recibirYDeserializarInt(&verificador, socketMemoria);
			if (verificador != -1) {
				char* resultado = malloc(sizeof(char) * 25);
				recibirYDeserializarChar(&resultado, socketMemoria);
				log_info(archivoLog,"Instruccion ejecutada:%s Proceso:%d. Resultado:%s",comandoLeido, pID, resultado);
				char* aux = string_from_format("mProc %d - Pagina %s leida: %s", pID, leidoSplit[1], resultado);
				string_append(&resultadosTot, aux);
				free(resultado);
				free(aux);
			} else {
				log_info(archivoLog,"Instruccion ejecutada: %s Proceso: %d Error de lectura",comandoLeido, pID);
			}
		}
		if (string_equals_ignore_case(instruccion, "escribir")) { //proximamente, solo en sisop
			escribirmProc();
		}
		if (string_equals_ignore_case(instruccion, "entrada-salida")) { //proximamente, solo en sisop
			entradaSalidamProc();
		}
		if (string_equals_ignore_case(instruccion, "finalizar")) {
			finalizarmProc(pID);
			int verificador;
			recibirYDeserializarInt(&verificador, socketMemoria);
			if (verificador != -1) {
				log_info(archivoLog,"Instruccio ejecutada:%s Proceso:%d finalizado",comandoLeido, pID);
				char* aux = string_from_format(&resultadosTot, "mProc %d finalizado",pID);
				string_append(&resultadosTot, aux);
			} else {
				log_info(archivoLog,"Instruccio ejecutada:%s Proceso:%d error al finalizar",comandoLeido, pID);
			}
		}

		sleep(retardo);
		fgets(comandoLeido, 14, mCod);

	}

	log_info(archivoLog, "Ejecucion de rafaga concluida. Proceso:%d", pID);
	free(comandoLeido);
	free(instruccion);
	free(leidoSplit[0]);
	free(leidoSplit[1]);
	free(leidoSplit);
	fclose(mCod);
	tamanioPaquete = strlen(resultadosTot) + 1 + sizeof(int);
	paqueteRafaga = malloc(tamanioPaquete);
	serializarChar(paqueteRafaga, resultadosTot);
	send(socketPlanificador, paqueteRafaga, tamanioPaquete, 0);
	free(resultadosTot);
	free(paqueteRafaga);

}

void escribirmProc() {

}

void entradaSalidamProc() {

}
