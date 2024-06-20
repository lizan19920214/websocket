#include "websocket.h"
#include "BaseFunc.h"
#include <openssl/sha.h>  //for SHA1
#include <arpa/inet.h>    //for ntohl
#include <string.h>
#include <iostream>
#include <sstream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <thread>

#define TestSTR_REQUEST "GET /ws/chat HTTP/1.1\r\nHost: server.example.com\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n"
#define WORKER_THREAD 1

WebSocket::WebSocket()
{
    m_hmClientBase.clear();
}

void WebSocket::initServer(const int port)
{
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenfd < 0)
    {
        std::cout << "socket error" << std::endl;
        return;
    }

	int iFlag = 1;
	int iReturn = setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &iFlag, sizeof(int));

    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(m_listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cout << "bind error" << std::endl;
        return;
    }

    if (listen(m_listenfd, SOMAXCONN) < 0)
    {
        std::cout << "listen error" << std::endl;
        return;
    }

    std::cout << "init server:" << m_listenfd << std::endl;

    m_epollfd = epoll_create(1111);
    std::cout << "addevent:" << addEpollEvent(m_listenfd, EPOLLIN) << std::endl;
}

void WebSocket::runServer()
{
    struct epoll_event events[2048];
    for(; ;)
    {
        int num = epoll_wait(m_epollfd, events, 2048, -1);

        if (num == -1)
        {
            if (errno == EINTR)
            {
                std::cout << "epoll_wait() error discarded" << std::endl;
                continue;
            }

            std::cout << "epoll_wait() error:" << errno << std::endl;
            break;
        }

        for (int i = 0; i < num; i++)
        {
            int socketfd = events[i].data.fd;
            if (socketfd == m_listenfd)
            {
                //新连接
                listenClient();
            }
            else if(events[i].events & EPOLLIN)
            {
                //新消息
                recvClient(socketfd);
            }
        }
    }
}

int WebSocket::addEpollEvent(int socketfd, int events)
{
    struct epoll_event ev;
    ev.data.fd = socketfd;
    ev.events = events;
    return epoll_ctl(m_epollfd, EPOLL_CTL_ADD, socketfd, &ev);
}

int WebSocket::deleteEpollEvent(int socketfd, int events)
{
    struct epoll_event ev;
    ev.data.fd = socketfd;
    ev.events = events;
    return epoll_ctl(m_epollfd, EPOLL_CTL_DEL, socketfd, &ev);
}

void WebSocket::closeClient(int clientfd)
{
    auto it = m_hmClientBase.find(clientfd);
    if (it == m_hmClientBase.end())
    {
        std::cout << "client not found" << std::endl;
        return;
    }

    deleteEpollEvent(clientfd, EPOLLIN | EPOLLET);
    close(clientfd);
    m_hmClientBase.erase(it);
}

void WebSocket::listenClient()
{
    std::cout << "listen new client" << std::endl;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int clientfd = accept(m_listenfd, (struct sockaddr*)&client_addr, &client_addr_len);
    if (clientfd < 0)
    {
        std::cout << "accept error" << std::endl;
        return;
    }

    //之前的连接没有删除，先关闭
    auto it = m_hmClientBase.find(clientfd);
    if (it != m_hmClientBase.end())
    {
        closeClient(clientfd);
        return;
    }

    clientBase cbase;
    cbase.socketfd = clientfd;
    //初始为握手状态
    cbase.status = WS_HANDSHARK;
    memset(cbase.buffer, 0, sizeof(cbase.buffer));

    std::cout << "new client connect" << clientfd << std::endl;

    m_hmClientBase.insert(std::make_pair(clientfd, cbase));
	addEpollEvent(clientfd, EPOLLIN | EPOLLET);
}

void WebSocket::recvClient(int clientfd)
{
    auto it = m_hmClientBase.find(clientfd);
    if (it == m_hmClientBase.end())
    {
        std::cout << "client not found" << std::endl;
        return;
    }

    int len = recv(clientfd, it->second.buffer, sizeof(it->second.buffer), 0);
    if (len <= 0)
    {
        if (errno == EINTR)
        {
        }
        else if(errno == EAGAIN)
        {
            return;
        }
        else
        {
            closeClient(clientfd);
        }
        return;
    }

    //首次消息处理握手
    if (it->second.status == WS_HANDSHARK)
    {
        //握手
        if (!parseHandshake(it->second.buffer))
        {
            std::cout << "parse handshake error" << std::endl;
            return;
        }
        //握手成功
        std::string response = respondHandshake();
        send(clientfd, response.c_str(), response.length(), 0);
        it->second.status = WS_TRANMISSION;
    }
    else if (it->second.status == WS_TRANMISSION)
    {
        //非首次正常处理消息
        std::vector<char> outBuf;
        outBuf.clear();
        int outLen = 0;
        getWSFrameData(it->second.buffer, sizeof(it->second.buffer), outBuf, &outLen);
    }
}

bool WebSocket::parseHandshake(const std::string& request)
{
    // 解析WEBSOCKET请求头信息
    bool ret = false;
    std::istringstream stream(request.c_str());
    std::string reqType;
    std::getline(stream, reqType);
    if (reqType.substr(0, 4) != "GET ")
        return ret;

    std::string header;
    std::string::size_type pos = 0;
    while (std::getline(stream, header) && header != "\r")
    {
        header.erase(header.end() - 1);
        pos = header.find(": ", 0);
        if (pos != std::string::npos)
        {
            std::string key = header.substr(0, pos);
            std::string value = header.substr(pos + 2);
            if (key == "Sec-WebSocket-Key")
            {
                ret = true;
                m_websocketKey = value;
                break;
            }
        }
    }

    return ret;
}


