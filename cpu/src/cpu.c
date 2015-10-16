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
char* ipPlanificador;
int puertoPlanificador;
char* ipMemoria;
int puertoMemoria;
int cantidadHilos;
int retardo;
int socketPlanificador;
int socketMemoria;
int programCounter;
pthread_t hilos[cantidadHilos];
int threadCounter;

void configurarCPU(char* config);
int configurarSocketCliente(char* ip, int puerto, int*);
void ejecutarmProc();
void iniciarmProc(int pID, int cantPaginas);
void leermProc(int pID, int nroPagina);
void finalizarmProc(int pID);
void escribirmProc(int pID, int nroPagina, char* texto);
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

	// Empiezo a probar con multihilos

	for(threadCounter=0; threadCounter<=cantidadHilos; threadCounter++ ){
		pthread_create(&hilos[threadCounter], NULL, (void *) ejecutarmProc, NULL);
		int thread_id= process_get_thread_id();
		log_info(archivoLog, "Instancia de CPU %d creada", thread_id);
		pthread_join(hilos[threadCounter], NULL);
	}

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

void iniciarmProc(int pID, int cantPaginas) {
	int tamPaquete = sizeof(int) * 3;
	char* paquete = malloc(tamPaquete);
	log_info(archivoLog, "Cantidad de paginas: %i.\n", cantPaginas);
	serializarInt(serializarInt(serializarInt(paquete, INICIOMEMORIA), pID),cantPaginas);
	log_info(archivoLog, "Paquete: %s.\n", paquete);
	send(socketMemoria, paquete, tamPaquete, 0);
	log_info(archivoLog, "mande el paquete\n");
	free(paquete);
}

void finalizarmProc(int pID) {
	int tamPaquete = sizeof(int) * 2;
	char* paquete = malloc(tamPaquete);
	serializarInt(serializarInt(paquete, FINALIZARPROCESO), pID);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);

}

void leermProc(int pID, int nroPagina) {
	int tamPaquete = sizeof(int) * 3;
	char* paquete = malloc(tamPaquete);
	serializarInt(serializarInt(serializarInt(paquete, LEERMEMORIA), pID), nroPagina);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);
}


void escribirmProc(int pID, int nroPagina, char* texto){
	int tamPaquete= strlen(texto) + 1 + sizeof(int) *3;
	char* paquete= malloc(tamPaquete);
	serializarChar(serializarInt(serializarInt(serializarInt(paquete, ESCRIBIRMEMORIA), pID), nroPagina), texto);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);

}

void entradaSalidamProc(int pID, int tiempo){
	int tamPaquete= sizeof(int)*3;
	char* paquete= malloc(tamPaquete);
	serializarInt(serializarInt(serializarInt(paquete, ENTRADASALIDA), pID), tiempo);
	send(socketPlanificador, paquete, tamPaquete, 0);
	free(paquete);
}


