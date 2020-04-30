/**
 * @file test.cpp
 * @brief  简单的httpd
 *  Reference since [tiny-httpd](https://sourceforge.net/projects/tiny-httpd/)
 * @author Ichheit, <13660591402@163.com>
 * @date 2020-04-27
 */
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <cstdarg>
#include <functional>
#include <cstdarg>
#include <cstring>
#include <string>

#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#define E_FAIL(ERR) do {               \
    perror(#ERR);                      \
    exit(EXIT_FAILURE);                \
  } while (0)

#define E_RETP(FMT, ...) do {          \
    fprintf(stderr, FMT, __VA_ARGS__); \
    exit(EXIT_FAILURE);                \
  } while (0)

// socket
int Socket(int domain, int type, int protocol) {
    int ret;
    if ((ret = socket(domain, type, protocol)) == -1) 
        E_FAIL("socket erorr");
    return ret;
}

// accept
int Accept(int socket, struct sockaddr_in *address, socklen_t *address_len) {
    int ret;
    if ((ret = accept(socket, (struct sockaddr*) address, address_len)) == -1)
        E_FAIL("accept error");
    return ret;
}

// setsockopt
int Setsockopt(int socket, int level, int option_name, void *option_value, socklen_t option_len) {
    int ret;
    if ((ret = setsockopt(socket, level, option_name, option_value, option_len)) == -1)
        E_FAIL("setsockopt error");
    return ret;
}

// Listen
int Listen(int socket, int backlog) {
    int ret;
    if ((ret = listen(socket, backlog)) == -1)
        E_FAIL("listen error");
    return ret;
}

// inet ntop
const char* InetNtop(int socket, const void *src, char *dst, socklen_t size) {
    const char *ret;
    if ((ret = inet_ntop(socket, src, dst, size)) == nullptr) 
        E_FAIL("inet_ntop error");
    return ret;
}

// 主机服务监听绑定
static constexpr unsigned int LISTENQ = 100;
static constexpr char *BIND_DEFAULT_HOST = "localhost";
static constexpr char *BIND_DEFAULT_PORT = "12307";
int BindAddress(const char *host, const char *port, char *srv_addr, size_t addr_len, char *srv_port, size_t port_len) {
    int listenfd, no;
    const int on = 1;
    struct addrinfo hints, *res, *ressave;
    struct sockaddr_in *sa;
    memset(&hints, 0, sizeof(struct addrinfo));
    if (host == nullptr && port == nullptr) {
        host = BIND_DEFAULT_HOST;
        port = BIND_DEFAULT_PORT;
    }

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if ((no = getaddrinfo(host, port, &hints, &res)) != 0)
        E_RETP("bind address error for %s, %s: %s\n", host, port, gai_strerror(no));
    else 
        std::cout << "Serivice got addrinfo" << std::endl;
    
    ressave = res;
    do {
        if ((listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) 
            continue;   // error, try next one
        // enable reuse address
        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void *) &on, sizeof(on));
        // 返回结果的ai_addr和ai_addrlen可以用于bind
        if (bind(listenfd, res->ai_addr, res->ai_addrlen) == 0) {
            std::cout << "Service bound" << std::endl;
            // 反馈的信息
            snprintf(srv_port, port_len, "%s", port);
            sa = (struct sockaddr_in*) res->ai_addr;
            InetNtop(AF_INET, &sa->sin_addr.s_addr, srv_addr, static_cast<socklen_t>(addr_len));
            break;      // success
        }
        close(listenfd);
    } while ((res = res->ai_next) != nullptr);
    if (res == nullptr)
        E_RETP("bind address error for %s, %s\n", host, port);
    
    // listen
    Listen(listenfd, LISTENQ);

    freeaddrinfo(ressave); // free addrinfo
    return listenfd;
}

// Start
int Start(int argc, const char **argv, char *srv_addr, size_t addr_len, char *srv_port, size_t port_len) {
    int ret_sock;

    switch (argc) {
        case 1: ret_sock = BindAddress(nullptr, nullptr, srv_addr, addr_len, srv_port, port_len); break;
        case 2: ret_sock = BindAddress(nullptr, argv[1], srv_addr, addr_len, srv_port, port_len); break;
        case 3: ret_sock = BindAddress(argv[1], argv[2], srv_addr, addr_len, srv_port, port_len); break;
        default: E_RETP("%s\n", "usage: httpd [<Host>] <Service|Port>"); break;
    }
    
    return ret_sock;
}

