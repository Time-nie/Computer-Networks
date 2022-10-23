#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <WinSock2.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include<iostream>
#include <time.h>
#include<string>
#pragma comment(lib,"ws2_32.lib")

# define IP "127.0.0.1"
using namespace std;

char userName[16] = { 0 };
boolean isPrint = false;  // 判断是否要在客户端打印名字

//日历时间是通过time_t数据类型来表示的，用time_t表示的时间（日历时间）是从一个时间点（例如：1970年1月1日0时0分0秒）到此时的秒数。time_t是一个长整型数：
time_t t;
char str[26];

//接收线程
DWORD WINAPI handlerRequest(void* param)
{
	char bufferRecv[128] = { 0 };
	// 如果接收正确，则一直处于接收状态
	while (true)
	{
		// 等待并接收消息
		if (recv(*(SOCKET*)param, bufferRecv, sizeof(bufferRecv), 0) == SOCKET_ERROR)
			break;
		if (strlen(bufferRecv) != 0)
		{
			//'\b'光标迁移
			// 坑！一定+2且带等号，有：和空格
			for (int i = 0; i <= strlen(userName) + 40; i++)
				cout << "\b";
			// 打印时间
			time(&t);
			strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
			cout << '(' << str << ")    ";
			// 发送源的名字+数据内容
			cout << bufferRecv << endl;
			////因为这是在用户的send态时，把本来打印出来的userName给退回去了，所以收到以后需要再把userName打印出来
			cout << '(' << str << ")  '[' "<< userName << "] : ";
		}
	}
	return 0;
}

int main()
{
	WSADATA wsaData = { 0 };//存放套接字信息
	SOCKET ClientSocket = INVALID_SOCKET;//客户端套接字
	SOCKADDR_IN ServerAddr = { 0 };//服务端地址
	USHORT uPort = 10000;//服务端端口

	//定义获得可用socket的详细信息的变量, 存放被WSAStartup函数调用后返回的Windows Sockets数据的数据结构
	if (WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "[System] WSAStartup创建失败，请通过" << WSAGetLastError() << "获取详情" << endl;
		return -1;
	}

	//创建套接字
	ClientSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (ClientSocket == INVALID_SOCKET)
	{
		cout << "[System] Socket创建错误，请通过" << WSAGetLastError() << "获取详情" << endl;
		return -1;
	}

	//设置服务器地址
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(uPort);//客户端端口号
	ServerAddr.sin_addr.S_un.S_addr = inet_addr(IP);//客户端IP地址

	cout << "[System] 正在连接..." << endl;

	//连接服务器
	if (SOCKET_ERROR == connect(ClientSocket, (SOCKADDR*)&ServerAddr, sizeof(ServerAddr)))
	{
		cout << "[System] Connect连接错误，请通过" << WSAGetLastError() << "获取详情" << endl;
		closesocket(ClientSocket);
		WSACleanup();
		return -1;
	}

	cout << "[System] 连接成功!，服务器端地址信息为：" << endl;
	cout << "[System] IP ：" << inet_ntoa(ServerAddr.sin_addr) << "     " << "端口号 ：" << htons(ServerAddr.sin_port) << endl;
	cout << "[System] 您已成功进入聊天室，请输入您的用户名: ";

	// 发送名字
	bool st = true;
	string na;
	do {
		if (st)
		{
			cin >> na;
			st = false;
		}
		else
		{
			cout << "您输入的用户名不合规，请重新输入：";
			cin >> na;
		}
	} while (na.length() > 16);
	strcpy_s(userName, na.c_str());

	cout << "[System] 提示：如果您想退出，请输入 exit " << endl;
	send(ClientSocket, userName, sizeof(userName), 0);


	cout << endl;
	cout << "=========================================================" << endl;
	cout << endl;

	// 开启接收线程
	HANDLE recvthread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)handlerRequest, &ClientSocket, 0, 0);

	// 删除句柄，线程仍在运行
	CloseHandle(recvthread);
	char bufferSend[128] = { 0 };
	bool start = false;
	while (true)
	{
		if (start)
		{
			// time函数获取从1970年1月1日0时0分0秒到此时的秒数
			time(&t);
			//将时间格式化
			strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
			//cout << str << endl;
			cout << '(' << str << ")  ";
			cout << '[' << userName << "] : ";
		}
		start = true;
		// 用户输入发送消息
		cin.getline(bufferSend, 128);
		//如果用户输入exit准备退出
		if (strcmp(bufferSend, "exit") == 0)
		{
			cout << "您已离开聊天室" << endl;
			CloseHandle(handlerRequest);
			if (send(ClientSocket, bufferSend, sizeof(bufferSend), 0) == SOCKET_ERROR)
				return -1;    //退出当前线程
			break;
		}
		send(ClientSocket, bufferSend, sizeof(bufferSend), 0);
	}
	closesocket(ClientSocket);
	WSACleanup();
	system("pause");
	return 0;
}