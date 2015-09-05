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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <commons/config.h>
#include <commons/log.h>
#include <commons/process.h>
#include <commons/string.h>
#include <commons/collections/list.h>

int configurarSocketCliente(int s, char* ip, char* puerto);

int main(int argc, char** argv) {

	return 0;

}

int configurarSocketCliente(int s, char* ip, char* puerto){
	int status;
	struct addrinfo hints, *serverInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;     	//Setea el tipo de IP
	hints.ai_socktype = SOCK_STREAM; 	// TCP stream sockets

	if((status = getaddrinfo(ip, puerto, &hints, &serverInfo)) == -1){
		return 0;
	}

	s = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);
	if((connect(s, serverInfo->ai_addr, serverInfo->ai_addrlen)) == -1){
		return 0;
	}

	freeaddrinfo(serverInfo);
	return 1;
}
