/*
 ============================================================================
 Name        : admMemoria.c
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
#include <commons/collections/queue.h>
#include <commons/collections/dictionary.h>
#include <sockets.h>

#define BACKLOG 5

t_log* archivoLog;

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

typedef enum{
	FIFO=0,
	LRU=1,
	CLOCK=2,
	CLOCKMEJORADO=3
} algoritmo_t;

typedef struct {
	int processID;
	int nroMarco;
	int bitPresencia;
	int bitModificado;
	int tiempoLRU;
	int tiempoFIFO;
} process_t;

int puertoEscucha;
char* ipSwap;
int puertoSwap;
int maximoMarcosPorProceso;
int cantidadMarcos;
int listeningSocket;
int tamanioMarco;
int entradasTLB;
char* TLBHabilitada;
int retardoMemoria;
int socketSwap;
int clienteCPU;
void* memoriaPrincipal;
int* marcos;
algoritmo_t algoritmoDeReemplazo = FIFO;
t_dictionary *tablaDeProcesos;

typedef enum{iniciar, leer, escribir, entradaSalida, finalizar} t_instruccion;

void configurarAdmMemoria(char* config);
int configurarSocketCliente(char* ip, int puerto, int*);
int configurarSocketServidor();
void admDeMemoria();
void iniciarProceso(int pid, int cantPaginas);
int finalizarProceso(int pid);
void* leerMemoria(int pid, int pagina);
int escribirMemoria(int pid,int pagina, void* contenido);
int asignarNuevoMarco();
int cantidadMarcosAsignados(t_dictionary *tablaDePaginas);
void actualizarTiempoLRU(char* key, process_t* value);
void actualizarTiempoFIFO(char* key, process_t* value);
int escribirEnSwap(char *contenido,int pid, int pagina);
int paginaAReemplazarPorAlgoritmo(t_dictionary *tablaDePaginas);

int main(int argc, char** argv) {
	//Creo el archivo de logs
	archivoLog = log_create("log_AdmMemoria", "AdmMemoria", 1, 0);
	log_info(archivoLog, "Archivo de logs creado.\n");

	//TODO Leer archivo de configuracion y extraer variables
	configurarAdmMemoria(argv[1]);

	if (configurarSocketCliente(ipSwap, puertoSwap,	&socketSwap))
		log_info(archivoLog, "Conectado al Administrador de Swap %i.\n", socketSwap);
	else
		log_error(archivoLog, "Error al conectar en el Administrador de Swap. %s %i \n", ipSwap, puertoSwap);

	configurarSocketServidor();

	struct sockaddr_storage direccionCliente;
	unsigned int len = sizeof(direccionCliente);
	clienteCPU = accept(listeningSocket, (void*) &direccionCliente, &len);
	log_info(archivoLog, "Se conecta el proceso CPU %d\n", clienteCPU);

	//admDeMemoria();
	pthread_t hiloMemoria;
	pthread_create(&hiloMemoria, NULL, (void *)admDeMemoria, NULL);

	pthread_join(hiloMemoria, NULL);

	return 0;
}

void configurarAdmMemoria(char* config) {

	t_config* configurarAdmMemoria = config_create(config);
	if (config_has_property(configurarAdmMemoria, "PUERTO_ESCUCHA"))
		puertoEscucha = config_get_int_value(configurarAdmMemoria, "PUERTO_ESCUCHA");
	if (config_has_property(configurarAdmMemoria, "IP_SWAP"))
		ipSwap = string_duplicate(config_get_string_value(configurarAdmMemoria, "IP_SWAP"));
	if (config_has_property(configurarAdmMemoria, "PUERTO_SWAP"))
		puertoSwap = config_get_int_value(configurarAdmMemoria, "PUERTO_SWAP");
	if (config_has_property(configurarAdmMemoria, "MAXIMO_MARCOS_POR_PROCESO"))
		maximoMarcosPorProceso = config_get_int_value(configurarAdmMemoria, "MAXIMO_MARCOS_POR_PROCESO");
	if (config_has_property(configurarAdmMemoria, "CANTIDAD_MARCOS"))
		cantidadMarcos = config_get_int_value(configurarAdmMemoria,	"CANTIDAD_MARCOS");
	if (config_has_property(configurarAdmMemoria, "TAMANIO_MARCO"))
		tamanioMarco = config_get_int_value(configurarAdmMemoria, "TAMANIO_MARCO");
	if (config_has_property(configurarAdmMemoria, "ENTRADAS_TLB"))
		entradasTLB = config_get_int_value(configurarAdmMemoria, "ENTRADAS_TLB");
	if (config_has_property(configurarAdmMemoria, "TLB_HABILITADA"))
		TLBHabilitada = string_duplicate(config_get_string_value(configurarAdmMemoria, "TLB_HABILITADA"));
	if (config_has_property(configurarAdmMemoria, "RETARDO_MEMORIA"))
		retardoMemoria = config_get_int_value(configurarAdmMemoria, "RETARDO_MEMORIA");

	config_destroy(configurarAdmMemoria);
}

int configurarSocketCliente(char* ip, int puerto, int* s) {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr(ip);
	direccionServidor.sin_port = htons(puerto);

	*s = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(*s, (void*) &direccionServidor, sizeof(direccionServidor)) == -1) {
		log_error(archivoLog, "No se pudo conectar");
		return 0;
	}

	return 1;
}

int configurarSocketServidor() {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = INADDR_ANY;
	direccionServidor.sin_port = htons(puertoEscucha);

	listeningSocket = socket(AF_INET, SOCK_STREAM, 0);

	int activado = 1;
	setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	if (bind(listeningSocket, (void*) &direccionServidor, sizeof(direccionServidor)) != 0) {
		log_error(archivoLog, "Falló el bind");
		return 1;
	}

	listen(listeningSocket, BACKLOG);


	log_info(archivoLog, "Servidor creado. %i\n", listeningSocket);

	return 1;
}

void admDeMemoria(){
	int i;
	memoriaPrincipal = malloc (cantidadMarcos*tamanioMarco);
	tablaDeProcesos = dictionary_create();
	marcos = malloc(sizeof(int)*cantidadMarcos);
	for (i=0; i<cantidadMarcos;i++)//iniicializo todos los marcos a 0 (vacios)
		marcos[i]=0;
	while(1){
		int instruccion;
		recibirYDeserializarInt(&instruccion, clienteCPU);

		switch(instruccion){
		case INICIOMEMORIA:{

			int pid, cantPaginas, tamanioPaquete, verificador;
			char *paquete;

			recibirYDeserializarInt(&pid, clienteCPU);
			recibirYDeserializarInt(&cantPaginas, clienteCPU);
			log_info(archivoLog, "Recibi pid %i.\n", pid);
			log_info(archivoLog, "Recibi cantidad de paginas %i.\n", cantPaginas);

			iniciarProceso(pid, cantPaginas); //TODO hacer un if para comprobar que lo hizo correctamente

			//Le pido a Swap que inicialice un proceso:
			tamanioPaquete = sizeof(int) * 3;
			paquete = malloc(tamanioPaquete);
			serializarInt(serializarInt(serializarInt(paquete, INICIARPROCESO), pid), cantPaginas);
			log_info(archivoLog, "Antes del send de paquete.\n");
			send(socketSwap, paquete, tamanioPaquete, 0);
			log_info(archivoLog, "Despues del send de paquete.\n");
			free(paquete);//Se puede sacar este free?

			//Recibo respuesta de Swap:
			recibirYDeserializarInt(&verificador, socketSwap);

			if (verificador != -1)
				log_info(archivoLog, "Memoria inicializada");
			else
				log_info(archivoLog,"Fallo al inicializar memoria");

			paquete = malloc(sizeof(int));//realloc?
			//Le contesto a CPU
			serializarInt(paquete,verificador);
			send(clienteCPU, paquete, sizeof(int),0);

			free(paquete);
			log_info(archivoLog, "Termino inicializar");
			break;
		}
		case LEERMEMORIA:{

			int pid, pagina, tamanioPaquete, verificador;
			char *paquete, *respuesta;
			recibirYDeserializarInt(&pid, clienteCPU);
			recibirYDeserializarInt(&pagina, clienteCPU);

			tamanioPaquete = sizeof(int) * 3;
			paquete = malloc(tamanioPaquete);

			serializarInt(serializarInt(serializarInt(paquete, LEERMEMORIA), pid), pagina);

			send(socketSwap, paquete, tamanioPaquete, 0);

			free(paquete);//está de más este free?

			recibirYDeserializarInt(&verificador, socketSwap);

			if (verificador != -1){
				recibirYDeserializarChar(&respuesta,socketSwap);
				log_info(archivoLog, "Página %d leida: %s",pagina,respuesta);
				tamanioPaquete = sizeof(int)*2+strlen(respuesta)+1;
				paquete = malloc(tamanioPaquete);//realloc?
				serializarChar(serializarInt(paquete, verificador),respuesta);
				free(respuesta);
			}
			else{
				log_info(archivoLog,"Fallo al leer página %d.", pagina);
				tamanioPaquete = sizeof(int);
				paquete = malloc(sizeof(int));
				serializarInt(paquete,verificador);
			}
			//Le contesto a CPU
			send(clienteCPU,paquete,tamanioPaquete,0);

			free(paquete);

			break;
		}
		case ESCRIBIRMEMORIA:{
			int pid, pagina, tamanioPaquete, verificador;
			char *paquete, *respuesta, *contenido;
			recibirYDeserializarInt(&pid, clienteCPU);
			recibirYDeserializarInt(&pagina, clienteCPU);
			recibirYDeserializarChar(&contenido,clienteCPU);
			verificador=escribirMemoria(pid,pagina,contenido);
			if (verificador != -1){
				log_info(archivoLog, "Página %d escrita: %s",pagina,respuesta);
				tamanioPaquete = sizeof(int)*2+strlen(respuesta)+1;
				paquete = malloc(tamanioPaquete);//realloc?
				serializarChar(serializarInt(paquete, verificador),respuesta);
				free(respuesta);
			}else{
				log_info(archivoLog,"Fallo al escribir página %d.", pagina);
				tamanioPaquete = sizeof(int);
				paquete = malloc(sizeof(int));
				serializarInt(paquete,verificador);
			}
			//Le contesto a CPU
			send(clienteCPU,paquete,tamanioPaquete,0);

			free(paquete);
			break;
		}
		case FINALIZARPROCESO:{
			int pid, tamanioPaquete, verificador;
			char *paquete;

			recibirYDeserializarInt(&pid, clienteCPU);

			tamanioPaquete = sizeof(int) * 2;
			paquete = malloc(tamanioPaquete);

			//Le pido a Swap que finalice un proceso:
			serializarInt(serializarInt(paquete, FINALIZARPROCESO), pid);

			send(socketSwap, paquete, tamanioPaquete, 0);

			free(paquete);//Se puede sacar este free?

			//Recibo la respuesta de Swap:
			recibirYDeserializarInt(&verificador, socketSwap);

			if (verificador != -1)
				log_info(archivoLog, "Proceso finalizado de memoria.");
			else
				log_info(archivoLog,"Fallo al finalizar proceso.");

			paquete = malloc(sizeof(int));//realloc?
			//Le contesto a CPU
			serializarInt(paquete,verificador);
			send(clienteCPU, paquete, sizeof(int),0);

			free(paquete);

			break;
		}
		}

	}
}

void iniciarProceso(int pid, int cantPaginas){
	t_dictionary* nuevaTablaDePaginas = dictionary_create();
	int nroPagina;
	for (nroPagina=0;nroPagina < cantPaginas;nroPagina++){
		process_t* nuevoProceso = malloc(sizeof(process_t));
		nuevoProceso->processID = pid;
		nuevoProceso->nroMarco = -1;
		nuevoProceso->bitPresencia = 0;
		nuevoProceso->bitModificado = 0;
		nuevoProceso->tiempoLRU = 0;
		dictionary_put(nuevaTablaDePaginas,string_itoa(nroPagina),nuevoProceso);
	}
	dictionary_put(tablaDeProcesos,string_itoa(pid),nuevaTablaDePaginas);
}

int finalizarProceso(int pid){

}

void* leerMemoria(int pid, int pagina){
	void* contenido=malloc(tamanioMarco);
	int success;
	t_dictionary *tablaDePaginas = dictionary_remove(tablaDeProcesos,string_itoa(pid));
	process_t *process = dictionary_remove(tablaDePaginas, string_itoa(pagina));
	if (process->bitPresencia==0){//fallo de pagina
		//reemplaza un marco
		int paginaAReemplazar;//todo obtener nroPagina a reemplazar
		process_t *victima = dictionary_remove(tablaDePaginas,string_itoa(paginaAReemplazar));
		process->nroMarco=victima->nroMarco;
		victima->bitPresencia=0;
		victima->tiempoLRU=0;
		if(victima->bitModificado==1){
			//todo mandar a escribir a swap
			victima->bitModificado=0;
		}
		dictionary_put(tablaDePaginas,string_itoa(paginaAReemplazar),victima);
	}
	memcpy(contenido,memoriaPrincipal+process->nroMarco,tamanioMarco);//todo completar el faltante de contenido con /0
	process->bitModificado=0;
	process->bitPresencia=1;
	process->tiempoLRU=1;
	dictionary_iterator(tablaDePaginas,(void*)actualizarTiempoLRU);
	dictionary_put(tablaDePaginas,string_itoa(pagina),process);
	dictionary_put(tablaDeProcesos,string_itoa(pid),tablaDePaginas);
	return contenido;
}


int escribirMemoria(int pid, int pagina, void* contenido){
	int success=1;
	void *paginaAEscribir = calloc(tamanioMarco,4);
	t_dictionary *tablaDePaginas = dictionary_remove(tablaDeProcesos,string_itoa(pid));
	process_t *process = dictionary_remove(tablaDePaginas, string_itoa(pagina));
	if (process->bitPresencia==0){//fallo de pagina
		if(cantidadMarcosAsignados(tablaDePaginas)<maximoMarcosPorProceso){//asigna un nuevo marco
			process->nroMarco = asignarNuevoMarco();
		}else{//reemplaza un marco
			int paginaAReemplazar = paginaAReemplazarPorAlgoritmo(tablaDePaginas);
			process_t *victima = dictionary_remove(tablaDePaginas,string_itoa(paginaAReemplazar));
			process->nroMarco=victima->nroMarco;
			victima->bitPresencia=0;
			victima->tiempoLRU=0;
			victima->tiempoFIFO=0;
			if(victima->bitModificado==1){
				success=escribirEnSwap(contenido,pid,pagina);
				victima->bitModificado=0;
			}
			dictionary_put(tablaDePaginas,string_itoa(paginaAReemplazar),victima);
		}
	}
	memcpy(paginaAEscribir,contenido,tamanioMarco);
	memcpy(memoriaPrincipal+process->nroMarco,paginaAEscribir,tamanioMarco);
	process->bitModificado=1;
	process->bitPresencia=1;
	process->tiempoLRU=1;
	process->tiempoFIFO=1;
	dictionary_iterator(tablaDePaginas,(void*)actualizarTiempoLRU);
	dictionary_iterator(tablaDePaginas,(void*)actualizarTiempoFIFO);
	dictionary_put(tablaDePaginas,string_itoa(pagina),process);
	dictionary_put(tablaDeProcesos,string_itoa(pid),tablaDePaginas);
	return success;// debería devolver -1 en error
}

int asignarNuevoMarco(){//first fit
	int nuevoMarco=-1;
	int encontrado =0;
	int i=0;
	while(i<cantidadMarcos&&!encontrado){
		if (marcos[i]==0){
			marcos[i]=1;
			nuevoMarco = marcos[i];
			encontrado =1;
		}
		i++;
	}
	return nuevoMarco;
}

int cantidadMarcosAsignados(t_dictionary *tablaDePaginas){
	int cantidad=0;
	int i=0;
	while(i<dictionary_size(tablaDePaginas) && cantidad<maximoMarcosPorProceso){
		process_t *aux = dictionary_get(tablaDePaginas,string_itoa(i));
		if(aux->bitPresencia==1)
			cantidad++;
		i++;
	}
	return cantidad;
}

void actualizarTiempoLRU(char* key, process_t* value) {
   if(value->bitPresencia==1)
	value->tiempoLRU++;
}

void actualizarTiempoFIFO(char* key, process_t* value) {
   if(value->bitPresencia==1)
	value->tiempoFIFO++;
}

int escribirEnSwap(char *contenido,int pid, int pagina){
	int tamanioPaquete, verificador;
	char *paquete, *respuesta;
	tamanioPaquete = sizeof(int) * 4+strlen(contenido)+1;
	paquete = malloc(tamanioPaquete);

	serializarChar(serializarInt(serializarInt(serializarInt(paquete, ESCRIBIRMEMORIA), pid), pagina),contenido);

	send(socketSwap, paquete, tamanioPaquete, 0);

	free(paquete);//está de más este free?

	recibirYDeserializarInt(&verificador, socketSwap);
	return verificador;

}

int paginaAReemplazarPorAlgoritmo(t_dictionary *tablaDePaginas){
	int i=0;
	int paginaAReemplazar=-1;
	int tiempoMaximo=-1;
	switch(algoritmoDeReemplazo){
	case FIFO:
		while(i<dictionary_size(tablaDePaginas)){
			process_t aux= dictionary_get(tablaDePaginas,string_itoa(i));
			if(aux->bitPresencia==1)
				if(aux.tiempoFIFO>tiempoMaximo){
					tiempoMaximo=aux.tiempoFIFO;
					paginaAReemplazar=i;
				}
			i++;
		}
		break;
	case LRU:
		break;
	case CLOCK:
		break;
	case CLOCKMEJORADO:
		break;
	}
	return paginaAReemplazar;
}
