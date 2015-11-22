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
#include <sockets.h>

#define BACKLOG 5

//Operaciones
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

#pragma pack(1)

//Recepcion de info de Memoria
typedef struct {
	int processID;
	int cantidadDePaginas;
} process_t;

//Struct que va dentro de la lista
typedef struct {
	int numeroPagina;
	int disponibilidad;
	int proceso;
} pagina_t;

t_log* archivoLog;
t_log* logDebug;

char* ipAdmMemoria;
char* puertoAdmMemoria;
int puertoEscucha;
char* nombreSwap;
int cantidadPaginas;
int listeningSocket;
int tamanioPagina;
int retardoCompactacion;
int clienteMemoria;

int fragmentacionExt;

t_list* listaGestionEspacios;

FILE* archivoSwap;

pthread_mutex_t accesoAMemoria;

//Funciones de configuración
void configurarAdmSwap(char* config);
int configurarSocketServidor();

//Funciones de gestion de espacios de memoria
void admDeEspacios();
int buscarEspacioDisponible(int espacioNecesario);
void asignarEspacio(int paginaInicio, process_t* proceso);
int liberarMemoria(int pid);
void liberarEspacioEnArchivo(int numeroDePagina);
int leerPagina(int pid, int numeroPagina, char* contenidoLeido);
int escribirPagina(int pid, int paginaAEscribir, char* contenido);

void compactador();

int main(int argc, char** argv) {

	//Creo los archivos de logs
	archivoLog = log_create("log_SWAP", "SWAP", 1, LOG_LEVEL_INFO);
	log_info(archivoLog, "Archivo de logs creado.\n");

	logDebug = log_create("log_Debug_Swap", "SWAP", 0, LOG_LEVEL_DEBUG);

	pthread_mutex_init(&accesoAMemoria, NULL);

	configurarAdmSwap(argv[1]);

	//Configuro el servidor
	configurarSocketServidor();

	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteMemoria = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso Memoria %i. \n", clienteMemoria);

	//Creo el archivo de Swap
	int tamanioArchivoSwap = cantidadPaginas * tamanioPagina;

	char* dd = string_from_format("echo utnso | sudo -S dd if=/dev/zero of=%s bs=%i count=1", nombreSwap, tamanioArchivoSwap);

	if(system(dd) == -1){
		log_error(logDebug, "No se pudo crear el archivo de Swap.\n");
		return -1;
	}

	log_debug(logDebug, "Se creó el archivo de Swap.\n");

	char* chmod = string_from_format("sudo chmod 666 %s", nombreSwap);
	if(system(chmod) == -1){
		log_error(logDebug, "Fallo al darle permisos al archivo.");
		return -1;
	}

	archivoSwap = fopen(nombreSwap, "r+");

	if(archivoSwap)
		log_info(archivoLog, "Abrio el archivo. %d", archivoSwap);
	else{
		log_debug(logDebug, "No se pudo abrir el archivo.");
		return -1;
	}

	//Creo la lista para la gestion de los espacios vacios
	listaGestionEspacios = list_create();
	log_debug(logDebug, "Se crea la lista para gestion de espacios.");

	int i;
	for(i = 0; i < cantidadPaginas; i++){
		pagina_t* pagina = malloc(sizeof(pagina_t));
		pagina->numeroPagina = i;
		pagina->disponibilidad = 1;
		pagina->proceso = -1;
		list_add(listaGestionEspacios, pagina);
		//Para saber si se está creando de forma correcta
		log_debug(logDebug, "N° pagina: %i, Disponibilidad: %i, Proceso: %i", pagina->numeroPagina, pagina->disponibilidad, pagina->proceso);
		free(pagina);
	}

	//Thread que gestiona los espacios en la lista de gestion
	pthread_t admDeEspacio;
	pthread_create(&admDeEspacio, NULL, (void *) admDeEspacios, NULL);

	pthread_join(admDeEspacio, NULL);

	list_destroy(listaGestionEspacios);
	fclose(archivoSwap);

	return 0;
}

