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
#include <sockets.h>
#include <signal.h>
#include <sys/time.h>

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
char* ipPlanificador;
char* ipMemoria;
int puertoPlanificador;
int puertoMemoria;
int cantidadHilos;
int retardo;
int socketPlanificador;
int socketMemoria;
int programCounter;
int threadCounter;
int quantum;          //si es -1, toy en fifo
int tamanioMarco;
pthread_mutex_t mutexMetricas;

int configurarSocketCliente(char* ip, int puerto, int*);
void configurarCPU(char* config);
void iniciarmProc(int pID, int cantPaginas);
void leermProc(int pID, int nroPagina);
void finalizarmProc(int pID);
void escribirmProc(int pID, int nroPagina, char* texto);
void ejecutarmProc();
void comandoCPU(int instruccionesEjecutadas);

//void timer_handler(int signum);

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_CPU", "CPU", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	configurarCPU(argv[1]);

	log_info(archivoLog, "cantidad de hilos: %d", cantidadHilos);

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

/*	//ARRANCO PRUEBA DE TEPORIZADOR

	struct sigaction sa;
	struct itimerval timer;

	  Install timer_handler as the signal handler for SIGVTALRM.
	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = &timer_handler;
	sigaction (SIGVTALRM, &sa, NULL);

	  Configure the timer to expire after 250 msec...
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_sec = 10;
	  ... and every 250 msec after that.
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_sec = 10;
	  Start a virtual timer. It counts down whenever this process is
	   executing.
	setitimer (ITIMER_VIRTUAL, &timer, NULL);

	  Do busy work.
	while (1);

	//FIN PRUEBA TEMPORIZADOR */

	recibirYDeserializarInt(&quantum, socketPlanificador);
	log_info(archivoLog, "Recibi quantum %d", quantum);

	char* paquetecpu=malloc(sizeof(int));
	serializarInt(paquetecpu, cantidadHilos);
	send(socketPlanificador, paquetecpu, sizeof(int), 0);

	recibirYDeserializarInt(&tamanioMarco, socketMemoria);
	log_info(archivoLog, "Recibi tamanio pagina %i", tamanioMarco);
	//TODO RECIBIR Y DESERIALIZAR INT DE MEMORIA (TAMAÑO PAGINA ) Y CAMBIAR EL 30

	pthread_t hilos;

	for(threadCounter = 0; threadCounter < cantidadHilos; threadCounter++ ){
		pthread_create(&hilos, NULL, (void *) ejecutarmProc, NULL);
		log_info(archivoLog, "Instancia de CPU %i creada.\n", threadCounter);
	}

	for(threadCounter = 0; threadCounter < cantidadHilos; threadCounter++ ){
		pthread_join(hilos,NULL);
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

	int tamPaquete= strlen(texto) + 1 + sizeof(int) *4;
	char* paquete= malloc(tamPaquete);
	serializarChar(serializarInt(serializarInt(serializarInt(paquete, ESCRIBIRMEMORIA), pID), nroPagina), texto);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);

}

