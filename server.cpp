/**
 * 对于websocket来说，它必须依赖http进行一次握手，握手成功之后，数据就直接从tcp通道传输了，
 * 与http无关了
 * 握手过程：
 * 1、客户端发送一个请求给服务器
 * 2、websocket把从中的key（Sec-WebSocket-Key）解析出来，解析出来之后与GUID（固定值）做一个拼接，然后通过sha1加密算法加密，然后通过base64编码，得到sec_accept
 * 3、服务器把sec_accept发送给客户端
 * 4、客户端收到sec_accept之后，与Sec-WebSocket-Key做拼接，然后通过sha1加密算法加密，然后通过base64编码，得到sec_accept进行校验
 * 5、如果校验成功，握手成功，进入Transmission状态，客户端和服务器就可以进行数据传输了
 * 
 * 需要依赖openssl库
 * 坑点：编译需要加 -lcrypto 参数
 * g++ -o server server.cpp -lcrypto
*/
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <string>
#include <fcntl.h>

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/evp.h>


using namespace std;

#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WEBSOCK_KEY_LENGTH  19
#define BUFFER_LENGTH   4096
#define TestSTR_REQUEST "GET /ws/chat HTTP/1.1\r\nHost: server.example.com\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n"

int base64_encode(char *in_str, int in_len, char *out_str) 
{    
  BIO *b64, *bio;    
  BUF_MEM *bptr = NULL;    
  size_t size = 0;    

  if (in_str == NULL || out_str == NULL)        
    return -1;    

  b64 = BIO_new(BIO_f_base64());    
  bio = BIO_new(BIO_s_mem());    
  bio = BIO_push(b64, bio);
  
  BIO_write(bio, in_str, in_len);    
  BIO_flush(bio);    

  BIO_get_mem_ptr(bio, &bptr);    
  memcpy(out_str, bptr->data, bptr->length);    
  out_str[bptr->length-1] = '\0';    
  size = bptr->length;    

  BIO_free_all(bio);    
  return size;
}

//定义几种状态(握手、传输、结束)
enum 
{
  WS_HANDSHARK = 0,
  WS_TRANMISSION = 1,
  WS_END = 2,
};

struct ntyevent
{
    int fd;
    int events;
    void (*callback)(int fd, int events, void *arg);
    void *arg;
    int status;
    char* buffer;
    int length;
    //状态
    int status_machine;
};

//根据\r\n结束符截取数据
int readline(char *allbuf, int idx, char *linebuf) 
{
  int len = strlen(allbuf);

  for( ; idx < len; idx++) 
  {
    if (allbuf[idx] == '\r' && allbuf[idx+1] == '\n') 
    {
      return idx+2;
    } 
    else 
    {
      *(linebuf++) = allbuf[idx];
    }
  }

  return -1;
}

//解析握手包
int handshark(struct ntyevent *ev)
{
    if (ev == nullptr)
    {
        return 0;
    }
    
    char linebuff[1024] = {0};
    int idx = 0;
    char sec_data[128] = {0};
    char sec_accept[32] = {0};

    do
    {
        //根据\r\n循环截取数据
        memset(linebuff, 0, sizeof(linebuff));
        idx = readline(ev->buffer, idx, linebuff);
        cout << "读取请求数据:" << linebuff << " 索引:" << idx << endl;
        //首次握手 找到key字段Sec-WebSocket-Key,设置大小写不敏感
        if (strcasestr(linebuff, "Sec-WebSocket-Key"))
        {
            //拼接GUID
            //linebuf: Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
            cout << "解析出key:" << linebuff << endl;
            strcat(linebuff, GUID);
            //linebuf: 
            //Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11
            //sha1加密
            SHA1(reinterpret_cast<const unsigned char*>(linebuff + WEBSOCK_KEY_LENGTH), strlen(linebuff + WEBSOCK_KEY_LENGTH), reinterpret_cast<unsigned char*>(sec_data)); // openssl

            //base64转码
            base64_encode(sec_data, strlen(sec_data), sec_accept);
            cout << "加密转码之后的数据:" << sec_accept << endl;

            memset(ev->buffer, 0, sizeof(   ev->buffer));
            //响应头
            ev->length = sprintf(ev->buffer, "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n\r\n",sec_accept);

            cout << "回应客户端:" << ev->buffer << endl;
            break;
        }

    } while ((ev->buffer[idx] != '\r' || ev->buffer[idx+1] != '\n') && idx != -1 );
    
    return 0;
}

int main()
{
    ntyevent ev;
    string req = TestSTR_REQUEST;

    ev.buffer = const_cast<char *>(req.c_str());
    ev.length = strlen(ev.buffer);
    handshark(&ev);
    
    return 0;
}