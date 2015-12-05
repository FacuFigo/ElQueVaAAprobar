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
	PEDIDOMETRICA = 8,
	PATHINVALIDO = 9
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
int* instruccionesEjecutadas;
int planiVive = 1;
int retardoTotal;

pthread_mutex_t mutexMetricas;
pthread_mutex_t mutex;

int configurarSocketCliente(char* ip, int puerto, int*);
void configurarCPU(char* config);
void iniciarmProc(int pID, int cantPaginas);
void leermProc(int pID, int nroPagina);
void finalizarmProc(int pID);
void escribirmProc(int pID, int nroPagina, char* texto);
void ejecutarmProc();
void comandoCPU();
void timer_handler(int signum);

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_CPU", "CPU", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	configurarCPU(argv[1]);

	log_info(archivoLog, "cantidad de hilos: %d", cantidadHilos);

	instruccionesEjecutadas = malloc(sizeof(int) * cantidadHilos);

	int f;
	for (f = 0; f < cantidadHilos; f++) {   //inicializa en 0
		instruccionesEjecutadas[f] = 0;
	}

	pthread_mutex_init(&mutex, NULL);
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

	recibirYDeserializarInt(&quantum, socketPlanificador);
	log_info(archivoLog, "Recibi quantum %d", quantum);

	char* paquetecpu = malloc(sizeof(int));
	serializarInt(paquetecpu, cantidadHilos);
	send(socketPlanificador, paquetecpu, sizeof(int), 0);

	recibirYDeserializarInt(&tamanioMarco, socketMemoria);
	log_info(archivoLog, "Recibi tamanio pagina %i", tamanioMarco);

	pthread_t hilos;

	for (threadCounter = 0; threadCounter < cantidadHilos; threadCounter++) {
		pthread_create(&hilos, NULL, (void *) ejecutarmProc, NULL);
		log_info(archivoLog, "Instancia de CPU %i creada.\n", threadCounter);
	}

	struct sigaction sa;               //arranca el temporizador
	struct itimerval timer;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &timer_handler;
	sigaction(SIGALRM, &sa, NULL);

	timer.it_value.tv_sec = 0;
	timer.it_value.tv_sec = 60;

	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_sec = 60;

	setitimer(ITIMER_REAL, &timer, NULL);  //termina el temporizador

	for (threadCounter = 0; threadCounter < cantidadHilos; threadCounter++) {
		pthread_join(hilos, NULL);
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
	if (connect(*s, (void*) &direccionServidor, sizeof(direccionServidor))
			== -1) {
		log_error(archivoLog, "No se pudo conectar");
		return 0;
	}

	return 1;
}

void iniciarmProc(int pID, int cantPaginas) {

	int tamPaquete = sizeof(int) * 3;
	char* paquete = malloc(tamPaquete);
	log_info(archivoLog, "Cantidad de paginas: %i.\n", cantPaginas);
	serializarInt(serializarInt(serializarInt(paquete, INICIOMEMORIA), pID),
			cantPaginas);
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
	serializarInt(serializarInt(serializarInt(paquete, LEERMEMORIA), pID),
			nroPagina);
	send(socketMemoria, paquete, tamPaquete, 0);
	free(paquete);

}

