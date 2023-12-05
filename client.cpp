
//client.cppʵ�ַ��Ͷ˲���
#include "UDP programming.h"
#include <WinSock.h>
#include <IPHlpApi.h>
#include <map>

static std::fstream Client_log; //��־���������Ϊclient_log.txt

std::deque<msg> Cache;		//���ݻ�����
std::deque<msg> CurrWnd;    //��ǰ����

std::deque<int> AckCache;
int isack = 0;
int tobeack = 1;

static int wndSize = 0;
SOCKET Client;
static struct fakeHead sendHead, recvHead;  //α�ײ�
static sockaddr_in server_addr;
static sockaddr_in client_addr;
static int addrlen;
static u_short baseSeq = 0;   //���ڵ���ʼseq,��ǰδ��ȷ�ϵ���Сseq
static u_short nextSeq = 0;		//���ڵ�ĩβseq,ָ��ǰδ�����͵���Сseq
double total_time = 0;
double total_size = 0;

bool ifDone = 0;			//�������߳��������̵߳�ͬ��
bool lossSet = 0;			//�Ƿ����ö�����
int lossrate = 0;			//�����ʣ� ÿlossrate��������һ�ζ���		
int delay = 0;

static std::string currentPath = "./test/";

std::shared_mutex cache_mutex;    //ͨ��mutex��,��ֹ�߳�֮���ͻ
std::shared_mutex wnd_mutex;
std::shared_mutex log_mutex;
std::shared_mutex ack_mutex;


int CachePush(msg message) {    //����Cache���еĲ�����װΪ�̰߳�ȫ��
	std::unique_lock lock(cache_mutex);
	Cache.push_back(message);
	return 0;
}

msg CachePop() {
	std::unique_lock lock(cache_mutex);
	msg message = Cache.front();
	Cache.pop_front();
	return message;
}


int wndPush(msg m) {
	std::unique_lock lock(wnd_mutex);
	CurrWnd.push_back(m);
	return 0;
}

int wndPop() {
	std::unique_lock lock(wnd_mutex);
	CurrWnd.pop_front();
	return 0;
}

int ackPop() {
	std::unique_lock lock(ack_mutex);
	AckCache.pop_front();
	return 0;
}

int ackPush(int ack) {
	std::unique_lock lock(ack_mutex);
	AckCache.push_back(ack);
	return 0;
}




//д����־
static void logger(std::string str, SYSTEMTIME sysTime) {
	printf("%s\n", str.c_str());
	std::string s = std::to_string(sysTime.wMinute) + " : " + std::to_string(sysTime.wSecond)+" : "+ std::to_string(sysTime.wMilliseconds) + "\n" + str+"\n";
	Client_log << s;
	
}

static void sendLog(msg m) {
	char info1[150];
	sprintf(info1, "[Log] Send Package %d, checkSum = %d,\t len = %d, baseSeq = %d, nextSeq = %d, wndSize = %d\n", m.seq, m.check,m.len, baseSeq, nextSeq, CurrWnd.size());
	std::string str = info1;
	SYSTEMTIME sysTime;
	GetSystemTime(&sysTime);
	logger(str,sysTime);
}

static void recvLog(msg m) {
	char info[100];
	sprintf(info, "[Log] Recieve Ack %d, Package %d is accepted,\t checkSum = %d ,\t wndSize = %d\n", m.ack,m.ack-1, m.check,CurrWnd.size());
	std::string str = info;
	SYSTEMTIME sysTime;
	GetSystemTime(&sysTime);
	logger(str,sysTime);
}

_Client::_Client() {
	
}

