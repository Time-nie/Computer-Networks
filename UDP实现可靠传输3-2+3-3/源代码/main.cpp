#include "UDP.h"
#include <iostream>
#include <fstream>
#include <ctime>
using namespace std;


int main(int argc, const char* argv[]) {

	string TEST_FILE;
	string TEST_FILE_REV;
	if (argc > 1) {
		TEST_FILE = argv[1];
		TEST_FILE_REV = "recv_" + TEST_FILE;
	}
	else {
		cout << "No file" << endl;
		return 0;
	}
	if (argc > 2 && strcmp(argv[2], "-s") == 0) {
		UDP sock("127.0.0.1", 4000, 5000);
		sock.accept(); // 三次握手建立连接

		// 发送端
		ifstream file(TEST_FILE, ios::binary);

		// 读指针设置为文件结尾，获取当前位置即为文件大小size
		file.seekg(0, ios::end);
		size_t size = file.tellg();
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
		cout << "Rate: " << abc.length() / time_red << "Bytes/s" << endl;   // 吞吐率
	}
	else {
		UDP sock("127.0.0.1", 5000, 4000);
		sock.connect(); // 接收端建立连接
		// 接收端
		time_t time_red = time(NULL);
		string cont = sock.recv();

		time_red = time(NULL) - time_red;
		cout << "Time: " << time_red << "s" << endl;
		cout << "Data: " << cont.length() << " Bytes" << endl;
		cout << "Rate: " << cont.length() / time_red <<"Bytes/s"<< endl;
		ofstream file2("c_" + TEST_FILE_REV, ios::binary);
		file2.write(cont.data(), cont.length());
	}
}