void ejecutarmProc() {

	FILE* mCod;

	char* path;
	char* instruccion;
	char* paqueteRafaga;

	int pID;
	int programCounter;
	int tiempoIO;
	int tamanioPaquete;
	int operacion;
	int entradaSalida;
	int quantumRafaga;
	int valor;
	int socketPlaniHilo;
	int tamanioComando = tamanioMarco+15;
	int instruccionesEjecutadas;
	int continuar = 1;

	pthread_t hiloMetricas;

	char comandoLeido[tamanioComando]; //TODO CAMBIAR EL 30

	quantumRafaga = quantum;

	if (configurarSocketCliente(ipPlanificador, puertoPlanificador,	&socketPlaniHilo))
		log_info(archivoLog, "Conectado al Planificador %i.\n", socketPlaniHilo);
	else
		log_error(archivoLog, "Error al conectar con Planificador. %s\n", ipPlanificador);

	pthread_create(&hiloMetricas, NULL, (void *) comandoCPU, &instruccionesEjecutadas);
	//TODO setear instruccionesEjecutadas cada 60 segundos


	while(continuar){

		entradaSalida=0;

		char* resultadosTot = string_new();
		log_info(archivoLog, "Quedo esperando, cpu: %i", process_get_thread_id());

		continuar = recibirYDeserializarInt(&operacion, socketPlaniHilo);

		if (continuar) {

		log_info(archivoLog, "Recibi operacion %i.\n", operacion);

		recibirYDeserializarInt(&pID, socketPlaniHilo);
		log_info(archivoLog, "Recibi pid %i.\n", pID);

		recibirYDeserializarInt(&programCounter, socketPlaniHilo);
		log_info(archivoLog, "Recibi program counter %i.\n", programCounter);

		recibirYDeserializarChar(&path, socketPlaniHilo);
		log_info(archivoLog, "Recibi path %s.\n", path);

		mCod=fopen(path,"r");

		int i;
		for(i=0; programCounter>i; i++){
			fgets(comandoLeido, tamanioComando, mCod);  //este primer fgets sirve para pararte en el programCounter cuando vuelve de quantum o e/s
		}

		do {

			fgets(comandoLeido, tamanioComando, mCod); //TODO cambiar el 30
			log_info(archivoLog,"PRIMER comando leido: %s", comandoLeido);

			char** leidoSplit = string_n_split(comandoLeido, 3, " ");
			instruccion = leidoSplit[0];

			if (strcmp(instruccion,"finalizar;")) {     //se fija si la instruccion es finalizar, si lo es no asigna leidoSplit[1] en valor
				valor = strtol(leidoSplit[1], NULL, 10);
			}

			programCounter++;

			if (string_equals_ignore_case(instruccion, "iniciar")) {

				int cantPaginas=valor;
				iniciarmProc(pID, cantPaginas);
				int verificador;

				recibirYDeserializarInt(&verificador, socketMemoria);

				if (verificador != -1) {

					log_info(archivoLog,"Instruccion ejecutada:iniciar %d Proceso:%d iniciado.",	cantPaginas, pID);
					char* aux = string_from_format("mProc %d - Iniciado.\n", pID);
					string_append(&resultadosTot, aux);
					free(aux);

				} else {

					log_info(archivoLog,"Instruccion ejecutada:iniciar %d Proceso:%d. FALLO!",cantPaginas, pID);
					char* aux = string_from_format("mProc %d - Fallo.\n", pID);
					string_append(&resultadosTot, aux);
					free(aux);

					operacion = FALLOPROCESO;   //PARA EL CASO EN QUE SE PIDAN MAS PAGINAS QUE LAS DISPONIBLES
					break;
				}
			}

			if (string_equals_ignore_case(instruccion, "leer")) {

				int nroPagina=valor;
				leermProc(pID, nroPagina);
				int verificador;

				recibirYDeserializarInt(&verificador, socketMemoria);

				if (verificador != -1) {

					char* resultado = malloc(sizeof(char) * 25);
					recibirYDeserializarChar(&resultado, socketMemoria);
					log_info(archivoLog,"Instruccion ejecutada:leer %d Proceso:%d. Resultado:%s",nroPagina, pID, resultado);
					char* aux = string_from_format("mProc %d - Pagina %d leida: %s.\n", pID, nroPagina, resultado);
					string_append(&resultadosTot, aux);

					free(resultado);
					free(aux);

				} else {

					log_info(archivoLog,"Instruccion ejecutada: leer %d  Proceso: %d - Error de lectura",nroPagina, pID);
					operacion = FALLOPROCESO;  //CASO EN QUE LEE ALGO QUE NO ESTA ?
					break;

				}
			}

			if (string_equals_ignore_case(instruccion, "escribir")) {

				int nroPagina=valor;
				char* texto=malloc(tamanioMarco);
				char* textoCorregido = string_substring(leidoSplit[2],1,strlen(leidoSplit[2])-4);//Esto corrige el texto

				escribirmProc(pID, nroPagina, textoCorregido);

				int verificador;
			    recibirYDeserializarInt(&verificador,socketMemoria);

			    if (verificador!= -1){

			    	recibirYDeserializarChar(&texto,socketMemoria);
					log_info(archivoLog, "Instruccion ejecutada: escribir %d %s Proceso: %d Resultado: %s.\n", nroPagina,texto, pID, texto);
					char* aux= string_from_format("mProc %d - Pagina %d escrita:%s.\n",pID, nroPagina, texto);
					string_append(&resultadosTot, aux);

					free(aux);

			    } else {

					log_info(archivoLog, "Instruccion ejecutada: escribir %d %s Proceso: %d - Error de escritura", nroPagina, texto, pID);

					operacion = FALLOPROCESO;  //EN CASO DE QUE QUIERA ESCRIBIR ALGO QUE NO SE PUEDE
					break;

				}
			}

			if (string_equals_ignore_case(instruccion, "entrada-salida")) {

				tiempoIO= valor;

				log_info(archivoLog, "Instruccion ejecutada: entrada-salida %d Proceso: %d ", tiempoIO, pID);
				char* aux= string_from_format("mProc %d en entrada-salida de tiempo %d.\n", pID, tiempoIO);
				string_append(&resultadosTot, aux);

				free(aux);

				operacion = ENTRADASALIDA;
				entradaSalida=1;
			}

			if (string_equals_ignore_case(instruccion, "finalizar;")) {

				finalizarmProc(pID);

				int verificador;
				recibirYDeserializarInt(&verificador, socketMemoria);

				if (verificador != -1) {

					log_info(archivoLog,"Instruccion ejecutada:finalizar Proceso:%d finalizado", pID);
					char* aux = string_from_format("mProc %d finalizado.\n",pID);
					string_append(&resultadosTot, aux);
					free(aux);

				} else {

					log_info(archivoLog,"Instruccion ejecutada:finalizar Proceso:%d - Error al finalizar", pID);

				}

				operacion = FINALIZARPROCESO;  //Podria meter un break aca en vez del if en quantum ?
			}

			instruccionesEjecutadas++;

			sleep(retardo);

			if (quantum != -1) {
				quantumRafaga--;
				if (quantumRafaga == 0) {
					if (operacion != FINALIZARPROCESO)
						operacion = RAFAGAPROCESO;
					break;
				}
			}

			free(leidoSplit);

		} while(!feof(mCod)&&!entradaSalida);//fin del super while

		log_info(archivoLog, "Ejecucion de rafaga concluida. Proceso:%d", pID);

		fclose(mCod);

		switch(operacion) {

			case ENTRADASALIDA: {

				tamanioPaquete = strlen(resultadosTot) + 1 + sizeof(int)*4;
				paqueteRafaga = malloc(tamanioPaquete);
				serializarChar(serializarInt(serializarInt(serializarInt(paqueteRafaga, operacion),programCounter),tiempoIO), resultadosTot);

				break;
			}

			case RAFAGAPROCESO: {

			tamanioPaquete = strlen(resultadosTot) + 1 + sizeof(int)*3;
			paqueteRafaga = malloc(tamanioPaquete);
			serializarChar(serializarInt(serializarInt(paqueteRafaga, operacion), programCounter), resultadosTot);

			break;
			}

			case FINALIZARPROCESO: {

			tamanioPaquete = strlen(resultadosTot) + 1 + sizeof(int)*2;
			paqueteRafaga = malloc(tamanioPaquete);
			serializarChar(serializarInt(paqueteRafaga, operacion), resultadosTot);

			break;
			}

			case FALLOPROCESO: {

			tamanioPaquete = strlen(resultadosTot) + 1 + sizeof(int)*2;
			paqueteRafaga = malloc(tamanioPaquete);
			serializarChar(serializarInt(paqueteRafaga, operacion), resultadosTot);

			break;
			}

		}

		send(socketPlaniHilo, paqueteRafaga, tamanioPaquete, 0);
		free(resultadosTot);
		free(paqueteRafaga);

		}
	} 	//fin while(continuar)
	log_info(archivoLog, "Mi intempestiva muerte llego intempestivamente!!!");
}       //fin ejecutarmProc


