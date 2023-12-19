//server.cpp完成接收端部分
#include"UDP programming.h"
#include<map>

std::fstream Server_log;
static int wndSize;
static SOCKET Server;
static struct fakeHead sendHead, recvHead;
static sockaddr_in server_addr;
static sockaddr_in client_addr;
static int addrlen;
static u_short NowSeq = 0;
static u_short NowAck = 0;

std::map<int, msg> msgCache;

std::fstream file;
std::string filename;
struct FileHead descriptor;
int filelen = 0;
int lenPointer = 0;

std::string savePath = "./save/";

static void logger(std::string str, SYSTEMTIME sysTime) {   //写入日志
	printf("%s\n", str.c_str());
	std::string s = std::to_string(sysTime.wMinute) + " : " + std::to_string(sysTime.wSecond) + " : " + std::to_string(sysTime.wMilliseconds) + "\n" + str + "\n";
	Server_log << s;
}

static void recvLog(msg m) {
	char info[100];
	sprintf(info , "[Log] RECIEVE seq = %d, len = %d ,check = %d ,NowAck = %d\n", m.seq, m.len, m.check, NowAck);
	std::string s = info;
	SYSTEMTIME sysTime;
	GetSystemTime(&sysTime);
	logger(s,sysTime);
}

static void sendLog(msg m) {
	char info[100];
	sprintf(info,"[Log] SEND ack = %d , checkSum = %d\n", m.ack,m.check);
	std::string s = info;
	SYSTEMTIME sysTime;
	GetSystemTime(&sysTime);
	logger(s,sysTime);

}

_Server::_Server() {

}

int _Server::server_init() {
	SYSTEMTIME sysTime;
	Server_log.open("server_log.txt", std::ios::out|std::ios::ate);
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("socket Error: %s (errno: %d)\n", strerror(errno), errno);
		return 1;
	}

	if ((Server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		printf("socket Error: %s (errno: %d)\n", strerror(errno), errno);
		return 1;
	}
	GetSystemTime(&sysTime);
	std::string s("Server Socket Start Up!");
	logger(s,sysTime);
	
	//初始化addr与伪首部
	client_addr.sin_family = AF_INET;       //IPV4
	client_addr.sin_port = htons(clientPort);     //PORT:8888,htons将主机小端字节转换为网络的大端字节
	inet_pton(AF_INET, clientIP.c_str(), &client_addr.sin_addr.S_un.S_addr);

	server_addr.sin_family = AF_INET;       //IPV4
	server_addr.sin_port = htons(serverPort);
	inet_pton(AF_INET, serverIP.c_str(), &server_addr.sin_addr.S_un.S_addr);

	sendHead.desIP = client_addr.sin_addr.S_un.S_addr;
	sendHead.srcIP = server_addr.sin_addr.S_un.S_addr;

	recvHead.desIP = server_addr.sin_addr.S_un.S_addr;
	recvHead.srcIP = client_addr.sin_addr.S_un.S_addr;

	addrlen = sizeof(server_addr);

	if (bind(Server, (LPSOCKADDR)&server_addr, addrlen) == SOCKET_ERROR) { //将Server与server_addr绑定
		printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
		return 1;
	}
	printf("Set the Window Size(4-32) : ");
	std::cin >> wndSize;

	if (wndSize < 4)
		wndSize = 4;
	else if (wndSize > 32)
		wndSize = 32;
	printf("\nWndSize : %d\n", wndSize);

	int nRecvBuf = 2*wndSize * MSS;//设置接收缓冲区大小  否则会出现丢包

	setsockopt(Server, SOL_SOCKET, SO_RCVBUF, (const char*)&nRecvBuf, sizeof(int));

	std::string str("Waiting for Connection....");
	GetSystemTime(&sysTime);
	logger(str,sysTime);
	
	recvfile();
	WSACleanup();
	system("pause");
	return 0;
}


int _Server::handleMsg(msg recvMsg,bool sendack) {

	char info[100];
	std::string s;
	SYSTEMTIME sysTime;
	if (recvMsg.if_SYN()) {  //握手建立连接
		sprintf(info, "[SYN] First_Hand_Shake");
		s = info;
		GetSystemTime(&sysTime);
		logger(s,sysTime);

		cnt_accept(recvMsg.seq + 1);
	}
	else if (recvMsg.if_FIN()) { //挥手断开连接

		sprintf(info, "[FIN] First_Wave");
		s = info;
		GetSystemTime(&sysTime);
		logger(s,sysTime);

		dic_accept(recvMsg.seq + 1);
		return 0;
	}
	else {

		if (recvMsg.if_FDS()) {  //文件头消息
			memset(&descriptor, 0, sizeof(struct FileHead));
			memcpy(&descriptor, recvMsg.message, sizeof(struct FileHead));
			sprintf(info, "[FDS] Start recieving file %s \n", descriptor.filename.c_str());
			s = info;
			GetSystemTime(&sysTime);
			logger(s,sysTime);
			filelen = descriptor.filelen;
			filename = savePath + descriptor.filename;
			lenPointer = 0;
			file.open(filename, std::ios::out | std::ios::binary); //打开新文件，准备写入

		}
		else {
			file.write(recvMsg.message, recvMsg.len); //文件消息,将数据写入file
			lenPointer += recvMsg.len;
			if (lenPointer >= filelen) { //指针移动到末尾，说明已经写入完成
				sprintf(info, "[Log] File %s recieved successfully!\n", filename.c_str());
				s = info;
				GetSystemTime(&sysTime);
				logger(s,sysTime);

				file.close();
			}
		}
		if (sendack) {
			NowAck = recvMsg.seq + 1;
			file_accept(NowAck);  //总是以Seq+1来更新ACK
		}
	}
}