void configurarAdmSwap(char* config) {
	t_config* configurarAdmSwap = config_create(config);

	if (config_has_property(configurarAdmSwap, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_int_value(configurarAdmSwap,	"PUERTO_ESCUCHA");
	if (config_has_property(configurarAdmSwap, "NOMBRE_SWAP"))
		nombreSwap = string_duplicate(config_get_string_value(configurarAdmSwap, "NOMBRE_SWAP"));
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

void admDeEspacios(){

	int paginaInicio = 0;
	int operacion, tamanioPaquete;

	log_info(archivoLog, "Comienza el hilo Administrador de Espacios.\n");

	while(1){

		process_t* proceso = malloc(sizeof(process_t));
		recibirYDeserializarInt(&operacion, clienteMemoria);
		log_info(archivoLog, "Recibi Operacion");

		pthread_mutex_lock(&accesoAMemoria);

		switch(operacion){
			case INICIARPROCESO:{

				//Recibir proceso de Memoria - PID, CANTIDAD DE PAGINAS A OCUPAR -
				recibirYDeserializarInt(&proceso->processID, clienteMemoria);
				recibirYDeserializarInt(&proceso->cantidadDePaginas, clienteMemoria);

				log_info(archivoLog, "Recibi pid %i.\n", proceso->processID);
				log_info(archivoLog, "Recibi cantidad de paginas %i.\n", proceso->cantidadDePaginas);

				paginaInicio = buscarEspacioDisponible(proceso->cantidadDePaginas);

				log_info(archivoLog, "Pagina de inicio %i.\n", paginaInicio);

				tamanioPaquete = sizeof(int);
				char* paquete = malloc(tamanioPaquete);

				if(paginaInicio == -1){
					//No hay espacio disponible
					log_info(archivoLog, "No hay paginas disponibles para el proceso %i.\n", proceso->processID);

					if(fragmentacionExt >= proceso->cantidadDePaginas){
						pthread_t compactador;
						pthread_create(&compactador, NULL, (void *) compactador, NULL);

						pthread_mutex_unlock(&accesoAMemoria);

						log_info(archivoLog, "Inicia el hilo compactador.");

						serializarInt(paquete, 1);
						send(clienteMemoria, paquete, tamanioPaquete, 0);

						free(paquete);
					}else{

						serializarInt(paquete, -1);
						send(clienteMemoria, paquete, tamanioPaquete, 0);

						free(paquete);
					}
				} else{
					//Se guarda en la lista de gestion de procesos el proceso que acaba de entrar a memoria
					asignarEspacio(paginaInicio, proceso);
					log_info(archivoLog, "Se le asignaron las paginas al proceso %i.\n", proceso->processID);

					serializarInt(paquete, 1);
					send(clienteMemoria, paquete, tamanioPaquete, 0);

					free(paquete);
				}

				break;
			}

			case FINALIZARPROCESO:{

				recibirYDeserializarInt(&proceso->processID, clienteMemoria);

				char* paquete = malloc(sizeof(int));

				if(liberarMemoria(proceso->processID) == -1){
					log_error(archivoLog, "No se pudo finalizar el proceso %i.\n", proceso->processID);

					serializarInt(paquete, -1);
					send(clienteMemoria, paquete, sizeof(int), 0);

					free(paquete);
				}else{
					//Enviar confirmacion
					serializarInt(paquete, 1);
					send(clienteMemoria, paquete, sizeof(int), 0);

					free(paquete);
				}
				break;
			}

			case LEERMEMORIA:{

				int* pid = malloc(sizeof(int));
				recibirYDeserializarInt(pid, clienteMemoria);

				int* paginaALeer = malloc(sizeof(int));
				recibirYDeserializarInt(paginaALeer, clienteMemoria);

				char* contenidoPagina = malloc(tamanioPagina);
				int resultado = leerPagina(*pid, *paginaALeer, contenidoPagina);

				log_info(archivoLog, "Termino de leer pagina %d",*paginaALeer);
				//Enviar contenidoPagina a Memoria
				tamanioPaquete = tamanioPagina + sizeof(int) * 2;
				char* paquete = malloc(tamanioPaquete);

				serializarChar(serializarInt(serializarInt(paquete, operacion), resultado), contenidoPagina);

				send(clienteMemoria, paquete, tamanioPaquete, 0);

				free(paginaALeer);
				free(contenidoPagina);
				free(paquete);

				break;
			}

			case ESCRIBIRMEMORIA:{

				int* pid = malloc(sizeof(int));
				recibirYDeserializarInt(pid, clienteMemoria);

				int* paginaAEscribir = malloc(sizeof(int));
				recibirYDeserializarInt(paginaAEscribir, clienteMemoria);

				char* contenido;
				recibirYDeserializarChar(&contenido, clienteMemoria);

				int resultado = escribirPagina(*pid, *paginaAEscribir, contenido);

				//Enviar confirmacion y contenido a Memoria
				tamanioPaquete = sizeof(int) * 2;
				char* paquete = malloc(tamanioPaquete);

				serializarInt(serializarInt(paquete, operacion), resultado);

				send(clienteMemoria, paquete, tamanioPaquete, 0);

				free(paquete);
				free(contenido);
				free(paginaAEscribir);

				break;
			}
		}
		free(proceso);
	}
}

//Si encontro el espacio, devuelve el byte en donde comienza el espacio, sino devuelve -1
int buscarEspacioDisponible(int paginasNecesarias){

	fragmentacionExt = 0;
	int paginaInicio = 0;
	int paginasEncontradas = 0;
	int numeroPagina = 0;
	int encontrado = 0;
	pagina_t* pagina;

	pagina = list_get(listaGestionEspacios, numeroPagina);
	paginaInicio = pagina->numeroPagina;

	while(numeroPagina <= cantidadPaginas){

		if(pagina->disponibilidad == 1){
			paginasEncontradas++;
		}else{
			paginasEncontradas = 0;
			paginaInicio = numeroPagina + 1;
		}

		if(paginasEncontradas == paginasNecesarias){
			encontrado++;
			break;
		}else{
			numeroPagina++;
			fragmentacionExt++;
			pagina = list_get(listaGestionEspacios, numeroPagina);
		}
	}

	if(!encontrado)
		return -1;

	return paginaInicio;
}

void asignarEspacio(int paginaInicio, process_t* proceso){

	int i;
	int paginaALeer;
	pagina_t* pagina;
	pagina = list_get(listaGestionEspacios, paginaInicio);
	paginaALeer = pagina->numeroPagina;

	for(i = 0; i < proceso->cantidadDePaginas; i++){
		//Asigno No Disponible y el ID del Proceso que la ocupa
		pagina->disponibilidad = 0;
		pagina->proceso = proceso->processID;

		paginaALeer++;
		pagina = list_get(listaGestionEspacios, paginaInicio);

	}

}

//Libera la parte de memoria ocupada por el proceso
int liberarMemoria(int pid){
	int numero = 0;

	pagina_t* pagina = malloc(sizeof(pagina_t));
	pagina = list_get(listaGestionEspacios, numero);

	while(pagina->proceso != pid){
		numero++;
		pagina = list_get(listaGestionEspacios, numero);
	}

	while(pagina->proceso == pid){

		liberarEspacioEnArchivo(pagina->numeroPagina);

		pagina->disponibilidad = 1;
		pagina->proceso = -1;
		numero++;
		pagina = list_get(listaGestionEspacios, numero);
	}

	return 0;
}

void liberarEspacioEnArchivo(int numeroDePagina){
	int numeroByte;
	int posicion = numeroDePagina * tamanioPagina;

	fseek(archivoSwap, posicion,SEEK_SET);

	for(numeroByte = 0; numeroByte <= tamanioPagina; numeroByte++){
		fputc('\0', archivoSwap);
	}

}

int leerPagina(int pid, int numeroPagina, char* contenidoLeido){

	pagina_t* pagina;
	int numero = 0;
	int paginaProceso = 0;

	pagina = list_get(listaGestionEspacios, numero);
	while(pagina->proceso != pid){
		numero++;
		pagina = list_get(listaGestionEspacios, numero);
	}

	while(pagina->proceso == pid){

		if(paginaProceso == numeroPagina){

			int posicion = pagina->numeroPagina * tamanioPagina;

			fseek(archivoSwap, posicion, SEEK_SET);

			fgets(contenidoLeido, tamanioPagina, archivoSwap);

			//string_trim(&contenido);

			return 1;
		}else{
			paginaProceso++;
			numero++;
			pagina = list_get(listaGestionEspacios, numero);
		}

	}

	return -1;
}

//Si se pudo escribir devuelve 1, 0 si el fputs no fue correcto y -1 si no se encontró la página
int escribirPagina(int pid, int paginaAEscribir, char* contenido){
	int numero = 0;
	pagina_t* pagina;
	int paginaProceso;

	pagina = list_get(listaGestionEspacios, numero);
	while(pagina->proceso != pid){
		numero++;
		pagina = list_get(listaGestionEspacios, numero);
	}

	while(pagina->proceso == pid){

		if(paginaProceso == paginaAEscribir){

			int posicion = pagina->numeroPagina * tamanioPagina;

			fseek(archivoSwap, posicion, SEEK_SET);

			int diferencia = tamanioPagina - strlen(contenido);
			char* vacios = calloc(diferencia, sizeof(char));

			fputs(contenido, archivoSwap);
			fputs(vacios, archivoSwap);

			free(vacios);

			return 1;

		}else{
			paginaProceso++;
			numero++;
			pagina = list_get(listaGestionEspacios, numero);
		}

	}

	return -1;
}

void compactador(){

	pthread_mutex_lock(&accesoAMemoria);

	pthread_mutex_unlock(&accesoAMemoria);
}
