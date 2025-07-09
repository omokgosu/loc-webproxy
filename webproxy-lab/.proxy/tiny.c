/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "../csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd; // 리스닝 소켓, 커넥티드 소켓
  char hostname[MAXLINE], port[MAXLINE]; // 요청 호스트이름, 포트
  socklen_t clientlen; // 부호없는 정수형 32비트 타입
  struct sockaddr_storage clientaddr; // 클라이언트 주소정보

  /* Check command line args */
  if (argc != 2) // ./tiny 8080 이런거아니면 다 꺼져
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 리스닝 소켓 생성 리슨..오오 리슨..
  while (1)
  {
    clientlen = sizeof(clientaddr); // 클라이언트 주소 구조체의 사이즈만큼 생성 구조체는 16바이트 or 28바이트
    connfd = Accept( // connfd 에 파일디스크립터 int 할당
        listenfd, // 리스닝 소켓은 변화없음 정보 읽기
        (SA *)&clientaddr, // clientaddr 에 클라이언트 정보가 담김
        &clientlen // cleintlen 에 clientaddr 구조체의 실제 크기가 저장됩니다.
    );
    
    Getnameinfo(
        (SA *)&clientaddr, // 클라이언트 주소 정보
        clientlen, // 클라이언트 주소 정보 구조체의 크기기
        hostname, // 호스트 이름으로 변환 ( 142.183.121.29 or 도메인네임 )
        MAXLINE, // hostname 의 버퍼 크기
        port, // 포트 번호가 문자열로 변환
        MAXLINE, // port 버퍼 크기
        0 // 입력 플래그
    ); 

    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
    printf("connfd closed\n");
  }
}

/*
 * doit - handle one HTTP request/response transaction
 */
 void doit(int fd)
 {
   int is_static; /* 정적/동적 컨텐츠 구분 플래그 */
   struct stat sbuf; /* 파일 정보를 저장하는 구조체 */
 
   char 
     buf[MAXLINE], /* http 요청을 읽어올 버퍼 */
     method[MAXLINE], /* HTTP 메소드 */
     uri[MAXLINE], /* 요청된 리소스 경로 */
     version[MAXLINE]; /* HTTP 버전 */
 
   char 
     filename[MAXLINE], /* 실제 서버의 파일 경로 */
     cgiargs[MAXLINE]; /* CGI 프로그램에 전달할 인자들 */
     
   rio_t rio; /* Robuts I/O 패키지를 위한 구조체 */

   /* Read request line and headers */
   Rio_readinitb(&rio, fd); /* 읽기를 위해 Rio 구조체를 초기화 */
 

   if (!Rio_readlineb(&rio, buf, MAXLINE)) return; /* 클라이언트로부터 한 줄을 읽음 실패시 함수 종료 */
   
   printf("%s", buf); /* 읽은 내용 출력 (디버깅용) */
   
   sscanf(buf, "%s %s %s", method, uri, version); /* 공백으로 읽은 세 부분을 추출 */
   
   /* 메소드가 GET이 아니면 에러 반환 */
   if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method , "HEAD") == 0)) {
     clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
     return;
   }
 
   read_requesthdrs(&rio);
 
   /* Parse URI from GET request */
   is_static = parse_uri(uri, filename, cgiargs);

   if (stat(filename, &sbuf) < 0) {
     clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
     return;
   }
 
   if (is_static) { /* Serve static content */
     if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
       clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
       return;
     }
 
     serve_static(fd, filename, sbuf.st_size, method);
   } else { /* Serve dynamic content */
     if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
       clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
       return;
     }
 
     printf("file description = %d" , fd);
     printf("filename = %s" , filename);
     printf("cgiargs = %s" , cgiargs);

     serve_dynamic(fd, filename, cgiargs, method);
   }
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
    sprintf(body, "%s<body bgcolor=" "ffffff" ">\r\n", body);
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

/*
 * read_requesthdrs - read HTTP request headers
 */
 void read_requesthdrs(rio_t *rp)
 {
   char buf[MAXLINE];

   Rio_readlineb(rp, buf, MAXLINE);
   printf("%s", buf);
   while (strcmp(buf, "\r\n"))
   {
     Rio_readlineb(rp, buf, MAXLINE);
     printf("%s", buf);
   }
   return;
 }

 /*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin"))
  { /* Static content */
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    
    if (uri[strlen(uri) - 1] == '/') {
      strcat(filename, "home.html");
    }
    
    return 1;
  }
  else { /* Dynamic content */
    ptr = index(uri, '?');

    if (ptr) {
        strcpy(cgiargs, ptr + 1);
        *ptr = '\0';
    } else {
        strcpy(cgiargs, "");
    }

    strcpy(filename, ".");
    strcat(filename, uri);
    
    return 0;
}
}

/*
 * serve_static - copy a file back to the client
 */
 void serve_static(int fd, char *filename, int filesize, char *method)
 {
   int srcfd;
   char *srcp, filetype[MAXLINE];
 
   char buf[MAXBUF];
   char *p = buf;
   int n;
   int remaining = sizeof(buf);
 
   /* Send response headers to client */
   get_filetype(filename, filetype);
 
   /* Build the HTTP response headers correctly - use separate buffers or append */
   n = snprintf(p, remaining, "HTTP/1.0 200 OK\r\n");
   p += n;
   remaining -= n;
 
   n = snprintf(p, remaining, "Server: Tiny Web Server\r\n");
   p += n;
   remaining -= n;
 
   n = snprintf(p, remaining, "Connection: close\r\n");
   p += n;
   remaining -= n;
 
   n = snprintf(p, remaining, "Content-length: %d\r\n", filesize);
   p += n;
   remaining -= n;
 
   n = snprintf(p, remaining, "Content-type: %s\r\n\r\n", filetype);
   p += n;
   remaining -= n;
 
   Rio_writen(fd, buf, strlen(buf));
   printf("Response headers:\n");
   printf("%s", buf);
 
   if (strcasecmp(method , "GET") == 0) {
        /* Send response body to client */
        srcfd = Open(filename, O_RDONLY, 0);
        srcp = malloc(filesize);
        buf[filesize];
        // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);

        // Close(srcfd);
        Rio_readn(srcfd, srcp, filesize);
        Close(srcfd);
        Rio_writen(fd, srcp, filesize);
        free(srcp);
   }
 }

 /*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}


 /*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
  char buf[MAXLINE], *emptylist[] = {NULL};
  pid_t pid;

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (strcasecmp(method, "GET") == 0) {
    /* Create a child process to handle the CGI program */
    if ((pid = Fork()) < 0)
    { /* Fork failed */
        perror("Fork failed");
        return;
    }

    if (pid == 0) { /* Child process */
        /* Real server would set all CGI vars here */
        setenv("QUERY_STRING", cgiargs, 1);

        /* Redirect stdout to client */
        if (Dup2(fd, STDOUT_FILENO) < 0)
        {
            perror("Dup2 error");
            exit(1);
        }

        Close(fd);

        /* Run CGI program */
        Execve(filename, emptylist, environ);

        /* If we get here, Execve failed */
        perror("Execve error");
        exit(1);
    } else { /* Parent process */
        /* Parent waits for child to terminate */
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            perror("Wait error");
        }

        printf("Child process %d terminated with status %d\n", pid, status);
        /* Parent continues normally - returns to doit() */
    }
    /* When we return from here, doit() will close the connection */
  }
}