// 防中断读
static const unsigned int IOSIZE = 4096;
static int s_read_cnt;
static char *s_read_ptr;
static char s_read_buf[IOSIZE];
static int read_r(int fd, char *ptr, int flag) {
    if (s_read_cnt <= 0) {
read_again:
        if ((s_read_cnt = recv(fd, s_read_buf, sizeof(s_read_buf), 0)) < 0) {
            if (errno == EINTR)
                goto read_again;
        } else if (s_read_cnt == 0) {
            return 0;
        } else if (flag == MSG_PEEK) {
            return 1;
        }
        s_read_ptr = s_read_buf;
    }
    // ret ptr set
    s_read_cnt--;
    *ptr = *s_read_ptr++;
    return 1;
}

// local global buffer
// 套接字读行
int RecvLine(int fd, char *vptr, size_t maxlen) {
    ssize_t n = 0;
    char ch = '\0';
    while (n < maxlen - 1 && ch != '\n') {
again:
        switch (recv(fd, &ch, 1, 0)) {
            case 1:
                if (ch == '\r') {
                    if (recv(fd, &ch, 1, MSG_PEEK) == 1 && ch == '\n') 
                        recv(fd, &ch, 1, 0);
                    else
                        ch = '\n';
                }
                vptr[n] = ch;
                n++;
                break;
            case 0:
                ch = '\n';
                break;
            case -1:
                if (errno == EINTR)
                    goto again;
                ch = '\n';
                break;
        }
    }
    vptr[n] = '\0';
    return n;
}

// 向套接字写行
// FIXME：How to exit security
int SendLineF(int fd, const char *cont) {
    int ret;
    char buf[IOSIZE];
    sprintf(buf, cont);
    if ((ret = send(fd, buf, strlen(buf), 0)) < 0) {
        shutdown(fd, SHUT_RDWR);
        E_FAIL("send error");
    }
    return ret;
}

// 501：未实现
static constexpr char *SERVER_STRING = "Server: testhttpd/0.1.0\r\n";
void Response501Unimplemented(int cli_sock) {
    SendLineF(cli_sock, "HTTP/1.0 501 Method Not Implemented\r\n");
    SendLineF(cli_sock, SERVER_STRING);
    SendLineF(cli_sock, "Content-Type: text/html\r\n");
    SendLineF(cli_sock, "\r\n");
    SendLineF(cli_sock, "<HTML><HEAD><TITLE>501 Unimplemented</TITLE>\r\n");
    SendLineF(cli_sock, "</HEAD><BODY ALIGN=\"center\">\r\n");
    SendLineF(cli_sock, "<h1>501 METHOD NOT IMPLEMENTED.</h1>\r\n");
    SendLineF(cli_sock, "<P>HTTP request method not supported.</p>\r\n");
    SendLineF(cli_sock, "</BODY></HTML>\r\n");
}

// 500：服务器未知错误
void Response500Cannotexecute(int cli_sock) {
    SendLineF(cli_sock, "HTTP/1.0 500 Internal Server Error\r\n");
    SendLineF(cli_sock, "Content-Type: text/html\r\n");
    SendLineF(cli_sock, "\r\n");
    SendLineF(cli_sock, "<HTML><HEAD><TITLE>500 Cannot Execute</TITLE>\r\n");
    SendLineF(cli_sock, "</HEAD><BODY ALIGN=\"center\">\r\n");
    SendLineF(cli_sock, "<h1>500 CANNOT EXECUTE.</h1>\r\n");
    SendLineF(cli_sock, "<P>Error prohibited CGI execution.</p>\r\n");
    SendLineF(cli_sock, "</BODY></HTML>\r\n");
}

// 404：未找到
void Response404Notfound(int cli_sock) {
    SendLineF(cli_sock, "HTTP/1.0 404 NOT FOUND\r\n");
    SendLineF(cli_sock, SERVER_STRING);
    SendLineF(cli_sock, "Content-Type: text/html\r\n");
    SendLineF(cli_sock, "\r\n");
    SendLineF(cli_sock, "<HTML><HEAD><TITLE>404 Not Found</TITLE>\r\n");
    SendLineF(cli_sock, "</HEAD><BODY ALIGN=\"center\">\r\n");
    SendLineF(cli_sock, "<h1>404 NOT FOUND.</h1>\r\n");
    SendLineF(cli_sock, "<p>The server could not fulfill\r\n");
    SendLineF(cli_sock, "your request becuase the resource specified\r\n");
    SendLineF(cli_sock, "is unavailable or nonexistent.</p>\r\n");
    SendLineF(cli_sock, "</BODY></HTML>\r\n");
}