void escribirmProc(int pID, int nroPagina, char* texto) {

	int tamPaquete = strlen(texto) + 1 + sizeof(int) * 4;
	char* paquete = malloc(tamPaquete);
	serializarChar(
			serializarInt(
					serializarInt(serializarInt(paquete, ESCRIBIRMEMORIA), pID),
					nroPagina), texto);
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
	int tamanioComando = tamanioMarco + 15;
	int continuar = 1;

	pthread_t hiloMetricas;

	char comandoLeido[tamanioComando];


	pthread_mutex_lock(&mutex);
	if (configurarSocketCliente(ipPlanificador, puertoPlanificador,
			&socketPlaniHilo))
		log_info(archivoLog, "Conectado al Planificador %i.\n",
				socketPlaniHilo);
	else
		log_error(archivoLog, "Error al conectar con Planificador. %s\n",
				ipPlanificador);

	int numeroCPU;
	recibirYDeserializarInt(&numeroCPU, socketPlaniHilo);

	pthread_mutex_init(&mutexMetricas, NULL);
	pthread_create(&hiloMetricas, NULL, (void *) comandoCPU, NULL);

	while (continuar) {

		quantumRafaga = quantum;
		entradaSalida = 0;

		char* resultadosTot = string_new();
		log_info(archivoLog, "Quedo esperando, cpu: %i",
				process_get_thread_id());

		continuar = recibirYDeserializarInt(&operacion, socketPlaniHilo);

		if (continuar) {

			log_info(archivoLog, "Recibi operacion %i.\n", operacion);

			recibirYDeserializarInt(&pID, socketPlaniHilo);
			log_info(archivoLog, "Recibi pid %i.\n", pID);

			recibirYDeserializarInt(&programCounter, socketPlaniHilo);
			log_info(archivoLog, "Recibi program counter %i.\n",
					programCounter);

			recibirYDeserializarChar(&path, socketPlaniHilo);
			log_info(archivoLog, "Recibi path %s.\n", path);

			char* ruta = string_from_format(
					"/home/utnso/tp-2015-2c-elquevaaaprobar/scripts/%s", path);

			mCod = fopen(ruta, "r");

			free(ruta);
			free(path);

			if (mCod) {

				switch (operacion) {

				case FINALIZARPROCESO: {

					do {
						fgets(comandoLeido, tamanioComando, mCod);
						char** leidoSplit = string_n_split(comandoLeido, 3,
								" ");
						instruccion = leidoSplit[0];
					} while (!string_equals_ignore_case(instruccion,
							"finalizar;"));

					finalizarmProc(pID);

					int verificador;
					recibirYDeserializarInt(&verificador, socketMemoria);

					if (verificador != -1) {

						log_info(archivoLog,
								"Instruccion ejecutada:finalizar Proceso:%d finalizado",
								pID);
						char* aux = string_from_format("mProc %d finalizado.\n",
								pID);
						string_append(&resultadosTot, aux);
						free(aux);

					} else {

						log_info(archivoLog,
								"Instruccion ejecutada:finalizar Proceso:%d - Error al finalizar",
								pID);

					}

					break;

				}

				case INICIARPROCESO: {

					int i;
					for (i = 0; programCounter > i; i++) {
						fgets(comandoLeido, tamanioComando, mCod); //este primer fgets sirve para pararte en el programCounter cuando vuelve de quantum o e/s
					}

					do {

						fgets(comandoLeido, tamanioComando, mCod);
						log_info(archivoLog, "PRIMER comando leido: %s",
								comandoLeido);

						char** leidoSplit = string_n_split(comandoLeido, 3,
								" ");
						instruccion = leidoSplit[0];

						if (strcmp(instruccion, "finalizar;")) { //se fija si la instruccion es finalizar, si lo es no asigna leidoSplit[1] en valor
							valor = strtol(leidoSplit[1], NULL, 10);
						}

						programCounter++;

						if (string_equals_ignore_case(instruccion, "iniciar")) {

							int cantPaginas = valor;
							iniciarmProc(pID, cantPaginas);
							int verificador;

							recibirYDeserializarInt(&verificador,
									socketMemoria);

							if (verificador != -1) {

								log_info(archivoLog,
										"Instruccion ejecutada:iniciar %d Proceso:%d iniciado.",
										cantPaginas, pID);
								char* aux = string_from_format(
										"mProc %d - Iniciado.\n", pID);
								string_append(&resultadosTot, aux);
								free(aux);

							} else {

								log_info(archivoLog,
										"Instruccion ejecutada:iniciar %d Proceso:%d. FALLO!",
										cantPaginas, pID);
								char* aux = string_from_format(
										"mProc %d - Fallo.\n", pID);
								string_append(&resultadosTot, aux);
								free(aux);

								operacion = FALLOPROCESO; //PARA EL CASO EN QUE SE PIDAN MAS PAGINAS QUE LAS DISPONIBLES
								break;
							}
						}

						if (string_equals_ignore_case(instruccion, "leer")) {

							int nroPagina = valor;
							leermProc(pID, nroPagina);
							int verificador;

							recibirYDeserializarInt(&verificador,
									socketMemoria);

							if (verificador != -1) {

								char* resultado;
								recibirYDeserializarChar(&resultado,
										socketMemoria);
								log_info(archivoLog,
										"Instruccion ejecutada:leer %d Proceso:%d. Resultado:%s",
										nroPagina, pID, resultado);
								char* aux = string_from_format(
										"mProc %d - Pagina %d leida: %s.\n",
										pID, nroPagina, resultado);
								string_append(&resultadosTot, aux);

								free(resultado);
								free(aux);

							} else {

								log_info(archivoLog,
										"Instruccion ejecutada: leer %d  Proceso: %d - Error de lectura",
										nroPagina, pID);
								char* aux =
										string_from_format(
												"mProc %d: fallo lectura de pagina %d.\n",
												pID, nroPagina);
								string_append(&resultadosTot, aux);
								operacion = FALLOPROCESO; //CASO EN QUE LEE ALGO QUE NO ESTA ?
								break;

							}
						}

						if (string_equals_ignore_case(instruccion,
								"escribir")) {

							int nroPagina = valor;
							char* textoCorregido = string_substring(
									leidoSplit[2], 1,
									strlen(leidoSplit[2]) - 4); //Esto corrige el texto

							escribirmProc(pID, nroPagina, textoCorregido);

							free(textoCorregido);

							int verificador;
							recibirYDeserializarInt(&verificador,
									socketMemoria);

							if (verificador != -1) {

								char* texto;
								recibirYDeserializarChar(&texto, socketMemoria);
								log_info(archivoLog,
										"Instruccion ejecutada: escribir %d %s Proceso: %d Resultado: %s.\n",
										nroPagina, texto, pID, texto);
								char* aux = string_from_format(
										"mProc %d - Pagina %d escrita:%s.\n",
										pID, nroPagina, texto);
								string_append(&resultadosTot, aux);

								free(texto);

								free(aux);

							} else {

								log_info(archivoLog,
										"Instruccion ejecutada: escribir %d %s Proceso: %d - Error de escritura",
										nroPagina, leidoSplit[2], pID);//TODO CAMBIAR ESTO

								operacion = FALLOPROCESO; //EN CASO DE QUE QUIERA ESCRIBIR ALGO QUE NO SE PUEDE
								break;

							}
						}

						if (string_equals_ignore_case(instruccion,
								"entrada-salida")) {

							tiempoIO = valor;

							log_info(archivoLog,
									"Instruccion ejecutada: entrada-salida %d Proceso: %d ",
									tiempoIO, pID);
							char* aux =
									string_from_format(
											"mProc %d en entrada-salida de tiempo %d.\n",
											pID, tiempoIO);
							string_append(&resultadosTot, aux);

							free(aux);

							operacion = ENTRADASALIDA;
							entradaSalida = 1;
						}

						if (string_equals_ignore_case(instruccion,
								"finalizar;")) {

							finalizarmProc(pID);

							int verificador;
							recibirYDeserializarInt(&verificador,
									socketMemoria);

							if (verificador != -1) {

								log_info(archivoLog,
										"Instruccion ejecutada:finalizar Proceso:%d finalizado",
										pID);
								char* aux = string_from_format(
										"mProc %d finalizado.\n", pID);
								string_append(&resultadosTot, aux);
								free(aux);

							} else {

								log_info(archivoLog,
										"Instruccion ejecutada:finalizar Proceso:%d - Error al finalizar",
										pID);

							}

							operacion = FINALIZARPROCESO; //Podria meter un break aca en vez del if en quantum ?
						}

						instruccionesEjecutadas[numeroCPU]++;
						log_info(archivoLog,
								" las instrucciones ejecutadas son: %i",
								instruccionesEjecutadas[numeroCPU]);

						usleep(retardo * 1000000);

						int j=0;
						while(leidoSplit[j]!=NULL){
							free(leidoSplit[j]);
							j++;
						}
						free(leidoSplit);
						if (quantum != -1) {
							quantumRafaga--;
							if (quantumRafaga == 0) {
								if (operacion != FINALIZARPROCESO)
									operacion = RAFAGAPROCESO;
								break;
							}
						}


					} while (!feof(mCod) && !entradaSalida); //fin del super while

					break; //rompe el case INICIAPROCESO

				}
				}

				log_info(archivoLog,
						"Ejecucion de rafaga concluida. Proceso:%d", pID);

				fclose(mCod);

				switch (operacion) {

				case ENTRADASALIDA: {

					tamanioPaquete = strlen(resultadosTot) + 1
							+ sizeof(int) * 4;
					paqueteRafaga = malloc(tamanioPaquete);
					serializarChar(
							serializarInt(
									serializarInt(
											serializarInt(paqueteRafaga,
													operacion), programCounter),
									tiempoIO), resultadosTot);

					break;
				}

				case RAFAGAPROCESO: {

					tamanioPaquete = strlen(resultadosTot) + 1
							+ sizeof(int) * 3;
					paqueteRafaga = malloc(tamanioPaquete);
					serializarChar(
							serializarInt(
									serializarInt(paqueteRafaga, operacion),
									programCounter), resultadosTot);

					break;
				}

				case FINALIZARPROCESO: {

					tamanioPaquete = strlen(resultadosTot) + 1
							+ sizeof(int) * 2;
					paqueteRafaga = malloc(tamanioPaquete);
					serializarChar(serializarInt(paqueteRafaga, operacion),
							resultadosTot);

					break;
				}

				case FALLOPROCESO: {

					tamanioPaquete = strlen(resultadosTot) + 1
							+ sizeof(int) * 2;
					paqueteRafaga = malloc(tamanioPaquete);
					serializarChar(serializarInt(paqueteRafaga, operacion),
							resultadosTot);

					break;
				}

				}

				send(socketPlaniHilo, paqueteRafaga, tamanioPaquete, 0);
				free(resultadosTot);
				free(paqueteRafaga);
			} else { //fin del if(mCod)

				log_info(archivoLog, "PATH INVALIDO AMEO");   //TODO PRUEBA DE COSAS NUEVAS

				operacion = PATHINVALIDO;

				tamanioPaquete = sizeof(int);
				paqueteRafaga = malloc(tamanioPaquete);
				serializarInt(paqueteRafaga, operacion);
				send(socketPlaniHilo, paqueteRafaga, tamanioPaquete, 0);

			}
		} 	//fin if(continuar)
		log_info(archivoLog, "FIN DEL IF CONTINUAR");
	}   //fin while(continuar)
	log_info(archivoLog, "Mi intempestiva muerte llego intempestivamente!!!");
}       //fin ejecutarmProc

