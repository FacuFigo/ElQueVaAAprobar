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

	system("rm log_Debug_Swap");

	//Creo los archivos de logs
	archivoLog = log_create("log_SWAP", "SWAP", 0, LOG_LEVEL_INFO);
	logDebug = log_create("log_Debug_Swap", "SWAP", 1, LOG_LEVEL_DEBUG);

	pthread_mutex_init(&accesoAMemoria, NULL);

	configurarAdmSwap(argv[1]);

	//Configuro el servidor
	configurarSocketServidor();

	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteMemoria = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(logDebug, "Se conecta el proceso Memoria %i. \n", clienteMemoria);

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
		//log_debug(logDebug, "N° pagina: %i, Disponibilidad: %i, Proceso: %i", pagina->numeroPagina, pagina->disponibilidad, pagina->proceso);
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

	log_info(logDebug, "Comienza el Administrador de Espacios.");

	while(1){

		process_t* proceso = malloc(sizeof(process_t));
		recibirYDeserializarInt(&operacion, clienteMemoria);
		log_debug(logDebug, "Recibi Operacion: %i.", operacion);

		pthread_mutex_lock(&accesoAMemoria);

		switch(operacion){
			case INICIARPROCESO:{

				//Recibir proceso de Memoria - PID, CANTIDAD DE PAGINAS A OCUPAR -
				recibirYDeserializarInt(&proceso->processID, clienteMemoria);
				recibirYDeserializarInt(&proceso->cantidadDePaginas, clienteMemoria);

				log_debug(logDebug, "Recibi pid %i.", proceso->processID);
				log_debug(logDebug, "Recibi cantidad de paginas %i.", proceso->cantidadDePaginas);

				paginaInicio = buscarEspacioDisponible(proceso->cantidadDePaginas);

				log_debug(logDebug, "Pagina de inicio %i.", paginaInicio);

				tamanioPaquete = sizeof(int);
				char* paquete = malloc(tamanioPaquete);

				if(paginaInicio == -1){
					//No hay espacio disponible
					log_debug(logDebug, "No hay paginas disponibles para el proceso %i.", proceso->processID);

					if(fragmentacionExt >= proceso->cantidadDePaginas){
						pthread_t compactador;
						pthread_create(&compactador, NULL, (void *) compactador, NULL);

						pthread_mutex_unlock(&accesoAMemoria);

						log_debug(logDebug, "Inicia el hilo compactador.");

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
					log_debug(logDebug, "Se le asignaron las paginas al proceso %i.", proceso->processID);

					serializarInt(paquete, 1);
					send(clienteMemoria, paquete, tamanioPaquete, 0);

					free(paquete);
				}

				break;
			}

			case FINALIZARPROCESO:{

				recibirYDeserializarInt(&proceso->processID, clienteMemoria);

				char* paquete = malloc(sizeof(int));

				log_debug(logDebug, "Antes de entrar a liberar, pid: %i", proceso->processID);
				if(liberarMemoria(proceso->processID) == -1){
					log_debug(logDebug, "No se pudo finalizar el proceso %i.", proceso->processID);

					serializarInt(paquete, -1);
					send(clienteMemoria, paquete, sizeof(int), 0);

					free(paquete);
				}else{
					log_debug(logDebug, "Finalizo de forma correcta el proceso %i.", proceso->processID);
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

				log_debug(logDebug, "Comienza la lectura de la pagina %i del proceso %i.", *paginaALeer, *pid);

				char* contenidoPagina = malloc(tamanioPagina);
				int resultado = leerPagina(*pid, *paginaALeer, contenidoPagina);

				log_debug(logDebug,"Contenido leido: %s y el strlen es: %i",contenidoPagina,strlen(contenidoPagina));

				tamanioPaquete = strlen(contenidoPagina)+1 + sizeof(int) * 2;
				char* paquete = malloc(tamanioPaquete);

				if(resultado == 1){
					log_debug(logDebug, "Termino de leer pagina %i del proceso %i.",*paginaALeer, *pid);
					//Enviar contenidoPagina a Memoria
					serializarChar(serializarInt(paquete, resultado), contenidoPagina);

					send(clienteMemoria, paquete, tamanioPaquete, 0);

					free(paginaALeer);
					free(contenidoPagina);
					free(paquete);
				}else{
					//No se pudo leer la pagina
					log_debug(logDebug, "No se pudo leer la pagina %i del proceso %i.", *paginaALeer, *pid);

					serializarInt(paquete, resultado);
					send(clienteMemoria, paquete, tamanioPaquete, 0);

					free(paginaALeer);
					free(contenidoPagina);
					free(paquete);
				}

				break;
			}

			case ESCRIBIRMEMORIA:{

				int* pid = malloc(sizeof(int));
				recibirYDeserializarInt(pid, clienteMemoria);

				int* paginaAEscribir = malloc(sizeof(int));
				recibirYDeserializarInt(paginaAEscribir, clienteMemoria);

				char* contenido;
				recibirYDeserializarChar(&contenido, clienteMemoria);

				log_debug(logDebug, "Comienza la escritura de la pagina %i del proceso %i.", *paginaAEscribir, *pid);

				int resultado = escribirPagina(*pid, *paginaAEscribir, contenido);

				if(resultado == -1)
					log_debug(logDebug,"No se pudo escribir la pagina %i del proceso %i.", *paginaAEscribir, *pid);
				else
					log_debug(logDebug, "Se logro escribir la pagina %i del proceso &i.", *paginaAEscribir, *pid);

				//Enviar confirmacion y contenido a Memoria
				tamanioPaquete = sizeof(int);
				char* paquete = malloc(tamanioPaquete);

				serializarInt(paquete, resultado);

				send(clienteMemoria, paquete, tamanioPaquete, 0);

				free(paquete);
				free(contenido);
				free(paginaAEscribir);

				break;
			}
		}
		pthread_mutex_unlock(&accesoAMemoria);
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
		log_debug(logDebug, "Pagina asignada: %i, proceso: %i.", pagina->numeroPagina, pagina->proceso);
		paginaALeer++;
		pagina = list_get(listaGestionEspacios, paginaALeer);

	}

}

//Libera la parte de memoria ocupada por el proceso
int liberarMemoria(int pid){
	int numero = 0;

	log_debug(logDebug, "Entro a liberarMemoria.");
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

			if(!fseek(archivoSwap, posicion, SEEK_SET))
				log_debug(logDebug, "Funciono el seek, posicion: %i.", posicion);

			fgets(contenidoLeido, tamanioPagina, archivoSwap);

			log_debug(logDebug, "Pagina: %i, Proceso: %i, Posicion: %i, Contenido leido: %s.", pagina->numeroPagina, pagina->proceso, posicion, contenidoLeido);

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
	int paginaProceso = 0;

	log_debug(logDebug, "Pagina a escribir: %i", paginaAEscribir);
	pagina = list_get(listaGestionEspacios, numero);
	while(pagina->proceso != pid){
		numero++;
		pagina = list_get(listaGestionEspacios, numero);
	}

	log_debug(logDebug, "Salgo del primer while, proceso: %i - pid: %i.", pagina->proceso, pid);

	while(pagina->proceso == pid){

		log_debug(logDebug, "Pagina: %i, pagina proceso: %i", pagina->numeroPagina, paginaProceso);

		if(paginaProceso == paginaAEscribir){
			log_debug(logDebug, "Se encontro la pagina a escribir: %i, contenido a escribir: %s.", pagina->numeroPagina, contenido);

			int posicion = pagina->numeroPagina * tamanioPagina;

			fseek(archivoSwap, posicion, SEEK_SET);

			//int diferencia = tamanioPagina - strlen(contenido);
			//char* vacios = calloc(diferencia, sizeof(char));

			fputs(contenido, archivoSwap);
			log_debug(logDebug, "Se realizo la escritura con el fputs.");
			//fputs(vacios, archivoSwap);

			log_debug(logDebug, "Se pudo escribir la pagina: %i.", pagina->numeroPagina);
			//free(vacios);

			return 1;

		}else{
			numero++;
			paginaProceso++;
			pagina = list_get(listaGestionEspacios, numero);
			log_debug(logDebug, "Pagina siguiente: %i, Proceso: %i.", pagina->numeroPagina, pagina->proceso);
		}

	}

	return -1;
}

void compactador(){

	pthread_mutex_lock(&accesoAMemoria);

	log_debug(logDebug,"CO-CO-CO-COMPACTATION!!");
	log_debug(logDebug, "Tamaño de fragmentacón: %i", fragmentacionExt);

	pagina_t* pagina;
	int numeroPagina = 0;
	int comienzoEspacioVacio = 0; //Pagina en la que inicia
	int tamanioEspacioVacio = 0;  //Tamaño del espacio en paginas
	int paginaAAsignar = 0; //Pagina a la que se va a asignar el proceso

	//TODO Sacar los espacios vacios
	//Despues de encontrar el primer espacio vacio, ubicar el siguiente proceso que se encuentra luego de este espacio

	pagina = list_get(listaGestionEspacios, numeroPagina);
	while(pagina->disponibilidad == 0){
		pagina = list_get(listaGestionEspacios, numeroPagina);
		numeroPagina++;
	}
	//Asigno la pagina inicial
	comienzoEspacioVacio = pagina->numeroPagina;

	//Leo la proxima pagina
	numeroPagina++;
	pagina = list_get(listaGestionEspacios, numeroPagina);
	//Recorro la lista para saber el tamaño del espacio vacio
	while(pagina->disponibilidad == 1){
		pagina = list_get(listaGestionEspacios, numeroPagina);
		numeroPagina++;
		tamanioEspacioVacio++;
	}

	//TODO Recorrer de nuevo la lista hasta que se encuentre otro blanco, pagina que se encuentre, pagina que se mueve al primer lugar blanco que se paso

	//TODO Hacer los movimientos dentro del archivo

	pthread_mutex_unlock(&accesoAMemoria);
}
