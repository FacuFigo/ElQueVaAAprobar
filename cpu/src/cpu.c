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
pthread_t hilos[cantidadHilos];

typedef struct _t_Package {
	char message[100];
	uint32_t message_long;
} t_Package;



typedef enum{iniciar, leer, escribir, entradaSalida, finalizar} t_instruccion;


void configurarCPU(char* config);
int configurarSocketCliente(char* ip, int puerto, int*);
int recieve_and_deserialize(t_Package *package, int socketCliente);
void ejecutarmProc(char* ruta);
char* iniciarmProc(char* comando);
char* leermProc(char* comando);
char* escribirmProc(char* comando);
char* entradaSalidamProc(char* comando);
char* finalizarmProc(char* comando);
void simulacionCPUs(char* path);

int main(int argc, char** argv) {


	archivoLog = log_create("log_CPU", "CPU", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	configurarCPU(argv[1]);


	if (configurarSocketCliente(ipPlanificador, puertoPlanificador,	&socketPlanificador))
		log_info(archivoLog, "Conectado al Planificador %i.\n", socketPlanificador);
	else
		log_error(archivoLog, "Error al conectar con Planificador. %s\n", ipPlanificador);

	if (configurarSocketCliente(ipMemoria, puertoMemoria,	&socketMemoria))
		log_info(archivoLog, "Conectado a la Memoria %i.\n", socketMemoria);
	else
		log_error(archivoLog, "Error al conectar con Memoria. %s\n", ipMemoria);


	char* mCod = malloc(15);

	recv(socketPlanificador, mCod, 15, 0);  //recibiria path y puntero a prox instrucc  junto
	log_info(archivoLog, "Recibi %s", mCod);



	char* path= malloc (30);

	simulacionCPUs(path);

	pthread_t hiloAdmin;
	pthread_create(&hiloAdmin, NULL, (void *) asignarmCodHilo, path);


	send(socketMemoria, mCod, 15, 0);

	char* notificacion = malloc(11);
	recv(socketMemoria, notificacion, 11, 0);
	log_info(archivoLog, "%s", notificacion);

	send(socketPlanificador, notificacion, 11, 0);

	free(mCod);
	free(notificacion);






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
		ipPlanificador = string_duplicate(config_get_string_value(configCPU, "IP_PLANIFICADOR"));
	if (config_has_property(configCPU, "PUERTO_PLANIFICADOR"))
		puertoPlanificador = config_get_int_value(configCPU, "PUERTO_PLANIFICADOR");
	if (config_has_property(configCPU, "IP_MEMORIA"))
		ipMemoria = string_duplicate(config_get_string_value(configCPU, "IP_MEMORIA"));
	if (config_has_property(configCPU, "PUERTO_MEMORIA"))
		puertoMemoria = config_get_int_value(configCPU, "PUERTO_MEMORIA");
	if (config_has_property(configCPU, "CANTIDAD_HILOS"))
		cantidadHilos = config_get_int_value(configCPU, "CANTIDAD_HILOS");
	if (config_has_property(configCPU, "RETARDO"))
		retardo = config_get_int_value(configCPU, "RETARDO");

	config_destroy(configCPU);
}


void simulacionCPUs(char* path){
	int i;
	for(i=0; i<=cantidadHilos; i++){
			pthread_create(&hilos[i], NULL, (void *) ejecutarmProc, path);
			unsigned int id=process_get_thread_id();
			log_info(archivoLog,"Instancia CPU %d creada",id);
		}
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





void ejecutarmProc(char* path){
	FILE* mCod;
	char* comando= malloc(20);
	char* instruccion= malloc(10);
    char** comandoSplit= malloc(sizeof (char*)*3);

	mCod= fopen(path, "r");
	while (fgets(comando, 20, mCod) != NULL) {

		comandoSplit= string_split(comando, " ");
        instruccion= comandoSplit[0];

		   switch(instruccion){
		   case iniciar:
			   iniciarmProc(comando);
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
			   break;
		}

		   sleep(retardo);
	}

  free(comandoSplit);
  free(instruccion);
  free(comando);
  fclose(mCod);


}



//funcion para asignar mCod al hilo que me diga el planificador
 asignarmCodHilo(){

 }


