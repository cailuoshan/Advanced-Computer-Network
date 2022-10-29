/* http-server */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>  
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>     
#include <stdlib.h>

#include <assert.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

 
void *Create_80_port(void *);
void *Create_443_port(void *);
void *Handle_HTTP_Request(int cs);
void *Handle_HTTPS_Request(void *args);
void https_serve_response(SSL *ssl,char *filename, int range_start, int range_end);
void cat(SSL *ssl, FILE *resource, int range_start, int range_end);
SSL_CTX * InitSSL();

struct param
{
    SSL *ssl;
    int csock;
};



int main(int argc, const char *argv[]){
    // create 2 threads for listening 80 and 443 ports
    pthread_t new_thread_80;
    pthread_t new_thread_443;
    if (pthread_create(&new_thread_80, NULL, Create_80_port, NULL) != 0)
        perror("pthread_create failed!");
    if (pthread_create(&new_thread_443, NULL, Create_443_port, NULL) != 0)
        perror("pthread_create failed!");
    while(1){
    	sleep(1);
    }
    return 0;
}

/************************************************************************************************/
void *Create_80_port(void * no){
    int s, cs;
    struct sockaddr_in server, client;
    
    // create socket
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("create socket failed!");
        fflush(stdout);
		return NULL;
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
        return NULL;
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
            return NULL;
        }
        printf("connection accepted from port 80.\n");
        // create a thread to handle the request
        pthread_t new_thread;
        printf("csock:%d\n",cs);
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
 
void get_path(char *msg,char *path){
    char *ptr;
    ptr=strstr(msg,"/");
    int i=0;
    while(*ptr!=' '){
        path[i]=*ptr;
        ptr++;
        i++;
    }
    path[i]='\0';
}

