#include <iostream>
#include <map>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdexcept>

using namespace std;
struct e_base : std::exception {
    virtual const char* id() = 0;
};

#define EX(X) struct e_##X: e_base {const char* id() {return #X ;}};
EX(send)
EX(recv)
EX(accept)
EX(socket)
EX(setsockopt)
EX(bind)
EX(listen)
EX(select)
EX(gethostbyname)
EX(connect)

int yes = 1;
class Socket {
    int _fd;
    public:
    Socket(int port = 0) {
        if ((_fd = ::socket(AF_INET, SOCK_STREAM, port)) == -1)
            throw e_socket();
        if (setsockopt(_fd, SOL_SOCKET,SO_REUSEADDR, &yes,sizeof(int)) == -1)
            throw e_setsockopt();
    }

    void bind(int port) {
        struct sockaddr_in myaddr;     // Server address
        myaddr.sin_family = AF_INET;
        myaddr.sin_addr.s_addr = INADDR_ANY;
        myaddr.sin_port = htons(port);
        memset(myaddr.sin_zero, 0, sizeof myaddr.sin_zero);
        if (::bind(_fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1)
            throw e_bind();
    }

    void listen(int bl=10) {
        if (::listen(_fd, bl) == -1)
            throw e_listen();
    }

    int fd() {
        return _fd;
    }

    ~Socket() {
        close(_fd);
    }

    static struct hostent* gethost(const char* host) {
        struct hostent* he = ::gethostbyname(host);
        if (!he)
            throw e_gethostbyname();
        return he;
    }
};

class Server {
    int _listener;
    Socket* _sock;

    int create_listener(int port) {
        // get the lfd
        if (_sock) return _sock->fd();
        _sock = new Socket();

        _sock->bind(port);
        // listen
        _sock->listen();
        // add the lfd to the master set
        return _sock->fd();
    }

    public:
    Server(int port) {
        _sock = 0;
        _listener = create_listener(port);
    }

    int get_listener() {
        return _sock->fd();
    }

    ~Server() {
        delete _sock;
        _sock = 0;
    }

    int accept(int fd) {
        struct sockaddr_in remoteaddr; // client address
        socklen_t addrlen = sizeof(remoteaddr);
        int newfd = ::accept(fd, (struct sockaddr *)&remoteaddr, &addrlen);
        if(newfd == -1)
            throw e_accept();
        cout << "<" << inet_ntoa(remoteaddr.sin_addr) << "|" << newfd << "|" << endl;
        return newfd;
    }

    bool is_listener(int fd) {
        return fd == _listener;
    }

};

class Client {
    Socket* _sock;
    struct sockaddr_in _addr; // connector's address information
    public:
    Client(const char* host,const int port) : _sock(0) {
        struct hostent* he = Socket::gethost(host);
        _addr.sin_family = AF_INET;    // host byte order
        _addr.sin_port = htons(port);  // short, network byte order
        _addr.sin_addr = *((struct in_addr *)he->h_addr);
        memset(_addr.sin_zero, 0, sizeof _addr.sin_zero);
    }

    ~Client() {
        if (_sock)
            delete _sock;
    }

    int connect() {
        _sock = new Socket();
        if (::connect(_sock->fd(), (struct sockaddr *)&_addr, sizeof(struct sockaddr)) == -1) {
            delete _sock;
            _sock = 0;
            throw e_connect();
        }
        return _sock->fd();
    }
};

class Trance {
    fd_set _master;   // master file descriptor list
    fd_set _cur;
    int _maxfd;
    Server* _server;
    Client* _client;
    char _buf[1025];    // buffer for client data
    std::map<int,int> _fd_map;
    std::map<int,int> _client_fd_map;
    bool _color;
    public:
    Trance(const int port, const char* remote_host, const int remote_port, bool color=true)
    :_maxfd(0), _color(color) {
        FD_ZERO(&_master);    // clear the master and temp sets
        // create a server socket first
        _server = new Server(port);
        add_fd(_server->get_listener());
        _client = new Client(remote_host, remote_port);
    }

    ~Trance() {
        delete _server;
        delete _client;
    }

    void add_fd(int fd) {
        FD_SET(fd, &_master);
        if (fd > _maxfd) {    // keep track of the maximum
            _maxfd = fd;
        }
    }

    void remove_fd(int fd) {
        FD_CLR(fd, &_master); // remove from master set
    }

