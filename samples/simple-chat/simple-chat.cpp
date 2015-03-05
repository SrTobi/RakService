// Only for kbhit which is not cross-platform
// If you don't have, remove it and remove the call to kbhit(),
// it'll block the loop each iteration but work.
#include <conio.h> // Defines kbhit

#include <iostream> // Defines std::cin, std::cout, ...
#include <string> // Defines std::string
#include <sstream> // Defines std::istringstream

// RakNet headers
#include "RakAssert.h"
#include "MessageIdentifiers.h"
//#include <NetworkIDManager.h>
#include "RakPeerInterface.h"
#include "NetworkIDManager.h"
#include "PacketConsoleLogger.h"

#include "protocol.hpp"

using namespace std;
using namespace RakNet;

enum
{
	MAX_CLIENTS = 10,
	SERVER_PORT = 60000,
};

// Moved out of main - needs global scope
bool isServer = false;

bool AskIsServer()
{
	while (true)
	{
		cout << "(C)lient or (S)erver?" << endl;

		char answer;
		cin >> answer;
		cin.sync();
		answer = tolower(answer);

		if (answer == 's')
			return true;
		if (answer == 'c')
			return false;
	}
}

string AskServerIP()
{
	cout << "Enter server IP or hit enter for 127.0.0.1" << endl;

	// Input a line (so that return returns an empty line).
	string answerLine;
	getline(cin, answerLine);
	cin.sync();

	// Get the first word of that line as the answer.
	istringstream iss(answerLine);
	string answer;
	iss >> answer;

	// Return the answer.
	if (answer.empty())
		return "127.0.0.1";
	else
		return answer;
}

// Some example function we want to call using AutoRPC.
class TestServiceImpl : public TestService
{
public:
	virtual void print(RakNet::RakString _test, std::function<void() > done) override
	{
		cout << _test << std::endl;
		done();
	}
};


int main()
{
	auto* info = TestService::MetaInfo();
	std::cout << "Service " << info->name() << std::endl;
	for (auto& finfo : info->functions())
	{
		std::cout << "\t[" << int(finfo.id()) << "] " << finfo.name() << "(" << finfo.signatur() << ")" << std::endl;
	}
	std::cout << std::endl;


	RakPeerInterface* peer = RakPeerInterface::GetInstance();

	// Ask the user if this process should be a client or server.
	isServer = AskIsServer();

	if (isServer)
	{//Act as server
		cout << "Starting the server..." << endl;
		peer->Startup(MAX_CLIENTS, &SocketDescriptor(SERVER_PORT, 0), 1);

		// We need to let the server accept incoming connections from the clients
		peer->SetMaximumIncomingConnections(MAX_CLIENTS);
	}
	else
	{//Act as client
		string serverIP = AskServerIP();

		cout << "Starting the client..." << endl;
		peer->Startup(1, &SocketDescriptor(), 1);

		cout << "Connecting to server on " << serverIP << ':' << SERVER_PORT << endl;
		peer->Connect(serverIP.c_str(), SERVER_PORT, 0, 0);
	}


	// Register RPC.
	NetworkIDManager networkIDManager;
	RakNet::PacketLogger logger;
	RakNet::RakServicePlugin srvPlugin(&networkIDManager);
	peer->AttachPlugin(&srvPlugin);
	//peer->AttachPlugin(&logger);
	// rpc3.SetNetworkIDManager(&networkIDManager);


	TestService* service = nullptr;
	if (isServer)
	{
		service = new TestServiceImpl();
		srvPlugin.AddService("test", service);
	}

	// Main loop.
	while (true)
	{
		// If we are client and we typed some key, ask to input some chat message.
		if (service && !isServer && _kbhit() != 0)
		{
			// Input some chat message.
			cout << "\nBlocking message pump; Enter a chat message: ";
			string chatMessage;
			getline(cin, chatMessage);
			cin.sync();

			// RPC call.
			cout << "RPC call PrintMessage(\"" << chatMessage << "\")" << endl;
			service->print(RakNet::RakString(chatMessage.c_str()), []() {std::cout << "Call returned!" << std::endl; });
		}

		// Get the next packet.
		Packet* packet = peer->Receive();

		// Process that packet.
		if (packet)
		{
			switch (packet->data[0])
			{
			case ID_REMOTE_DISCONNECTION_NOTIFICATION:
				cout << "Another client has disconnected." << endl;
				break;

			case ID_REMOTE_CONNECTION_LOST:
				cout << "Another client has lost the connection." << endl;
				break;

			case ID_REMOTE_NEW_INCOMING_CONNECTION:
				cout << "Another client has connected." << endl;
				break;

			case ID_CONNECTION_REQUEST_ACCEPTED:
				cout << "Our connection request has been accepted." << endl;


				if (!isServer)
				{
					RakNet::SystemAddress other;
					unsigned short num = 1;
					peer->GetConnectionList(&other, &num);
					RakAssert(num == 1);
					srvPlugin.ConnectService<TestService>("test", other,
						[&](TestService* _service)
					{
						service = _service; std::cout << "Service connected!\n";
					});
				}
				break;

			case ID_NEW_INCOMING_CONNECTION:
				cout << "A connection is incoming." << endl;
				break;

			case ID_NO_FREE_INCOMING_CONNECTIONS:
				cout << "The server is full." << endl;
				break;

			case ID_DISCONNECTION_NOTIFICATION:
				if (isServer)
					cout << "A client has disconnected." << endl;
				else
					cout << "We have been disconnected." << endl;
				break;

			case ID_CONNECTION_LOST:
				if (isServer)
					cout << "A client lost the connection." << endl;
				else
					cout << "Connection lost." << endl;
				break;

			default:
				cout << "Message with identifier " << packet->data[0] << " has arrived." << endl;;
				break;
			}

			peer->DeallocatePacket(packet);
		}
	}

	RakPeerInterface::DestroyInstance(peer);

	return 0;
}