void comandoCPU() {
	int socketMetricas;
	int comando;
	int porcentaje;
	int maxInstXMin;
	int continuarMetricas = 1;

	if (configurarSocketCliente(ipPlanificador, puertoPlanificador,
			&socketMetricas))
		log_info(archivoLog, "Conectado al Planificador %i.\n", socketMetricas);
	else
		log_error(archivoLog, "Error al conectar con Planificador. %s\n",
				ipPlanificador);

	int numeroCPU;
	recibirYDeserializarInt(&numeroCPU, socketMetricas);

	pthread_mutex_unlock(&mutex);

	while (continuarMetricas) {

		continuarMetricas = recibirYDeserializarInt(&comando, socketMetricas);

		if (continuarMetricas) {
			log_info(archivoLog, "recibí operación: %i", comando);
			switch (comando) {
			case PEDIDOMETRICA: {
				pthread_mutex_lock(&mutexMetricas);
				//la regla de 3 seria:  maximoPosibleDeInstEn60Seg ----> %100
				//						instruccionesEjecutadas -------> %X  por lo tanto la cuenta del porcentaje seria (instEjec*100)/maxPos60Seg
				maxInstXMin = (60 / retardo);
				porcentaje = (instruccionesEjecutadas[numeroCPU] * 100)
						/ maxInstXMin;
				log_info(archivoLog, "EL PORCENTAJE ES: %i", porcentaje);
				pthread_mutex_unlock(&mutexMetricas);

				log_info(archivoLog, "SALI DEL MUTEX");

				int tamanioPorcentaje = sizeof(int);
				char* paquetePorcentaje = malloc(tamanioPorcentaje);
				serializarInt(paquetePorcentaje, porcentaje);
				send(socketMetricas, paquetePorcentaje, tamanioPorcentaje, 0);
				free(paquetePorcentaje);
			}
			}

		} else {
			log_info(archivoLog, "Muere hilo metricas");
		}
	}
}

void timer_handler(int signum) {
	int i;
//pensar en mutex
	for (i = 0; i < cantidadHilos; i++) {
		instruccionesEjecutadas[i] = 0;
	}

}

