#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int clientfd);
void read_requesthdrs(rio_t *rio_pointer, char *clientbuf, int serverfd, char *hostname, char* port);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
    
int main(int argc, char **argv)
{
    int listenfd, connfd; // 리스닝소켓 파일 디스크립터, 커넥션 파일 디스크립터
    char hostname[MAXLINE], port[MAXLINE]; // 호스트네임 , 포트
    socklen_t clientlen; // 32비트 부호없는 정수형 타입인 clientlen 선언
    struct sockaddr_storage clientaddr; // 소켓 스토리지 구조체를 가지는 clientaddr 선언

    /* Check command line args */
    if (argc != 2) // 매개변수가 두개가아니면 ./tiny 8080 O  , ./tiny hi 8080 X 
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]); /* 리스닝 소켓 생성 */
    
    while (1)
    {
        clientlen = sizeof(clientaddr); // clientaddr 의 크기만큼 clientlen 에 할당
        connfd = Accept(
            listenfd, // 리스닝소켓
            (SA *)&clientaddr, // 클라이언트 주소 구조체
            &clientlen // 클라이언트 주소 구조체 크기
        ); // line:netp:tiny:accept
    
        Getnameinfo(
            (SA *)&clientaddr, // 클라이언트 주소 구조체
            clientlen, // 클라이언트 주소 구조체 크기
            hostname, // 호스트 이름
            MAXLINE, // 호스트 이름 최대길이
            port, // 포트 번호
            MAXLINE, // 포트번호 최대 길이
            0 // 0
        );
        
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        
        doit(connfd);  // line:netp:tiny:doit
        Close(connfd); // line:netp:tiny:close
    }
}

/*
    * doit - handle one HTTP request/response transaction
    */
