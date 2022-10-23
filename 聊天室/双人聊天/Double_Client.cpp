// 双人聊天——客户端
#include <WinSock2.h>
#include <stdio.h>
#include <stdlib.h>
#include<iostream>
#include <time.h>

# define IP "127.0.0.1"
#pragma comment(lib,"ws2_32.lib")
using namespace std;
//日历时间是通过time_t数据类型来表示的，用time_t表示的时间（日历时间）是从一个时间点（例如：1970年1月1日0时0分0秒）到此时的秒数。time_t是一个长整型数：
time_t t;

int main()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);//声明使用socket2.2版本

	//创建套接字，IP地址类型为AF_INET（IPV-4 32位），服务类型为流式(SOCK_STREAM)，Protocol（协议）为0代表系统自动选则
	SOCKET ClientSocket;
	ClientSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (ClientSocket == INVALID_SOCKET)
	{
		cout << "套接字创建失败，请通过:" << WSAGetLastError() << "获取错误详情"<< endl;
		return -1;
	}

	// time函数获取从1970年1月1日0时0分0秒到此时的秒数
	time(&t);
	char str[26];
	//将时间格式化
	strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
	cout << str << endl;
	char clientName[32] = { 0 };
	cout << "请输入你的昵称: " << endl;
	cin >> clientName;

	//设定所连接服务器地址
	SOCKADDR_IN ServerAddr; 
	USHORT uPort = 2022;
	ServerAddr.sin_family = AF_INET;  //指定IP地址类型（IPV-4 32位）
	ServerAddr.sin_port = htons(uPort);  //htons将主机序转网络序
	ServerAddr.sin_addr.S_un.S_addr = inet_addr(IP);


	cout << "开始连接......" << endl;
	//把自己的socket与远端连接：
	if (SOCKET_ERROR == connect(ClientSocket, (SOCKADDR*)&ServerAddr, sizeof(ServerAddr)))
	{
		cout << "连接失败，请通过" << WSAGetLastError() << "获取详情" << endl;
		closesocket(ClientSocket);
		WSACleanup();
		return -1;
	}

	cout << "成功连接至服务器端，如果您想断开连接请输入 exit " << endl;
	cout << "服务器端信息：" << endl;
	cout << "IP ： " << inet_ntoa(ServerAddr.sin_addr) << "      " << "端口号 : " << htons(ServerAddr.sin_port) << endl << endl;

	//下面是对收发消息的存储和显示
	char buffer[4096] = { 0 };
	char serverName[32] = { 0 };
	int RecvLen = 0;    //实际收到的字节数
	int SendLen = 0;    //实际发送的字节数

	//把自己的名字发过去
	if (SOCKET_ERROR == send(ClientSocket, clientName, strlen(clientName), 0))
	{
		cout << "send发送失败，请通过" << WSAGetLastError() <<"获取详情"<< endl;
		closesocket(ClientSocket);
		WSACleanup();
		return -1;
	}

	//接收对方的名字，存进serverName
	//双方无论谁先发送结束字符，在等到对方回复一条消息后，双方都会结束。
	if (SOCKET_ERROR == recv(ClientSocket, serverName, sizeof(serverName), 0))
	{
		cout << "recv失败，请通过" << WSAGetLastError()<< "获取详情" << endl;
		closesocket(ClientSocket);
		WSACleanup();
		return -1;
	}

	while (true)
	{
		// time函数获取从1970年1月1日0时0分0秒到此时的秒数
		time(&t);
		//将时间格式化
		strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
		cout << str << endl;
		memset(buffer, 0, sizeof(buffer));

		cout << '[' <<clientName << "] :";
		cin >> buffer;
		if (strcmp(buffer ,"exit")==0)
		{
			if (SOCKET_ERROR == send(ClientSocket, buffer, strlen(buffer), 0))
			{
				cout << "send发送失败，请通过" << WSAGetLastError() <<"获取详情"<< endl;
				closesocket(ClientSocket);
				WSACleanup();
				return -1;
			}
			// time函数获取从1970年1月1日0时0分0秒到此时的秒数
			time(&t);
			//将时间格式化
			strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
			cout << str << endl;
			cout <<'['<< serverName << "] :";
			memset(buffer, 0, sizeof(buffer));
			recv(ClientSocket, buffer, sizeof(buffer), 0);
			cout << buffer << endl;
			break;
		}
		if (SOCKET_ERROR == send(ClientSocket, buffer, strlen(buffer), 0))
		{
			cout << "send发送失败，请通过" << WSAGetLastError() <<"获取详情"<< endl;
			closesocket(ClientSocket);
			WSACleanup();
			return -1;
		}

		memset(buffer, 0, sizeof(buffer));
		
		// time函数获取从1970年1月1日0时0分0秒到此时的秒数
		time(&t);
		//将时间格式化
		strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
		cout << str << endl;
		cout << '[' << serverName << "] :";

		if (SOCKET_ERROR == recv(ClientSocket, buffer, sizeof(buffer), 0))
		{
			cout << "recv接收失败，请通过" << WSAGetLastError() <<"了解详情"<< endl;
			closesocket(ClientSocket);
			WSACleanup();
			return -1;
		}
		if (strcmp(buffer, "exit") == 0)
		{
			cout << buffer << endl;
			memset(buffer, 0, sizeof(buffer));
			cout <<'['<< clientName << "] :";
			cin >> buffer;
			send(ClientSocket, buffer, strlen(buffer), 0);
			break;
		}
		cout << buffer << endl;
	}
	closesocket(ClientSocket);
	WSACleanup();
}