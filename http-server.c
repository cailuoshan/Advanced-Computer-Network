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
 *  all http requests return 301, new https URL
 *  GET http://10.0.0.1/index.html HTTP/1.1\r\n or notfound.html or dir/index.html
 *  Range: bytes=100-200\r\n
 *  ......
 */
void Handle_HTTP_Request(int csock){
    char msg[256];
    int msg_len = 0;
    // receive a message from client
    while ((msg_len = recv(csock, msg, sizeof(msg), 0)) > 0) {
        /* parse the request */
        char url[50];
        int i = 0;
        char *str_start = strstr(msg,'http://');
        while (*str_start != ' ')
        {
            url[i++]=*str_start;
            str_start++;
            
            if (i==4)
                url[i++] = 's';        
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

/*  
 *  GET https://10.0.0.1/index.html HTTP/1.1\r\n or notfound.html or dir/index.html
 *  Range: bytes=100-200\r\n
 *  ......
 */
void Handle_HTTPS_Request(void *args){
    SSL *ssl = (*(param *)args).ssl;
    int csock = (*(param *)args).csock;
    char msg[256];
    int msg_len = 0;
    // receive a message from client and parse the request
    while ((msg_len = SSL_read(ssl, msg, sizeof(msg))) > 0) {
        /* parse the request */
        char *str_start = NULL;
        char file_path[20];
        int range_start = -1;
        int range_end = -1;
        if (str_start = strstr(msg,'10.0.0.1'))
        {
            str_start += 9;
            int i=0;
            while (*str_start != ' '){
                file_path[i++] = *str_start;
                str_start++;
            }
            file_path[i]='\0';
        }else{
            perror("parse request failed!");
        	fflush(stdout);
        	return;
        }

        if (str_start = strstr(msg,'Range'))
        {
            str_start += 12;
            char tmp[10];
            int i=0;
            while (*str_start != '-'){
                tmp[i++]=*str_start;
                str_start++;     
            }
            tmp[i]='\0';
            range_start=atoi(tmp);
            i=0;str_start++;
            while (*str_start != '\r'){
                tmp[i++]=*str_start;
                str_start++;     
            }
            if (i!=0){
                tmp[i]='\0';
                range_end=atoi(tmp);
            }
        }
        
        /* look up the file and make response */
        https_serve_response(ssl, file_path, range_start, range_end);

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

void https_serve_response(SSL *ssl, const char *filename, int range_start, int range_end)
{   
    FILE *file = NULL;
    struct stat file_stat;
    char buf[1024];
	if (stat(filename, &file_stat) == -1) {
        /*no such file*/
        strcpy(buf, "HTTP/1.1 404 File Not Found\r\n");
    	strcat(buf,"Server: 10.0.0.1\r\n");
        strcat(buf,"Connection: close\r\n");
    	strcat(buf,"\r\n");
    	SSL_write(ssl, buf, strlen(buf));
    }else {
        if((file = fopen(filename, "r"))==NULL){
            perror("open failed");
		    return;
        }
        /*send HTTP header */
        strcpy(buf, "HTTP/1.1 200 OK\r\n");
        SSL_write(ssl, buf, strlen(buf));
        sprintf(buf, "Content-Type: %s\r\n",filename);
        SSL_write(ssl, buf, strlen(buf));
        sprintf(buf, "Content-Length: %ld\r\n",st.st_size);  // need change!!
        SSL_write(ssl, buf, strlen(buf));
        strcpy(buf,"Server: 10.0.0.1\r\n");
        strcat(buf,"\r\n");
 	    SSL_write(ssl, buf, strlen(buf));
        /*send file body*/
        cat(ssl, file, range_start, range_end);
        fclose(file);
    }

    /*open the file*/

        /*
        string buf_w = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=UTF-8\r\n"
                        "Connection: close\r\n"
                        "Date: Fri, 23 Nov 2018 02:01:05 GMT\r\n"
                        "Content-Length: " + to_string(file_stat.st_size) + "\r\n"
                        "\r\n";
        buf_w += (char *)html_;
        SSL_write(ssl, (void*)buf_w.c_str(), buf_w.size());
        munmap(html_, file_stat.st_size);*/
    
}
/**********************************************************************/
/* Put the entire contents of a file out on a socket.  
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(SSL *ssl, FILE *resource, int range_start, int range_end)
{
    char buf[1024];
    int size;
    if (range_start == -1){
        while(!feof(resource)){
            size = fread(buf,1,1024,resource);
            SSL_write(ssl, buf, size);
        }
    } else if (range_start!=-1 && range_end==-1){
        fseek(resource, range_start, SEEK_SET);
        while(!feof(resource)){
            size = fread(buf,1,1024,resource);
            SSL_write(ssl, buf, size);
        }
    } else {
        fseek(resource, range_start, SEEK_SET);
        int total_size = 0;
        while (total_size < (range_end-range_start) && !feof(resource))
        {
            int remain_size = range_end-range_start-total_size;
            if (remain_size <= 1024)
            {
                size = fread(buf,1,remain_size,resource);
            }else{
                size = fread(buf,1,1024,resource);
            }
            total_size += size;
            SSL_write(ssl, buf, size);
        }
    }
    
}
