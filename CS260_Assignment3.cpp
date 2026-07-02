#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <chrono>
#include <thread>


void HandleClientRequest(SOCKET client)
{
	char buffer[10000];
	memset(buffer, 0, sizeof(buffer));

	char host[256]{};
	memset(host, 0, sizeof(host));
	int amountReceived = 0;
	int bytesSent = 0;
	int res = 0;

	// receiving HTTP request, end if no more bytes
	do
	{
		res = recv(client, buffer + amountReceived, sizeof(buffer) - amountReceived - 1, 0);
		if (res > 0)
		{
			amountReceived += res;
		}
	} while (res > 0);

	// shutdown client socket for receiving
	shutdown(client, SD_RECEIVE);

	//parse host 
	char* hostInfo = strstr(buffer, "Host: ");
	if (!hostInfo)
	{
		closesocket(client);
		return;
	}
	sscanf_s(hostInfo + (u_int)6, "%255[^\r\n]", host, (u_int)sizeof(host));

	addrinfo hints = {};
	addrinfo* result = nullptr;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	host[256-1] = '\0';
	if (inet_pton(AF_INET, host, &hints.ai_addr) == 1)
	{
		freeaddrinfo(result);
	}
	else if (getaddrinfo(host, "80", &hints, &result) != 0)
	{
		freeaddrinfo(result);
		closesocket(client);
		return;
	}

	//socket to connect to webserver
	SOCKET serverSock = socket(AF_INET, SOCK_STREAM, 0);
	if (result)
	{
		if (connect(serverSock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR)
		{
			closesocket(client);
			closesocket(serverSock);
			return;
		}
	}

	bytesSent = send(serverSock, buffer, amountReceived, 0);
	if (bytesSent != amountReceived)
	{
		closesocket(client);
		closesocket(serverSock);
		return;
	}

	// forward to client socket
	while ((amountReceived = recv(serverSock, buffer, sizeof(buffer) - 1, 0)) > 0) 
	{
		int totalSentToClient = 0;
		while (totalSentToClient < amountReceived) 
		{
			bytesSent = send(client, buffer + totalSentToClient, amountReceived - totalSentToClient, 0);
			if (bytesSent == SOCKET_ERROR) 
			{
				break;
			}
			totalSentToClient += bytesSent;
		}
	}

	shutdown(serverSock, SD_BOTH);
	shutdown(client, SD_SEND);
	closesocket(serverSock);
	closesocket(client);

}



void AcceptUpdate(SOCKET listener)
{
	SOCKET client = accept(listener, NULL, NULL);

	if (client == INVALID_SOCKET)
	{
		//std::cerr << "Accepting client failed: " << WSAGetLastError() << std::endl;
	}
	else
	{
		std::thread clientThread(HandleClientRequest, client);
		clientThread.detach();
	}
}

int InitializeWinsock()
{
	WSADATA wsaData;;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		std::cerr << "Error in WSAStartup: " << WSAGetLastError() << std::endl;
		return 1;
	}
	return 0;
}

sockaddr_in CreateListeningAddr(long port)
{
	sockaddr_in address;
	memset(&address, 0, sizeof(sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_addr.S_un.S_addr = INADDR_ANY;
	address.sin_port = htons((u_short)port);
	return address;
}

int main(int argc, char* argv[])
{

	char* end = NULL;
	long port = strtol(argv[1], &end, 10);

	//check if port is valid
	if (*end != '\0' || port <= 0 || port > 65535)
	{
		std::cerr << "Port number: " << port << " is invalid. Please select a port between 1 and 65535" << std::endl;
	}

	InitializeWinsock();

	//create listening address
	sockaddr_in addy = CreateListeningAddr(port);

	//create listening socket
	SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSock == INVALID_SOCKET)
	{
		std::cerr << "Failed to create a socket " << WSAGetLastError() << std::endl;
		WSACleanup();
		return 1;
	}

	//make socket non-blocking
	u_long arg = 1;
	if (ioctlsocket(listenSock, FIONBIO, &arg) == WSAEWOULDBLOCK)
	{
		std::cerr << "Failed to create non-blocking socket " << WSAGetLastError() << std::endl;
	}

	//bind socket to the address
	bind(listenSock, (sockaddr*)&addy, sizeof(addy));

	//start listening
	if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cerr << "Failed to listen " << WSAGetLastError() << std::endl;
	}
	std::cout << "Listening on port " << port << std::endl;

	while (true)
	{
		AcceptUpdate(listenSock);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	WSACleanup();
}