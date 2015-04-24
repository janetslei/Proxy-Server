#include <netdb.h> 
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>	/* system type defintions */
#include <sys/socket.h>	/* network system functions */
#include <netinet/in.h>	/* protocol & struct definitions */

#define BACKLOG	5
#define BUF_SIZE	1024
#define LISTEN_PORT	1357
#define SERVER_R_SIZE 10000

int threadCount = BACKLOG;
void *client_handler(void *arg);

int main(int argc, char *argv[]){
    int status, *sock_tmp;
    pthread_t a_thread;
    void *thread_result;
	int sock_listen;
    struct sockaddr_in addr_mine;
    struct sockaddr_in addr_remote;
    int sock_aClient;
    int addr_size;
    int reuseaddr = 1;


    sock_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_listen < 0) {
        perror("socket() failed");
        exit(0);
    }

    memset(&addr_mine, 0, sizeof (addr_mine));
    addr_mine.sin_family = AF_INET;
    addr_mine.sin_addr.s_addr = htonl(INADDR_ANY);
    addr_mine.sin_port = htons((unsigned short)LISTEN_PORT);

    status = bind(sock_listen, (struct sockaddr *) &addr_mine,
    	sizeof (addr_mine));
    if (status < 0) {
        perror("bind() failed");
        close(sock_listen);
        exit(1);
    }

    status = listen(sock_listen, 5);
    if (status < 0) {
        perror("listen() failed");
        close(sock_listen);
        exit(1);
    }

    addr_size = sizeof(struct sockaddr_in);
    printf("waiting for a client\n");
    while(1) {
    	if (threadCount < 1) {
    		sleep(1);
    	}

    	sock_aClient = accept(sock_listen, (struct sockaddr *) &addr_remote,
            &addr_size);
    	if (sock_aClient == -1){
    		close(sock_listen);
        	exit(1);
    	}

    	printf("Got a connection from %s on port %d\n",
                    inet_ntoa(addr_remote.sin_addr),
                    htons(addr_remote.sin_port));
    	sock_tmp = malloc(1);
    	*sock_tmp = sock_aClient;
    	printf("thread count = %d\n", threadCount);
    	threadCount--;
 		status = pthread_create(&a_thread, NULL, client_handler,
            (void *) sock_tmp);
 		if (status != 0) {
 			perror("Thread creation failed");
 			close(sock_listen);
 			close(sock_aClient);
 			free(sock_tmp);
        	exit(1);
 		}
    }

    return 0;
}

void *client_handler(void *sock_desc) {
	int msg_size;
	char buf[BUF_SIZE];
	int sock = *(int*)sock_desc;
	char message[BUF_SIZE], temp2[50];
	char *temp, *address, *token, *host;

	printf("In client_handler\n");
	memset (buf, 0, 1024);
	while ((msg_size = recv(sock, buf, BUF_SIZE, 0)) > 0) {
        buf[msg_size] = 0;
        //receive the request message from the client
		printf("Received: %s\n", buf);
		//Extract the path of the requested object from the buf 
		//the path is the second part of HTTP header
		strcpy (message, buf);
		temp = strtok(message, "/");
		address = strtok(NULL, " ");
		printf ("%s\n", address); //address = www.host.com
		
		//get host name, so get rid of "www."
		strcpy (temp2, address);
		token = strtok(temp2, ".");
		host = strtok(NULL, " ");
		printf("Connecting to %s\n", host);
		
		//turn host name into ip address
		struct hostent *he;
		if ((he = gethostbyname(host)) == NULL) {
			fprintf(stderr, "Could not get host\n");	
			continue;
		}
		
		//create a socket on the proxy server 
		int newSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (newSocket < 0) {
			perror("socket() failed");
			exit(0);
		}
		struct sockaddr_in addr_req;
		memset(&addr_req, 0, sizeof (addr_req));
		memcpy(&addr_req.sin_addr, he->h_addr_list[0], he->h_length);
		addr_req.sin_family = AF_INET;
		addr_req.sin_port = htons(80); //connect to port 80
		
		//attempt connection with assigned socket
		if (connect(newSocket, (struct sockaddr *)&addr_req, sizeof(addr_req)) < 0) {
			fprintf(stderr, "Thread could not connect to server\n");
			exit(0);
		}
		
		//first check whether the file exists in the cache
		char *filename = (char *)malloc(strlen(address)+9);
		sprintf(filename, "cache/%s.txt", address);
		if (access(filename, F_OK) != -1) {
			//file exists 
			printf ("Read from cache..\n");
			FILE *f = fopen(filename, "r");
			if (f == NULL) {
				fprintf(stderr, "Can't open cache file!\n");
				exit(1);
			}
			//retrieve file content
			char *file_content = (char *)malloc(SERVER_R_SIZE+1);
			file_content[0] = '\0';
			char fileBuff[255];
			while (fgets(fileBuff, 255, f) != NULL) {
				strcat(file_content, fileBuff);
			}
			fclose(f);
			free(filename);
			
			//write to the client
			if (write(sock, file_content, strlen(file_content)) < 0) {
				fprintf(stderr, "ERROR writing to socket\n");
				exit(0);
			}
		}
		else {
			//file doesn't exist
			//generate request based on passed address
			char *request = (char *)malloc(strlen(address)+23);
			sprintf(request, "GET %s HTTP/1.0\n\n", address);
		
			//send request to socket
			if (send(newSocket, request, strlen(request), 0) < 0) {
				fprintf(stderr, "[-] Failed to send message\n");
				close(newSocket);
				exit(0);
			}
		
			//handle response from the server
			char server_r[SERVER_R_SIZE];
			if (recv(newSocket, server_r, SERVER_R_SIZE, 0) < 0) {
				fprintf(stderr, "[-] Failed to receive message\n");
				close(newSocket);
				exit(0);
			}
			printf("Received From Server: %s\n", server_r);
			
			//check if website is blacklisted
			char block_response[] = "HTTP/1.0 200 OK\r\n"
						"Content-Type: text/html; charset=UTF-8\r\n\r\n"
						"<!DOCTYPE html><html><head><title>NO! NO! NO! NO!</title>"
						"<style>body { background-color: #111 }"
						"h1 { font-size:4cm; text-align: center; color: black;"
						" text-shadow: 0 0 2mm red}</style></head>"
						"<body><h1>BLOCKED..FOR A MYSTERIOUS REASON!!</h1></body></html>\r\n";
			
			//send block response to website
			if ( (strcmp(host, "facebook.com") == 0) || (strcmp(host, "youtube.com") == 0)  || 
				 (strcmp(host, "hulu.com") == 0) || (strcmp(host, "virus.com") == 0) ) {
				write(sock, block_response, strlen(block_response));
			}
			else {
				//write to client
				if (write(sock, server_r, strlen(server_r)) < 0) {
					fprintf(stderr, "ERROR writing to socket\n");
					exit(0);
				}
			}
		
			//write to cache
			char *filename = (char *)malloc(strlen(address)+9);
			sprintf(filename, "cache/%s.txt", address);
			FILE *f = fopen (filename, "w");
		
			if (f == NULL) {
				fprintf(stderr, "Can't open cache file!\n");
				exit(1);
			}
			fprintf(f, "%s\n", server_r);
			fclose(f);
			free (filename);
		
			//reset the buf variable
			memset(buf, 0, 1024);
			close (newSocket);
			free (request);
		}
	}
   
	printf ("Connection closed\n");
    	close(sock);
	free(sock_desc);
	threadCount++;
}
