/*
 ============================================================================
 Name        : admSwap.c
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
char* ipAdmMemoria;
char* puertoAdmMemoria;
int puertoEscucha;
char* nombreSwap;
int cantidadPaginas;
int listeningSocket;
int tamanioPagina;
unsigned retardoCompactacion; //son segundos sino lo cambio a int
int clienteMemoria;

//Funciones de configuración
void configurarAdmSwap(char* config);
int configurarSocketServidor();

int main(int argc, char** argv) {

	//Creo el archivo de logs
	archivoLog = log_create("log_SWAP", "SWAP", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	configurarAdmSwap(argv[1]);

	//Configuro el servidor
	configurarSocketServidor();

	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteMemoria = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso Memoria %i. \n", clienteMemoria);

	//Creo el archivo de Swap
	int tamanioArchivoSwap = cantidadPaginas * tamanioPagina;

	char* comando = string_from_format("sudo dd if=/dev/sda1 of=/home/utnso/Swap bs=%i count=%i", tamanioArchivoSwap);

	if(system(comando) == -1)
		log_info(archivoLog, "No se pudo crear el archivo de Swap.\n");
	else
		log_info(archivoLog, "Se creó el archivo de Swap.\n");

	//Abro e inicializo el archivo con "\0"
	FILE* archivoSwap;

	archivoSwap = fopen("archivoSwap", "r+");

	while(!feof(archivoSwap)){
		fputc('\0',archivoSwap);
	}

//CHECKPOINT 1
	char* mCod = malloc(15);
	recv(clienteMemoria, mCod, 15, 0);
	log_info(archivoLog, "Recibí %s", mCod);

	char* notificacion = "Recibido.";
	send(clienteMemoria, notificacion, strlen(notificacion), 0);
	log_info(archivoLog, "%s", notificacion);

// Recibe el mProc de Memoria
	char* mProc = malloc(15);
	recv(clienteMemoria, mProc, 15, 0);
	log_info(archivoLog, "Recibí %s", mProc);

	free(mProc);
	free(mCod);
	return 0;

}

void configurarAdmSwap(char* config) {
	t_config* configurarAdmSwap = config_create(config);
	if (config_has_property(configurarAdmSwap, "NOMBRE_SWAP"))
		nombreSwap = string_duplicate(config_get_string_value(configurarAdmSwap, "NOMBRE_SWAP"));
	if (config_has_property(configurarAdmSwap, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_int_value(configurarAdmSwap,	"PUERTO_ESCUCHA");
	if (config_has_property(configurarAdmSwap, "CANTIDAD_PAGINAS"))
		cantidadPaginas = config_get_int_value(configurarAdmSwap, "CANTIDAD_PAGINAS");
	if (config_has_property(configurarAdmSwap, "TAMANIO_PAGINA"))
		tamanioPagina = config_get_int_value(configurarAdmSwap,	"TAMANIO_PAGINA");
	if (config_has_property(configurarAdmSwap, "RETARDO_COMPACTACION"))
		retardoCompactacion = config_get_int_value(configurarAdmSwap, "RETARDO_COMPACTACION");

	config_destroy(configurarAdmSwap);
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

