#include <WinSock2.h>
#include <stdio.h>
#include <stdlib.h>
#include<iostream>
#include <time.h>
//加载ws2_32.lib库  
#pragma comment(lib,"ws2_32.lib")
time_t t;

using namespace std;

int main()
{
	//定义获得可用socket的详细信息的变量, 存放被WSAStartup函数调用后返回的Windows Sockets数据的数据结构
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);//声明使用socket2.2版本


	//创建套接字Socket，并绑定到一个特定的传输层服务，IP地址类型为AF_INET（IPV-4 32位），服务类型为流式(SOCK_STREAM)，Protocol（协议）为0代表系统自动选则
	SOCKET ServerSocket; 
	ServerSocket = socket(AF_INET, SOCK_STREAM, 0);  //ipv4的地址类型；流结构的服务类型；Protocol（协议）为0代表系统自动选则

	if (ServerSocket == INVALID_SOCKET)
	{
		cout << "套接字创建失败，请通过:" << WSAGetLastError() << "获取错误详情" << endl;
		return -1;
	}

	//通过bind绑定本地地址到socket上
	SOCKADDR_IN ServerAddr;
	USHORT uPort = 2022;
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(uPort);
	//宏INADDR_ANY转换过来就是0.0.0.0，泛指本机的意思，也就是表示本机的所有IP
	ServerAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);


	//将本地地址绑定到指定的Socket
	if (SOCKET_ERROR ==  bind(ServerSocket, (SOCKADDR*)&ServerAddr, sizeof(ServerAddr)))
	{
		cout << "bind 失败，请通过" << WSAGetLastError() << "获取错误详情" << endl;
		closesocket(ServerSocket);
		return -1;
	}
	// time函数获取从1970年1月1日0时0分0秒到此时的秒数
	time(&t);
	char str[26];
	//将时间格式化
	strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
	cout << str << endl;
	//界面初始化，提示服务器输入名字
	cout << "请输入服务器端用户名" << endl;
	char serverName[10] = { 0 };
	cin >> serverName;


	cout << "开启监听，请等待其他客户端连接..." << endl;

	// 绑定完成后开始listen，这里设置的允许等待连接的最大队列为1
	// 使socket进入监听状态，监听远程连接是否到来
	if (listen(ServerSocket, 1)!=0)
	{
		cout << "监听失败，请通过" << WSAGetLastError() <<"获取错误详情"<< endl;
		closesocket(ServerSocket);
		WSACleanup();
		return -1;
	}


	SOCKET Conn_new_Socket;
	SOCKADDR_IN Conn_new_Addr;
	int iClientAddrLen = sizeof(Conn_new_Addr);

	// accept()阻塞进程直到有客户端连接，接受一个特定socket请求等待队列中的连接请求
	//Conn_new_Socket的信息不用bind，因为它会随着serveraccept的地址传过来
	// accept参数：socket描述符，保存地址，保存地址长度，返回新连接的socket描述符
	Conn_new_Socket = accept(ServerSocket, (SOCKADDR*)&Conn_new_Addr, &iClientAddrLen);

	if (Conn_new_Socket == INVALID_SOCKET)
	{
		cout << "accept接受请求失败，请通过" << WSAGetLastError() <<"获取错误详情"<< endl;
		closesocket(ServerSocket);
		WSACleanup();
		return -1;
	}

	//界面提示，显示连接者的信息
	cout << "已经与成功建立连接，如果您想结束结束对话请输入：exit" << endl;
	cout << "客户端信息:" << endl;
	cout << "IP: " << inet_ntoa(Conn_new_Addr.sin_addr) << "    " << " 端口号: " << htons(Conn_new_Addr.sin_port) << endl << endl;

	
	char clientName[32] = { 0 }; // clientName保存客户端用户名
	char buffer[4096] = { 0 };
	int RecvLen = 0;  //实际收到的字节数
	int SendLen = 0;  //实际发送的字节数

	// 把serverName发给对方
	// send函数向远程socket发送数据，返回实际发送的字节数，已经建立TCP连接，不需要指定对方地址，直接将数据放入TCP连接中
	// 参数：socket描述符，buf发送数据缓存区，len发送缓冲区长度，flags-对调用的处理方式，如OOB等
	if (SOCKET_ERROR == send(Conn_new_Socket, serverName, strlen(serverName), 0))
	{
		cout << "send发送失败，请通过" << WSAGetLastError() <<"获取详情"<< endl;
		closesocket(Conn_new_Socket);
		closesocket(ServerSocket);
		WSACleanup();
		return -1;
	}

	//接收对方发过来的信息
	if (SOCKET_ERROR == recv(Conn_new_Socket, clientName, sizeof(clientName), 0))
	{
		cout << "recv失败，请通过" << WSAGetLastError() <<"获取详情"<< endl;
		closesocket(Conn_new_Socket);
		closesocket(ServerSocket);
		WSACleanup();
		return -1;
	}

	//下面的逻辑就是先显示从client方收到的消息，判断是否是结束字符；
	//而后输入server自己想发的消息，传到client去;
	//任意一方如果发出结束符，那么等对方回复一条消息后，两边都会结束对话。
	while (true)
	{
		// 接收客户端信息
		memset(buffer, 0, sizeof(buffer));
		if (SOCKET_ERROR == recv(Conn_new_Socket, buffer, sizeof(buffer), 0))
		{
			cout << "recv失败，请通过" << WSAGetLastError() << "获取详情" << endl;
			closesocket(Conn_new_Socket);
			closesocket(ServerSocket);
			WSACleanup();
			return -1;
		}
		if (strcmp(buffer, "exit") == 0)
		{
			// time函数获取从1970年1月1日0时0分0秒到此时的秒数
			time(&t);
			char str[26];
			//将时间格式化
			strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
			cout << str << endl;
			cout << clientName << ":";
			cout << buffer << endl;
			memset(buffer, 0, sizeof(buffer));
			cout << serverName << ":";
			cin >> buffer;
			SendLen = send(Conn_new_Socket, buffer, strlen(buffer), 0);
			break;
		}
		// time函数获取从1970年1月1日0时0分0秒到此时的秒数
		time(&t);
		char str[26];
		//将时间格式化
		strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
		cout << str << endl;	
		cout << '['<<clientName << "] :";
		cout << buffer << endl;

		// 服务器端发送信息
		memset(buffer, 0, sizeof(buffer));
		// time函数获取从1970年1月1日0时0分0秒到此时的秒数
		time(&t);
		//将时间格式化
		strftime(str, 20, "%Y-%m-%d %X", localtime(&t));
		cout << str << endl;
		cout << '['<<serverName << "] :";

		cin >> buffer;
		if (strcmp(buffer, "exit") == 0)
		{
			if (SOCKET_ERROR == send(Conn_new_Socket, buffer, strlen(buffer), 0))
			{
				cout << "send失败，请通过" << WSAGetLastError() << "获取详情" << endl;
				closesocket(Conn_new_Socket);
				closesocket(ServerSocket);
				WSACleanup();
				return -1;
			}
			cout << clientName << ":";
			memset(buffer, 0, sizeof(buffer));
			RecvLen = recv(Conn_new_Socket, buffer, sizeof(buffer), 0);
			cout << buffer << endl;
			break;
		}
		if (SOCKET_ERROR == send(Conn_new_Socket, buffer, strlen(buffer), 0))
		{
			cout << "send发送失败，请通过" << WSAGetLastError() << "获取详情" << endl;
			closesocket(Conn_new_Socket);
			closesocket(ServerSocket);
			WSACleanup();
			return -1;
		}
	}
	//关闭socket
	closesocket(Conn_new_Socket);
	closesocket(ServerSocket);
	// 结束使用socket，释放Socket DLL资源
	WSACleanup();
	//system("pause");
	return 0;
}


