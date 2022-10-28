/* http-server */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>  
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>     
#include <stdlib.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

 
void Create_80_port();
void Create_443_port();

int main(int argc, const char *argv[]){
    // create 2 threads for listening 80 and 443 ports
    pthread_t new_thread_80;
    pthread_t new_thread_443;
    if (pthread_create(&new_thread_80, NULL, Create_80_port, NULL) != 0)
        perror("pthread_create failed!");
    if (pthread_create(&new_thread_443, NULL, Create_443_port, NULL) != 0)
        perror("pthread_create failed!");
    
    sleep(1);
    return 0;
}

/************************************************************************************************/
void Create_80_port(){
    int s, cs;
    struct sockaddr_in server, client;
    
    // create socket
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("create socket failed!");
        fflush(stdout);
		return -1;
    }
    printf("socket created.\n");
    
    // prepare the sockaddr_in structure
    int port_num =  80;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port_num);  
     
    // bind socketfd with the addr
    if (bind(s,(struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind failed!");
        fflush(stdout);
        return -1;
    }
    printf("bind done.\n");
     
    // listen
    listen(s, 100);
    printf("waiting for incoming connections...\n");
     
    while (1)
    {
        // accept connection from an incoming client
        int c = sizeof(struct sockaddr_in);
        if ((cs = accept(s, (struct sockaddr *)&client, (socklen_t *)&c)) < 0) {
            perror("accept failed!");
            fflush(stdout);
            return -1;
        }
        printf("connection accepted.\n");
        // create a thread to handle the request
        pthread_t new_thread;
        if (pthread_create(&new_thread, NULL, Handle_HTTP_Request, cs) != 0)
            perror("pthread_create failed");
    }
}

/*  
 *  GET http://10.0.0.1/dir/index.html HTTP/1.1\r\n
 */
void Handle_HTTP_Request(int csock){
    char msg[200];
    int msg_len = 0;
    // receive a message from client and parse the request
    while ((msg_len = recv(csock, msg, sizeof(msg), 0)) > 0) {
        /* get the request method */
        char method[5];
        int i=0,j=0;
        while((msg[j]!=' ') && (i<sizeof(method)) && (j<msg_len)){
            method[i] = msg[j];
            i++;j++;
        }
        method[i] = '\0';
        /* if not GET, claim error */
    	if (strcmp(method, "GET"))
    	{
        	perror("request failed!");
        	fflush(stdout);
        	return;
    	}
        /* get the url addr and change to https url*/
    	char url[100];
        i = 0;
    	j+=2;
    	while ((msg[j]!=' ') && (i<sizeof(url)) && (j<msg_len))
    	{
        	url[i] = msg[j];
            i++; j++;

            if (i==3){
                url[i++] = 's';
            }
    	}
    	url[i] = '\0';
        
    	/* ******************************* 
         * respond with 301 new location:  
         * HTTP/1.1 301 Moved Permanently
         * Location: https://10.0.0.1/index.html
         * *******************************/
        char buf[1024];
        strcpy(buf, "HTTP/1.1 301 Moved Permanently\r\n");
        sprintf(buf, "Location: %s\r\n",url);
        strcat(buf,"\r\n");
        send(csock, buf, strlen(buf), 0);

        // close server socket
        close(csock);
    }
     
    if (msg_len == 0) {
        printf("client disconnected!");
        fflush(stdout);
    }
    return;           
}


/**************************************************************************************************/
struct param
{
    SSL *ssl;
    int csock;
};

void Create_443_port(){
    // init ssl certification
    SSL_CTX *ctx = InitSSL();
    if(ctx==NULL){
        perror("init ssl error!");
        return;
    }

    // create socket
    int s, cs;
    struct sockaddr_in server, client;
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("create socket failed!");
        fflush(stdout);
		return -1;
    }
    printf("secure socket created.\n");
    
    // prepare the sockaddr_in structure
    int port_num =  443;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port_num);  
     
    // bind socketfd with the addr
    if (bind(s,(struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind failed!");
        fflush(stdout);
        return -1;
    }
    printf("bind done.\n");
     
    // listen
    listen(s, 100);
    printf("waiting for incoming connections...\n");
     
    while (1)
    {
        // accept connection from an incoming client
        int c = sizeof(struct sockaddr_in);
        if ((cs = accept(s, (struct sockaddr *)&client, (socklen_t *)&c)) < 0) {
            perror("accept failed!");
            fflush(stdout);
            return -1;
        }

        // create ssl connection
        SSL *ssl = SSL_new(ctx);
        if(ssl == NULL){
            perror("ssl new wrong!");
            return;
        }
        SSL_set_accept_state(ssl);
        // connect ssl with socket fd
        SSL_set_fd(ssl, cs);
        // ssl handshake
        if (SSL_accept(ssl) == -1){
            perror("ssl get error!");
            return;
        }
        printf("ssl connection accepted.\n");        

        // create a thread to handle ssl_read/write
        pthread_t new_thread;
        struct param param1 = {ssl,cs};
        if (pthread_create(&new_thread, NULL, Handle_HTTPS_Request, &param1) != 0)
            perror("pthread_create failed");
    }

    SSL_CTX_free(ctx);
}

SSL_CTX *InitSSL(){
    // init ssl environment
    SSL_library_init();
    SSL_load_error_strings();

    // create ssl context
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());

    // load server's cert and private key
    SSL_CTX_use_certificate_file(ctx, "./keys/cnlab.cert", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx, "./keys/cnlab.prikey", SSL_FILETYPE_PEM);
    
    // 判定私钥是否正确  
    assert(SSL_CTX_check_private_key(ctx));

    return ctx;
}