std::string WebSocket::respondHandshake()
{
    // 算出WEBSOCKET响应信息
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: upgrade\r\n";
    response += "Sec-WebSocket-Accept: ";

    //使用请求传过来的KEY+协议字符串，先用SHA1加密然后使用base64编码算出一个应答的KEY
    const std::string magicKey("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    std::string serverKey = m_websocketKey + magicKey;

    //SHA1
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)serverKey.c_str(), serverKey.length(), (unsigned char*)&digest);

    //Base64
    char basestr[1024] = {0};
    base64_encode((char*)digest, SHA_DIGEST_LENGTH, basestr);

    //完整的握手应答
    response = response + std::string(basestr) + "\r\n";

    //握手应答结束
    response = response + "\r\n";

    std::cout << "RESPONSE:" << response << std::endl;

    return response;
}


int WebSocket::getWSFrameData(char* msg, int msgLen, std::vector<char>& outBuf, int* outLen)
{
    if(msgLen < 2)
        return INCOMPLETE_FRAME;

    uint8_t fin_ = 0;
    uint8_t opcode_ = 0;
    uint8_t mask_ = 0;
    uint8_t masking_key_[4] = {0,0,0,0};
    uint64_t payload_length_ = 0;
    int pos = 0;
    //FIN
    fin_ = (unsigned char)msg[pos] >> 7;
    //Opcode
    opcode_ = msg[pos] & 0x0f;
    pos++;
    //MASK
    mask_ = (unsigned char)msg[pos] >> 7;
    //Payload length
    payload_length_ = msg[pos] & 0x7f;
    pos++;
    if(payload_length_ == 126)
    {
        uint16_t length = 0;
        memcpy(&length, msg + pos, 2);
        pos += 2;
        payload_length_ = ntohs(length);
    }
    else if(payload_length_ == 127)
    {
        uint32_t length = 0;
        memcpy(&length, msg + pos, 4);
        pos += 4;
        payload_length_ = ntohl(length);
    }
    //Masking-key
    if(mask_ == 1)
    {
        for(int i = 0; i < 4; i++)
            masking_key_[i] = msg[pos + i];
        pos += 4;
    }
    //取出消息数据
    if (msgLen >= pos + payload_length_ )
    {
        //Payload data
        *outLen = pos + payload_length_;
        outBuf.clear();
        if(mask_ != 1)
        {
            char* dataBegin = msg + pos;
            outBuf.insert(outBuf.begin(), dataBegin, dataBegin+payload_length_);
        }
        else
        {
            for(uint i = 0; i < payload_length_; i++)
            {
                int j = i % 4;
                outBuf.push_back(msg[pos + i] ^ masking_key_[j]);
            }
        }
    }
    else
    {
        return INCOMPLETE_FRAME;
    }

   printf("WEBSOCKET PROTOCOL\n"
           "FIN: %d\n"
           "OPCODE: %d\n"
           "MASK: %d\n"
           "PAYLOADLEN: %d\n"
           "outLen:%d\n",
           fin_, opcode_, mask_, payload_length_, *outLen);

    //断开连接类型数据包
    if ((int)opcode_ == 0x8)
        return -1;

    return 0;
}


int WebSocket::makeWSFrameData(char* msg, int msgLen, std::vector<char>& outBuf)
{
    std::vector<char> header;
    makeWSFrameDataHeader(msgLen, header);
    outBuf.insert(outBuf.begin(), header.begin(), header.end());
    outBuf.insert(outBuf.end(), msg, msg+msgLen);
    return 0;
}

int WebSocket::makeWSFrameDataHeader(int len, std::vector<char>& header)
{
    header.push_back((char)BINARY_FRAME);
    if(len <= 125)
    {
        header.push_back((char)len);
    }
    else if(len <= 65535)
    {
        header.push_back((char)126);//16 bit length follows
        header.push_back((char)((len >> 8) & 0xFF));// leftmost first
        header.push_back((char)(len & 0xFF));
    }
    else // >2^16-1 (65535)
    {
        header.push_back((char)127);//64 bit length follows

        // write 8 bytes length (significant first)
        // since msg_length is int it can be no longer than 4 bytes = 2^32-1
        // padd zeroes for the first 4 bytes
        for(int i=3; i>=0; i--)
        {
            header.push_back((char)0);
        }
        // write the actual 32bit msg_length in the next 4 bytes
        for(int i=3; i>=0; i--)
        {
            header.push_back((char)((len >> 8*i) & 0xFF));
        }
    }

    return 0;
}

void WebSocket::printWebsocketKey()
{
    std::cout << "websocketkey:" << m_websocketKey << std::endl;
}

void startWebSocket()
{
    WebSocket* ws = new WebSocket();
    ws->initServer(8888);
    ws->runServer();
}

int main()
{
    for (int i = 0; i < WORKER_THREAD; i++)
    {
        sleep(1);
        std::thread th(startWebSocket);
        th.detach();
    }

    // ws->parseHandshake(TestSTR_REQUEST);
    // ws->printWebsocketKey();
    // ws->respondHandshake();

    // delete ws;
    // ws = nullptr;

    while (true)
    {
        
    }
    
    return 0;
}