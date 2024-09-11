#ifdef _WIN32
    #include <WS2tcpip.h>
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

const int PORT = 8080;
const int BUFFER_SIZE = 1024;

void init_sockets();

void cleanup_sockets();

int readCross(int socket, char* buffer, const int BUFFER_SIZE);

void closeCross(int socket);

std::string chop_by_delimiter(std::string& input, char delimiter);

int inetPtonCross(sockaddr_in& service);

int readCross(int socket, char* buffer, const int BUFFER_SIZE);