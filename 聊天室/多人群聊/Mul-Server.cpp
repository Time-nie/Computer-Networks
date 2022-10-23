#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <WinSock2.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include<iostream>
#include <time.h>
#include<string>
#pragma comment(lib,"ws2_32.lib")
using namespace std;

int i = 0;  //标记
SOCKET ServerSocket = INVALID_SOCKET;		 //服务端套接字
SOCKADDR_IN ClientAddr = { 0 };			 //客户端地址
int ClientAddrLen = sizeof(ClientAddr); //客户端地址长度


//在服务器端，每⼀个端⼝的服务都由⼀个单独的线程进⾏管理，同时这些线程共享⼀个⽤户列表
HANDLE HandleRecv[10] = { NULL };				 //接收消息线程句柄
HANDLE Handle;							 //用于accept的线程句柄

//日历时间是通过time_t数据类型来表示的，用time_t表示的时间（日历时间）是从一个时间点（例如：1970年1月1日0时0分0秒）到此时的秒数。time_t是一个长整型数：
time_t t;
char str[26];

//客户端信息结构体
struct Client
{
	SOCKET sClient;      //客户端套接字
	char buffer[128];		 //数据缓冲区
	char userName[10];   //客户端用户名，最大长度为9
	char identify[16];   //用于标识转发的范围
	char IP[20];		 //客户端IP
	UINT_PTR flag;       //标记客户端，用来区分不同的客户端
} inClient[10];   //创建一个客户端结构体,最多同时10人在线


//接收与发送数据线程
DWORD WINAPI Rec_Send_thread(void* param)
{
	bool Start = true;
	SOCKET client = INVALID_SOCKET;
	int flag = 0;
	for (int j = 0; j < i; j++) {
		if (*(int*)param == inClient[j].flag)            //判断是为哪个客户端开辟的接收数据线程
		{
			client = inClient[j].sClient;
			flag = j;
		}
	}
	char temp[128] = { 0 };  //临时数据缓冲区
	string sendname, content;
	while (true)
	{
		//拆包，解析发送消息范围
		memset(temp, 0, sizeof(temp));
		if (recv(client, temp, sizeof(temp), 0) == SOCKET_ERROR)
			continue;
		string contents = temp;
		sendname = contents.substr(0, contents.find(':'));
		content = contents.substr(contents.find(':') + 1 == 0 ? contents.length() : contents.find(':') + 1);
		if (content.length() == 0)
		{
			strcpy(inClient[flag].identify, "all");
			strcpy_s(temp, sendname.c_str());
		}
		else
		{
			strcpy(temp, content.c_str());
			strcpy(inClient[flag].identify, sendname.c_str());
		}
		memcpy(inClient[flag].buffer, temp, sizeof(inClient[flag].buffer));

		if (strcmp(temp, "exit") == 0)   //判断如果客户发送exit请求，那么直接关闭线程，不打开转发线程
		{
			closesocket(inClient[flag].sClient);//关闭该套接字
			CloseHandle(HandleRecv[flag]); //这里关闭了线程句柄
			inClient[flag].sClient = 0;  //把这个位置空出来，留给以后进入的线程使用
			HandleRecv[flag] = NULL;
			cout << "[System] 用户 [" << inClient[flag].userName << "] " << "离开聊天室 " << endl;
		}
		else if (Start == true)
		{
			Start = false;
			continue;
		}
		else
		{
			time(&t);
			strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
			//cout << str << endl;
			cout << '(' << str << ")  [" << inClient[flag].userName << "] : " << temp << endl;
			char temp[128] = { 0 };		    //创建一个临时的数据缓冲区，用来存放接收到的数据
			memcpy(temp, inClient[flag].buffer, sizeof(temp));
			sprintf(inClient[flag].buffer, "%s: %s", inClient[flag].userName, temp); //把发送源的名字添进转发的信息里
			if (strlen(temp) != 0) //如果数据不为空且还没转发则转发
			{
				// 向所有用户发送
				if (strcmp(inClient[flag].identify, "all") == 0)
				{
					for (int j = 0; j < i; j++)
						if (j != flag)
							//向除自己之外的所有客户端发送信息
							if (send(inClient[j].sClient, inClient[flag].buffer, sizeof(inClient[j].buffer), 0) == SOCKET_ERROR)
								return -1;
				}
				else
					// 向指定用户发送
					for (int j = 0; j < i; j++)
						if (strcmp(inClient[j].userName, inClient[flag].identify) == 0)
							if (send(inClient[j].sClient, inClient[flag].buffer, sizeof(inClient[j].buffer), 0) == SOCKET_ERROR)
								return 1;
			}
		}
	}
	return 0;
}

