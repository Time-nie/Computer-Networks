#ifndef TCP_H
#define TCP_H

#include <iostream>
#include <string>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <time.h>
#include <deque>
#include <list>
#include <utility>
using namespace std;

#define CONNECT_RECV_TIMEOUT 4000
#define SLEEP_TIME 100


// 数据报
typedef struct recv_pkg {
	size_t size;
	bool isend;
	const unsigned char* buf;
} recv_pkg;

class UDP {
private:
	SOCKET sock;
	struct sockaddr* addr;       // 目的地址
	struct sockaddr* local_addr; // 源地址
	string host; // 主机IP
	unsigned short port;       // 目的端口
	unsigned short local_port; // 源端口
	bool isconnect; // 是否连接标识

	CRITICAL_SECTION sendbuf_lock; // 临界缓冲区，保存多个线程的共享资源，防止冲突
	CRITICAL_SECTION recvbuf_lock;
	deque<recv_pkg> recvbuf;    // 接收缓冲区
	list<string> sendbuf;       // 发送缓冲区

	const size_t bufsize; // 缓冲区大小
	size_t ack; // ACK：确认序列号
	size_t seq; // SEQ：发送序列号
	unsigned short window_size;               // 窗口大小
	unsigned short max_window_size;
	const unsigned short window_size_default; // 默认窗口大小
	size_t max_send_size;               // MSS：最大段长度
	const size_t max_send_size_default; // 默认最大段长度
	const size_t head_length = 24; // 协议头长度：24字节
	const size_t autoclose_tcp_loop = 10; // 10次丢失，通信异常，自动断开

	bool immsend = false;
	bool re = false;
	bool rere = true;

	// 发送和接收
	int sendmeg(const string& data, unsigned char flag, size_t* seq_spec = nullptr);
	int recvmeg(unsigned char* buf, size_t buf_size, int timeout = -1);

	// 发送线程和接收线程
	friend DWORD WINAPI Send_Thread(LPVOID lpParam);
	friend DWORD WINAPI Recv_Thread(LPVOID lpParam);
	HANDLE Send = NULL;
	HANDLE Recv = NULL;
	void reset();

	// 封装协议头
	bool generate_meg_head(unsigned char* message, size_t length, unsigned char flag, size_t* seq_spec = nullptr);

	// set flag 设置协议头标志
	void set_flag_cwr(unsigned char* flag, bool value) { value ? *flag |= (unsigned char)1 << 7 : *flag &= ~((unsigned char)1 << 7); }
	void set_flag_ece(unsigned char* flag, bool value) { value ? *flag |= (unsigned char)1 << 6 : *flag &= ~((unsigned char)1 << 6); }
	void set_flag_over(unsigned char* flag, bool value) { value ? *flag |= (unsigned char)1 << 5 : *flag &= ~((unsigned char)1 << 5); }
	void set_flag_ack(unsigned char* flag, bool value) { value ? *flag |= (unsigned char)1 << 4 : *flag &= ~((unsigned char)1 << 4); }
	void set_flag_end(unsigned char* flag, bool value) { value ? *flag |= (unsigned char)1 << 3 : *flag &= ~((unsigned char)1 << 3); }
	void set_flag_rst(unsigned char* flag, bool value) { value ? *flag |= (unsigned char)1 << 2 : *flag &= ~((unsigned char)1 << 2); }
	void set_flag_syn(unsigned char* flag, bool value) { value ? *flag |= (unsigned char)1 << 1 : *flag &= ~((unsigned char)1 << 1); }
	void set_flag_fin(unsigned char* flag, bool value) { value ? *flag |= (unsigned char)1 : *flag &= ~((unsigned char)1); }

	// get flag 获得协议头标志
	bool get_flag_cwr(unsigned char flag) { return flag & (unsigned char)1 << 7; }
	bool get_flag_ece(unsigned char flag) { return flag & (unsigned char)1 << 6; }
	bool get_flag_over(unsigned char flag) { return flag & (unsigned char)1 << 5; }
	bool get_flag_ack(unsigned char flag) { return flag & (unsigned char)1 << 4; }
	bool get_flag_end(unsigned char flag) { return flag & (unsigned char)1 << 3; }
	bool get_flag_rst(unsigned char flag) { return flag & (unsigned char)1 << 2; }
	bool get_flag_syn(unsigned char flag) { return flag & (unsigned char)1 << 1; }
	bool get_flag_fin(unsigned char flag) { return flag & (unsigned char)1; }

	// 获得协议头信息
	unsigned short get_send_port(const unsigned char* message) { return (unsigned short)message[0] << 8 | (unsigned short)message[1]; }
	unsigned short get_recv_port(const unsigned char* message) { return (unsigned short)message[2] << 8 | (unsigned short)message[3]; }
	unsigned int get_seq(const unsigned char* message) { return (unsigned int)message[4] << 24 | (unsigned int)message[5] << 16 | (unsigned int)message[6] << 8 | (unsigned int)message[7]; }
	unsigned int get_ack(const unsigned char* message) { return (unsigned int)message[8] << 24 | (unsigned int)message[9] << 16 | (unsigned int)message[10] << 8 | (unsigned int)message[11]; }
	size_t get_head_length(const unsigned char* message) { return (size_t)message[12] >> 2; }
	unsigned char get_flag(const unsigned char* message) { return message[13]; }
	unsigned short get_window_size(const unsigned char* message) { return (unsigned short)message[14] << 8 | (unsigned short)message[15]; }
	size_t get_max_send_size(const unsigned char* message) { return (size_t)message[20] << 24 | (size_t)message[21] << 16 | (size_t)message[22] << 8 | (size_t)message[23]; }
	bool check_message(unsigned char* message, size_t length);
	unsigned short get_checksum(const unsigned char* message) { return (unsigned short)message[16] << 8 | (unsigned short)message[17]; };

public:
	UDP(const char* host, unsigned short port = 4000, unsigned short local_port = 4000, size_t mss = 2048, size_t bufsize = 20480, unsigned short window_size = 20);
	~UDP();
	bool connect(); // 客户端建立连接
	bool accept(); // 服务器端建立连接
	bool send(string data); // 将数据放入发送缓冲区
	string recv(); // 从接收缓冲区读取数据
	void close();  // 当发送缓冲区为空时关闭线程
	bool isConnect() { return this->isconnect; }
	int getrecvsize() { return this->bufsize; }
};

#endif