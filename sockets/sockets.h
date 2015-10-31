/*
 * sockets.h
 *
 *  Created on: 29/9/2015
 *      Author: utnso
 */

#ifndef SOCKETS_H_
#define SOCKETS_H_

/*
typedef enum {
	INICIARPROCESO,
	ENTRADASALIDA,
	INICIOMEMORIA,
	LEERMEMORIA,
	ESCRIBIRMEMORIA,
	FINALIZARPROCESO
} operacion_t;
*/

char* serializarChar(char* paqueteSerializado,char* texto);


char* serializarInt(char* paqueteSerializado,int entero);


int recibirYDeserializarChar(char **package, int socketCliente);


int recibirYDeserializarInt(int *package, int socketCliente);


#endif /* SOCKETS_H_ */
