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

// Socket������Ϣ
int UDP::sendmeg(const string& data, unsigned char flag, int* seq_spec) {
	// ������ݱ���������copy��message������������װЭ��ͷ������Ϊ��head_length + data.length()
	unsigned char* meg_buf = new unsigned char[this->head_length + data.length()];
	// ������ݶ�
	memcpy(meg_buf + this->head_length, data.data(), data.length());
	// ��װЭ��ͷ
	generate_meg_head(meg_buf, (int)(this->head_length + data.length()), flag, seq_spec);

	// �������ݱ�
	// ͨ��sendto�������ض���Ŀ�ĵط������� this->addrΪĿ�ĵ�ַ
	int result = sendto(this->sock, (const char*)meg_buf, (int)(this->head_length + data.length()), 0, this->addr, sizeof(sockaddr));
	delete[] meg_buf;
	return result;
}

// Socket������Ϣ
int UDP::recvmeg(unsigned char* buf, int buf_size, int timeout) 
{
	// addr_lengthԴ��ַ����
	int addr_length = sizeof(sockaddr);
	// ���ý���ʱ��
	setsockopt(this->sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	// �������ݱ�
	// ����recvfrom��socket���������������ݵĻ����������ջ������ĳ��ȣ�flags�Ե��÷�ʽ�Ĵ���Դsocket��ַ��Դ��ַ���ȣ����ض���Ŀ�ĵؽ������ݣ�����ֵΪ���յ����ֽ���
	int len = recvfrom(this->sock, (char*)buf, buf_size, 0, (sockaddr*)(this->local_addr), &addr_length);
	// �����
	if (len != -1 && !check_message(buf, len)) 
		len = -1;
	return len;
}

// ����
void UDP::reset() {
	this->MSS = this->MSS_default;
	this->window_size = this->window_size_default;
	this->ack = 0;
	this->seq = 0;
}

//�����߳�
DWORD WINAPI Recv_thread(LPVOID s) {
	UDP* cls = (UDP*)s;
	unsigned char* buf = new unsigned char[cls->MSS + cls->head_length];
	int timeout_round = 0;
	while (cls->isconnect) {
		// ��ʱ�ش���������ʱ��CONNECT_RECV_TIMEOUT��������immsend�ش�����
		// lenth����Э��ͷ����+���ݶγ���
		int length = cls->recvmeg(buf, cls->MSS + cls->head_length, CONNECT_RECV_TIMEOUT);
		// 10�ζ�ʧ��ͨ���쳣���Զ��Ͽ�
		if (length == -1) {
			timeout_round++;
			if (timeout_round >= cls->autoclose_tcp_loop) break;
			Sleep(0);
		}
		else if (length >= cls->head_length) {
			// �������ö�ʧ����
			timeout_round = 0;
			// FIN��־��λ���Ͽ�����
			if (cls->get_flag_fin(cls->get_flag(buf))) {
				cout << "Closing..." << endl;
				break;
			}
			EnterCriticalSection(&(cls->sendbuf_lock));
			// ���յ�ACK������߳��ڴ������к�SEQ�����
			// ���ظ�ACKʱ����������ش�
			if (cls->seq != cls->get_ack(buf)) {

				// cout << "ACK!" << cls->get_ack(buf) << " " << cls->seq << endl;
				cls->sendbuf.begin()->assign(*(cls->sendbuf.begin()), cls->get_ack(buf) - cls->seq);
				if (cls->sendbuf.begin()->length() == 0) 
					cls->sendbuf.pop_front();
				cls->seq = cls->get_ack(buf);
				cls->immsend = true;
			}
			LeaveCriticalSection(&(cls->sendbuf_lock));

			// �����������
			if (length > cls->head_length) {
				if (cls->ack == cls->get_seq(buf)) {
					if (cls->recvbuf.max_size() > cls->recvbuf.size()) {
						// �����ȥ�����ݱ�ͷ
						unsigned char* temp = new unsigned char[length - cls->head_length];
						memcpy(temp, buf + cls->head_length, length - cls->head_length);
						// �����ݷ�����ջ�������˫�˶���β���������ݣ�
						cls->recvbuf.push_back({ length - cls->head_length, cls->get_flag_end(cls->get_flag(buf)), temp });
						cls->ack = cls->get_seq(buf) + length - cls->head_length;
					}
				}
				cout << "Recv: " << length - cls->head_length << " [SEQ] " << cls->seq << " [ACK] " << cls->ack << " [checksum] " << cls->get_checksum(buf) << endl;
				cls->immsend = true;
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
		cout << "�Ĵλ���: [ACK] -> [FIN]";
		cls->recvmeg(buf, cls->MSS + cls->head_length, CONNECT_RECV_TIMEOUT);
		cout << " -> [ACK]" << endl;
	}
	else { // ���Ͷ�
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
	cout << "�Ͽ�����!" << endl;
	return 0;
}

//�����߳�
// �ڷ����߳��У������ͻ�������Ϊ��ʱ����ȡ���������ݴ�����ݱ������ͣ����� END ��ʶ������Ƿ�Ϊ���һ�����ݱ�����ӡ��Ӧ�����к���Ϣ��
DWORD WINAPI Send_thread(LPVOID s) {
	UDP* cls = (UDP*)s;
	unsigned char flag;
	unsigned long long last_stamp = GetTickCount64(); // ��ʱ��ʼ
	while (cls->isconnect) {
		if (!cls->immsend && GetTickCount64() - last_stamp < CONNECT_RECV_TIMEOUT * 0.5) 
		{
			Sleep(0);
			continue;
		}
		last_stamp = GetTickCount64();
		cls->immsend = false;
		flag = 0;
		cls->set_flag_end(&flag, true); // ��ʼ��Ϊ���һ�����ݱ�
		cls->set_flag_ack(&flag, true); // ACK��Ч
		string sendcontent;
		// send
		//���� �������Ĵ��봦������в����������߳̽��в�������������LeaveCriticalSection
		EnterCriticalSection(&(cls->sendbuf_lock));
		int seq_temp = cls->seq; // ����seq
		if (cls->sendbuf.size()) { // ���ͻ�������Ϊ��ʱ
			string& sendpkg = *(cls->sendbuf).begin();
			// ���ͻ����� > ���ݱ���С��MSS * window_size)��ֻ��ȡ���ݱ���С�����ݲ���Ƿ����һ�����ݱ�
			// ����ֱ�Ӷ�ȡȫ�����ͻ��������ݲ���ʶΪ���һ�����ݱ�
			if (sendpkg.length() > cls->MSS * cls->window_size) {
				sendcontent.assign(sendpkg, 0, cls->MSS * cls->window_size);
				cls->set_flag_end(&flag, false);
			}
			else sendcontent = sendpkg;
		}
		//���� ��EnterCriticalSection֮�������Դ�Ѿ��ͷ��ˣ������߳̿��Խ��в���
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
				// ÿ��seq+mss��ʾ�������к�
				seq_temp += cls->MSS; 
				cout << "Send: " << sendcontent.length() << " [SEQ] " << seq_temp << " [ACK] " << cls->ack << endl;
			}
		}
		Sleep(0);
	}
	cls->sendbuf.clear();
	return 0;
}

// ��װЭ��ͷ
bool UDP::generate_meg_head(unsigned char* message, int length, unsigned char flag, int* seq_spec) {
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

	// head_length 8 | flag 8
	// Э��ͷ����head_lengthΪ24
	message[12] = this->head_length << 2;
	message[13] = flag;

	// ���ڴ�С
	message[14] = (unsigned char)(window_size >> 8);
	message[15] = (unsigned char)window_size;

	// У��ͣ���ʼ��Ϊ0
	message[16] = 0;
	message[17] = 0;

	// MSS���γ���
	message[20] = (unsigned char)(MSS >> 24);
	message[21] = (unsigned char)(MSS >> 16);
	message[22] = (unsigned char)(MSS >> 8);
	message[23] = (unsigned char)MSS;

	// ����У��ͣ���������2�ֽ����ȡ��������2�ֽڲ���
	unsigned short val = 0;
	for (int i = 0; i < length / 2; i++) 
		val += (unsigned short)message[i * 2] << 8 | (unsigned short)message[i * 2 + 1];
	if (length % 2) val += (unsigned short)message[length - 1] << 8;
	val = ~val;

	// ����У���
	message[16] = (unsigned char)(val >> 8);
	message[17] = (unsigned char)val;
	return true;
}

// �����
bool UDP::check_message(unsigned char* message, int length) {
	unsigned short val = 0;
	// ��������2�ֽ����
	for (int i = 0; i < length / 2; i++) 
		val += (unsigned short)message[i * 2] << 8 | (unsigned short)message[i * 2 + 1];
	if (length % 2) val += (unsigned short)message[length - 1] << 8;
	// �Խ��յ����ݱ��� 16bits ���������ͣ�������ȫ 1�������ݱ���ȷ���������ݱ����ڴ���
	return !(unsigned short)(val + 1);
}

UDP::UDP(const char* host, unsigned short port, unsigned short local_port, int mss, int bufsize, unsigned short window_size) :
	MSS_default(mss), local_port(local_port), host(host), port(port), isconnect(false), window_size(window_size), window_size_default(window_size),
	MSS(mss), recvbuf(deque<recv_pkg>(ceil(bufsize / (float)mss))), bufsize(bufsize) {
	// ����Socket
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	WSAStartup(wVersionRequested, &wsaData);
	//ipv4�ĵ�ַ���ͣ����ݱ��ķ������ͣ�Protocol��Э�飩ΪUDP
	this->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  

	// Ŀ�ĵ�ַ
	sockaddr_in* temp_addr = new sockaddr_in;
	temp_addr->sin_family = AF_INET;
	temp_addr->sin_port = htons(port);
	inet_pton(AF_INET, host, &(temp_addr->sin_addr.s_addr));
	this->addr = (sockaddr*)temp_addr;

	// Դ��ַ
	temp_addr = new sockaddr_in;
	temp_addr->sin_family = AF_INET;
	temp_addr->sin_port = htons(local_port);
	inet_pton(AF_INET, "127.0.0.1", &(temp_addr->sin_addr.s_addr));
	this->local_addr = (sockaddr*)temp_addr;

	// �������˽����ص�ַ�󶨵�һ��Socket
	bind(this->sock, this->local_addr, sizeof(sockaddr));

	// ��ʼ����
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

// �����ͻ�����Ϊ��ʱ�ر��߳�
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

// �������ӡ����ͻ���
bool UDP::connect() {
	if (this->isconnect) return true;
	if (this->tcp_runner_recv) WaitForSingleObject(this->tcp_runner_recv, INFINITE);
	if (this->tcp_runner_send) WaitForSingleObject(this->tcp_runner_send, INFINITE);

	cout << "��������..." << endl;
	cout << endl;
	cout << "=================================================" << endl;
	reset();
	unsigned char flag = 0;
	set_flag_syn(&flag, true); // ��λSYN�����ź�
	if (sendmeg("", flag) == -1) // ������Ϣʧ�ܣ�return false
		return false; 
	cout << "[SYN]" << endl; // �����ź���ʾ

	unsigned char* buf = new unsigned char[this->head_length];
	int length = recvmeg(buf, this->head_length, CONNECT_RECV_TIMEOUT);

	if (length == -1 || length < this->head_length || !get_flag_syn(get_flag(buf))|| !get_flag_ack(get_flag(buf))) {
		cout << " [SYNACK] WRONG!" << endl;
		delete[] buf;
		return false;
	}
	cout << "[SYN & ACK] -> " << " [SEQ] " << get_seq(buf) << " [ACK] " << get_ack(buf) << " [SYN_FLAG] " << get_flag_syn(get_flag(buf)) << " [ACK_FLAG] " << get_flag_ack(get_flag(buf)) << endl;
	// ����MSS�ʹ��ڴ�С
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
	cout<<"���ӳɹ���" << endl;
	cout << "=================================================" << endl;
	cout << endl;
	this->isconnect = true; // �������ӳɹ���������־��λ
	// �����̺߳ͷ����߳�
	this->tcp_runner_send = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Send_thread, (LPVOID)this, 0, 0);
	this->tcp_runner_recv = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Recv_thread, (LPVOID)this, 0, 0);
	return true;
}

//�������ӡ�����������
bool UDP::accept() {
	if (this->isconnect) return true;
	if (this->tcp_runner_send) WaitForSingleObject(this->tcp_runner_send, INFINITE);
	if (this->tcp_runner_recv) WaitForSingleObject(this->tcp_runner_recv, INFINITE);

	unsigned char* buf = new unsigned char[this->head_length];    //head_length����Э��ͷ���ȣ�24�ֽڣ�
	int length = -1;
	unsigned char flag = 0;
	while (true) {
		reset(); //���ò���
		flag = 0;
		cout << "�ȴ�����..." << endl;
		cout << endl;
		cout << "=================================================" << endl;
		
		length = recvmeg(buf, this->head_length);
		// ���SYN
		if (length == -1 || length < this->head_length || !get_flag_syn(get_flag(buf))) 
			continue;

		// ����MSS
		if (get_MSS(buf) < this->MSS) {
			this->MSS = get_MSS(buf);
			// ��MSS�ı�ʱ��ͬʱ��Ҫresize�ı�deque˫�˶�����Ԫ�ظ���
			recvbuf.resize(ceil(this->bufsize / (float)(this->MSS)));
		}
		// Э�̴��ڴ�С
		if (get_window_size(buf) < this->window_size) {
			this->window_size = get_window_size(buf);
		}

		// ��������
		cout << "[SYN] -> " <<  " [SEQ] " << get_seq(buf) << " [ACK] " << get_ack(buf) << " [SYN_FLAG] " << get_flag_syn(get_flag(buf)) << " [ACK_FLAG] " << get_flag_ack(get_flag(buf)) << endl;

		// ack=seq+1
		this->ack = get_seq(buf) + 1;

		// ����SYN|ACK������־
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
		cout << "���ӳɹ���" << endl;
		cout << "=================================================" << endl;
		cout << endl;
		break;
	}
	this->isconnect = true;
	cout.flush();
	delete[] buf;
	// �����߳�
	this->tcp_runner_send = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Send_thread, (LPVOID)this, 0, 0);
	// �����߳�
	this->tcp_runner_recv = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Recv_thread, (LPVOID)this, 0, 0);
	return true;
}

// �����ݷ��뷢�ͻ�����
bool UDP::send(string data) {
	if (!this->isconnect) 
		return false;
	// sendbuf ����Ϊ list<string>
	this->sendbuf.push_back(data);
	return true;
}

// �ӽ��ջ�������ȡ����
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
			// ɾ��˫�˶���buf����ǰһ��Ԫ��
			this->recvbuf.pop_front();
		}
		if (isend) break;
	}
	return res;
}