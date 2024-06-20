#ifndef _WEBSOCKET_H_
#define _WEBSOCKET_H_

#include <string>
#include <vector>
#include <unordered_map>

#define BUFFER_LENGTH   4096

enum
{
    ERROR_FRAME = 0xFF00,
    INCOMPLETE_FRAME = 0XFE00,

    OPENING_FRAME = 0X3300,
    CLOSING_FRAME = 0X3400,

    INCOMPLETE_TEXT_FRAME=0x01,
    INCOMPLETE_BINARY_FRAME=0x02,

    TEXT_FRAME=0x81,
    BINARY_FRAME=0x82,

    PING_FRAME=0x19,
    PONG_FRAME=0x1A
};

//定义几种状态(握手、传输、结束)
enum WSStatus
{
    WS_HANDSHARK = 0,
    WS_TRANMISSION = 1,
    WS_END = 2,
};

struct clientBase
{
    int socketfd;
    WSStatus status;
    char buffer[BUFFER_LENGTH];
};


class WebSocket
{
public:
    WebSocket();

    //初始化
    void initServer(const int port);
    //启动循环监听
    void runServer();
    //epoll事件
    int addEpollEvent(int socketfd, int events);
    int deleteEpollEvent(int socketfd, int events);
    //关闭连接
    void closeClient(int clientfd);
    //监听新连接
    void listenClient();
    //出来新消息
    void recvClient(int clientfd);

    //解析 WebSocket 的握手数据
    bool parseHandshake(const std::string& request);

    //应答 WebSocket 的握手
    std::string respondHandshake();

    //解析 WebSocket 的协议具体数据，客户端-->服务器
    int getWSFrameData(char* msg, int msgLen, std::vector<char>& outBuf, int* outLen);

    //封装 WebSocket 协议的数据，服务器-->客户端
    int makeWSFrameData(char* msg, int msgLen, std::vector<char>& outBuf);

    //封装 WebSocket 协议的数据头（二进制数据）
    static int makeWSFrameDataHeader(int len, std::vector<char>& header);

    //输出一下加密key
    void printWebsocketKey();

private:
    //握手中客户端发来的key
    std::string m_websocketKey;
    int m_listenfd;
    int m_epollfd;
    //客户端信息
    std::unordered_map<int, clientBase> m_hmClientBase;

};

#endif // _WEBSOCKET_H_