void comandoCPU(int instruccionesEjecutadas){
	int socketMetricas;
	int comando;
	int porcentaje;
	pthread_mutex_init(&mutexMetricas, NULL);

	if (configurarSocketCliente(ipPlanificador, puertoPlanificador,	&socketMetricas))
		log_info(archivoLog, "Conectado al Planificador %i.\n", socketMetricas);
	else
		log_error(archivoLog, "Error al conectar con Planificador. %s\n", ipPlanificador);

    while(1){

    	recibirYDeserializarInt(&comando, socketMetricas);

	switch(comando){
	case PEDIDOMETRICA:{
		pthread_mutex_lock(&mutexMetricas);
		porcentaje= (instruccionesEjecutadas*100)/(60/retardo);
		pthread_mutex_unlock(&mutexMetricas);

		int tamanioPorcentaje=sizeof(int);
		char* paquetePorcentaje=malloc(tamanioPorcentaje);
		serializarInt(paquetePorcentaje, porcentaje);
		send(socketPlanificador, paquetePorcentaje, tamanioPorcentaje, 0);
        free(paquetePorcentaje);
	}
	}

    }

}


/*void timer_handler (int signum)
{
 static int count = 0;
 printf ("HOLA, SOY EL TEMPORIZADOR.\n", ++count);
} */

