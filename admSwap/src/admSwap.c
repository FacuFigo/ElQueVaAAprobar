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
	int paginaInicio;
	int cantidadDePaginas;
	int cantPagEscritas;
	int cantPagsLeidas;
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
int retardoSwap;
int clienteMemoria;

int fragmentacionExt;

t_list* listaGestionEspacios;
t_list* listaProcesos;

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
void leerPaginaCompactador(int pagina, char* contenidoLeido);
void escribirPaginaCompactador(char* contenidoAMover, int paginaAMover);
void vaciarPagina(int paginaAVaciar);
void sacarProceso(process_t* proceso);
process_t* buscarProceso(int pid);

int main(int argc, char** argv) {

	//Creo los archivos de logs
	archivoLog = log_create("log_Obligatorio", "SWAP", 0, LOG_LEVEL_INFO);
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

	listaProcesos = list_create();

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
	if (config_has_property(configurarAdmSwap, "RETARDO_SWAP"))
		retardoSwap= config_get_int_value(configurarAdmSwap, "RETARDO_SWAP");

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

	log_debug(logDebug, "Servidor creado %i.", listeningSocket);

	return 1;
}

void admDeEspacios(){

	int paginaInicio = 0;
	int operacion, tamanioPaquete;
	int conexion;

	log_info(logDebug, "Comienza el Administrador de Espacios.");

	while(1){

		process_t* proceso = malloc(sizeof(process_t));
		conexion = recibirYDeserializarInt(&operacion, clienteMemoria);

		if(!conexion){
			log_debug(logDebug, "Finaliza el administrador de espacios inesperadamente.");
			break;
		}

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

					if(fragmentacionExt >= proceso->cantidadDePaginas){
						log_debug(logDebug, "Adentro del if");
						pthread_t hiloCompactador;
						pthread_create(&hiloCompactador, NULL, (void *) compactador, NULL);

						pthread_join(hiloCompactador, NULL);

						paginaInicio = buscarEspacioDisponible(proceso->cantidadDePaginas);

						//Se guarda en la lista de gestion de procesos el proceso que acaba de entrar a memoria
						asignarEspacio(paginaInicio, proceso);

						log_debug(logDebug, "Se le asignaron las paginas al proceso %i.", proceso->processID);

						int* byteInicial = malloc(sizeof(int));
						*byteInicial = paginaInicio * tamanioPagina;

						int* tamanioProceso = malloc(sizeof(int));
						*tamanioProceso = proceso->cantidadDePaginas * tamanioPagina;

						log_info(archivoLog, "Se le asigno el espacio al mProc %i, byte inicial: %i, tamaño proceso: %i", proceso->processID, *byteInicial, *tamanioProceso);

						list_add(listaProcesos, proceso);

						free(byteInicial);
						free(tamanioProceso);

						serializarInt(paquete, 1);
						send(clienteMemoria, paquete, tamanioPaquete, 0);

						free(paquete);

					}else{

						//No hay espacio disponible
						log_debug(logDebug, "No hay paginas disponibles para el proceso %i.", proceso->processID);
						log_info(archivoLog, "Se rechaza al mProc %i por falta de espacio.", proceso->processID);

						serializarInt(paquete, -1);
						send(clienteMemoria, paquete, tamanioPaquete, 0);

						free(paquete);
					}
				} else{

					//Se guarda en la lista de gestion de procesos el proceso que acaba de entrar a memoria
					asignarEspacio(paginaInicio, proceso);

					log_debug(logDebug, "Se le asignaron las paginas al proceso %i.", proceso->processID);

					int* byteInicial = malloc(sizeof(int));
					*byteInicial = paginaInicio * tamanioPagina;

					int* tamanioProceso = malloc(sizeof(int));
					*tamanioProceso = proceso->cantidadDePaginas * tamanioPagina;

					log_info(archivoLog, "Se le asigno el espacio al mProc %i, byte inicial: %i, tamaño proceso: %i", proceso->processID, *byteInicial, *tamanioProceso);

					//Añadir el proceso a una lista
					proceso->cantPagEscritas = 0;
					proceso->cantPagsLeidas = 0;
					list_add(listaProcesos, proceso);

					free(byteInicial);
					free(tamanioProceso);

					serializarInt(paquete, 1);
					send(clienteMemoria, paquete, tamanioPaquete, 0);

					free(paquete);
				}

				break;
			}

			case FINALIZARPROCESO:{

				int* pid = malloc(sizeof(int));
				recibirYDeserializarInt(pid, clienteMemoria);

				proceso = buscarProceso(*pid);

				free(pid);

				char* paquete = malloc(sizeof(int));

				log_debug(logDebug, "Antes de entrar a liberar, pid: %i", proceso->processID);
				if(liberarMemoria(proceso->processID) == -1){
					log_debug(logDebug, "No se pudo finalizar el proceso %i.", proceso->processID);

					serializarInt(paquete, -1);
					send(clienteMemoria, paquete, sizeof(int), 0);

					free(paquete);
				}else{

					log_info(archivoLog, "Finaliza proceso: %i , cantidad de paginas escritas: %i, cantidad de paginas leidas: %i", proceso->processID, proceso->cantPagEscritas, proceso->cantPagsLeidas);
					log_debug(logDebug, "Finaliza proceso: %i , cantidad de paginas escritas: %i, cantidad de paginas leidas: %i", proceso->processID, proceso->cantPagEscritas, proceso->cantPagsLeidas);
					//Saco el proceso de la lista
					sacarProceso(proceso);

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

				proceso = buscarProceso(*pid);

				free(pid);

				log_debug(logDebug, "Comienza la lectura de la pagina %i del proceso %i.", *paginaALeer, proceso->processID);

				char* contenidoPagina = malloc(tamanioPagina);
				int resultado = leerPagina(proceso->processID, *paginaALeer, contenidoPagina);

				log_debug(logDebug,"Contenido leido: %s y el strlen es: %i",contenidoPagina,strlen(contenidoPagina));

				tamanioPaquete = strlen(contenidoPagina)+1 + sizeof(int) * 2;
				char* paquete = malloc(tamanioPaquete);

				sleep(retardoSwap);

				if(resultado == 1){
					log_debug(logDebug, "Termino de leer pagina %i del proceso %i.",*paginaALeer, proceso->processID);
					proceso->cantPagsLeidas++;
					//Enviar contenidoPagina a Memoria
					serializarChar(serializarInt(paquete, resultado), contenidoPagina);

					send(clienteMemoria, paquete, tamanioPaquete, 0);

					free(paginaALeer);
					free(contenidoPagina);
					free(paquete);
				}else{
					//No se pudo leer la pagina
					log_debug(logDebug, "No se pudo leer la pagina %i del proceso %i.", *paginaALeer, proceso->processID);

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

				proceso = buscarProceso(*pid);

				free(pid);

				log_debug(logDebug, "Comienza la escritura de la pagina %i del proceso %i.", *paginaAEscribir, proceso->processID);

				int resultado = escribirPagina(proceso->processID, *paginaAEscribir, contenido);

				sleep(retardoSwap);
				if(resultado == -1){
					log_debug(logDebug,"No se pudo escribir la pagina %i del proceso %i.", *paginaAEscribir, proceso->processID);
				}else{
					log_debug(logDebug, "Se logro escribir la pagina %i del proceso %i.", *paginaAEscribir, proceso->processID);
					proceso->cantPagEscritas++;
					log_debug(logDebug,"Cantidad de veces que escribe: %i.", proceso->cantPagEscritas);
				}
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

	while(numeroPagina < cantidadPaginas){

		if(pagina->disponibilidad == 1){
			paginasEncontradas++;
			fragmentacionExt++;
		}else{
			paginasEncontradas = 0;
			paginaInicio = numeroPagina + 1;
		}

		if(paginasEncontradas == paginasNecesarias){
			encontrado++;
			break;
		}else{
			numeroPagina++;
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
	int paginaInicial = 0;
	int cantidadPagProceso = 0;

	log_debug(logDebug, "Entro a liberarMemoria.");
	pagina_t* pagina = malloc(sizeof(pagina_t));
	pagina = list_get(listaGestionEspacios, numero);

	while(pagina->proceso != pid){
		numero++;
		//Si alcanza la cantidad de paginas, no lo encontro
		if(numero == cantidadPaginas)
			return -1;
		pagina = list_get(listaGestionEspacios, numero);
	}

	paginaInicial = pagina->numeroPagina;

	while(pagina->proceso == pid){

		liberarEspacioEnArchivo(pagina->numeroPagina);

		pagina->disponibilidad = 1;
		pagina->proceso = -1;
		numero++;
		cantidadPagProceso++;
		if(numero == cantidadPaginas)
			break;
		else
			pagina = list_get(listaGestionEspacios, numero);
	}

	int* byteInicial = malloc(sizeof(int));
	*byteInicial = paginaInicial * tamanioPagina;
	int* totalBytes = malloc(sizeof(int));
	*totalBytes = cantidadPagProceso * tamanioPagina;

	log_info(archivoLog, "Se libera el espacio del mProc %i, byte inicial: %i, tamaño total liberado: %i.", pid, *byteInicial, *totalBytes);

	free(byteInicial);
	free(totalBytes);
	return 0;
}

void liberarEspacioEnArchivo(int numeroDePagina){
	int numeroByte;
	int posicion = numeroDePagina * tamanioPagina;

	log_debug(logDebug, "Entro a liberar espacio en archivo.");

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

			log_info(archivoLog, "Escritura solicitada del proceso: %i, byte inicial: %i, tamaño: %i, contenido leido: %s.", pid, posicion, strlen(contenidoLeido), contenidoLeido);

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
			fputs(contenido, archivoSwap);

			log_debug(logDebug, "Se pudo escribir la pagina: %i.", pagina->numeroPagina);

			log_info(archivoLog, "Escritura solicitada del proceso %i, byte inicial %i, tamaño %i, contenido a escribir %s.", pid, posicion, strlen(contenido), contenido);

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

	//Indice de paginas
	int numeroPagina = 0;
	//Indice de espacios en blanco
	int numeroPaginaBlanco = 0;
	int* paginaBlanco = malloc(sizeof(int));

	t_list* espaciosVacios;
	pagina_t* pagina;
	pagina_t* paginaNueva;

	espaciosVacios = list_create();

	log_debug(logDebug,"CO-CO-CO-COMPACTATION!!");
	log_debug(logDebug, "Tamaño de fragmentacón: %i", fragmentacionExt);

	log_info(archivoLog, "Inicia compactación.");

	pagina = list_get(listaGestionEspacios, numeroPagina);

	log_debug(logDebug, "Inicia en pagina: %i.", pagina->numeroPagina);

	while(numeroPagina < cantidadPaginas){

		while(pagina->disponibilidad == 0){
			numeroPagina++;
			if(numeroPagina == cantidadPaginas)
				break;
			pagina = list_get(listaGestionEspacios, numeroPagina);
		}

		list_add(espaciosVacios, &pagina->numeroPagina);
		log_debug(logDebug, "Se agrega la pagina %i al espacio vacio.", pagina->numeroPagina);
		//Leo la proxima pagina
		numeroPagina++;
		pagina = list_get(listaGestionEspacios, numeroPagina);

		//Agrego los espacios vacios a la lista
		while(pagina->disponibilidad == 1){
			list_add(espaciosVacios, &pagina->numeroPagina);
			log_debug(logDebug, "Se agrega la pagina %i al espacio vacio.", pagina->numeroPagina);
			numeroPagina++;
			if(numeroPagina == cantidadPaginas)
				break;
			pagina = list_get(listaGestionEspacios, numeroPagina);
			log_debug(logDebug, "Pagina %i, proceso: %i, disponibilidad %i", pagina->numeroPagina, pagina->proceso, pagina->disponibilidad);
		}

		//Lista de espacios creada con solo los numeros de pagina
		//Se acabo un espacio en blanco
		//Mientras no encuentre un espacio en blanco, sigue moviendo las paginas
		while(pagina->disponibilidad == 0){
			paginaBlanco = list_get(espaciosVacios, numeroPaginaBlanco);
			log_debug(logDebug, "Pagina en blanco %i", *paginaBlanco);

			char* contenidoAMover = malloc(sizeof(tamanioPagina));

			//Leo la pagina del proceso
			leerPaginaCompactador(pagina->numeroPagina, contenidoAMover);

			//Escribo la pagina en la nueva posicion
			escribirPaginaCompactador(contenidoAMover, *paginaBlanco);

			//Vaciar pagina en la que estaba
			vaciarPagina(pagina->numeroPagina);

			free(contenidoAMover);

			//Muevo la pagina escrita en la lista
			paginaNueva = list_get(listaGestionEspacios, *paginaBlanco);
			paginaNueva->disponibilidad = 0;
			paginaNueva->proceso = pagina->proceso;

			log_debug(logDebug, "Proceso: %i, Pagina vieja: %i, Pagina nueva: %i", pagina->proceso, pagina->numeroPagina, paginaNueva->numeroPagina);

			pagina->disponibilidad = 1;
			pagina->proceso = -1;

			//Al ser ahora un espacio en blanco, se addea a la lista de espacios en blanco
			list_add(espaciosVacios, &pagina->numeroPagina);

			log_debug(logDebug, "Se añande la pagina %i a espacios vacios.", pagina->numeroPagina);

			numeroPagina++;
			numeroPaginaBlanco++;
			if (numeroPagina < cantidadPaginas)
				//Vuelvo a leer para ver si hay más espacios en blanco
				pagina = list_get(listaGestionEspacios, numeroPagina);
		}
	}

	list_destroy(espaciosVacios);

	log_info(archivoLog, "Finaliza compactación.");
}

void leerPaginaCompactador(int pagina, char* contenidoLeido){
	int posicion = pagina * tamanioPagina;

	fseek(archivoSwap, posicion, SEEK_SET);
	fgets(contenidoLeido, tamanioPagina, archivoSwap);

}

void escribirPaginaCompactador(char* contenidoAEscribir, int paginaAMover){
	int posicion = paginaAMover * tamanioPagina;

	fseek(archivoSwap, posicion, SEEK_SET);
	fputs(contenidoAEscribir, archivoSwap);

}

void vaciarPagina(int paginaAVaciar){
	int posicion = paginaAVaciar * tamanioPagina;
	char* vacios = string_repeat('\0', tamanioPagina);

	fseek(archivoSwap, posicion, SEEK_SET);
	fputs(vacios, archivoSwap);

}

process_t* buscarProceso(int pid){
	process_t* procesoAux;
	int indice = 0;

	procesoAux = list_get(listaProcesos, indice);
	while(procesoAux->processID != pid){
		indice++;
		procesoAux = list_get(listaProcesos, indice);
	}

	return procesoAux;
}

void sacarProceso(process_t* proceso){
	process_t* procesoAux = malloc(sizeof(process_t));

	procesoAux = buscarProceso(proceso->processID);

	log_debug(logDebug, "Proceso aux: %i , proceso: %i", procesoAux->processID, proceso->processID);

	free(procesoAux);
}
