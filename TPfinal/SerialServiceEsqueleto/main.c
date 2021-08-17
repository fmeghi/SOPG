#include <stdio.h>
#include <stdlib.h>
#include "SerialManager.h"
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>

/*===================================Constant definitions=================================*/

#define UART_PORT 1
#define UART_SPEED 115200
#define SOCKET_PORT 10000
#define BUFFER_SIZE 128
#define UART_THREAD_SLEEP 4000


/*==================================Private global variables==============================*/


_Bool socketFlag;
//volatile sig_atomic_t sigFlag = 0;
pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER;
int s;
int newfd;
int n;
pthread_t serialThread;


/*=====================================Private functions==================================*/



void sigint_handler(int sig)
{
	//cerrar todo correctamente
	//sigFlag = 1;
	serial_close();
	pthread_cancel(serialThread);
	close(newfd);
	close(s);
	exit(1);

}

void sigint_init(void)
{
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM,&sa,NULL);
}

int socket_TCP_init(void)
{
	struct sockaddr_in serveraddr;

	// Creamos socket
	int s = socket(AF_INET,SOCK_STREAM, 0);

	// Cargamos datos de IP:PORT del server
	bzero((char*) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(SOCKET_PORT);
	if(inet_pton(AF_INET, "127.0.0.1", &(serveraddr.sin_addr))<=0)
	{
		fprintf(stderr,"ERROR invalid server IP\r\n");
		return 0;
	}

	// Abrimos puerto con bind()
	if (bind(s, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) == -1) 
	{
		close(s);
		perror("listener: bind");
		return 0;
	}

	// Seteamos socket en modo Listening
	if (listen (s, 10) == -1) // backlog=10
	{
		perror("error en listen");
		exit(1);
	}
	return s;
}

void* serial_thread (void* arg)
{
	int bytesReceive;
	char buffer[BUFFER_SIZE];

	while(1)
	{
		bytesReceive = serial_receive(buffer, sizeof(buffer));
		if (bytesReceive != 0)
		{
			pthread_mutex_lock (&mutexData);
			if (socketFlag == 1)
			{
				// Enviamos mensaje a cliente
    			if (write (newfd, buffer, strlen(buffer)) == -1)
    			{
      				perror("Error escribiendo mensaje en socket");
      				exit (1);
        		}
			pthread_mutex_unlock (&mutexData);
			}
		}		
		
	    usleep(UART_THREAD_SLEEP);
	}
}


void bloquearSign(void)
{
    sigset_t set;
    int s;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    //sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void desbloquearSign(void)
{
    sigset_t set;
    int s;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    //sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

/*=======================================Main Program=====================================*/

int main(void)
{
	
	socklen_t addr_len;
	struct sockaddr_in clientaddr;
	struct sockaddr_in serveraddr;
	char buffer[BUFFER_SIZE];
	int ret;



	/*Se inicializa el puerto serie*/
	serial_open(UART_PORT, UART_SPEED);
	/*Se inicializan las interrupciones*/
	sigint_init();
	/*Se inicializa el socket TCP*/
	s = socket_TCP_init();
	
	printf("Inicio Serial Service\r\n");


	while(1)
	{
		char ipClient[32];

		// Ejecutamos accept() para recibir conexiones entrantes
		addr_len = sizeof(struct sockaddr_in);
    	if ( (newfd = accept(s, (struct sockaddr *)&clientaddr, &addr_len)) == -1)
      	{
			perror("error en accept");
			break;
	    }

		bloquearSign();	// Bloqueo SIGN
		// Creo Thread para manejar comunicacion con puerto serie
		ret = pthread_create (&serialThread, NULL, serial_thread, NULL); 
		desbloquearSign();	// Desbloqueo SIGN
		
		if(ret)
		{
			errno = ret;
			perror("pthread_create");
			return -1;
		}

		inet_ntop(AF_INET, &(clientaddr.sin_addr), ipClient, sizeof(ipClient));
		printf  ("server:  conexion desde:  %s\n",ipClient);

		pthread_mutex_lock (&mutexData);
		socketFlag = 1;	// Indico que la conexión con el socket esta activa
		pthread_mutex_unlock (&mutexData);
		
		while( socketFlag == 1 )
		{
			// Funcion de lectura bloqueante
        	n = read(newfd,buffer,128);
        	switch ( n )
			{
        		case -1:
                	perror("Error leyendo mensaje en socket");
            	case 0:
					pthread_mutex_lock (&mutexData);
                	socketFlag = 0;	// Indico que el socket se desconecto.
				    pthread_mutex_unlock (&mutexData);
	            	break;
            	default:
					serial_send(buffer,sizeof(buffer));
        	}
		}

        pthread_cancel(serialThread);
	    pthread_join (serialThread, NULL);
		close(newfd);
	}
	
	


	return 0;
}
