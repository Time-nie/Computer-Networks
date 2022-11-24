#include "UDP.h"
#include <iostream>
#include <string>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <time.h>
#include <deque>
#include <utility>
#include <cmath>
#pragma comment (lib, "ws2_32.lib")

using namespace std;

// Socket发送信息
int UDP::sendmeg(const string& data, unsigned char flag, int* seq_spec) {
	// 打包数据报，将数据copy到message缓冲区，并封装协议头，长度为：head_length + data.length()
	unsigned char* meg_buf = new unsigned char[this->head_length + data.length()];
	// 填充数据段
	memcpy(meg_buf + this->head_length, data.data(), data.length());
	// 封装协议头
	generate_meg_head(meg_buf, (int)(this->head_length + data.length()), flag, seq_spec);

	// 发送数据报
	// 通过sendto函数向特定的目的地发送数据 this->addr为目的地址
	int result = sendto(this->sock, (const char*)meg_buf, (int)(this->head_length + data.length()), 0, this->addr, sizeof(sockaddr));
	delete[] meg_buf;
	return result;
}

// Socket接收信息
int UDP::recvmeg(unsigned char* buf, int buf_size, int timeout) 
{
	// addr_length源地址长度
	int addr_length = sizeof(sockaddr);
	// 设置接收时限
	setsockopt(this->sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	// 接收数据报
	// 函数recvfrom（socket描述符，接收数据的缓冲区，接收缓冲区的长度，flags对调用方式的处理，源socket地址，源地址长度）从特定的目的地接收数据，返回值为接收到的字节数
	int len = recvfrom(this->sock, (char*)buf, buf_size, 0, (sockaddr*)(this->local_addr), &addr_length);
	// 差错检测
	if (len != -1 && !check_message(buf, len)) 
		len = -1;
	return len;
}

// 重置
void UDP::reset() {
	this->MSS = this->MSS_default;
	this->window_size = this->window_size_default;
	this->ack = 0;
	this->seq = 0;
}

//接收线程
DWORD WINAPI Recv_thread(LPVOID s) {
	UDP* cls = (UDP*)s;
	unsigned char* buf = new unsigned char[cls->MSS + cls->head_length];
	int timeout_round = 0;
	while (cls->isconnect) {
		// 超时重传：当超过时限CONNECT_RECV_TIMEOUT，则设置immsend重传分组
		// lenth包括协议头长度+数据段长度
		int length = cls->recvmeg(buf, cls->MSS + cls->head_length, CONNECT_RECV_TIMEOUT);
		// 10次丢失，通信异常，自动断开
		if (length == -1) {
			timeout_round++;
			if (timeout_round >= cls->autoclose_tcp_loop) break;
			Sleep(0);
		}
		else if (length >= cls->head_length) {
			// 重新设置丢失次数
			timeout_round = 0;
			// FIN标志置位，断开连接
			if (cls->get_flag_fin(cls->get_flag(buf))) {
				cout << "Closing..." << endl;
				break;
			}
			EnterCriticalSection(&(cls->sendbuf_lock));
			// 接收的ACK与接收线程期待的序列号SEQ不相等
			// 当重复ACK时，标记立即重传
			if (cls->seq != cls->get_ack(buf)) {

				// cout << "ACK!" << cls->get_ack(buf) << " " << cls->seq << endl;
				cls->sendbuf.begin()->assign(*(cls->sendbuf.begin()), cls->get_ack(buf) - cls->seq);
				if (cls->sendbuf.begin()->length() == 0) 
					cls->sendbuf.pop_front();
				cls->seq = cls->get_ack(buf);
				cls->immsend = true;
			}
			LeaveCriticalSection(&(cls->sendbuf_lock));

			// 处理接收数据
			if (length > cls->head_length) {
				if (cls->ack == cls->get_seq(buf)) {
					if (cls->recvbuf.max_size() > cls->recvbuf.size()) {
						// 拆包：去掉数据报头
						unsigned char* temp = new unsigned char[length - cls->head_length];
						memcpy(temp, buf + cls->head_length, length - cls->head_length);
						// 将数据放入接收缓冲区（双端队列尾部增加数据）
						cls->recvbuf.push_back({ length - cls->head_length, cls->get_flag_end(cls->get_flag(buf)), temp });
						cls->ack = cls->get_seq(buf) + length - cls->head_length;
					}
				}
				cout << "Recv: " << length - cls->head_length << " [SEQ] " << cls->seq << " [ACK] " << cls->ack << " [checksum] " << cls->get_checksum(buf) << endl;
				cls->immsend = true;
			}
		}
	}

	// 四次挥手断开连接
	unsigned char flag = 0;
	if (cls->isconnect) { // 接收端主动断开连接
		cls->isconnect = false;
		cls->set_flag_end(&flag, true);
		cls->set_flag_fin(&flag, true);
		cls->set_flag_ack(&flag, true);
		cls->sendmeg("", flag);
		cout << "四次挥手: [ACK] -> [FIN]";
		cls->recvmeg(buf, cls->MSS + cls->head_length, CONNECT_RECV_TIMEOUT);
		cout << " -> [ACK]" << endl;
	}
	else { // 发送端
		cls->set_flag_end(&flag, true);
		cls->set_flag_fin(&flag, true);
		cls->set_flag_ack(&flag, false);
		cout << "Close: [FIN]";
		cls->sendmeg("", flag);

		cls->recvmeg(buf, cls->MSS + cls->head_length, CONNECT_RECV_TIMEOUT);
		cout << " -> [ACK] -> [FIN]";

		flag = 0;
		cls->set_flag_end(&flag, true);
		cls->set_flag_fin(&flag, true);
		cls->set_flag_ack(&flag, true);
		cls->sendmeg("", flag);
		cout << " -> [ACK]" << endl;
	}
	delete[] buf;
	for (auto& i : cls->recvbuf) delete[] i.buf;
	cls->recvbuf.clear();

	if (timeout_round >= cls->autoclose_tcp_loop) cout << "Time out!" << endl;
	cout << "断开连接!" << endl;
	return 0;
}

//发送线程
// 在发送线程中，当发送缓冲区不为空时，读取缓冲区数据打包数据报并发送，设置 END 标识来标记是否为最后一个数据报，打印相应的序列号信息。
DWORD WINAPI Send_thread(LPVOID s) {
	UDP* cls = (UDP*)s;
	unsigned char flag;
	unsigned long long last_stamp = GetTickCount64(); // 计时开始
	while (cls->isconnect) {
		if (!cls->immsend && GetTickCount64() - last_stamp < CONNECT_RECV_TIMEOUT * 0.5) 
		{
			Sleep(0);
			continue;
		}
		last_stamp = GetTickCount64();
		cls->immsend = false;
		flag = 0;
		cls->set_flag_end(&flag, true); // 初始化为最后一个数据报
		cls->set_flag_ack(&flag, true); // ACK有效
		string sendcontent;
		// send
		//加锁 接下来的代码处理过程中不允许其他线程进行操作，除非遇到LeaveCriticalSection
		EnterCriticalSection(&(cls->sendbuf_lock));
		int seq_temp = cls->seq; // 发送seq
		if (cls->sendbuf.size()) { // 发送缓冲区不为空时
			string& sendpkg = *(cls->sendbuf).begin();
			// 发送缓冲区 > 数据报大小（MSS * window_size)，只读取数据报大小的数据并标记非最后一个数据报
			// 否则直接读取全部发送缓冲区内容并标识为最后一个数据报
			if (sendpkg.length() > cls->MSS * cls->window_size) {
				sendcontent.assign(sendpkg, 0, cls->MSS * cls->window_size);
				cls->set_flag_end(&flag, false);
			}
			else sendcontent = sendpkg;
		}
		//解锁 到EnterCriticalSection之间代码资源已经释放了，其他线程可以进行操作
		LeaveCriticalSection(&(cls->sendbuf_lock));
		if (sendcontent.length() == 0) {
			cls->sendmeg(sendcontent, flag);
		}
		else {
			unsigned char flag_copy = flag;
			for (int i = 0; i < sendcontent.length(); i += cls->MSS) {
				flag = flag_copy;
				if (i + cls->MSS < sendcontent.length()) 
					cls->set_flag_end(&flag, false);
				cls->sendmeg(sendcontent.substr(i, ((i + cls->MSS) >= sendcontent.length() ? sendcontent.length() - i : cls->MSS)), flag, &seq_temp);
				// 每次seq+mss表示发送序列号
				seq_temp += cls->MSS; 
				cout << "Send: " << sendcontent.length() << " [SEQ] " << seq_temp << " [ACK] " << cls->ack << endl;
			}
		}
		Sleep(0);
	}
	cls->sendbuf.clear();
	return 0;
}

// 封装协议头
bool UDP::generate_meg_head(unsigned char* message, int length, unsigned char flag, int* seq_spec) {
	if (length < this->head_length) 
		return false;

	// 源端口
	message[0] = (unsigned char)(this->local_port >> 8);
	message[1] = (unsigned char)this->local_port;
	// 目的端口
	message[2] = (unsigned char)(this->port >> 8);
	message[3] = (unsigned char)this->port;

	// seq序列号
	if (seq_spec) {
		message[4] = (unsigned char)((*seq_spec) >> 24);
		message[5] = (unsigned char)((*seq_spec) >> 16);
		message[6] = (unsigned char)((*seq_spec) >> 8);
		message[7] = (unsigned char)(*seq_spec);
	}
	else {
		message[4] = (unsigned char)(this->seq >> 24);
		message[5] = (unsigned char)(this->seq >> 16);
		message[6] = (unsigned char)(this->seq >> 8);
		message[7] = (unsigned char)(this->seq);
	}

	// ACK确认序列号
	message[8] = (unsigned char)(ack >> 24);
	message[9] = (unsigned char)(ack >> 16);
	message[10] = (unsigned char)(ack >> 8);
	message[11] = (unsigned char)ack;

	// head_length 8 | flag 8
	// 协议头长度head_length为24
	message[12] = this->head_length << 2;
	message[13] = flag;

	// 窗口大小
	message[14] = (unsigned char)(window_size >> 8);
	message[15] = (unsigned char)window_size;

	// 校验和：初始化为0
	message[16] = 0;
	message[17] = 0;

	// MSS最大段长度
	message[20] = (unsigned char)(MSS >> 24);
	message[21] = (unsigned char)(MSS >> 16);
	message[22] = (unsigned char)(MSS >> 8);
	message[23] = (unsigned char)MSS;

	// 生成校验和：所有数据2字节求和取反，不足2字节补零
	unsigned short val = 0;
	for (int i = 0; i < length / 2; i++) 
		val += (unsigned short)message[i * 2] << 8 | (unsigned short)message[i * 2 + 1];
	if (length % 2) val += (unsigned short)message[length - 1] << 8;
	val = ~val;

	// 存入校验和
	message[16] = (unsigned char)(val >> 8);
	message[17] = (unsigned char)val;
	return true;
}

// 差错检测
bool UDP::check_message(unsigned char* message, int length) {
	unsigned short val = 0;
	// 所有数据2字节求和
	for (int i = 0; i < length / 2; i++) 
		val += (unsigned short)message[i * 2] << 8 | (unsigned short)message[i * 2 + 1];
	if (length % 2) val += (unsigned short)message[length - 1] << 8;
	// 对接收的数据报的 16bits 数组进行求和，如果结果全 1，则数据报正确；否则数据报存在错误。
	return !(unsigned short)(val + 1);
}

UDP::UDP(const char* host, unsigned short port, unsigned short local_port, int mss, int bufsize, unsigned short window_size) :
	MSS_default(mss), local_port(local_port), host(host), port(port), isconnect(false), window_size(window_size), window_size_default(window_size),
	MSS(mss), recvbuf(deque<recv_pkg>(ceil(bufsize / (float)mss))), bufsize(bufsize) {
	// 创建Socket
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	WSAStartup(wVersionRequested, &wsaData);
	//ipv4的地址类型；数据报的服务类型；Protocol（协议）为UDP
	this->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  

	// 目的地址
	sockaddr_in* temp_addr = new sockaddr_in;
	temp_addr->sin_family = AF_INET;
	temp_addr->sin_port = htons(port);
	inet_pton(AF_INET, host, &(temp_addr->sin_addr.s_addr));
	this->addr = (sockaddr*)temp_addr;

	// 源地址
	temp_addr = new sockaddr_in;
	temp_addr->sin_family = AF_INET;
	temp_addr->sin_port = htons(local_port);
	inet_pton(AF_INET, "127.0.0.1", &(temp_addr->sin_addr.s_addr));
	this->local_addr = (sockaddr*)temp_addr;

	// 服务器端将本地地址绑定到一个Socket
	bind(this->sock, this->local_addr, sizeof(sockaddr));

	// 初始化锁
	InitializeCriticalSection(&(this->sendbuf_lock));
}

UDP::~UDP() {
	close();
	closesocket(sock);
	WSACleanup();
	for (auto& i : recvbuf) delete[] i.buf;
	delete addr;
	delete local_addr;
}

// 当发送缓冲区为空时关闭线程
void UDP::close() {
	if (!this->isconnect) return;
	while (this->sendbuf.size()) {
		Sleep(0);
	}
	this->isconnect = false;
	WaitForSingleObject(this->tcp_runner_send, INFINITE);
	WaitForSingleObject(this->tcp_runner_recv, INFINITE);
	CloseHandle(this->tcp_runner_send);
	CloseHandle(this->tcp_runner_recv);
	for (auto& i : recvbuf) delete[] i.buf;
	this->sendbuf.clear();
	this->recvbuf.clear();
	reset();
}

// 建立连接——客户端
bool UDP::connect() {
	if (this->isconnect) return true;
	if (this->tcp_runner_recv) WaitForSingleObject(this->tcp_runner_recv, INFINITE);
	if (this->tcp_runner_send) WaitForSingleObject(this->tcp_runner_send, INFINITE);

	cout << "正在连接..." << endl;
	cout << endl;
	cout << "=================================================" << endl;
	reset();
	unsigned char flag = 0;
	set_flag_syn(&flag, true); // 置位SYN握手信号
	if (sendmeg("", flag) == -1) // 发送消息失败，return false
		return false; 
	cout << "[SYN]" << endl; // 握手信号提示

	unsigned char* buf = new unsigned char[this->head_length];
	int length = recvmeg(buf, this->head_length, CONNECT_RECV_TIMEOUT);

	if (length == -1 || length < this->head_length || !get_flag_syn(get_flag(buf))|| !get_flag_ack(get_flag(buf))) {
		cout << " [SYNACK] WRONG!" << endl;
		delete[] buf;
		return false;
	}
	cout << "[SYN & ACK] -> " << " [SEQ] " << get_seq(buf) << " [ACK] " << get_ack(buf) << " [SYN_FLAG] " << get_flag_syn(get_flag(buf)) << " [ACK_FLAG] " << get_flag_ack(get_flag(buf)) << endl;
	// 设置MSS和窗口大小
	this->MSS = get_MSS(buf);
	this->window_size = get_window_size(buf);
	recvbuf.resize(ceil(this->bufsize / (float)(this->MSS)));
	this->seq = get_ack(buf);
	this->ack = get_seq(buf) + 1;
	delete[] buf;

	set_flag_syn(&flag, false); 
	set_flag_ack(&flag, true);

	if (sendmeg("", flag) == -1) {
		cout << "[ACK] WRONG!" << endl;
		return false;
	}
	cout << "[ACK] " << endl;
	cout<<"连接成功！" << endl;
	cout << "=================================================" << endl;
	cout << endl;
	this->isconnect = true; // 建立连接成功，建连标志置位
	// 接收线程和发送线程
	this->tcp_runner_send = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Send_thread, (LPVOID)this, 0, 0);
	this->tcp_runner_recv = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Recv_thread, (LPVOID)this, 0, 0);
	return true;
}

