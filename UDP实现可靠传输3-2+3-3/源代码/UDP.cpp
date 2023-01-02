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
// Socket������Ϣ
int UDP::sendmeg(const string& data, unsigned char flag, size_t* seq_spec) {
	// ������ݱ���������copy��message������������װЭ��ͷ
	unsigned char* meg_buf = new unsigned char[this->head_length + data.length()];
	memcpy(meg_buf + this->head_length, data.data(), data.length());
	generate_meg_head(meg_buf, (size_t)(this->head_length + data.length()), flag, seq_spec);

	// �������ݱ�
	int result = sendto(this->sock, (const char*)meg_buf, (size_t)(this->head_length + data.length()), 0, this->addr, sizeof(sockaddr));
	delete[] meg_buf;
	return result;
}


int UDP::recvmeg(unsigned char* buf, size_t buf_size, int timeout) {
	int addr_length = sizeof(sockaddr);
	// ���ý���ʱ��
	int tim = setsockopt(this->sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	// �������ݱ�
	int result = recvfrom(this->sock, (char*)buf, buf_size, 0, (sockaddr*)(this->local_addr), &addr_length);
	// �����
	if (result != -1 && !check_message(buf, result)&& tim == -1) result = -1;
	return result;
}

// ����
void UDP::reset() {
	this->max_send_size = this->max_send_size_default;
	this->window_size = this->window_size_default;
	this->ack = 0;
	this->seq = 0;
}

//�����߳�
DWORD WINAPI Recv_Thread(LPVOID s) {
	//cout << "��������߳�" << endl;
	UDP* cls = (UDP*)s;
	unsigned char* buf = new unsigned char[cls->max_send_size + cls->head_length];
	size_t timeout_round = 0; // ��ʱ����
	size_t block_num = 0; // �ظ�ack����
	size_t slow_num = 0; // ӵ������
	while (cls->isconnect) {
		size_t length = cls->recvmeg(buf, cls->max_send_size + cls->head_length, CONNECT_RECV_TIMEOUT);
		if (length == -1) {
			// ��ʱ��⣺��ֵSST��Ϊ���ڵ�һ�룬cwnd=1,�����������׶�
			timeout_round++;  //��ʱ������¼
			slow_num = 0;     // ӵ������׶μ������������ڴ�С�仯��
			block_num = 0;   //�����ظ�ACK�����������ν�����ٻָ��׶Σ�
			cls->max_window_size = cls->window_size / 2;
			cls->window_size = 1;
			cout << "Time Out!" << endl;
			// 10�ζ�ʧ��ͨ���쳣���Զ��Ͽ�
			if (timeout_round >= cls->autoclose_tcp_loop) 
				break;
			Sleep(0);
		}
		else if (length >= cls->head_length) {
			timeout_round = 0;  //���¼�¼��ʱ����

			// FIN��־��λ���Ͽ�����
			if (cls->get_flag_fin(cls->get_flag(buf))) {
				cout << "Closing..." << endl;
				break;
			}
			
			EnterCriticalSection(&(cls->sendbuf_lock));
			// ��ACK
			if (cls->seq != cls->get_ack(buf)) {  
				if (block_num >= 3)  //���ٻָ� -> ӵ������
				{
					cout << "�����ٻָ� -> ӵ�����⡿" << endl;
					cls->window_size = cls->max_window_size;
				}
				else if (cls->window_size < cls->max_window_size)  // �������׶�
					cls->window_size++;
				else {
					// ӵ������׶Σ�ӵ�����ڴﵽ��ֵʱ������ӵ������׶�
					slow_num++;
					if (slow_num >= cls->window_size) {
						cls->window_size += 1;
						slow_num = 0;
					}
				}
				block_num = 0;   //�ظ�ACK������0
				// ���ͻ�����beginָ�����
				cls->sendbuf.begin()->assign(*(cls->sendbuf.begin()), cls->get_ack(buf) - cls->seq);

				// �����ͻ��������Ѿ�����һ�������ļ��󣬽����ļ������޳�
				if (cls->sendbuf.begin()->length() == 0) 
					cls->sendbuf.pop_front();
				// �����ƶ�
				cls->seq = cls->get_ack(buf);

				// ����������һ�����ķ���
				cls->immsend = true;
			}
			//�ظ�ACK
			else {   
				if (length <= cls->head_length)
				{
					// �����ظ�ACK��ⶪʧ����ֵ��Ϊӵ�����ڵ�һ�룬cwnd=SST+3����������������ӵ������׶Σ�
					block_num++;
					// �����ظ�ACK��׼��������ٻָ�
					if (block_num == 3) {
						slow_num = 0;
						cls->max_window_size = cls->window_size / 2;
						cls->window_size = cls->max_window_size + 3;
						seq_temp = min(cls->seq + cls->window_size*cls->max_send_size, seq_temp);
						cout << "�����ظ� ACK!" << endl;
						cls->re = 1;
					}
					// ���ٻָ��׶�
					else if (block_num > 3)
					{
						cls->re = 1;
						cls->window_size += 1;
					}
				}
			}
			LeaveCriticalSection(&(cls->sendbuf_lock));

			// ����������ݣ����նˣ�
			if (length > cls->head_length) {
				if (cls->ack == cls->get_seq(buf)) {   //�����յ��������
					EnterCriticalSection(&(cls->recvbuf_lock));
					if (cls->recvbuf.max_size() > cls->recvbuf.size()) {
						// �����ȥ�����ݱ�ͷ
						unsigned char* temp = new unsigned char[length - cls->head_length];
						memcpy(temp, buf + cls->head_length, length - cls->head_length);
						// �����ݷ�����ջ�����
						cls->recvbuf.push_back({ length - cls->head_length, cls->get_flag_end(cls->get_flag(buf)), temp });

						// ����ACK
						// ������յ�����ʧ���������ظ����飬�򲻸���ACK
						cls->ack = cls->get_seq(buf) + length - cls->head_length;
					}
					LeaveCriticalSection(&(cls->recvbuf_lock));
				}
				// ����������һ�����ķ���
				cls->immsend = true;
				cout << "Recv: " << length - cls->head_length << " [ACK] " << cls->ack << " [checksum] " << cls->get_checksum(buf) << endl;
			}
		}
	}
	// �Ĵλ��ֶϿ�����
	unsigned char flag = 0;
	if (cls->isconnect) { // ���ն������Ͽ�����
		cls->isconnect = false;
		cls->set_flag_end(&flag, true);
		cls->set_flag_fin(&flag, true);
		cls->set_flag_ack(&flag, true);
		cls->sendmeg("", flag);
		cout << "Close: [ACK] -> [FIN]";
		cls->recvmeg(buf, cls->max_send_size + cls->head_length, CONNECT_RECV_TIMEOUT);
		cout << " -> [ACK]" << endl;
	}
	else { // ���Ͷ�
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


//�����߳�
DWORD WINAPI Send_Thread(LPVOID s) {
	//cout << "���뷢���߳�" << endl;
	UDP* cls = (UDP*)s;
	unsigned char flag;
	
	unsigned long long last_stamp = GetTickCount64(); // ��ʱ��ʼ
	while (cls->isconnect) 
	{
		
		if (!cls->immsend && GetTickCount64() - last_stamp < CONNECT_RECV_TIMEOUT) {
			Sleep(0);
			continue;
		}
		if (GetTickCount64() - last_stamp > CONNECT_RECV_TIMEOUT)
		{
			//cout << "�ѳ�ʱ" << endl;
			time_flag = 1;
		}
		last_stamp = GetTickCount64();  //���¼�ʱ����ʱ/base�ƶ���base!=nextseqnum/�տ�ʼ��
		cls->immsend = false;
		flag = 0;
		cls->set_flag_end(&flag, true); // ��ʼ��Ϊ���һ�����ݱ�
		cls->set_flag_ack(&flag, true); // ACK��Ч
		string sendcontent;   //��������
		EnterCriticalSection(&(cls->sendbuf_lock));  //���� �������Ĵ��봦������в����������߳̽��в�������������LeaveCriticalSection
		if (cls->sendbuf.size())
		{
			string& sendpkg = *(cls->sendbuf).begin();
			int remain = cls->max_send_size * cls->window_size - (seq_temp - cls->seq);
			if (time_flag )
			{
				sendcontent.assign(sendpkg, 0, seq_temp - cls->seq);//�ط�
				seq_temp = cls->seq; // seq_temp����nextseqnum
			}
			if (cls->re)
			{
				cls->re = 0;
				sendcontent.assign(sendpkg, 0, cls->max_send_size);   //�����ش�
				seq_temp = cls->seq; // seq_temp����nextseqnum
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
					cout << "����������" << endl;
					Sleep(0);
					continue;
				}
			}
		}
		LeaveCriticalSection(&(cls->sendbuf_lock));  //����
		
		// ������Э��ͷ�����նˣ�
		if (sendcontent.length() == 0) {
			cls->sendmeg(sendcontent, flag);
		}
		else {  //�������ݣ����Ͷˣ�
			unsigned char flag_copy = flag;

			for (size_t i = 0; i < sendcontent.length(); i += cls->max_send_size) {
				flag = flag_copy;
				// �ж��Ƿ�Ϊ���һ�����ķ���
				if (i + cls->max_send_size < sendcontent.length()) 
					cls->set_flag_end(&flag, false);
				// ���͸ñ��ķ���
				int len = ((i + cls->max_send_size) >= sendcontent.length() ? sendcontent.length() - i : cls->max_send_size);
				if ((++book) % 10000000000 || time_flag == 1)  //���ö���
					cls->sendmeg(sendcontent.substr(i, len), flag, &seq_temp);
				else
				{
					cout << "���÷��鶪����";
					//Sleep(8000);
					//cls->sendmeg(sendcontent.substr(i, len), flag, &seq_temp);
				}
					
				Sleep(0);
				// �ı���û�δ����λ��
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

// ��װЭ��ͷ
bool UDP::generate_meg_head(unsigned char* message, size_t length, unsigned char flag, size_t* seq_spec) {
	if (length < this->head_length) 
		return false;
	// Դ�˿�
	message[0] = (unsigned char)(this->local_port >> 8);
	message[1] = (unsigned char)this->local_port;
	// Ŀ�Ķ˿�
	message[2] = (unsigned char)(this->port >> 8);
	message[3] = (unsigned char)this->port;

	// seq���к�
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

	// ACKȷ�����к�
	message[8] = (unsigned char)(ack >> 24);
	message[9] = (unsigned char)(ack >> 16);
	message[10] = (unsigned char)(ack >> 8);
	message[11] = (unsigned char)ack;

	// head_length 4 | reserve 4 | flag 8
	message[12] = this->head_length << 2;
	message[13] = flag;

	// ���ڴ�С
	message[14] = (unsigned char)(window_size >> 8);
	message[15] = (unsigned char)window_size;

	// У��ͣ���ʼ��Ϊ0
	message[16] = 0;
	message[17] = 0;

	// MSS���γ���
	message[20] = (unsigned char)(max_send_size >> 24);
	message[21] = (unsigned char)(max_send_size >> 16);
	message[22] = (unsigned char)(max_send_size >> 8);
	message[23] = (unsigned char)max_send_size;

	// ����У��ͣ���������2�ֽ����ȡ��������2�ֽڲ���
	unsigned short val = 0;
	for (size_t i = 0; i < length / 2; i++) val += (unsigned short)message[i * 2] << 8 | (unsigned short)message[i * 2 + 1];
	if (length % 2) val += (unsigned short)message[length - 1] << 8;
	val = ~val;

	// ����У���
	message[16] = (unsigned char)(val >> 8);
	message[17] = (unsigned char)val;
	return true;
}

// �����
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
	// ����Socket
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	WSAStartup(wVersionRequested, &wsaData);
	this->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// Ŀ�ĵ�ַ
	sockaddr_in* temp_addr = new sockaddr_in;
	temp_addr->sin_family = AF_INET;
	temp_addr->sin_port = htons(port);
	inet_pton(AF_INET, host, &(temp_addr->sin_addr.s_addr));
	this->addr = (sockaddr*)temp_addr;

	// Դ��ַ
	temp_addr = new sockaddr_in;
	temp_addr = new sockaddr_in;
	temp_addr->sin_family = AF_INET;
	temp_addr->sin_port = htons(local_port);
	inet_pton(AF_INET, "127.0.0.1", &(temp_addr->sin_addr.s_addr));
	this->local_addr = (sockaddr*)temp_addr;

	// �������˽����ص�ַ�󶨵�һ��Socket
	bind(this->sock, this->local_addr, sizeof(sockaddr));

	// ��ʼ����
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

// �����ͻ�����Ϊ��ʱ�ر��߳�
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

// �������ӡ����ͻ���
bool UDP::connect() {
	if (this->isconnect) return true;
	if (this->Recv) WaitForSingleObject(this->Recv, INFINITE);
	if (this->Send) WaitForSingleObject(this->Send, INFINITE);

	cout << "Connecting..." << endl;
	reset();
	unsigned char flag = 0;
	set_flag_syn(&flag, true); // ��λSYN�����ź�
	if (sendmeg("", flag) == -1)  // ������Ϣʧ�ܣ�return false
		return false; 
	cout << "�������� [SYN] -> "; // �����ź���ʾ

	unsigned char* buf = new unsigned char[this->head_length];
	size_t length = recvmeg(buf, this->head_length, CONNECT_RECV_TIMEOUT);
	if (length == -1 || length < this->head_length || !get_flag_syn(get_flag(buf))) {
		cout << " [SYNACK] WRONG!" << endl;
		delete[] buf;
		return false;
	}
	// ����MSS�ʹ��ڴ�С
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
	cout << "====================�������ֳɹ�=====================" << endl;
	this->isconnect = true; // �������ӳɹ���������־��λ
	this->window_size = 1; // ���ӳ�ʼ�����������������׶Σ���ʼ��ӵ������cwnd=1
	// �����̺߳ͷ����߳�
	this->Send = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Send_Thread, (LPVOID)this, 0, 0);
	this->Recv = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Recv_Thread, (LPVOID)this, 0, 0);
	return true;
}

//�������ӡ�����������
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
		// ����MSS
		if (get_max_send_size(buf) < this->max_send_size) {
			this->max_send_size = get_max_send_size(buf);
			recvbuf.resize(ceil(this->bufsize / (float)(this->max_send_size)));
		}
		// Э�̴��ڴ�С
		if (get_window_size(buf) < this->window_size) {
			this->window_size = get_window_size(buf);
		}
		cout << "TCP [SYN] -> ";

		this->ack = get_seq(buf) + 1;
		// ����SYN������־
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
	cout << "====================�������ֳɹ�=====================" << endl;
	delete[] buf;
	this->window_size = 1; // ���ӳ�ʼ�����������������׶Σ���ʼ��ӵ������cwnd=1
	// �����̺߳ͷ����߳�
	this->Send = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Send_Thread, (LPVOID)this, 0, 0);
	this->Recv = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Recv_Thread, (LPVOID)this, 0, 0);
	return true;
}

// �����ݷ��뷢�ͻ�����
bool UDP::send(string data) {
	if (!this->isconnect) 
		return false;
	this->sendbuf.push_back(data);
	return true;
}

// �ӽ��ջ�������ȡ����
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