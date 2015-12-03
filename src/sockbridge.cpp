/**
 * @file commtool.cpp
 * @brief comm tool
 * @author hulei
 * @version 1.0
 * @date 2011-06-30
 */

#include <string>
#include <iostream>
#include <cstdio>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (-1)
#define SD_BOTH SHUT_RDWR
#define closesocket(fd) close(fd)
#define Sleep(ms)       usleep((ms) * 1000)
#endif
#include <boost/thread.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
using namespace std;

boost::mutex mu_printing;
boost::mutex mu_server;
boost::mutex mu_client;

string client_ip;
unsigned short client_port;
unsigned short server_port;
bool silent;
#ifdef _WIN32
bool nocolor;
WORD origColor;
#endif
char client_buf[0xffff];
char server_buf[0xffff];

void print_bytes(char* buffer, int len, bool is_server)
{
    if (silent)
    {
        return;
    }
    char linebuf[100];
    boost::mutex::scoped_lock lock(mu_printing);

#ifdef _WIN32
    if (!nocolor)
    {
        WORD color = is_server ? FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY : FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY;
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
    }
#endif

    int start = 0;
    int end;
    while (start < len)
    {
        end = start + 16;
        int offset = 0;
        if (start != 0)
        {
            offset += sprintf(linebuf, "       |");
        }
        else
        {
            if (is_server)
            {
                offset += sprintf(linebuf, "S >> C |");
            }
            else
            {
                offset += sprintf(linebuf, "S << C |");
            }
        }
        for (int i = start; i < end; ++i)
        {
            if (i % 4 == 0)
            {
                linebuf[offset++] = ' ';
            }
            if (i < len)
            {
                offset += sprintf(linebuf + offset, "%02X ", (int)buffer[i] & 0xff);
            }
            else
            {
                offset += sprintf(linebuf + offset, "   ");
            }
        }
        offset += sprintf(linebuf + offset, "| ");
        for (int i = start; i < end; ++i)
        {
            if (i < len)
            {
                if (buffer[i] < 128 && buffer[i] >= 0 && isprint(buffer[i]))
                {
                    linebuf[offset++] = buffer[i];
                }
                else
                {
                    linebuf[offset++] = '.';
                }
            }
            else
            {
                linebuf[offset++] = ' ';
            }
        }
        linebuf[offset] = '\0';
        cout << linebuf << endl;
        start += 16;
    }
#ifdef _WIN32
    if (!nocolor)
    {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), origColor);
    }
#endif
    cout.flush();
}


void print_msg(const string& str)
{
    if (silent)
    {
        return;
    }
    boost::mutex::scoped_lock lock(mu_printing);
    cout << str << endl;
    cout.flush();
}

void print_err(const string& err)
{
    if (silent)
    {
        return;
    }
    boost::mutex::scoped_lock lock(mu_printing);
    cerr << err << endl;
    cerr.flush();
}

SOCKET client_connect(void)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
#ifdef _WIN32
    addr.sin_addr.S_un.S_addr = inet_addr(client_ip.c_str());
#else
    unsigned long ip = inet_addr(client_ip.c_str());
    memcpy(&addr.sin_addr, &ip, sizeof(ip));
    //addr.sin_addr = inet_addr(client_ip.c_str());
#endif
    addr.sin_port = htons(client_port);
    SOCKET sc = socket(AF_INET, SOCK_STREAM, 0);
    if (sc == (SOCKET)SOCKET_ERROR)
    {
        print_err("create client socket error.");
        exit(1);
    }
    while (connect(sc, (const sockaddr*)&addr, sizeof(addr)) < 0)
    {
        Sleep(1000);
    }
    print_msg((boost::format("connect to client %s : %d succeeded.") % client_ip % client_port).str());
    return sc;
}

SOCKET server_listen(void)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    SOCKET ss = socket(AF_INET, SOCK_STREAM, 0);
    if (ss == (SOCKET)SOCKET_ERROR)
    {
        print_err("create server socket error.");
        exit(1);
    }
    if (::bind(ss, (const sockaddr*)&addr, sizeof(addr)) < 0)
    {
        print_err("bind server socket error.");
        exit(1);
    }
    listen(ss, 1);
    print_msg((boost::format("server listening on port %d ...") % ((int)server_port & 0xffff)).str());
    return ss;
}