//建立连接——服务器端
bool UDP::accept() {
	if (this->isconnect) return true;
	if (this->tcp_runner_send) WaitForSingleObject(this->tcp_runner_send, INFINITE);
	if (this->tcp_runner_recv) WaitForSingleObject(this->tcp_runner_recv, INFINITE);

	unsigned char* buf = new unsigned char[this->head_length];    //head_length代表协议头长度（24字节）
	int length = -1;
	unsigned char flag = 0;
	while (true) {
		reset(); //重置参数
		flag = 0;
		cout << "等待连接..." << endl;
		cout << endl;
		cout << "=================================================" << endl;
		
		length = recvmeg(buf, this->head_length);
		// 检测SYN
		if (length == -1 || length < this->head_length || !get_flag_syn(get_flag(buf))) 
			continue;

		// 设置MSS
		if (get_MSS(buf) < this->MSS) {
			this->MSS = get_MSS(buf);
			// 当MSS改变时，同时需要resize改变deque双端队列中元素个数
			recvbuf.resize(ceil(this->bufsize / (float)(this->MSS)));
		}
		// 协商窗口大小
		if (get_window_size(buf) < this->window_size) {
			this->window_size = get_window_size(buf);
		}

		// 三次握手
		cout << "[SYN] -> " <<  " [SEQ] " << get_seq(buf) << " [ACK] " << get_ack(buf) << " [SYN_FLAG] " << get_flag_syn(get_flag(buf)) << " [ACK_FLAG] " << get_flag_ack(get_flag(buf)) << endl;

		// ack=seq+1
		this->ack = get_seq(buf) + 1;

		// 设置SYN|ACK建连标志
		set_flag_syn(&flag, true);
		set_flag_ack(&flag, true);
		if (sendmeg("", flag) == -1) {
			cout << "[SYN & ACK] WRONG!" << endl;
			continue;
		}
		cout << "[SYN & ACK] " << endl;

		length = recvmeg(buf, this->head_length, CONNECT_RECV_TIMEOUT);
		flag = get_flag(buf);
		
		
		if (length == -1 || length < this->head_length || !get_flag_ack(get_flag(buf)) || get_ack(buf) != this->seq + 1 || get_seq(buf) != this->ack)
		{
			cout << "[ACK] WRONG!" << endl;
			continue;
		}
		this->seq = get_ack(buf);
		cout << "[ACK] -> " << "[SEQ] " << get_seq(buf) << "[ACK] " << get_ack(buf) << "[SYN_FLAG] " << get_flag_syn(get_flag(buf)) << "[ACK_FLAG] " << get_flag_ack(get_flag(buf)) << endl;
		cout << "连接成功！" << endl;
		cout << "=================================================" << endl;
		cout << endl;
		break;
	}
	this->isconnect = true;
	cout.flush();
	delete[] buf;
	// 接收线程
	this->tcp_runner_send = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Send_thread, (LPVOID)this, 0, 0);
	// 发送线程
	this->tcp_runner_recv = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Recv_thread, (LPVOID)this, 0, 0);
	return true;
}

// 将数据放入发送缓冲区
bool UDP::send(string data) {
	if (!this->isconnect) 
		return false;
	// sendbuf 类型为 list<string>
	this->sendbuf.push_back(data);
	return true;
}

// 从接收缓冲区读取数据
string UDP::recv() {
	string res;
	while (this->isconnect) {
		if (this->recvbuf.size() == 0) {
			Sleep(0);
			continue;
		}
		bool isend = false;
		while (!isend && this->recvbuf.size()) {
			auto buf = *(this->recvbuf.begin());
			isend = buf.isend;
			res += string((const char*)buf.buf, buf.size);
			delete[] buf.buf;
			// 删除双端队列buf中最前一个元素
			this->recvbuf.pop_front();
		}
		if (isend) break;
	}
	return res;
}