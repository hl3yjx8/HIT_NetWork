#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <tchar.h>
#pragma comment(lib,"Ws2_32.lib")
#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口

//Http 重要头部数据
struct HttpHeader {
	char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
	char url[1024]; // 请求的 url
	char host[1024]; // 目标主机
	char cookie[1024 * 10]; //cookie
	HttpHeader() { ZeroMemory(this, sizeof(HttpHeader));}
};

//函数声明
BOOL InitSocket();
void ParseHttpHead(char *buffer, HttpHeader * httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket, char *host);
bool ForbiddenToConnect(char *httpheader);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

//代理相关参数
SOCKET ProxyServer;//套接字命名
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;//监听端口号

//由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};

struct ProxyParam { SOCKET clientSocket;  SOCKET serverSocket; };//代理参数,客户机和服务器套接字

/*主函数*/
int _tmain(int argc, _TCHAR* argv[])
{
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()) {  //初始化套接字失败
		printf("socket 初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;//设置为无效套接字
	ProxyParam *lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;  //代理服务器不断监听
	while (true)
	{
		acceptSocket = accept(ProxyServer, NULL, NULL);//从处于监听状态的ProxyServer客户连接请求队列中取出最前的一个客户请求
													   //创建一个新的套接字来与客户套接字创建连接通道
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL)
		{
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;//最前的客户机套接字
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);//创建子线程
		CloseHandle(hThread);//关闭句柄
		Sleep(200);
	}
	closesocket(ProxyServer);//关闭套接字
	WSACleanup();//解除与Socket库的绑定，释放Socket库所占用的系统资源
	return 0;
}