void Handle_HTTPS_Request(void *args){
    SSL *ssl = (*(param *)args).ssl;
    int csock = (*(param *)args).csock;
    char msg[200];
    int msg_len = 0;
    // receive a message from client and parse the request
    while ((msg_len = SSL_read(ssl, msg, sizeof(msg))) > 0) {
        /* TODO: parse the coming message detailed !!!!!!! path\range*/
        char file_path[100] = "/../index.html";
        struct stat file_stat;
    	if (stat(file_path, &file_stat) == -1) {
        	/*no such file*/
        	char buf[1024];
            strcpy(buf, "HTTP/1.1 404 File Not Found\r\n");
    		strcat(buf,"Server: 10.0.0.1\r\n");
            strcat(buf,"Connection: close\r\n");
    		strcat(buf,"\r\n");
    		SSL_write(ssl, buf, strlen(buf));
    	}else if(){
            /* TODO: read the partial file */
      		
    	}else {
            /* TODO: read the total file */
        }

        // ssl shutdown
        SSL_shutdown(ssl);
        SSL_free(ssl);
        // close server socket
        close(cs);
    }
     
    if (msg_len == 0) {
        printf("client disconnected!");
        fflush(stdout);
    }
    return;    
}

void serve_file(SSL *ssl, const char *filename, struct stat st)
{
    FILE *resource = NULL;
    char buf[1024];
    
    /*open the file*/
    if((resource = fopen(filename, "r"))==NULL){
        perror("open failed");
		return;
    }

        /*void *html_ = mmap(nullptr, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        string buf_w = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=UTF-8\r\n"
                        "Connection: close\r\n"
                        "Date: Fri, 23 Nov 2018 02:01:05 GMT\r\n"
                        "Content-Length: " + to_string(file_stat.st_size) + "\r\n"
                        "\r\n";
        buf_w += (char *)html_;
        SSL_write(ssl, (void*)buf_w.c_str(), buf_w.size());
        munmap(html_, file_stat.st_size);*/

    /*send HTTP header */
    strcpy(buf, "HTTP/1.1 200 OK\r\n");
    SSL_write(ssl, buf, strlen(buf));
    sprintf(buf, "Content-Type: %s\r\n",filename);
    SSL_write(ssl, buf, strlen(buf));
    sprintf(buf, "Content-Length: %ld\r\n",st.st_size);
    SSL_write(ssl, buf, strlen(buf));
    strcpy(buf,"Server: 10.0.0.1\r\n");
    strcat(buf,"\r\n");
 	SSL_write(ssl, buf, strlen(buf));
    /*send file body*/
    cat(ssl, resource);
    fclose(resource);
}
/**********************************************************************/
/* Put the entire contents of a file out on a socket.  
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(SSL *ssl, FILE *resource)
{
    char buf[1024];
    int size;
    size = fread(buf,1,1024,resource);
    send(csock, buf, size, 0);
    while(!feof(resource)){
        size = fread(buf,1,1024,resource);
        send(csock, buf, size, 0);
    }
}