int _Server::recvfile() { //接收文件的函数
	msg recvMsg;
	SYSTEMTIME sysTime;
	char info[100];
	std::string s;
	while (true) {
		if (recvfrom(Server, (char*)&recvMsg, sizeof(msg), 0, (struct sockaddr*)&server_addr, &addrlen) == SOCKET_ERROR) {
			std::string str("[Log] Recieving Error , try again!\n");
			GetSystemTime(&sysTime);
			logger(str,sysTime);
		}
		else {
			if (recvMsg.checkValid(&recvHead) && recvMsg.seq == NowAck) { // 校验和正确 并且Seq与Ack对应，顺序确认
				recvLog(recvMsg);
				handleMsg(recvMsg);
				if (recvMsg.if_FIN()) {
					return 0;
				}
				while (!msgCache.empty() && msgCache.find(NowAck)!=msgCache.end()) {  //如果此时缓存中有下一个要接受的分组，进行接收
					recvMsg = msgCache[NowAck];
					sprintf(info, "[Log] Get Package %d from Cache!\n", recvMsg.seq);
					std::string str = info;
					GetSystemTime(&sysTime);
					logger(str, sysTime);
					handleMsg(recvMsg,false);
					msgCache.erase(NowAck);
					NowAck++;
				}
			}
			else { 
				if (!recvMsg.if_FIN() && recvMsg.checkValid(&recvHead)) {//如果校验和正确，但是乱序，可以提前确认（FIN消息不可提前确认）
					sprintf(info, "[Log] Recieve Package %d, disorder! Store in Cache!\n", recvMsg.seq);
					std::string str = info;
					GetSystemTime(&sysTime);
					logger(str, sysTime);
					file_accept(recvMsg.seq + 1);
					msgCache[recvMsg.seq] = recvMsg;
				}
			}

		}
	}

}

int _Server::cnt_accept(int ack) { // ACK + SYN
	NowAck = ack;
	msg second_shake;
	second_shake.set_srcPort(serverPort);
	second_shake.set_desPort(clientPort);
	second_shake.set_len(0);
	second_shake.set_ACK();
	second_shake.set_SYN(); // SYN+ACK 表示同意连接
	second_shake.set_ack(ack);
	char size = wndSize;
	second_shake.message[0] = size;   //message携带窗口大小信息
	second_shake.set_check(&sendHead);
	sendto(Server, (char*)&second_shake, sizeof(msg), 0, (struct sockaddr*)&client_addr, addrlen);
	sendLog(second_shake);
	char info[50];
	sprintf(info,"[Log] Connection set up!");
	std::string s = info;
	SYSTEMTIME sysTime;
	GetSystemTime(&sysTime);
	logger(s,sysTime);
	
	return 0;
}

int _Server::dic_accept(int ack) { // ACK + FIN
	NowAck = ack;
	msg dic;
	dic.set_srcPort(serverPort);
	dic.set_desPort(clientPort);
	dic.set_len(0);
	dic.set_ACK();
	dic.set_FIN();
	dic.set_ack(ack);
	dic.set_check(&sendHead);
	sendto(Server, (char*)&dic, sizeof(msg), 0, (struct sockaddr*)&client_addr, addrlen);
	sendLog(dic);
	std::thread wait2MSL([&]() {  //等待2MSL的时间
		msg recvMsg;
		while (true) {
			int valid = recvfrom(Server, (char*)&recvMsg, sizeof(msg), 0, (struct sockaddr*)&server_addr, &addrlen);
			if (valid == SOCKET_ERROR) {
				break;
			}
			else {
				if (recvMsg.if_FIN()) {
					sendto(Server, (char*)&dic, sizeof(msg), 0, (struct sockaddr*)&client_addr, addrlen);
				}
			}
		}

	});
	char info[50];
	sprintf(info,"[Log] Wait for 2 MSL...");
	std::string s = info;
	SYSTEMTIME sysTime;
	GetSystemTime(&sysTime);
	logger(s,sysTime);
	Sleep(2 * MSL);
	closesocket(Server);  //等待2MSL时间后，若无连接则关闭Socket，使thread退出

	wait2MSL.join();
	sprintf(info,"[Log] Connection Killed!");
	s = info;
	GetSystemTime(&sysTime);
	logger(s,sysTime);
	Server_log.close();
	return 0;
}

int _Server::file_accept(int ack) { // ACK
	msg msg_recv;
	msg_recv.set_srcPort(serverPort);
	msg_recv.set_desPort(clientPort);
	msg_recv.set_len(0);
	msg_recv.set_ACK();
	msg_recv.set_ack(ack);
	msg_recv.set_check(&sendHead);
	sendto(Server, (char*)&msg_recv, sizeof(msg), 0, (struct sockaddr*)&client_addr, addrlen);
	sendLog(msg_recv);
	return 0;
}