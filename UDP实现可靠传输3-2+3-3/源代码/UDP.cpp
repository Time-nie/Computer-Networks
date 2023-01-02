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
#include <vector>
#include <ctime>


#pragma comment (lib, "ws2_32.lib")

using namespace std;
int book = 0;
size_t seq_temp = 1;
bool time_flag = 0, re =0;
// Socket发送信息
int UDP::sendmeg(const string& data, unsigned char flag, size_t* seq_spec) {
	// 打包数据报，将数据copy到message缓冲区，并封装协议头
	unsigned char* meg_buf = new unsigned char[this->head_length + data.length()];
	memcpy(meg_buf + this->head_length, data.data(), data.length());
	generate_meg_head(meg_buf, (size_t)(this->head_length + data.length()), flag, seq_spec);

	// 发送数据报
	int result = sendto(this->sock, (const char*)meg_buf, (size_t)(this->head_length + data.length()), 0, this->addr, sizeof(sockaddr));
	delete[] meg_buf;
	return result;
}


int UDP::recvmeg(unsigned char* buf, size_t buf_size, int timeout) {
	int addr_length = sizeof(sockaddr);
	// 设置接收时限
	int tim = setsockopt(this->sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	// 接收数据报
	int result = recvfrom(this->sock, (char*)buf, buf_size, 0, (sockaddr*)(this->local_addr), &addr_length);
	// 差错检测
	if (result != -1 && !check_message(buf, result)&& tim == -1) result = -1;
	return result;
}

// 重置
void UDP::reset() {
	this->max_send_size = this->max_send_size_default;
	this->window_size = this->window_size_default;
	this->ack = 0;
	this->seq = 0;
}

//接收线程
DWORD WINAPI Recv_Thread(LPVOID s) {
	//cout << "进入接收线程" << endl;
	UDP* cls = (UDP*)s;
	unsigned char* buf = new unsigned char[cls->max_send_size + cls->head_length];
	size_t timeout_round = 0; // 超时次数
	size_t block_num = 0; // 重复ack次数
	size_t slow_num = 0; // 拥塞避免
	while (cls->isconnect) {
		size_t length = cls->recvmeg(buf, cls->max_send_size + cls->head_length, CONNECT_RECV_TIMEOUT);
		if (length == -1) {
			// 超时检测：阈值SST减为窗口的一半，cwnd=1,进入慢启动阶段
			timeout_round++;  //超时次数记录
			slow_num = 0;     // 拥塞避免阶段计数（用作窗口大小变化）
			block_num = 0;   //计算重复ACK次数（满三次进入快速恢复阶段）
			cls->max_window_size = cls->window_size / 2;
			cls->window_size = 1;
			cout << "Time Out!" << endl;
			// 10次丢失，通信异常，自动断开
			if (timeout_round >= cls->autoclose_tcp_loop) 
				break;
			Sleep(0);
		}
		else if (length >= cls->head_length) {
			timeout_round = 0;  //重新记录超时次数

			// FIN标志置位，断开连接
			if (cls->get_flag_fin(cls->get_flag(buf))) {
				cout << "Closing..." << endl;
				break;
			}
			
			EnterCriticalSection(&(cls->sendbuf_lock));
			// 新ACK
			if (cls->seq != cls->get_ack(buf)) {  
				if (block_num >= 3)  //快速恢复 -> 拥塞避免
				{
					cout << "【快速恢复 -> 拥塞避免】" << endl;
					cls->window_size = cls->max_window_size;
				}
				else if (cls->window_size < cls->max_window_size)  // 慢启动阶段
					cls->window_size++;
				else {
					// 拥塞避免阶段：拥塞窗口达到阈值时，进入拥塞避免阶段
					slow_num++;
					if (slow_num >= cls->window_size) {
						cls->window_size += 1;
						slow_num = 0;
					}
				}
				block_num = 0;   //重复ACK计数清0
				// 发送缓冲区begin指针后移
				cls->sendbuf.begin()->assign(*(cls->sendbuf.begin()), cls->get_ack(buf) - cls->seq);

				// 当发送缓冲区中已经发完一个完整文件后，将该文件整个剔除
				if (cls->sendbuf.begin()->length() == 0) 
					cls->sendbuf.pop_front();
				// 窗口移动
				cls->seq = cls->get_ack(buf);

				// 继续发送下一个报文分组
				cls->immsend = true;
			}
			//重复ACK
			else {   
				if (length <= cls->head_length)
				{
					// 三次重复ACK检测丢失：阈值减为拥塞窗口的一半，cwnd=SST+3，进入线性增长（拥塞避免阶段）
					block_num++;
					// 三次重复ACK，准备进入快速恢复
					if (block_num == 3) {
						slow_num = 0;
						cls->max_window_size = cls->window_size / 2;
						cls->window_size = cls->max_window_size + 3;
						seq_temp = min(cls->seq + cls->window_size*cls->max_send_size, seq_temp);
						cout << "三次重复 ACK!" << endl;
						cls->re = 1;
					}
					// 快速恢复阶段
					else if (block_num > 3)
					{
						cls->re = 1;
						cls->window_size += 1;
					}
				}
			}
			LeaveCriticalSection(&(cls->sendbuf_lock));

			// 处理接收数据（接收端）
			if (length > cls->head_length) {
				if (cls->ack == cls->get_seq(buf)) {   //按序收到所需分组
					EnterCriticalSection(&(cls->recvbuf_lock));
					if (cls->recvbuf.max_size() > cls->recvbuf.size()) {
						// 拆包：去掉数据报头
						unsigned char* temp = new unsigned char[length - cls->head_length];
						memcpy(temp, buf + cls->head_length, length - cls->head_length);
						// 将数据放入接收缓冲区
						cls->recvbuf.push_back({ length - cls->head_length, cls->get_flag_end(cls->get_flag(buf)), temp });

						// 更新ACK
						// 如果接收到的是失序分组或者重复分组，则不更新ACK
						cls->ack = cls->get_seq(buf) + length - cls->head_length;
					}
					LeaveCriticalSection(&(cls->recvbuf_lock));
				}
				// 继续发送下一个报文分组
				cls->immsend = true;
				cout << "Recv: " << length - cls->head_length << " [ACK] " << cls->ack << " [checksum] " << cls->get_checksum(buf) << endl;
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
		cout << "Close: [ACK] -> [FIN]";
		cls->recvmeg(buf, cls->max_send_size + cls->head_length, CONNECT_RECV_TIMEOUT);
		cout << " -> [ACK]" << endl;
	}
	else { // 发送端
		cls->set_flag_end(&flag, true);
		cls->set_flag_fin(&flag, true);
		cls->set_flag_ack(&flag, false);
		cout << "Close: [FIN]";
		cls->sendmeg("", flag);

		cls->recvmeg(buf, cls->max_send_size + cls->head_length, CONNECT_RECV_TIMEOUT);
		cout << " -> [ACK] -> [FIN]";

		flag = 0;
		cls->set_flag_end(&flag, true);
		cls->set_flag_fin(&flag, true);
		cls->set_flag_ack(&flag, true);
		cls->sendmeg("", flag);
		cout << " -> [ACK]" << endl;
	}
	delete[] buf;
	EnterCriticalSection(&(cls->recvbuf_lock));
	for (auto& i : cls->recvbuf) delete[] i.buf;
	cls->recvbuf.clear();
	LeaveCriticalSection(&(cls->recvbuf_lock));

	if (timeout_round >= cls->autoclose_tcp_loop) 
		cout << "Time out!" << endl;
	cout << "Connect Interupt!" << endl;
	return 0;
}


//发送线程
DWORD WINAPI Send_Thread(LPVOID s) {
	//cout << "进入发送线程" << endl;
	UDP* cls = (UDP*)s;
	unsigned char flag;
	
	unsigned long long last_stamp = GetTickCount64(); // 计时开始
	while (cls->isconnect) 
	{
		
		if (!cls->immsend && GetTickCount64() - last_stamp < CONNECT_RECV_TIMEOUT) {
			Sleep(0);
			continue;
		}
		if (GetTickCount64() - last_stamp > CONNECT_RECV_TIMEOUT)
		{
			//cout << "已超时" << endl;
			time_flag = 1;
		}
		last_stamp = GetTickCount64();  //重新计时（超时/base移动且base!=nextseqnum/刚开始）
		cls->immsend = false;
		flag = 0;
		cls->set_flag_end(&flag, true); // 初始化为最后一个数据报
		cls->set_flag_ack(&flag, true); // ACK有效
		string sendcontent;   //发送内容
		EnterCriticalSection(&(cls->sendbuf_lock));  //加锁 接下来的代码处理过程中不允许其他线程进行操作，除非遇到LeaveCriticalSection
		if (cls->sendbuf.size())
		{
			string& sendpkg = *(cls->sendbuf).begin();
			int remain = cls->max_send_size * cls->window_size - (seq_temp - cls->seq);
			if (time_flag )
			{
				sendcontent.assign(sendpkg, 0, seq_temp - cls->seq);//重发
				seq_temp = cls->seq; // seq_temp代表nextseqnum
			}
			if (cls->re)
			{
				cls->re = 0;
				sendcontent.assign(sendpkg, 0, cls->max_send_size);   //快速重传
				seq_temp = cls->seq; // seq_temp代表nextseqnum
			}
			else 
			{
				if (remain){
					if (remain < sendpkg.length() - (seq_temp - cls->seq))
						sendcontent.assign(sendpkg, seq_temp - cls->seq, remain);
					else
						sendcontent = sendpkg;
				}
				else{
					cout << "窗口内满了" << endl;
					Sleep(0);
					continue;
				}
			}
		}
		LeaveCriticalSection(&(cls->sendbuf_lock));  //解锁
		
		// 仅发送协议头（接收端）
		if (sendcontent.length() == 0) {
			cls->sendmeg(sendcontent, flag);
		}
		else {  //发送数据（发送端）
			unsigned char flag_copy = flag;

			for (size_t i = 0; i < sendcontent.length(); i += cls->max_send_size) {
				flag = flag_copy;
				// 判断是否为最后一个报文分组
				if (i + cls->max_send_size < sendcontent.length()) 
					cls->set_flag_end(&flag, false);
				// 发送该报文分组
				int len = ((i + cls->max_send_size) >= sendcontent.length() ? sendcontent.length() - i : cls->max_send_size);
				if ((++book) % 10000000000 || time_flag == 1)  //设置丢包
					cls->sendmeg(sendcontent.substr(i, len), flag, &seq_temp);
				else
				{
					cout << "【该分组丢包】";
					//Sleep(8000);
					//cls->sendmeg(sendcontent.substr(i, len), flag, &seq_temp);
				}
					
				Sleep(0);
				// 改变可用还未发送位置
				seq_temp += len;
				cout << "Send: " << len << " [SEQ](Base) " << cls->seq << " [Nextseqnum] " << seq_temp << " [LimitWindow] " << cls->seq + cls->max_send_size * cls->window_size << " [SSThresh] " << cls->max_window_size << " [Window_Size] " << cls->window_size << endl;
			}
			time_flag = 0;
		}
		Sleep(0);
	}
	EnterCriticalSection(&(cls->sendbuf_lock));
	cls->sendbuf.clear();
	LeaveCriticalSection(&(cls->sendbuf_lock));
	return 0;
}

// 封装协议头
bool UDP::generate_meg_head(unsigned char* message, size_t length, unsigned char flag, size_t* seq_spec) {
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

	// head_length 4 | reserve 4 | flag 8
	message[12] = this->head_length << 2;
	message[13] = flag;

	// 窗口大小
	message[14] = (unsigned char)(window_size >> 8);
	message[15] = (unsigned char)window_size;

	// 校验和：初始化为0
	message[16] = 0;
	message[17] = 0;

	// MSS最大段长度
	message[20] = (unsigned char)(max_send_size >> 24);
	message[21] = (unsigned char)(max_send_size >> 16);
	message[22] = (unsigned char)(max_send_size >> 8);
	message[23] = (unsigned char)max_send_size;

	// 生成校验和：所有数据2字节求和取反，不足2字节补零
	unsigned short val = 0;
	for (size_t i = 0; i < length / 2; i++) val += (unsigned short)message[i * 2] << 8 | (unsigned short)message[i * 2 + 1];
	if (length % 2) val += (unsigned short)message[length - 1] << 8;
	val = ~val;

	// 存入校验和
	message[16] = (unsigned char)(val >> 8);
	message[17] = (unsigned char)val;
	return true;
}

// 差错检测
bool UDP::check_message(unsigned char* message, size_t length) {
	unsigned short val = 0;
	for (size_t i = 0; i < length / 2; i++) 
		val += (unsigned short)message[i * 2] << 8 | (unsigned short)message[i * 2 + 1];
	if (length % 2) 
		val += (unsigned short)message[length - 1] << 8;
	return !(unsigned short)(val + 1);
}

UDP::UDP(const char* host, unsigned short port, unsigned short local_port, size_t mss, size_t bufsize, unsigned short window_size) :
	max_send_size_default(mss), local_port(local_port), host(host), port(port), isconnect(false), window_size(window_size), window_size_default(window_size),max_window_size(window_size),
	max_send_size(mss), recvbuf(deque<recv_pkg>(ceil(bufsize / (float)mss))), bufsize(bufsize) {
	this->max_window_size = 10;
	// 创建Socket
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	WSAStartup(wVersionRequested, &wsaData);
	this->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// 目的地址
	sockaddr_in* temp_addr = new sockaddr_in;
	temp_addr->sin_family = AF_INET;
	temp_addr->sin_port = htons(port);
	inet_pton(AF_INET, host, &(temp_addr->sin_addr.s_addr));
	this->addr = (sockaddr*)temp_addr;

	// 源地址
	temp_addr = new sockaddr_in;
	temp_addr = new sockaddr_in;
	temp_addr->sin_family = AF_INET;
	temp_addr->sin_port = htons(local_port);
	inet_pton(AF_INET, "127.0.0.1", &(temp_addr->sin_addr.s_addr));
	this->local_addr = (sockaddr*)temp_addr;

	// 服务器端将本地地址绑定到一个Socket
	bind(this->sock, this->local_addr, sizeof(sockaddr));

	// 初始化锁
	InitializeCriticalSection(&(this->sendbuf_lock));
	InitializeCriticalSection(&(this->recvbuf_lock));
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
	if (!this->isconnect) 
		return;
	while (this->sendbuf.size()) {
		Sleep(0);
	}
	this->isconnect = false;
	WaitForSingleObject(this->Send, INFINITE);
	WaitForSingleObject(this->Recv, INFINITE);
	CloseHandle(this->Send);
	CloseHandle(this->Recv);
	for (auto& i : recvbuf) delete[] i.buf;
	this->sendbuf.clear();
	this->recvbuf.clear();
	reset();
}

// 建立连接——客户端
bool UDP::connect() {
	if (this->isconnect) return true;
	if (this->Recv) WaitForSingleObject(this->Recv, INFINITE);
	if (this->Send) WaitForSingleObject(this->Send, INFINITE);

	cout << "Connecting..." << endl;
	reset();
	unsigned char flag = 0;
	set_flag_syn(&flag, true); // 置位SYN握手信号
	if (sendmeg("", flag) == -1)  // 发送消息失败，return false
		return false; 
	cout << "三次握手 [SYN] -> "; // 握手信号提示

	unsigned char* buf = new unsigned char[this->head_length];
	size_t length = recvmeg(buf, this->head_length, CONNECT_RECV_TIMEOUT);
	if (length == -1 || length < this->head_length || !get_flag_syn(get_flag(buf))) {
		cout << " [SYNACK] WRONG!" << endl;
		delete[] buf;
		return false;
	}
	// 设置MSS和窗口大小
	this->max_send_size = get_max_send_size(buf);
	this->window_size = get_window_size(buf);

	recvbuf.resize(ceil(this->bufsize / (float)(this->max_send_size)));
	this->seq = get_ack(buf);
	this->ack = get_seq(buf) + 1;
	delete[] buf;
	cout << "[SYNACK] -> ";

	if (sendmeg("", 0) == -1) {
		cout << "[ACK] WRONG!" << endl;
		return false;
	}
	cout << "[ACK] -> Connected!" << endl;
	cout << " [SEQ] " << this->seq << " [ACK] " << this->ack << " [MSS] " << this->max_send_size << " [WSZ] " << this->window_size << endl;
	cout << "====================三次握手成功=====================" << endl;
	this->isconnect = true; // 建立连接成功，建连标志置位
	this->window_size = 1; // 连接初始建立，进入慢启动阶段，初始化拥塞窗口cwnd=1
	// 接收线程和发送线程
	this->Send = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Send_Thread, (LPVOID)this, 0, 0);
	this->Recv = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Recv_Thread, (LPVOID)this, 0, 0);
	return true;
}

//建立连接——服务器端
bool UDP::accept() {
	if (this->isconnect) return true;
	if (this->Send) WaitForSingleObject(this->Send, INFINITE);
	if (this->Recv) WaitForSingleObject(this->Recv, INFINITE);

	unsigned char* buf = new unsigned char[this->head_length];
	size_t length = -1;
	unsigned char flag = 0;
	while (true) {
		reset();
		flag = 0;

		cout << "Waiting..." << endl;
		length = recvmeg(buf, this->head_length);
		if (length == -1 || length < this->head_length || !get_flag_syn(get_flag(buf))) continue;
		// 设置MSS
		if (get_max_send_size(buf) < this->max_send_size) {
			this->max_send_size = get_max_send_size(buf);
			recvbuf.resize(ceil(this->bufsize / (float)(this->max_send_size)));
		}
		// 协商窗口大小
		if (get_window_size(buf) < this->window_size) {
			this->window_size = get_window_size(buf);
		}
		cout << "TCP [SYN] -> ";

		this->ack = get_seq(buf) + 1;
		// 设置SYN建连标志
		set_flag_syn(&flag, true);
		if (sendmeg("", flag) == -1) {
			cout << "[SYNACK] WRONG!" << endl;
			continue;
		}
		cout << "[SYNACK] -> ";

		length = recvmeg(buf, this->head_length, CONNECT_RECV_TIMEOUT);
		flag = get_flag(buf);
		if (length == -1 || length < this->head_length || get_flag_syn(flag) || get_flag_ack(flag) || get_ack(buf) != this->seq + 1 || get_seq(buf) != this->ack) {
			cout << "[ACK] WRONG!" << endl;
			continue;
		}
		this->seq = get_ack(buf);
		cout << "[ACK] -> Connect!" << endl;
		break;
	}
	this->isconnect = true;
	cout << " [SEQ] " << this->seq << " [ACK] " << this->ack << "[Checksum]" << this->get_checksum(buf) << " [MSS] " << this->max_send_size << "[WSZ]" << this->window_size << endl;
	cout.flush();
	cout << "====================三次握手成功=====================" << endl;
	delete[] buf;
	this->window_size = 1; // 连接初始建立，进入慢启动阶段，初始化拥塞窗口cwnd=1
	// 接收线程和发送线程
	this->Send = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Send_Thread, (LPVOID)this, 0, 0);
	this->Recv = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Recv_Thread, (LPVOID)this, 0, 0);
	return true;
}

// 将数据放入发送缓冲区
bool UDP::send(string data) {
	if (!this->isconnect) 
		return false;
	this->sendbuf.push_back(data);
	return true;
}

// 从接收缓冲区读取数据
string UDP::recv() {
	string res;
	while (this->isconnect) {
		EnterCriticalSection(&(this->recvbuf_lock));
		if (this->recvbuf.size() == 0) {
			Sleep(0);
			LeaveCriticalSection(&(this->recvbuf_lock));
			continue;
		}

		bool isend = false;
		while (!isend && this->recvbuf.size()) {
			auto buf = *(this->recvbuf.begin());
			isend = buf.isend;
			res += string((const char*)buf.buf, buf.size);
			delete[] buf.buf;
			this->recvbuf.pop_front();
		}
		LeaveCriticalSection(&(this->recvbuf_lock));
		//if (isend) 
		//	break;
	}
	return res;
}