// HTTP 200 状态码
void HTTPHeader(int cli_sock) {
    SendLineF(cli_sock, "HTTP/1.0 200 OK\r\n");
    SendLineF(cli_sock, SERVER_STRING);
    SendLineF(cli_sock, "Content-Type: text/html\r\n");
    SendLineF(cli_sock, "\r\n");
}

void Response400Badrequest(int cli_sock) {
    SendLineF(cli_sock, "HTTP/1.0 400 BAD REQUEST\r\n");
    SendLineF(cli_sock, "Content-type: text/html\r\n");
    SendLineF(cli_sock, "\r\n");
    SendLineF(cli_sock, "<HTML><HEAD><TITLE>404 Not Found</TITLE>\r\n");
    SendLineF(cli_sock, "</HEAD><BODY ALIGN=\"center\">\r\n");
    SendLineF(cli_sock, "<h1>400 BAD REQUEST</h1>\r\n");
    SendLineF(cli_sock, "<P>Your browser sent a bad request, ");
    SendLineF(cli_sock, "such as a POST without a Content-Length.</p>\r\n");
    SendLineF(cli_sock, "</BODY></HTML>\r\n");
}

// 清空接收套接字缓存
// XXX
void StealALlMessage(int cli_sock) {
    char buf[IOSIZE];
    while (RecvLine(cli_sock, buf, sizeof(buf)) > 0 && strcmp(buf, "\n"));
}

// 传送index页面
int SendFile(int fd, const char *file) {
    FILE *resource = nullptr;
    char buf[IOSIZE];

    StealALlMessage(fd);
    if ((resource = fopen(file, "r")) == nullptr) {
        Response404Notfound(fd);
    } else {
        // OK
        HTTPHeader(fd);
        do {
            fgets(buf, sizeof(buf), resource);
            send(fd, buf, strlen(buf), 0);
        } while (!feof(resource));
    }
    fclose(resource);
}

// Pipe
int Pipe(int cli_sock, int fields[2]) {
    int ret;
    if ((ret = pipe(fields)) == -1) {
        Response500Cannotexecute(cli_sock);
        E_FAIL("pipe error");
    }
    return ret;
}

// Fork
int Fork(int cli_sock) {
    int ret;
    if ((ret = fork()) == -1) {
        Response500Cannotexecute(cli_sock);
        E_FAIL("fork error");
    }
    SendLineF(cli_sock, "HTTP/1.0 200 OK\r\n");
    return ret;
}

// Putenv
int Putenv(int cli_sock, const char *key, const char *option) {
    int ret;
    size_t blen;
    char buffer[255];
    if ((blen = snprintf(buffer, sizeof(buffer), "%s=%s", key, option)) >= sizeof(buffer) - 1) {
        Response500Cannotexecute(cli_sock);
        E_RETP("%s! That's %zu bytes\n", "string overflow", blen);
    }
    if ((ret = putenv(buffer)) != 0) {
        Response500Cannotexecute(cli_sock);
        E_FAIL("putenv error");
    }
    return ret;
}


// 处理CGI
// FIXME：加载CGI错误
int ExecuteCgi(int cli, const char *file, const char *method, const char *query_string) {
    // pipe
    int cgi_input[2], cgi_output[2];
    char buf[IOSIZE];
    pid_t pid;
    int status, content_length = -1, size = -1;
    char ch;

    if (!strcasecmp(method, "GET")) {         // 处理GET请求
        StealALlMessage(cli);
    } else if (!strcasecmp(method, "POST")) { // 请求POST请求
        for (size = RecvLine(cli, buf, sizeof(buf));
                size > 0 && strcmp("\n", buf);
                size = RecvLine(cli, buf, sizeof(buf))) {
            buf[15] = '\0';
            if (!strcasecmp(buf, "Content-Length:"))
                content_length = atoi(&buf[16]);
        }
        if (content_length == -1) {
            Response400Badrequest(cli);
            E_RETP("%s", "bad_request");
        }
    } else {
        // unimplemented other wise
    }

    Pipe(cli, cgi_input);
    Pipe(cli, cgi_output);

    switch (pid = Fork(cli)) {
        // child：重定向stdin、stdout到cgi_input、cgi_output的pipe_read和pipe_write
        // 并关闭cgi_input的pipe_write和cgi_output的pipe_read
        case 0:  
            dup2(cgi_input[0], STDIN_FILENO);
            dup2(cgi_output[1], STDOUT_FILENO);
            close(cgi_input[1]);
            close(cgi_output[0]);

            // CGI ENV：
            // REQUEST_METHOD：server和CGI之间的传输方式
            // QUERY_STRING：GET时的传输信息
            // CONTENT_LENGTH：STDIO中有效信息长度
            Putenv(cli, "REQUEST_METHOD", method);
            if (!strcasecmp(method, "GET")) // GET
                Putenv(cli, "QUERY_STRING", query_string);
            else                            // POST
                Putenv(cli, "CONTENT_LENGTH", std::to_string(content_length).c_str());

            // exec program
            execlp(file, nullptr);
            _exit(EXIT_SUCCESS);
            break;
        // parent：关闭cgi_input的pipe_read和cgi_ouptut的pipe_write
        // 因此形成一个流向，CGI output ->> child_stdout<>child_write_poutput|parent_read_output -> send to socket
        //                   recv from socket ->> parent_write_input|child_read_input<>child_input -> ???
        default:
            close(cgi_input[0]);
            close(cgi_output[1]);

            // POST request to pipe
            if (!strcasecmp(method, "POST")) {
                for (int i = 0; i < content_length; i++) {
                    recv(cli, &ch, 1, 0);
                    write(cgi_input[1], &ch, 1);
                }
            }
            // attain mes from pipe & display page
            while (read(cgi_output[0], &ch, 1) > 0) 
                send(cli, &ch, 1, 0);

            close(cgi_output[0]);
            close(cgi_input[1]);
            waitpid(pid, &status, 0);
            break;
    }
}

