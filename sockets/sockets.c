/*
 * sockets.c
 *
 *  Created on: 19/10/2015
 *      Author: utnso
 */

#include "sockets.h"
#include <stdint.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>

char* serializarChar(char* paqueteSerializado,char* texto) {
	int tamTexto = strlen(texto)+1;
	int offset = 0;
	int size_to_send;
	size_to_send = sizeof(tamTexto);
	memcpy(paqueteSerializado + offset, &(tamTexto), size_to_send);
	offset += size_to_send;
	size_to_send = tamTexto;
	memcpy(paqueteSerializado + offset, texto, size_to_send);
	offset += size_to_send;
	return paqueteSerializado + offset;
}

char* serializarInt(char* paqueteSerializado,int entero) {
	int offset = 0;
	memcpy(paqueteSerializado, &(entero), sizeof(entero));
	offset = sizeof(entero);
	return paqueteSerializado + offset;
}

int recibirYDeserializarChar(char **package, int socketCliente) {
	int status;
	int buffer_size;
	char *buffer = malloc(buffer_size = sizeof(uint32_t));
	uint32_t message_long;
	status = recv(socketCliente, buffer, buffer_size, 0);
	memcpy(&(message_long), buffer, buffer_size);
	if (!status)
		return 0;
	*package = malloc(message_long);
	status = recv(socketCliente, *package, message_long, 0);
	if (!status)
		return 0;
	free(buffer);
	return status;
}

int recibirYDeserializarInt(int *package, int socketCliente) {
	int status;
	int buffer_size;
	char *buffer = malloc(buffer_size = sizeof(uint32_t));
	status = recv(socketCliente, buffer, buffer_size, 0);
	memcpy(package, buffer, buffer_size);
	if (!status)
		return 0;
	free(buffer);
	return status;
}