    bool is_fd_set(int fd) {
        return FD_ISSET(fd, &_master);
    }

    bool has_fd(int fd) {
        return FD_ISSET(fd, &_cur);
    }

    fd_set get_fds() {
        return _master;
    }

    int max_fd() {
        return _maxfd;
    }

    int receive(int fd) {
        // handle data from a client
        int nbytes;
        nbytes = ::recv(fd, _buf, sizeof(_buf) - 1, 0);
        switch (nbytes) {
            case -1:
                // got error or connection closed by client
                perror("recv");
            case 0:
                // connection closed
                remove_fdset(fd); // bye!
                return 0;
            default:
                _buf[nbytes] = 0;
                return nbytes;
        }
    }

    int send(int fd, int size) {
        int sent = 0;
        int to_send = size;
        while (to_send) {
            sent = ::send(fd, _buf + sent, to_send, 0);
            switch (sent) {
                case -1:
                    perror("send");
                case 0:
                    remove_fdset(fd); // bye!
                    return -1;
                default:
                    to_send = (size - sent);
            }
        }
    }

    void remove_fdset(int fd) {
        cout << "|" << _fd_map[fd] << "|>" <<endl;
        close(fd); // bye!
        remove_fd(fd); // remove from master set
        close(_fd_map[fd]); // remove the pair
        remove_fd(_fd_map[fd]);
    }

    void poll() {
        _cur = get_fds();
        if (select(max_fd() +1, &_cur, NULL, NULL, NULL) == -1)
            throw e_select();
    }

    int process() {
        // main loop
        while(true) {
            poll();
            // run through the existing connections looking for data to read
            for(int i = 0; i <= max_fd(); i++) {
                int fd = 0;
                try {
                    if (has_fd(i)) { // we got one!!
                        if (_server->is_listener(i)) {
                            // handle new connections
                            fd = _server->accept(i);
                            int client_fd = _client->connect();
                            _fd_map[fd] = client_fd;
                            _client_fd_map[client_fd] = 1;
                            _fd_map[client_fd] = fd;
                            // update master set.
                            add_fd(fd);
                            add_fd(client_fd);
                        } else {
                            // we got some data from a client/server
                            send(_fd_map[i],receive(i));
                            if (_client_fd_map[i] == 1) {
                                trace_client(_buf);
                            } else {
                                trace_server(_buf);
                            }
                            _buf[0] = 0;
                        }
                    }
                } catch (e_base& e) {
                    // accept & connect , we do not terminate.
                    perror(e.id());
                    if (fd) close(fd);
                }
            }
        }
        return 0;
    }

    void trace_client(char* buf) {
        if (_color)
            cout << "\e[31m" << "<[\n" << buf << "]" <<  "\e[0m" << endl;
        else
            cout << "<[\n" << buf << "]" << endl;
    }
    void trace_server(char* buf) {
        if (_color)
            cout << "\e[34m" << "<[\n" << buf << "]" <<  "\e[0m" << endl;
        else
            cout << ">[\n" << buf << "]" << endl;
    }
};

void usage() {
    cout << "trance [-c] <port> remote[:port]"<<endl;
}


int main(int argc, char* argv[]) {
    int _verbose = 0;
    int _color = 1;
    int c = 0;
    int errflg = 0;
    try {
        while((c = getopt(argc, argv, "cvh")) != -1) {
            switch (c) {
                case 'v':
                    _verbose++;
                    break;
                case 'c':
                    _color--;
                    break;
                case 'h':
                    usage();
                    return(0);
                default:
                    errflg++;
            }
        }
        argc -= optind;
        argv += optind;

        /* let us check options compatibility and completness*/

        if(errflg) {
            usage();
            exit(-1);
        }

        switch (argc) {
            case 0:
            case 1:
                usage();
                return 0;
            case 2:
                {
                    int port = atoi(argv[0]);
                    if (port < 1) {
                        usage();
                        return 0;
                    }
                    char* host = argv[1];
                    char* p = strchr(host, ':');
                    int remote_port = 80;
                    if (p) {
                        *p = 0;
                        remote_port = atoi(p+1);
                        if (remote_port < 1) {
                            usage();
                            return 0;
                        }
                    }
                    Trance t(port, host, remote_port, _color);
                    t.process();
                    return 0;
                }
            default:
                usage();
                return 0;
        }
    } catch (e_base& e) {
        perror(e.id());
    } catch (char const * e) {
        perror(e);
    }
}