// 接收请求
static constexpr unsigned int BUFFSIZE = IOSIZE;
void AcceptRequest(void *arg) {
    int cli_sock = (intptr_t) arg;
    char buf[BUFFSIZE], method[255], url[255], doc_path[255];
    size_t size;
    int i, s, cgi = 0;
    char *query_string = nullptr;
    struct stat st;
    auto nospace = std::bind([&](const char &ch) -> bool { return !std::isspace(ch); }, std::placeholders::_1);

    // method
    size = RecvLine(cli_sock, buf, sizeof(buf));
    for (i = 0; nospace(buf[i]) && i < sizeof(method); method[i] = buf[i], i++);
    method[i] = '\0';
    s = i;

    if (!strcasecmp(method, "POST")) {
        cgi = 1;
    } else if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        Response501Unimplemented(cli_sock);
        StealALlMessage(cli_sock);
        shutdown(cli_sock, SHUT_RDWR);
        return;
    }

    // url
    for (; !nospace(buf[s]) && s < size; s++);
    for (i = 0; nospace(buf[s]) && (i < sizeof(url) - 1) && s < size; url[i] = buf[s], i++, s++);
    url[i] = '\0';

    // mothod get
    if (!strcasecmp(method, "GET")) {
        for (query_string = url; *query_string != '?' && *query_string != '\0'; query_string++);
        if (*query_string == '?') {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    // 指定资源路径
    sprintf(doc_path, "docs%s", url);

    // pathname automatic stitch to pathname
    if (doc_path[strlen(doc_path) - 1] == '/')  {
        strncat(doc_path, "index.html", 11);
    }

    if (stat(doc_path, &st) == -1) {
        StealALlMessage(cli_sock);
        Response404Notfound(cli_sock);
    }
    else {
        // CGI操作权限
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
        if (cgi)
            ExecuteCgi(cli_sock, doc_path, method, query_string);
        else
            SendFile(cli_sock, doc_path);
    }
    
    shutdown(cli_sock, SHUT_RDWR);
}

int main(int argc, const char *argv[])
{
    int srv_sock, cli_sock;
    socklen_t srv_len, cli_len;
    struct sockaddr_in cli_addr;
    char srv_addr[16], srv_port[6];
    std::string port = argv[2];

    pthread_t new_thread;
    pthread_attr_t thread_attr;

    std::cout << "Service running..." << std::endl;
    srv_sock = Start(argc, argv, srv_addr, sizeof(srv_addr), srv_port, sizeof(srv_port));
    std::cout << "Service " << basename(argv[0]) << " running on " << srv_addr << " " << srv_port << std::endl;

    daemon(1, 1);
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
    for (;;) {
        cli_len = sizeof(cli_addr);
        cli_sock = Accept(srv_sock, &cli_addr, &cli_len);
        if (pthread_create(&new_thread, &thread_attr, (void *(*)(void*))AcceptRequest, (void *)(intptr_t)cli_sock) != 0)
            E_FAIL("pthread create error");
    }

    pthread_attr_destroy(&thread_attr);
    return 0;
}