void *Handle_HTTP_Request(int csock){
    printf("csock:%d\n",csock);
    char msg[256];
    int msg_len = 0;
    // receive a message from client
    while ((msg_len = recv(csock, msg, sizeof(msg), 0)) > 0) {
        /* parse the request */
        
        //only support GET method
        char path[50];
        char url[100];   
        memset(path,0,sizeof(path));
        printf("%s\n",msg);
        get_path(msg, path);
        strcpy(url,"https://10.0.0.1");
        strcat(url,path);
        printf("%s\n",url);
        /* ******************************* 
         * respond with 301 new location:  
         * HTTP/1.1 301 Moved Permanently
         * Location: https://10.0.0.1/index.html
         * *******************************/
        char buf[1024];
        strcpy(buf, "HTTP/1.1 301 Moved Permanently\r\n");
        strcat(buf, "Location: ");
        strcat(buf,url);
        strcat(buf,"\r\n\r\n\r\n");
        printf("%s\n",buf);
        write(csock, buf, strlen(buf));

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

void *Create_443_port(void * no){
    // init ssl certification
    SSL_CTX *ctx = InitSSL();
    if(ctx==NULL){
        perror("init ssl error!");
        return NULL;
    }

    // create socket
    int s, cs;
    struct sockaddr_in server, client;
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("create socket failed!");
        fflush(stdout);
		return NULL;
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
        return NULL;
    }
    printf("bind done.\n");
     
    // listen
    listen(s, 100);
    printf("waiting for incoming connections...\n");
    SSL *ssl;
    while (1)
    {
        // accept connection from an incoming client
        int c = sizeof(struct sockaddr_in);
        if ((cs = accept(s, (struct sockaddr *)&client, (socklen_t *)&c)) < 0) {
            perror("accept failed!");
            fflush(stdout);
            return NULL;
        }

        // create ssl connection
        ssl = SSL_new(ctx);
        if(ssl == NULL){
            perror("ssl new wrong!");
            return NULL;
        }
        printf("new ctx;\n");
        SSL_set_accept_state(ssl);
        // connect ssl with socket fd
        printf("set accept state\n");
        SSL_set_fd(ssl, cs);
        // ssl handshake
        printf("set fd\n");
        if (SSL_accept(ssl) == -1){
            perror("ssl get error!");
            return NULL;
        }
        printf("ssl connection accepted.\n");        
        // create a thread to handle ssl_read/write
        pthread_t new_thread;
        struct param param1 = {ssl,cs};
        if (pthread_create(&new_thread, NULL, Handle_HTTPS_Request, &param1) != 0)
            perror("pthread_create failed");
    }

    
    // ssl shutdown
    SSL_shutdown(ssl);
    SSL_free(ssl);
    // close server socket
    close(cs);
    SSL_CTX_free(ctx);
}

SSL_CTX * InitSSL(){
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
void *Handle_HTTPS_Request(void *args){
    struct param *p = (struct param *)args;
    SSL *ssl = (*p).ssl;
    int csock = (*p).csock;
    char msg[256];
    int msg_len = 0;
    // receive a message from client and parse the request
    while ((msg_len = SSL_read(ssl, msg, sizeof(msg))) > 0) {
        /* parse the request */
        char *str_start = NULL;
        int range_start = -1;
        int range_end = -1;

        char path[50];
        memset(path,0,sizeof(path));
        path[0]='.';   
        printf("%s\n",msg);
        char *ptr;
        //only support GET method
    	ptr=&msg[4];
    	int i=1;
    	while(*ptr!=' '){
        	path[i]=*ptr;
        	ptr++;
        	i++;
    	}
        printf("path:%s\n",path);
        str_start = strstr(msg,"Range");
        if (str_start != NULL)
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
        printf("args:%p,%p,%d,%d\n",ssl,path,range_start,range_end);
        //https_serve_response(ssl,path, range_start, range_end);


FILE *file = NULL;
    struct stat file_stat;
    
    char buf[1024*1024*50];
    SSL *ssl = NULL;
    if (stat(path, &file_stat) == -1) {
        //no such file
        strcpy(buf, "HTTP/1.1 404 File Not Found\r\n");
    	strcat(buf,"Server: 10.0.0.1\r\n");
        strcat(buf,"Connection: close\r\n");
    	strcat(buf,"\r\n");
    	SSL_write(ssl, buf, strlen(buf));
    }else {
        if((file = fopen(path, "r"))==NULL){
            perror("open failed");
		    return;
        }
        fseek(file, 0, SEEK_END);
        int st_size = ftell(file);
        fseek(file, 0, 0);  
        //send HTTP header 
        strcpy(buf, "HTTP/1.1 200 OK\r\n");
        sprintf(buf, "Content-Type: %s\r\n",path);
        sprintf(buf, "Content-Length: %d\r\n",st_size); 
        strcpy(buf,"Server: 10.0.0.1\r\n");
        strcat(buf,"\r\n");
        SSL_write(ssl, buf, strlen(buf));
        //send file body
        cat(ssl, file, range_start, range_end);
        SSL_write(ssl, buf, strlen(buf));
        fclose(file);
    }





        
        
    }
    
    if (msg_len == 0) {
        printf("client disconnected!");
        fflush(stdout);
    }
    
    return NULL;    
}

void https_serve_response(SSL *ssl,char *filename, int range_start, int range_end)
{   
    FILE *file = NULL;
    struct stat file_stat;
    char buf[1024*1024*50];
    //SSL *ssl = NULL;
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
        fseek(file, 0, SEEK_END);
        int st_size = ftell(file);
        fseek(file, 0, 0);  
        /*send HTTP header */
        strcpy(buf, "HTTP/1.1 200 OK\r\n");
        sprintf(buf, "Content-Type: %s\r\n",filename);
        sprintf(buf, "Content-Length: %d\r\n",st_size); 
        strcpy(buf,"Server: 10.0.0.1\r\n");
        strcat(buf,"\r\n");
        SSL_write(ssl, buf, strlen(buf));
        /*send file body*/
        cat(ssl, file, range_start, range_end);
        SSL_write(ssl, buf, strlen(buf));
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
