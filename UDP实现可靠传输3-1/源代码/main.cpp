#include "UDP.h"
#include <iostream>
#include <fstream>
#include <ctime>
using namespace std;
//int main()
//{
//	cout << "请选择您想传输的文件：" << endl;
//	cout << "1: 1.jpg    2：2.jpg    3：3.jpg    4：4.jpg" << endl;
//	int num;
//	string TEST_FILE, TEST_FILE_REV;
//	cin >> num;
//	switch (num)
//	{
//	case 1:
//		TEST_FILE = "1.jpg";
//	case 2:
//		TEST_FILE = "2.jpg";
//	case 3:
//		TEST_FILE = "3.jpg";
//	case 4:
//		TEST_FILE = "helloword.txt";
//	}
//	TEST_FILE_REV = "recv_" + TEST_FILE;   //接收文件名
//
//
//	cout << "请选择您的传输类型：" << endl;
//	cout << "1: 发送    2：接收" << endl;
//	cin >> num;
//	switch (num)
//	{
//	case 1:
//	{
//		// 发送端
//	// 127.0.0.1代表客户端IP地址，4000为客户端端口号，5000为服务器端本机端口号
//		UDP sock_send("127.0.0.1", 4000, 5000);
//		sock_send.accept(); // 三次握手建立连接
//		//打开文件读取
//		ifstream file(TEST_FILE, ios::binary);
//		// 读指针设置为文件结尾，获取当前位置即为文件大小size
//		file.seekg(0, ios::end);
//		int size = file.tellg();
//		char* buf = new char[size];
//		// 读指针设置为文件开始，读取所有文件数据至缓冲区
//		file.seekg(0, ios::beg);
//		file.read(buf, size);
//		// 将数据放入发送缓冲区，并开始计时
//		string abc(buf, size);
//		time_t time_send = time(NULL);
//		sock_send.send(abc);
//		delete[] buf;
//
//		// 当发送缓冲区为空时关闭线程，结束计时
//		sock_send.close();
//		time_send = time(NULL) - time_send;
//		cout << "Time: " << time_send << "s" << endl;           // 时间
//		cout << "Data: " << abc.length() << " Bytes" << endl;  // 数据大小
//		cout << "Rate: " << abc.length() / time_send << endl;   // 吞吐率
//	}
//	case 2:
//	{
//		// 接收端
//		UDP sock_recv("127.0.0.1", 5000, 4000);
//		sock_recv.connect(); // 接收端建立连接
//
//		// 接收端
//		time_t time_red = time(NULL);
//		// 从接收缓冲区读取数据
//		string cont = sock_recv.recv();
//		time_red = time(NULL) - time_red;
//		cout << "Time: " << time_red << "s" << endl;
//		cout << "Data: " << cont.length() << " Bytes" << endl;
//		cout << "Rate: " << cont.length() / time_red << endl;
//		ofstream file2("c_" + TEST_FILE_REV, ios::binary);
//		file2.write(cont.data(), cont.length());
//	}
//	}
//}

int main(int argc, const char* argv[]) {
	string TEST_FILE;
	string TEST_FILE_REV;
	if (argc > 1) {
		TEST_FILE = argv[1];  // 读取文件名
		TEST_FILE_REV = "Recv_" + TEST_FILE;   //接收文件名
	}
	else {
		// 提示找不到文件
		cout << "No file" << endl;
		return 0;
	}
	// 当输入第三个参数为-s时代表发送数据
	if (argc > 2 && strcmp(argv[2], "-s") == 0) {
		// 127.0.0.1代表客户端IP地址，4000为客户端端口号，5000为服务器端本机端口号
		UDP sock("127.0.0.1", 4000, 5000);
		sock.accept(); // 三次握手建立连接
		// 发送端
		//打开文件读取
		ifstream file(TEST_FILE, ios::binary);
		// 读指针设置为文件结尾，获取当前位置即为文件大小size
		file.seekg(0, ios::end);
		int size = file.tellg();
		char* buf = new char[size];
		// 读指针设置为文件开始，读取所有文件数据至缓冲区
		file.seekg(0, ios::beg);
		file.read(buf, size);
		// 将数据放入发送缓冲区，并开始计时
		string abc(buf, size);
		time_t time_red = time(NULL);
		sock.send(abc);
		delete[] buf;

		// 当发送缓冲区为空时关闭线程，结束计时
		sock.close();
		time_red = time(NULL) - time_red;
		cout << "Time: " << time_red << "s" << endl;           // 时间
		cout << "Data: " << abc.length() << " Bytes" << endl;  // 数据大小
		cout << "Rate: " << abc.length() / time_red << "Bytes/s" << endl; //吞吐率
	}
	else {
		UDP sock("127.0.0.1", 5000, 4000);
		sock.connect(); // 接收端建立连接

		// 接收端
		time_t time_red = time(NULL);
		// 从接收缓冲区读取数据
		string cont = sock.recv();
		time_red = time(NULL) - time_red;
		cout << "Time: " << time_red << "s" << endl;
		cout << "Data: " << cont.length() << " Bytes" << endl;
		cout << "Rate: " << cont.length() / time_red << "Bytes/s" << endl;  //吞吐率
		ofstream file2(TEST_FILE_REV, ios::binary);
		file2.write(cont.data(), cont.length());
	}
}