void doit(int clientfd)
{
    char 
    clientbuf[MAXLINE], /* http 요청을 읽어올 버퍼 */
    method[MAXLINE], /* HTTP 메소드 */
    uri[MAXLINE], /* 요청된 리소스 경로 */
    hostname[MAXLINE], /* 호스트 이름 */
    port[MAXLINE], /* 포트 */
    path[MAXLINE], /* 경로 */
    version[MAXLINE]; /* HTTP 버전 */

    char serverbuf[MAXLINE]; /* 서버와 통신할 버퍼 */
    
    rio_t rio; /* Robust I/O 패키지를 위한 구조체 */
    rio_t rioServer; /* 서버와 통신할 Rio 구조체 */

    /* Read request line and headers */
    Rio_readinitb(&rio, clientfd); /* 읽기를 위해 Rio 구조체를 초기화 */
    if (!Rio_readlineb(&rio, clientbuf, MAXLINE)) return; /* 클라이언트로부터 한 줄을 읽음 실패시 함수 종료 */
    printf("Request_headers: %s\n", clientbuf); /* 읽은 내용 출력 (디버깅용) */
    
    // 요청 라인 parsing 을 통해 'method, uri, hostname, port, path 찾기 
    sscanf(clientbuf, "%s %s %s", method, uri, version); /* 공백으로 읽은 세 부분을 추출 */
    
    if (parse_uri(uri, hostname, port, path) < 0) {
        clienterror(clientfd, uri, "400", "Bad Request", "Invalid URI format");
        return;
    }

    /* 메소드가 GET이 아니면 에러 반환 */
    if (strcasecmp(method, "GET")) {
        clienterror(clientfd, method, "501", "Not Implemented", "Tiny does not implement this method");
        return;
    }
    
    /* 서버랑 통신할 정보 */
    int serverfd; /* 서버랑 연결된 디스크립터 파일 정보 */

    // 서버랑 연결된 파일디스크립터 생성
    serverfd = open_clientfd(hostname, port);
    
    // 서버와 연결되지 않았을 경우
    if (serverfd < 0) {
        clienterror(clientfd, hostname, "502", "Bad Gateway", "Failed to establish connection with the end server");
        return;
    }

    // 서버에 요청 라인 전송 (GET /path HTTP/1.0)
    sprintf(serverbuf, "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, serverbuf, strlen(serverbuf));
    
    // 서버에 헤더 데이터 전송
    read_requesthdrs(&rio, clientbuf, serverfd, hostname, port);

    // 서버에서 오는 응답 처리
    Rio_readinitb(&rioServer, serverfd);
    
    // 서버 응답을 클라이언트로 전달
    ssize_t n;
    while ((n = Rio_readnb(&rioServer, serverbuf, MAXLINE)) > 0) {
        Rio_writen(clientfd, serverbuf, n);
    }
    
    Close(serverfd);
}

/*
    * read_requesthdrs - read HTTP request headers
    */
void read_requesthdrs(rio_t *rio_pointer, char *clientbuf, int serverfd, char *hostname, char* port)
{
    int is_host_exist = 0;
    int is_connection_exist = 0;
    int is_proxy_connection_exist = 0;
    int is_user_agent_exist = 0;

    Rio_readlineb(rio_pointer, clientbuf, MAXLINE); // 첫 번째 줄 읽기

    while(strcmp(clientbuf, "\r\n")) {
    
        if (strstr(clientbuf, "Proxy-Connection") != NULL)  {
            sprintf(clientbuf, "Proxy-Connection: close\r\n");
            is_proxy_connection_exist = 1;
        } else if (strstr(clientbuf, "Connection") != NULL) {
            sprintf(clientbuf, "Connection: close\r\n");
            is_connection_exist = 1;
        } else if (strstr(clientbuf, "User-Agent") != NULL) {
            sprintf(clientbuf, "%s", user_agent_hdr);
            is_user_agent_exist = 1;
        } else if (strstr(clientbuf, "Host") != NULL) {
            is_host_exist = 1;
        }

        Rio_writen(serverfd, clientbuf, strlen(clientbuf)); // Server에 전송
        Rio_readlineb(rio_pointer, clientbuf, MAXLINE);       // 다음 줄 읽기
    }

    // 필수 헤더 미포함 시 추가로 전송
    if (!is_proxy_connection_exist) {
        sprintf(clientbuf, "Proxy-Connection: close\r\n");
        Rio_writen(serverfd, clientbuf, strlen(clientbuf));
    } 
    if (!is_connection_exist) {
        sprintf(clientbuf, "Connection: close\r\n");
        Rio_writen(serverfd, clientbuf, strlen(clientbuf));
    } 
    if (!is_host_exist) {
        sprintf(clientbuf, "Host: %s:%s\r\n", hostname, port);
        Rio_writen(serverfd, clientbuf, strlen(clientbuf));
    } 
    if (!is_user_agent_exist) {
        sprintf(clientbuf, "%s", user_agent_hdr);
        Rio_writen(serverfd, clientbuf, strlen(clientbuf));
    }

    sprintf(clientbuf, "\r\n"); // 종료문
    Rio_writen(serverfd, clientbuf, strlen(clientbuf));
    
    return;
}

/*
    * parse_uri - parse URI into hostname, port, and path
    */
int parse_uri(char *uri, char *hostname, char *port, char *path)
{
    // uri를 `hostname`, `port`, `path`로 파싱하는 함수
    // uri 형태: `http://hostname:port/path` 혹은 `http://hostname/path` (port는 optional)

    char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri; // 호스트이름
    char *port_ptr = strstr(hostname_ptr, ":"); // port 위치 없으면 NULL
    char *path_ptr = strstr(hostname_ptr, "/"); // path 위치 없으면 NULL

    // path가 없으면 기본값 "/"
    if (path_ptr == NULL) {
        strcpy(path, "/");
        path_ptr = hostname_ptr + strlen(hostname_ptr);
    } else {
        strcpy(path, path_ptr); // path에 구한 path_ptr 을 넣어준다.
    }
    
    if (port_ptr && port_ptr < path_ptr) {
        // 찾은 문자열이 : 와 / 기 때문에 1씩 더하고빼준다.
        strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1);
        port[path_ptr - port_ptr - 1] = '\0';
        strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
        hostname[port_ptr - hostname_ptr] = '\0';
    } else {
        strcpy(port, "80");
        strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
        hostname[path_ptr - hostname_ptr] = '\0';
    }
    
    return 0;
}

/*
    * clienterror - returns an error message to the client
    */
void clienterror(int fd, char *cause, char *errnum,
                    char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