volatile SOCKET sock_server;
volatile SOCKET sock_client;


void close_all_socket(void)
{
    {
        boost::mutex::scoped_lock sl(mu_server);
        if (sock_server != INVALID_SOCKET)
        {
            shutdown(sock_server, SD_BOTH);
            closesocket(sock_server);
            sock_server = INVALID_SOCKET;
        }
    }
    {
        boost::mutex::scoped_lock sc(mu_client);
        if (sock_client != INVALID_SOCKET)
        {
            shutdown(sock_client, SD_BOTH);
            closesocket(sock_client);
            sock_client = INVALID_SOCKET;
        }
    }
}

void thread_server_to_client(void)
{
    SOCKET ss = server_listen();
#ifdef _WIN32
    int len = sizeof(sockaddr_in);
#else
    socklen_t len = sizeof(sockaddr_in);
#endif
    while (1)
    {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        sock_server = accept(ss, (sockaddr*)&addr, &len);
        print_msg((boost::format("server port was connected from %s : %d.") % inet_ntoa(addr.sin_addr) % ntohs(addr.sin_port)).str());
        while (sock_client == INVALID_SOCKET)
        {
            Sleep(10);
        }
        while (1)
        {
            int ret = recv(sock_server, server_buf, sizeof(server_buf), 0);
            if (ret <= 0)
            {
                close_all_socket();
                break;
            }
            print_bytes(server_buf, ret, true);

            while (send(sock_client, server_buf, ret, 0) < 0)
            {
                Sleep(10);
            }
        }
    }
}

void thread_client_to_server(void)
{
    while (1)
    {
        while (sock_server == INVALID_SOCKET)
        {
            Sleep(10);
        }
        sock_client = client_connect();
        while (1)
        {
            int ret;
            ret = recv(sock_client, client_buf, sizeof(client_buf), 0);
            if (ret <= 0)
            {
                close_all_socket();
                break;
            }
            print_bytes(client_buf, ret, false);
            while (sock_server == INVALID_SOCKET)
            {
                Sleep(10);
            }
            while (send(sock_server, client_buf, ret, 0) < 0)
            {
                Sleep(10);
            }
        }
    }
}

void init(void)
{
#if _WIN32
    if (!nocolor)
    {
        CONSOLE_SCREEN_BUFFER_INFO screenBufInfo;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &screenBufInfo);
        origColor = (WORD)screenBufInfo.wAttributes;
    }
#endif
    sock_server = INVALID_SOCKET;
    sock_client = INVALID_SOCKET;
}
namespace bpo = boost::program_options;

void help(bpo::options_description& opts)
{
    cout << "usage: sockbridge -c 9025 -s 9026" << endl;
    cout << opts << endl;
}


void parse_commandline(int argc, const char* argv[])
{
    bpo::options_description opts("sockbridge options");
    opts.add_options()
    ("help,h", "show help information")
    ("clientip,i", bpo::value<string>(&client_ip)->default_value("127.0.0.1"), "set client ip addr")
    ("clientport,c", bpo::value<unsigned short>(&client_port)->default_value(9025), "set client port")
    ("serverport,s", bpo::value<unsigned short>(&server_port)->default_value(9026), "set server port")
    ("silent,S", bpo::value<bool>(&silent)->default_value(0)->implicit_value(1), "dump nothing")
#ifdef _WIN32
    ("nocolor,C", bpo::value<bool>(&nocolor)->default_value(0)->implicit_value(1), "do not use color")
#endif
    ;
    bpo::variables_map vm;
    try
    {
        bpo::store(bpo::parse_command_line(argc, argv, opts), vm);
    }
    catch (bpo::error& e)
    {
        cout << e.what() << endl;
        help(opts);
        exit(1);
    }

    if (vm.count("help"))
    {
        help(opts);
        exit(0);
    }

    vm.notify();
}

void clean_up(void)
{
#if _WIN32
    if (!nocolor)
    {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), origColor);
    }
#endif
    close_all_socket();
}

int main(int argc, const char* argv[])
{
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    init();
    parse_commandline(argc, argv);
    atexit(clean_up);
    boost::thread s(thread_server_to_client);
    boost::thread c(thread_client_to_server);
    s.join();
    c.join();
    return 0;
}