//************************************
// Method:    InitSocket
// FullName:  InitSocket
// Access:    public
// Returns:   BOOL
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket() {
	//加载套接字库（必须）
	WORD wVersionRequested;//指明程序请求使用的Winsock版本，为十六进制整数
	WSADATA wsaData;  //套接字加载时错误提示，返回实际的Winsock版本信息
	int err;  //版本 2.2
	wVersionRequested = MAKEWORD(2, 2);  //加载dll文件Scoket库，MAKEWORD创建一个无符号16位整型
	err = WSAStartup(wVersionRequested, &wsaData);//初始化Windows Socket API)
	if (err != 0) {   //找不到 winsock.dll
		printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {//低位代表主版本，高位代表副版本
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();//释放Socket库占用的
		return FALSE;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);//创建套接字
	if (INVALID_SOCKET == ProxyServer)//非法套接字
	{
		printf("创建套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;//协议族
	ProxyServerAddr.sin_port = htons(ProxyPort);//存储端口号
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;//存储IP地址
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)//绑定套接字的本地端点地址失败
	{
		printf("绑定套接字失败\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR)//监听失败
	{
		printf("监听端口%d 失败", ProxyPort);
		return FALSE;
	}
	return TRUE;
}

//************************************
// Method:    ProxyThread
// FullName:  ProxyThread
// Access:    public
// Returns:   unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter)
{
	char Buffer[MAXSIZE];
	char FishBuffer[MAXSIZE];
	char *CacheBuffer;
	ZeroMemory(Buffer, MAXSIZE);//向Buffer的内存空间填充0
	SOCKADDR_IN clientAddr;//客户机地址
	int length = sizeof(SOCKADDR_IN);//地址长度
	int recvSize;//接收数据的长度
	int ret;
	recvSize = recv(((ProxyParam *)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);//从连接的另一端接收数据
	HttpHeader* httpHeader = new HttpHeader();
	if (recvSize <= 0)//接收的数据块长度为负
	{
		goto error;//跳转到error
	}

	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);//向CacheBuffer的内存空间填充0
	memcpy(CacheBuffer, Buffer, recvSize);//从Buffer中拷贝至CacheBuffer
	ParseHttpHead(CacheBuffer, httpHeader);//解析HTTP头
	delete CacheBuffer;
	if (!ConnectToServer(&((ProxyParam *)lpParameter)->serverSocket, httpHeader->host))//连接到服务器失败
	{
		goto error;
	}
	if (ForbiddenToConnect(httpHeader->host)==false)//网站过滤
	{
		printf("访问非法网站\n");
		goto error;
	}
	printf("代理连接主机 %s 成功\n", httpHeader->host);  //将客户端发送的 HTTP 数据报文直接转发给目标服务器
	ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);

	//等待目标服务器返回数据
	recvSize = recv(((ProxyParam *)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0)
	{
		goto error;
	}  //将目标服务器返回的数据直接转发给客户端
	if (strcmp(httpHeader->host,"pku.edu.cn")==0)
	{   
		char* pr;
		int fishing_len = strlen("HTTP/1.1 302 Moved Temporarily\r\n"); //302报文
		memcpy(FishBuffer, "HTTP/1.1 302 Moved Temporarily\r\n", fishing_len);
		pr = FishBuffer + fishing_len;//重定向到今日哈工大
		fishing_len = strlen("Location: http://today.hit.edu.cn/\r\n\r\n");  //定向的网址
		memcpy(pr, "Location: http://today.hit.edu.cn/\r\n\r\n", fishing_len);//将302报文返回给客户端
		ret = send(((ProxyParam*)lpParameter)->clientSocket, FishBuffer, sizeof(FishBuffer), 0);
		goto error;
	}
	ret = send(((ProxyParam *)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);  //错误处理
error:
	printf("关闭套接字\n");
	Sleep(200);//挂起
	closesocket(((ProxyParam*)lpParameter)->clientSocket);//关闭套接字
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete  lpParameter;
	_endthreadex(0);//结束线程
	return 0;
}

//************************************
// Method:    ParseHttpHead
// FullName:  ParseHttpHead
// Access:    public
// Returns:   void
// Qualifier: 解析 TCP 报文中的 HTTP 头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char *buffer, HttpHeader * httpHeader)
{
	char *p;
	char *ptr;
	const char * delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	printf("%s\n", p);
	if (p[0] == 'G')//GET 方式
	{
		memcpy(httpHeader->method, "GET", 3);//拷贝至method
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);//拷贝至URL
	}
	else if (p[0] == 'P')//POST 方式
	{
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	printf("%s\n", httpHeader->url);
	p = strtok_s(NULL, delim, &ptr);//提取第二行
	while (p)
	{
		switch (p[0])
		{
		case 'H'://Host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);//拷贝主机域名
			break;
		case 'C'://Cookie
			if (strlen(p) > 8)
			{
				char header[8];
				ZeroMemory(header, sizeof(header));//用0填充
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie"))//比较header字串不是Cookie
				{
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//************************************
// Method:    ConnectToServer
// FullName:  ConnectToServer
// Access:    public
// Returns:   BOOL
// Qualifier: 根据主机创建目标服务器套接字，并连接
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET *serverSocket, char *host)
{
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;//协议族
	serverAddr.sin_port = htons(HTTP_PORT);//端口
	HOSTENT *hostent = gethostbyname(host);//返回对应于给定主机名的包含主机名字和地址信息的hostent结构的指针
	if (!hostent)//返回指针为空
	{
		return FALSE;
	}
	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));//inet_addr将点分十进制的IP转换成长整数型
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);//创建套接字
	if (*serverSocket == INVALID_SOCKET)//套接字无效
	{
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)//连接远端服务器失败
	{
		closesocket(*serverSocket);//关闭套接字
		return FALSE;
	}
	return TRUE;
}


/*网站过滤*/
bool ForbiddenToConnect(char *httpheader)
{
	char * forbiddernUrl = "jwts.hit.edu.cn"; //屏蔽的含有关键字的网址
	if (strstr(httpheader, forbiddernUrl) != NULL) //是否含有屏蔽的关键字
	{
		return false;
	}
	else return true;
}