int _Client::start_client() {
	Client_log.open("client_log.txt", std::ios::out);
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("[Error] Socket Error: %s (errno: %d)\n", strerror(errno), errno);
		return 1;
	}

	if ((Client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		printf("[Error] Socket Error: %s (errno: %d)\n", strerror(errno), errno);
		return 1;
	}

	SYSTEMTIME sysTime;
	GetSystemTime(&sysTime);
	std::string s("[Log] Client Socket Start Up!"); 
	logger(s,sysTime);
	// ��ʼ����ַ��α�ײ�
	client_addr.sin_family = AF_INET;       //IPV4
	client_addr.sin_port = htons(clientPort);     //PORT:8888,htons������С���ֽ�ת��Ϊ����Ĵ���ֽ�
	inet_pton(AF_INET, clientIP.c_str(), &client_addr.sin_addr.S_un.S_addr);

	server_addr.sin_family = AF_INET;       //IPV4
	server_addr.sin_port = htons(serverPort);
	inet_pton(AF_INET, serverIP.c_str(), &server_addr.sin_addr.S_un.S_addr);

	sendHead.srcIP = client_addr.sin_addr.S_un.S_addr;
	sendHead.desIP = server_addr.sin_addr.S_un.S_addr; //α�ײ���ʼ��

	recvHead.srcIP = server_addr.sin_addr.S_un.S_addr;
	recvHead.desIP = client_addr.sin_addr.S_un.S_addr;

	addrlen = sizeof(server_addr);

	if (bind(Client, (LPSOCKADDR)&client_addr, sizeof(client_addr)) == SOCKET_ERROR) { //��Client��client_addr��
		printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
		return 1;
	}

	s = "[Log] Connecting to server..........";
	GetSystemTime(&sysTime);
	logger(s,sysTime);

	printf("set the loss rate(1-99): ");
	std::cin >> lossrate;
	if (lossrate <= 0 || lossrate > 100) {
		lossSet = false;
		std::string str("\nLoss rate out of range! No Loss Rate!\n");
		GetSystemTime(&sysTime);
		logger(str,sysTime);
	}
	else
	{
		lossSet = true;
		lossrate = 100 / lossrate;
		std::string str = "\nMiss occurs every " + std::to_string(lossrate) + " packages\n";
		GetSystemTime(&sysTime);
		logger(str,sysTime);
	}


	printf("set the delay(ms): ");
	std::cin >> delay;
	if (delay < 0) delay = 0;
	s = "\nEach package has a delay of " + std::to_string(delay) + " ms\n";
	GetSystemTime(&sysTime);
	logger(s,sysTime);

	//����������ʱ(��ʱ�ش�)
	int timeout = wait_time;
	setsockopt(Client, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

	cnt_setup();  //��������

	//�޸Ļ�������С(��ʵ���Ͷ˻���������Ҫ, ���ն˻�������С���ǹؼ�)
	int buffer_size = wndSize * MSS;
	setsockopt(Client, SOL_SOCKET, SO_SNDBUF, (const char*)&buffer_size, sizeof(buffer_size));

	int nRecvBuf = 2*wndSize * MSS;//���ý��ջ�������С  �������ֶ���

	setsockopt(Client, SOL_SOCKET, SO_RCVBUF, (const char*)&nRecvBuf, sizeof(int));

	auto begin = std::chrono::steady_clock::now();
	packupFiles(); //��װ�����߳�,�����������ݴ�����뻺����

	sendFiles();  //�������п�λʱ,��������,�����ݴӻ��������봰�ڲ�����
	recvAcks();		//�����߳�
	while (!ifDone) {   //�����߳�һͬ�˳�

	}
	auto after = std::chrono::steady_clock::now();
	total_time = std::chrono::duration<double>(after - begin).count();

	s = "Successfully sent all the files, Time used: "+std::to_string(total_time)+" s, Throughput :" + std::to_string(total_size / total_time)+" Bytes / s";
	GetSystemTime(&sysTime);
	logger(s,sysTime);
	Client_log.close();
	closesocket(Client);
	WSACleanup();


}

void _Client::recvAcks() {
	std::thread recvacks([&]() {
		SYSTEMTIME sysTime;
		while (true) {
			msg recvBuff;
			int recvLen = recvfrom(Client, (char*)&recvBuff, sizeof(msg), 0, (struct sockaddr*)&server_addr, &addrlen);
			// if receive buffer is valid
			if (recvLen > 0) {
				if (recvBuff.checkValid(&recvHead) && recvBuff.ack > baseSeq && (recvBuff.ack <= baseSeq + wndSize)) { //У�����ȷ��ack��seq�ܶ�Ӧ,����������������,���к���
					int acked = recvBuff.ack - 1;
					if (acked == baseSeq) {
						recvLog(recvBuff);
						wndPop();
						baseSeq++;
						while (!AckCache.empty() && AckCache.front() == baseSeq+1) {
							//std::cout << AckCache.front() <<" "<<baseSeq<< CurrWnd.front().seq<< std::endl;
							ackPop();
							wndPop();
							baseSeq++;
							char info[100];
							sprintf(info, "[Log] Package %d accepted by ack in Cache, \tbaseSeq = %d, nextSeq = %d, wndSize = %d\n", baseSeq-1, baseSeq,nextSeq, CurrWnd.size());
							std::string str = info;
							GetSystemTime(&sysTime);
							logger(str, sysTime);
						}
					}
					else{
						ackPush(recvBuff.ack);
						char info[100];
						sprintf(info, "[Log] Recieve Ack %d, store in Cache! \t checkSum = %d ,\t wndSize = %d\n", recvBuff.ack, recvBuff.ack - 1, recvBuff.check, CurrWnd.size());
						std::string str = info;
						GetSystemTime(&sysTime);
						logger(str, sysTime);
					}
					if (recvBuff.if_FIN() && recvBuff.if_ACK()) {
						//������Ϣ
						std::string s("[FIN] Destroy the connection!");
						GetSystemTime(&sysTime);
						logger(s,sysTime);
						ifDone = 1;
						return;
					}
				}
			}
			else {
				msg rsndMsg = CurrWnd.front();
				sendto(Client, (char*)&rsndMsg, sizeof(msg), 0, (struct sockaddr*)&server_addr, addrlen);
				char info[100];
				sprintf(info, "[Rsd] Time out ! Resend Package %d!\n", rsndMsg.seq);
				std::string s = info;
				GetSystemTime(&sysTime);
				logger(s, sysTime);

			}
		}
		});
	recvacks.detach();
}

void _Client::sendFiles() {
	std::thread sendfiles([&]() {
		SYSTEMTIME sysTime;
		msg message;
		while (true) {
			if (CurrWnd.size() < wndSize) {  //�����ֿ�λ(���ڻ���)
				while (Cache.empty()) {

				}
				message = CachePop();    //ȡ��������������
				message.set_seq(nextSeq);
				message.set_check(&sendHead);
				wndPush(message);		//���봰��,������
				if (!lossSet || nextSeq % (lossrate + 1)) {  //û�з�����������û�����ö�����
					Sleep(delay);	//�ӳ���Sleepʵ��
					sendto(Client, (char*)&message, sizeof(msg), 0, (struct sockaddr*)&server_addr, addrlen);
					sendLog(message);
				}
				else {	//��������
					std::string s = "[Miss] Miss package with seq " + std::to_string(message.seq) + "\n";
					GetSystemTime(&sysTime);
					logger(s,sysTime);
				}
				nextSeq++;    //����nextSeq
				if (message.if_FIN()) {
					return;
				}
			}
		}
		});
	sendfiles.detach();
}

void _Client::packupFiles() {    //����߳�
	std::thread packupfiles([&]() {
		for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {  //��ȡ�����ļ�entry
			packupFile(entry);  
		}
		msg wave;
		wave.set_srcPort(clientPort);
		wave.set_desPort(serverPort);
		wave.set_len(0);
		wave.set_seq(baseSeq);
		wave.set_FIN();  //����Finλ
		wave.set_check(&sendHead);
		CachePush(wave);     //���Ҫ���һ��FIN��Ϣ

		Client_log << "[Log] Packed all files!" << std::endl;
		return;
	}
	);
	packupfiles.detach();
}

int _Client::packupFile(std::filesystem::directory_entry entry) {
	std::string filename = entry.path().filename().string();

	int filelen = entry.file_size(); //�ļ���С
	total_size += filelen;
	struct FileHead descriptor {
		filename,
		filelen
	};

	msg *fileDescriptor = new msg;   //�ļ�ͷ,�������Ⱥ�����

	fileDescriptor->set_srcPort(clientPort);
	fileDescriptor->set_desPort(serverPort);
	fileDescriptor->set_len(sizeof(struct FileHead));
	fileDescriptor->set_ACK();
	fileDescriptor->set_FDS(); //FileDescriptor,��ʾ����һ����������Ϣ
	fileDescriptor->set_seq(baseSeq);
	fileDescriptor->set_data((char*)&descriptor);
	fileDescriptor->set_check(&sendHead);

	CachePush(*fileDescriptor);   //���뻺����
	delete fileDescriptor;

	std::string path = currentPath + filename;

	std::ifstream input(path, std::ios::binary);

	int segments = ceil((float)filelen / MSS);  //��Ϊ����ڷ���
	msg *file_msg;
	for (int i = 0; i < segments; i++) {
		char buffer[MSS];
		int send_len = (i == segments - 1) ? filelen % MSS : MSS;
		input.read(buffer, send_len);
		
		file_msg = new msg;
		file_msg->set_srcPort(clientPort);
		file_msg->set_desPort(serverPort);
		file_msg->set_len(send_len);
		file_msg->set_ACK();
		file_msg->set_seq(baseSeq);
		file_msg->set_data(buffer);
		file_msg->set_check(&sendHead);
		CachePush(*file_msg);    //���뻺����
		delete file_msg;
		}

	return 0;
}

int _Client::cnt_setup() {
	msg *first_shake = new msg;
	first_shake->set_srcPort(clientPort);
	first_shake->set_desPort(serverPort);
	first_shake->set_len(0);
	first_shake->set_seq(baseSeq);
	first_shake->set_SYN();   //��һ������,������һ��SYN��Ϣ
	first_shake->set_check(&sendHead);
	SYSTEMTIME sysTime;

	char info[50];
	sprintf(info, "[SYN] Seq=%d  len=%d", first_shake->seq, first_shake->len);
	std::string s = info;
	GetSystemTime(&sysTime);
	logger(s,sysTime);

	baseSeq += 1;  //����Seq
	nextSeq += 1;
	sendto(Client, (char*)first_shake, sizeof(msg), 0, (struct sockaddr*)&server_addr, addrlen);
	msg* recv_msg = new msg;
	while (true) {
		
		int result = recvfrom(Client, (char*)recv_msg, sizeof(msg), 0, (struct sockaddr*)&server_addr, &addrlen);
		if (result < 0) {         //��ʱ
			std::string str("[Error] Time out , try to resend!");
			GetSystemTime(&sysTime);
			logger(str,sysTime);

			sendto(Client, (char*)&first_shake, sizeof(msg), 0, (struct sockaddr*)&server_addr, addrlen);
		}
		else { //�ɹ����յ��ظ�
			recvLog(*recv_msg);
			if (recv_msg->checkValid(&recvHead) && recv_msg->ack == baseSeq) {  //У�����ȷ,��Seq��ack��Ӧ

				if (recv_msg->if_SYN()) {   //������Ϣ
					int size = recv_msg->message[0];   //��ȡ���ڴ�С
					wndSize = size;
					sprintf(info,"Set wndSize: %d", size);
					s = info;
					GetSystemTime(&sysTime);
					logger(s,sysTime);

				}
				delete recv_msg;
				delete first_shake;
				break;

			}
			else {
				sendto(Client, (char*)first_shake, sizeof(msg), 0, (struct sockaddr*)&server_addr, addrlen);  //��ʱ�ش�
			}
		}

	}

	sprintf(info, "[Log] Connection Set Up��");
	s = info;
	GetSystemTime(&sysTime);
	logger(s,sysTime);
	return 0;
}