//接收数据线程
DWORD WINAPI Accept_thread(void* param)
{
	int flag[10] = { 0 };
	while (true)
	{
		if (inClient[i].flag != 0)   //找到从前往后第一个没被连接的inClient
		{
			i++;
			continue;
		}
		// accept()阻塞进程直到有客户端连接，接受一个特定socket请求等待队列中的连接请求
		//Conn_new_Socket的信息不用bind，因为它会随着serveraccept的地址传过来
		// accept参数：socket描述符，保存地址，保存地址长度，返回新连接的socket描述符
		if ((inClient[i].sClient = accept(ServerSocket, (SOCKADDR*)&ClientAddr, &ClientAddrLen)) == INVALID_SOCKET)
		{
			cout << "[System] Accept错误，请通过 " << WSAGetLastError() << "获取详情" << endl;
			closesocket(ServerSocket);
			WSACleanup();
			return -1;
		}
		//接收用户名
		recv(inClient[i].sClient, inClient[i].userName, sizeof(inClient[i].userName), 0);
		cout << "[System] 客户端 [" << inClient[i].userName << "]" << " 连接成功" << endl;
		memcpy(inClient[i].IP, inet_ntoa(ClientAddr.sin_addr), sizeof(inClient[i].IP)); //记录客户端IP
		inClient[i].flag = inClient[i].sClient; //不同的socke有不同UINT_PTR类型的数字来标识
		i++;

		//遍历其他客户端并创建进程
		for (int j = 0; j < i; j++)
		{
			if (inClient[j].flag != flag[j])
			{
				if (HandleRecv[j]) 
					CloseHandle(HandleRecv[j]);
				//开启接收消息的线程
				HandleRecv[j] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Rec_Send_thread, &inClient[j].flag, 0, 0);
			}
		}
		for (int j = 0; j < i; j++)
			flag[j] = inClient[j].flag;//防止ThreadRecv线程多次开启
		Sleep(3000);
	}
	return 0;
}

int main()
{
	WSADATA wsaData = { 0 };

	//初始化套接字
	if (WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "[System] WSAStartup发生错误，请通过" << WSAGetLastError() << "" << endl;
		return -1;
	}

	//创建套接字，IP地址类型为AF_INET（IPV-4 32位），服务类型为流式(SOCK_STREAM)，Protocol（协议）为0代表系统自动选则
	ServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ServerSocket == INVALID_SOCKET)
	{
		cout << "[System] Socket创建错误，请通过" << WSAGetLastError() << "获取详情" << endl;
		return -1;
	}

	SOCKADDR_IN ServerAddr = { 0 };				//服务端地址
	USHORT uPort = 10000;						//服务器监听端口
	//设置服务器地址
	ServerAddr.sin_family = AF_INET;//连接方式
	ServerAddr.sin_port = htons(uPort);//服务器监听端口
	ServerAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//宏INADDR_ANY转换过来就是0.0.0.0，泛指本机的意思，也就是表示本机的所有IP

	//绑定服务器
	if (SOCKET_ERROR == bind(ServerSocket, (SOCKADDR*)&ServerAddr, sizeof(ServerAddr)))
	{
		cout << "[System] Bind绑定出现错误，请通过" << WSAGetLastError() << "获取详情" << endl;
		closesocket(ServerSocket);
		return -1;
	}

	// 绑定完成后开始listen，这里设置的允许等待连接的最大队列为5
	// 使socket进入监听状态，监听远程连接是否到来
	if (SOCKET_ERROR == listen(ServerSocket, 5))
	{
		cout << "Listen监听出现错误，请通过" << WSAGetLastError() << "获取详情" << endl;
		closesocket(ServerSocket);
		WSACleanup();
		return -1;
	}

	cout << "[System] 正在等待客户端连接..." << endl;

	Handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Accept_thread, NULL, 0, 0);
	cout << "[System] 提示：如果您想退出服务器，请输入 exit " << endl;
	cout << endl;
	cout << "=========================================================" << endl;
	cout << endl;

	char Serversignal[10];
	cin.getline(Serversignal, 10);
	if (strcmp(Serversignal, "exit") == 0)
	{
		cout << "[System] 服务器已关闭" << endl;
		for (int j = 0; j <= i; j++) //依次关闭套接字
			if (inClient[j].sClient != INVALID_SOCKET)
				closesocket(inClient[j].sClient);
		CloseHandle(Handle);
		exit(1);
		closesocket(ServerSocket);
		WSACleanup();
	}
}