void ejecutarmProc() {
	FILE* mCod;
	int pID;
	int programCounter;
	char* path;
	int tamanioPaquete;
	char* comandoLeido = malloc(30); //a cambiar en posteriores checkpoints
	char* instruccion;
	char* resultadosTot = string_new();
	char* paqueteRafaga;

	recibirYDeserializarInt(&pID, socketPlanificador);
	log_info(archivoLog, "Recibi pid %i.\n", pID);
	recibirYDeserializarInt(&programCounter, socketPlanificador);
	log_info(archivoLog, "Recibi program counter %i.\n", programCounter);
	recibirYDeserializarChar(&path, socketPlanificador);
	log_info(archivoLog, "Recibi path %s.\n", path);

	//mCod = fopen(string_from_format("/home/utnso/tp-2015-2c-elquevaaaprobar/cpu/Debug/%s",path), "r");
	mCod=fopen(path,"r");
	log_info(archivoLog, "Conectado a la Memoria %i.\n", mCod);

	fgets(comandoLeido, 30, mCod);

	char** leidoSplit = string_split(comandoLeido, " ");
	instruccion = leidoSplit[0];
	log_info(archivoLog, "Conectado a la Memoria %s.\n", instruccion);

	while (!feof(mCod)) {
		programCounter++;

		if (string_equals_ignore_case(instruccion, "iniciar")) {
			int cantPaginas= strtol(leidoSplit[1], NULL, 10);
			iniciarmProc(pID, cantPaginas);
			int verificador;
			recibirYDeserializarInt(&verificador, socketMemoria);
			if (verificador != -1) {
				log_info(archivoLog,"Instruccion ejecutada:iniciar %d Proceso:%d iniciado",	cantPaginas, pID);
				char* aux = string_from_format("mProc %d - Iniciado", pID);
				string_append(&resultadosTot, aux);
				free(aux);
			}
			else {
				log_info(archivoLog,"Instruccion ejecutada:iniciar %d Proceso:%d. FALLO!",cantPaginas, pID);
				char* aux = string_from_format("mProc %d - Fallo", pID);
				string_append(&resultadosTot, aux);
				free(aux);
			}
		}

		if (string_equals_ignore_case(instruccion, "leer")) {
			int nroPagina=strtol(leidoSplit[1], NULL, 10);
			leermProc(pID, nroPagina);
			int verificador;
			recibirYDeserializarInt(&verificador, socketMemoria);
			if (verificador != -1) {
				char* resultado = malloc(sizeof(char) * 25);
				recibirYDeserializarChar(&resultado, socketMemoria);
				log_info(archivoLog,"Instruccion ejecutada:leer %d Proceso:%d. Resultado:%s",nroPagina, pID, resultado);
				char* aux = string_from_format("mProc %d - Pagina %d leida: %s", pID, nroPagina, resultado);
				string_append(&resultadosTot, aux);
				free(resultado);
				free(aux);
			} else {
				log_info(archivoLog,"Instruccion ejecutada: leer %d  Proceso: %d - Error de lectura",nroPagina, pID);
			}
		}
		if (string_equals_ignore_case(instruccion, "escribir")) {
			int nroPagina=strtol(leidoSplit[1], NULL, 10);
			char* texto=malloc(30);
			corregirTexto(leidoSplit[2], texto);  //hice esta funcion para sacar el ; y las "  pero nose si esta bien
			escribirmProc(pID, nroPagina, texto);
			int verificador;
			recibirYDeserializarInt(&verificador,socketMemoria);
			if (verificador!= -1){
				log_info(archivoLog, "Instruccion ejecutada: escribir %d %s Proceso: %d Resultado: %s", nroPagina,texto, pID, texto);
				char* aux= string_from_format("mProc %d - Pagina %d escrita:%s",pID, nroPagina, texto);
				string_append(&resultadosTot, aux);
				free(aux);
			}
			else{
				log_info(archivoLog, "Instruccion ejecutada: escribir %d %s Proceso: %d - Error de escritura", nroPagina, texto, pID);
			}

		}
		if (string_equals_ignore_case(instruccion, "entrada-salida")) {
			int tiempo= strtol(leidoSplit[1], NULL, 10);
			entradaSalidamProc(pID, tiempo);
			log_info(archivoLog, "Instruccion ejecutada: entrada-salida %d Proceso: %d ", tiempo, pID);
			char* aux=string_fom_format("mProc %d en entrada-salida de tiempo %d", pID, tiempo);
			string_append(&resultadosTot, aux);
			free(aux);

		}
		if (string_equals_ignore_case(instruccion, "finalizar")) {
			finalizarmProc(pID);
			int verificador;
			recibirYDeserializarInt(&verificador, socketMemoria);
			if (verificador != -1) {
				log_info(archivoLog,"Instruccion ejecutada:finalizar Proceso:%d finalizado", pID);
				char* aux = string_from_format("mProc %d finalizado",pID);
				string_append(&resultadosTot, aux);
			} else {
				log_info(archivoLog,"Instruccion ejecutada:finalizar Proceso:%d - Error al finalizar", pID);
			}
		}

		sleep(retardo);
		fgets(comandoLeido, 30, mCod);

	}

	log_info(archivoLog, "Ejecucion de rafaga concluida. Proceso:%d", pID);
	free(comandoLeido);
	free(leidoSplit);
	fclose(mCod);
	tamanioPaquete = strlen(resultadosTot) + 1 + sizeof(int)*2;
	paqueteRafaga = malloc(tamanioPaquete);
	serializarInt(serializarChar(paqueteRafaga, resultadosTot), RAFAGAPROCESO);
	send(socketPlanificador, paqueteRafaga, tamanioPaquete, 0);
	free(resultadosTot);
	free(paqueteRafaga);

}



corregirTexto(char* textoOriginal,char* textoCorregido){
	int i=1;
	int t=0;

	while(textoOriginal[i]!= '"'){
		textoCorregido[t]= textoOriginal[i];
		i++;
		t++;

	}

}





