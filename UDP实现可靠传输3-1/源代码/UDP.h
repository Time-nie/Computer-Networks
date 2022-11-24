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

#define CONNECT_RECV_TIMEOUT 500

// ���ݱ�
typedef struct recv_pkg {
	int size;  //���ݴ�С
	bool isend;  //�Ƿ�Ϊ��β
	const unsigned char* buf;  //��������
} recv_pkg;

class UDP {
private:
	SOCKET sock;
	struct sockaddr* addr;       // Ŀ�ĵ�ַ
	struct sockaddr* local_addr; // Դ��ַ
	string host;				// ����IP
	unsigned short port;       // Ŀ�Ķ˿�
	unsigned short local_port; // Դ�˿�
	bool isconnect; // �Ƿ����ӱ�ʶ

	CRITICAL_SECTION sendbuf_lock; // �ٽ绺�������������̵߳Ĺ�����Դ����ֹ��ͻ��ȫ������
	deque<recv_pkg> recvbuf;    // ���ջ�����
	list<string> sendbuf;       // ���ͻ�����

	const int bufsize; // ��������С
	int ack; // ACK��ȷ�����к�
	int seq; // SEQ���������к�
	unsigned short window_size;               // ���ڴ�С
	const unsigned short window_size_default; // Ĭ�ϴ��ڴ�С
	int MSS;               // MSS�����Ʊ������ݶ���󳤶ȣ�������һ�����ļ�ʱ��ͨ�����ļ�����Ϊ����ΪMSS�����ɿ�
	const int MSS_default; // Ĭ�����γ���
	const int head_length = 24; // Э��ͷ���ȣ�24�ֽ�
	const int autoclose_tcp_loop = 10; // 10�ζ�ʧ��ͨ���쳣���Զ��Ͽ�

	bool immsend = false; // �����ط�

	// send and recv
	// ���ͺͽ���
	int sendmeg(const string& data, unsigned char flag, int* seq_spec = nullptr);
	int recvmeg(unsigned char* buf, int buf_size, int timeout = -1);

	// �����̺߳ͽ����߳�
	friend DWORD WINAPI Send_thread(LPVOID lpParam);
	friend DWORD WINAPI Recv_thread(LPVOID lpParam);
	HANDLE tcp_runner_send = NULL;
	HANDLE tcp_runner_recv = NULL;

	void reset();

	// ��װЭ��ͷ
	bool generate_meg_head(unsigned char* message, int length, unsigned char flag, int* seq_spec = nullptr);

	// set flag ����Э��ͷ��־
	void set_flag_cwr(unsigned char* flag, bool value) 
	{ 
		value ? *flag |= (unsigned char)1 << 7 : *flag &= ~((unsigned char)1 << 7); 
	}
	void set_flag_ece(unsigned char* flag, bool value) 
	{ 
		value ? *flag |= (unsigned char)1 << 6 : *flag &= ~((unsigned char)1 << 6); 
	}
	void set_flag_over(unsigned char* flag, bool value) 
	{ 
		value ? *flag |= (unsigned char)1 << 5 : *flag &= ~((unsigned char)1 << 5); 
	}
	void set_flag_ack(unsigned char* flag, bool value) 
	{ 
		value ? *flag |= (unsigned char)1 << 4 : *flag &= ~((unsigned char)1 << 4); 
	}
	void set_flag_end(unsigned char* flag, bool value) 
	{ 
		value ? *flag |= (unsigned char)1 << 3 : *flag &= ~((unsigned char)1 << 3); 
	}
	void set_flag_rst(unsigned char* flag, bool value) 
	{ 
		value ? *flag |= (unsigned char)1 << 2 : *flag &= ~((unsigned char)1 << 2); 
	}
	void set_flag_syn(unsigned char* flag, bool value) 
	{ 
		value ? *flag |= (unsigned char)1 << 1 : *flag &= ~((unsigned char)1 << 1); 
	}
	void set_flag_fin(unsigned char* flag, bool value) 
	{ 
		value ? *flag |= (unsigned char)1 : *flag &= ~((unsigned char)1); 
	}

	// get flag ���Э��ͷ��־
	bool get_flag_cwr(unsigned char flag) { return flag & (unsigned char)1 << 7; }
	bool get_flag_ece(unsigned char flag) { return flag & (unsigned char)1 << 6; }
	bool get_flag_over(unsigned char flag) { return flag & (unsigned char)1 << 5; }
	bool get_flag_ack(unsigned char flag) { return flag & (unsigned char)1 << 4; }
	bool get_flag_end(unsigned char flag) { return flag & (unsigned char)1 << 3; }
	bool get_flag_rst(unsigned char flag) { return flag & (unsigned char)1 << 2; }
	bool get_flag_syn(unsigned char flag) { return flag & (unsigned char)1 << 1; }
	bool get_flag_fin(unsigned char flag) { return flag & (unsigned char)1; }

	// ���Э��ͷ��Ϣ
	unsigned short get_send_port(const unsigned char* message) 
	{ 
		return (unsigned short)message[0] << 8 | (unsigned short)message[1]; 
	}

	unsigned short get_recv_port(const unsigned char* message) 
	{ 
		return (unsigned short)message[2] << 8 | (unsigned short)message[3]; 
	}

	unsigned int get_seq(const unsigned char* message) 
	{ 
		return (unsigned int)message[4] << 24 | (unsigned int)message[5] << 16 | (unsigned int)message[6] << 8 | (unsigned int)message[7]; 
	}

	unsigned int get_ack(const unsigned char* message) 
	{ 
		return (unsigned int)message[8] << 24 | (unsigned int)message[9] << 16 | (unsigned int)message[10] << 8 | (unsigned int)message[11]; 
	}

	int get_head_length(const unsigned char* message) 
	{ 
		return (int)message[12] >> 2; 
	}

	unsigned char get_flag(const unsigned char* message) 
	{ 
		return message[13]; 
	}

	unsigned short get_window_size(const unsigned char* message) 
	{ 
		return (unsigned short)message[14] << 8 | (unsigned short)message[15]; 
	}

	int get_MSS(const unsigned char* message) 
	{ 
		return (int)message[20] << 24 | (int)message[21] << 16 | (int)message[22] << 8 | (int)message[23]; 
	}

	bool check_message(unsigned char* message, int length);
	
	unsigned short get_checksum(const unsigned char* message) 
	{ 
		return (unsigned short)message[16] << 8 | (unsigned short)message[17]; 
	}

public:
	UDP(const char* host, unsigned short port = 4000, unsigned short local_port = 4000, int mss = 2048, int bufsize = 20480, unsigned short window_size = 1);  //��ʼ������
	bool connect(); // �ͻ��˽�������
	bool accept(); // �������˽�������
	bool send(string data); // �����ݷ��뷢�ͻ�����
	string recv(); // �ӽ��ջ�������ȡ����
	void close();  // �����ͻ�����Ϊ��ʱ�ر��߳�
	bool isConnect() { return this->isconnect; }
	int getrecvsize() { return this->bufsize; }
	~UDP();
};
