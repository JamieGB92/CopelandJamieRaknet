#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
#include "BitStream.h"

#include <iostream>
#include <string>
#include <istream>
#include <sstream>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>

using namespace std;
static unsigned int SERVER_PORT = 65000;
static unsigned int CLIENT_PORT = 65001;
static unsigned int MAX_CONNECTIONS = 4;

enum NetworkState
{
	NS_Init = 0,
	NS_PendingStart,
	NS_Started,
	NS_Lobby,
	NS_Pending,
	NS_Waiting_for_others,
	NS_PlayerMessage,
	NS_PlayerLobby,
	NS_GamePlayer_Choosing,
	NS_GamePlayer_ReadytoFight,
	NS_GamePlayerReady,
	NS_GameStateLoop,
	NS_GamePlayer_Blocked


};

bool isServer = false;
bool isRunning = true;

RakNet::RakPeerInterface *g_rakPeerInterface = nullptr;
RakNet::SystemAddress g_serverAddress;

std::mutex g_networkState_mutex;
NetworkState g_networkState = NS_Init;

enum {
ID_THEGAME_LOBBY_READY = ID_USER_PACKET_ENUM,
ID_PLAYER_READY,
ID_THEGAME_START,
ID_GAMECHAT,
ID_GAMECHAT_MESSAGE,
ID_GAMELOOP,
ID_GAMELOOP_PREFIGHT,
ID_GAMELOOP_FIGHT,
ID_GAMELOOP_SETUP,
ID_ALLPLAYERS_READY,
ID_UPDATE_PLAYER_STATES,
ID_GAMELOOP_POSTFIGHT_MESSAGE,
 };

enum EPlayerClass
{
	Knight = 0,
	Rogue,
	Cleric,
};
enum PlayerState {

	Waiting,
	Choosing

};

struct SPlayer
{
	std::string m_name;
	bool isReady = false;
	EPlayerClass m_class;
	PlayerState pState;
	//NetworkState pState;
	 int m_health;
	unsigned int m_attack;
	unsigned int m_blocks;
	bool isDead()
	{
		if (m_health <= 0)
		{
			return true;
		}
		return false;
	}
	bool isSetup = false;
	bool readyToFight = false;
	bool isBlocking = false;
	bool isAttacking = false;
	string message;
	string Target;
	string Class;
	//function to send a packet with name/health/class etc
	void SendName(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(m_name.c_str());
		writeBs.Write(name);
		
		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}
	void processStates()
	{
		if (pState == Waiting)
		{
			g_networkState = NS_Pending;
		}
		 if (pState == Choosing)
		{
			g_networkState = NS_GamePlayer_Choosing;
		}
	}
	void resetforTurn()
	{
		isBlocking = false;
		isAttacking = false;
		readyToFight = false;
	}
	void setupPlayer()
	{
		
		if (m_class == Rogue)
		{
			m_health = 20;
			m_attack = 20;
			m_blocks = 3;
			Class = "Rogue";
		}
		if (m_class == Knight)
		{
			m_health = 10;
			m_attack = 30;
			m_blocks = 2;
			Class= "Kight" ;
		}
		if (m_class == Cleric)
		{
			m_health = 30;
			m_attack = 10;
			m_blocks = 2;
			Class = "Cleric";
		}
		string Temp = m_name + " is a " + Class;
		cout << Temp << endl;
		isSetup = true;


		//returns 0 when something is wrong
		//assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}
	void sendReady(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		if (g_networkState == NS_GameStateLoop)
		{
			RakNet::BitStream writeBs;
			writeBs.Write((RakNet::MessageID)ID_THEGAME_START);
			RakNet::RakString name("Ready");
			writeBs.Write(name);
			//std::cout << "sendReady Ran" << std :: endl;
		} 
		
	}
	void setTarget(string PlayerName)
	{
		Target = PlayerName;
	}
	void displayMessageToPlayers(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_GAMECHAT_MESSAGE);
		RakNet::RakString name(message.c_str());
		writeBs.Write(name);
		
		
		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
		message.clear();
	}
	
};
#pragma region Dont need to look

std::map<unsigned long, SPlayer> m_players;

SPlayer& GetPlayer(RakNet::RakNetGUID raknetId)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(raknetId);
	std::map<unsigned long, SPlayer>::iterator it = m_players.find(guid);
	assert(it != m_players.end());//out of bounds check?
	return it->second;
}

bool checkPlayersReady(RakNet::Packet* packet)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);

	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{

		SPlayer& player = it->second;
		if (!player.isReady)
		{
			return false;
		}

	}
	RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_UPDATE_PLAYER_STATES);
	RakNet::RakString name('G');
	writeBs.Write(name);
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));

	cout << "allplayers ready" << endl;
	return true;
	
}




void OnLostConnection(RakNet::Packet* packet)
{
	SPlayer& lostPlayer = GetPlayer(packet->guid);
	lostPlayer.SendName(RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
	unsigned long keyVal = RakNet::RakNetGUID::ToUint32(packet->guid);
	m_players.erase(keyVal);
}

//server
void OnIncomingConnection(RakNet::Packet* packet)
{
	//must be server in order to recieve connection
	assert(isServer);
	m_players.insert(std::make_pair(RakNet::RakNetGUID::ToUint32(packet->guid), SPlayer()));

	std::cout << "Total Players: " << m_players.size() << std::endl;
	
}

//client
void OnConnectionAccepted(RakNet::Packet* packet)
{
	//server should not ne connecting to anybody, 
	//clients connect to server
	assert(!isServer);
	g_networkState_mutex.lock();
	g_networkState = NS_Lobby;
	g_networkState_mutex.unlock();
	g_serverAddress = packet->systemAddress;
}

//this is on the client side

void OnLobbyReady(RakNet::Packet* packet)//server
{
	//read packet 
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);
	player.m_name = userName;
	std::cout << userName.C_String() << " aka " << player.m_name.c_str() << " IS RECIEVING!!!!" << std::endl;

	//notify all other connected players that this plyer has joined the game
	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		//skip over the player who just joined
		if (guid == it->first)
		{
			continue;
		}

		SPlayer& player = it->second;
		player.SendName(packet->systemAddress, false);
		/*RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(player .m_name.c_str());
		writeBs.Write(name);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false));*/
	}
	

	player.SendName(packet->systemAddress, true);

	if (m_players.size() >0)
	{
		RakNet::BitStream bs;
		bs.Write((RakNet::MessageID)ID_PLAYER_READY);
		
		
	//RakNet::RakString name(player.m_name.c_str());
	//	bs.Write(name);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
		g_networkState_mutex.lock();
		g_networkState = NS_GamePlayerReady;
		g_networkState_mutex.unlock();
	}
	/*RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
	RakNet::RakString name(player.m_name.c_str());
	writeBs.Write(name);
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, true));*/

}

void DisplayPlayerReady(RakNet::Packet* packet)
{

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	

std::cout << userName.C_String() << " has joined" << std::endl;


}

void OnPlayerReady(RakNet::Packet* packet)//server
{
	//std::cout << "on Playerready is running" << std::endl;
	//read packet 
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	//std::cout << "on Playerready is running" << std::endl;

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);
	//player.m_name = userName;
	//std::cout << "on Playerready is running" << std::endl;
	if (userName=="Ready")
	{
		//std::cout << "Pooop" << std::endl;
		player.isReady = true;
		std::cout << player.m_name.c_str() << " is Ready" << std::endl;
		
	}
	//std::cout << "on Playerready is running" << std::endl;
	//notify all other connected players that this plyer has joined the game
	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		//skip over the player who just joined
		if (guid == it->first)
		{
			
		continue;
		}
		
		SPlayer& player = it->second;
		player.sendReady(packet->systemAddress, false);
		
		/*RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(player .m_name.c_str());
		writeBs.Write(name);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false));*/
	}


	player.sendReady(packet->systemAddress, true);

	if (m_players.size() > 0)
	{
		
		/*	RakNet::RakString name(userInput);
		bs.Write(name);*/

		//returns 0 when something is wrong
		
	}
	/*RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
	RakNet::RakString name(player.m_name.c_str());
	writeBs.Write(name);
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, true));*/

}

unsigned char GetPacketIdentifier(RakNet::Packet *packet)
{
	if (packet == nullptr)
		return 255;

	if ((unsigned char)packet->data[0] == ID_TIMESTAMP)
	{
		RakAssert(packet->length > sizeof(RakNet::MessageID) + sizeof(RakNet::Time));
		return (unsigned char)packet->data[sizeof(RakNet::MessageID) + sizeof(RakNet::Time)];
	}
	else
		return (unsigned char)packet->data[0];
}
#pragma endregion
void DisplayPlayerMessages(RakNet::Packet* packet)
{
	//read packet 
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	std::cout << userName.C_String() << endl;
}
void changePlayerStates(RakNet::Packet* packet)
{

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString targetState;
	bs.Read(targetState);
	string temp = targetState.C_String();

	if (targetState == 'P')
	{
		g_networkState = NS_Pending;
	}
	else if (targetState == 'C')
	{
		g_networkState = NS_GamePlayer_Choosing;
	}
	else if (targetState == 'W')
	{
		g_networkState = NS_Waiting_for_others;
	}
	else if (targetState == 'G')
	{
		g_networkState = NS_GameStateLoop;
	}
	
}

void OnPlayerMessage(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);
	player.message = userName;
	string temp = player.m_name + " says:" + player.message;
	cout << player.m_name<<" says:"<<player.message << endl;
	//notify all other connected players that this plyer has joined the game
	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		
		SPlayer& player = it->second;
		player.displayMessageToPlayers(packet->systemAddress, false);
		
	}
	
	player.displayMessageToPlayers(packet->systemAddress, true);

	if (m_players.size() >= 0)
	{
		//string temp =player.m_name+ " says:"+player.message;
		userName = temp.c_str();
		RakNet::BitStream bs;
		bs.Write((RakNet::MessageID)ID_GAMECHAT_MESSAGE);
		RakNet::RakString name(userName);
		bs.Write(name);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
		g_networkState_mutex.lock();
		//g_networkState = NS_GamePlayerReady;
		g_networkState_mutex.unlock();
	}
	
}

void playerSetUp(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userClass;
	bs.Read(userClass);
	SPlayer& player = GetPlayer(packet->guid);

	if (userClass == 'k')
	{
		player.m_class = Knight;
	}
	if (userClass == 'c')
	{
		player.m_class = Cleric;
	}
	if (userClass == 'r')
	{
		player.m_class = Rogue;
	}
	else
	{

	}
	
	player.setupPlayer();

}
void setPlayerStates(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userClass;
	bs.Read(userClass);
	SPlayer& player = GetPlayer(packet->guid);
	//cout << userClass << endl;
	if (userClass == 'a') {
		player.isAttacking = true;
		player.isBlocking = false;
		player.readyToFight = true;
	}
	 if (userClass == 'b')
	{
		 //cout << "block" << endl;
		player.isBlocking = true;
		player.isAttacking = false;
		player.readyToFight = true;
	}
}
void proccessFight(SPlayer& a, SPlayer& b)
{
	
	if (a.isAttacking&&b.isAttacking)
	{
		a.m_health -= b.m_attack;
		b.m_health -= a.m_attack;
		//cout << "a a" << endl;
	}
	if (a.isAttacking&&b.isBlocking)
	{
		b.m_health -= a.m_attack / 2;
		b.m_blocks--;
		//cout << " a b" << endl;
	}
	
	

	
	

	
}
void FIGHT(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	//std::cout << "on Playerready is running" << std::endl;


	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);
	string Target = userName;
	player.Target = Target;
	cout << player.Target<< endl;
	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		SPlayer& player2 = it->second;
		cout << player2.m_name << endl;
		string temp;

		if (player2.m_name == player.Target&&player2.readyToFight)
		{
			if (player2.readyToFight) {

				cout << "Fight!!!" << endl;
				temp = player.m_name + " the " + player.Class + " Challanges "
					+ player2.m_name + " the " + player2.Class;
				cout << temp << endl;

				player.message = temp;
				player2.message = temp;
				proccessFight(player, player2);
				temp = player.m_name + "'s health is at " + std::to_string(player.m_health) + " with " + std::to_string(player.m_blocks) + " blocks left \n" +
					player2.m_name + "'s health is at " + std::to_string(player2.m_health) + " with " + std::to_string(player2.m_blocks) + " blocks left";
				player.message = temp;
				player2.message = temp;
				cout << temp << endl;

				
			}
		}
		
	}
	
	RakNet::BitStream bss;
	bss.Write((RakNet::MessageID)ID_UPDATE_PLAYER_STATES);
	RakNet::RakString Action('C');
	bss.Write(Action);
	assert(g_rakPeerInterface->Send(&bss, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));


	
}



void InputHandler()
{
	NetworkState temp;
	while (isRunning)
	{
		char userInput[255];
		if (g_networkState == NS_Init)
		{
			std::cout << "press (s) for server (c) for client" << std::endl;
			std::cin >> userInput;
			isServer = (userInput[0] == 's');
			g_networkState_mutex.lock();
			g_networkState = NS_PendingStart;
			g_networkState_mutex.unlock();
			
		}
		else if (g_networkState == NS_Lobby)
		{
			std::cout << "Enter your name to play or type quit to leave" << std::endl;
			std::cin >> userInput;
			//quitting is not acceptable in our game, create a crash to teach lesson
			assert(strcmp(userInput, "quit"));

			//write peacket
			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_THEGAME_LOBBY_READY);
			RakNet::RakString name(userInput);
			bs.Write(name);

			//returns 0 when something is wrong
			assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			g_networkState_mutex.lock();
			g_networkState = NS_GamePlayerReady;
			g_networkState_mutex.unlock();
		}
			
		else if (g_networkState == NS_GamePlayerReady)
		{

			if (!isServer) {

				std::cout << "if you are ready to play type :""Ready"" " << std::endl;
				std::cout << "if you wish to send a message, type:""Message""" << std::endl;
				//std::string poop;
				std::cin >> userInput;
				if (userInput[0] == 'R'&&userInput[1] == 'e'&&userInput[2] == 'a'&&userInput[3] == 'd'&&userInput[4] == 'y')
				{
					RakNet::BitStream bs;
					bs.Write((RakNet::MessageID)ID_THEGAME_START);
					RakNet::RakString name(userInput);
					bs.Write(name);

					assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
					g_networkState_mutex.lock();
					g_networkState =NS_Pending;
					
					g_networkState_mutex.unlock();
				}
				else if (userInput[0] == 'M'&&userInput[1] == 'e'&&userInput[2] == 's'&&userInput[3] == 's'&&userInput[4] == 'a'
					&&userInput[5]=='g'&&userInput[6]=='e')
				{
					temp = g_networkState;
					g_networkState = NS_PlayerLobby;
					
				}

				else
				{
					g_networkState_mutex.lock();
					g_networkState = NS_Pending;
					g_networkState_mutex.unlock();

					
					//doOnce = false;
				}
			}
		}
		else if (g_networkState == NS_PlayerLobby)
		{
			string message;

			cout << "Type your message now!" << std::endl;
			//cin>>message;
			cin.ignore();
			getline(cin, message);

			//cout << "your message-> " << message << endl;
			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_GAMECHAT);
			RakNet::RakString sMessage(message.c_str());
			bs.Write(sMessage);

			assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			g_networkState_mutex.lock();
			g_networkState = temp;
			g_networkState_mutex.unlock();
		}
		else if (g_networkState == NS_GameStateLoop)
		{
			if (!isServer) {
				std::cout << "Choose your class or" << endl;
				cout<< "type message to send a Message" << std::endl;
				cout << "r->Rogue: 20HP 20A 3 Blocks" << endl;
				cout << "k->Knight: 20HP 30A 2 Blocks" << endl;
				cout << "c->Cleric: 30HP 10A 2 Blocks" << endl;
				std::cin >> userInput;

				if (userInput[0] == 'M'&&userInput[1] == 'e'&&userInput[2] == 's'&&userInput[3] == 's'&&userInput[4] == 'a'
					&&userInput[5] == 'g'&&userInput[6] == 'e')
				{
					temp = g_networkState;
					g_networkState = NS_PlayerLobby;
				}
				else {

					RakNet::BitStream bs;
					bs.Write((RakNet::MessageID)ID_GAMELOOP_SETUP);
					RakNet::RakString Action(userInput);
					bs.Write(Action);

					//returns 0 when something is wrong
					assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
					g_networkState_mutex.lock();
					g_networkState = NS_GamePlayer_Choosing;
					g_networkState_mutex.unlock();
				}
			}
		} 
		else if (g_networkState==NS_GamePlayer_Choosing)
		{
			if (!isServer) {
				std::cout << "Choose Action" << std::endl;
				cout << "(a) to Attack (b) to Block" << endl;
				std::cin >> userInput;

				if (userInput[0] == 'a')
				{
					RakNet::BitStream bs;
					bs.Write((RakNet::MessageID)ID_GAMELOOP_PREFIGHT);
					RakNet::RakString Action(userInput);
					bs.Write(Action);

					//returns 0 when something is wrong
					assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
					g_networkState_mutex.lock();
					g_networkState = NS_GamePlayer_ReadytoFight;
					g_networkState_mutex.unlock();
				}

				else if (userInput[0] == 'b')
				{

					RakNet::BitStream bs;
					bs.Write((RakNet::MessageID)ID_GAMELOOP_PREFIGHT);
					RakNet::RakString Action(userInput);
					bs.Write(Action);

					//returns 0 when something is wrong
					assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
					g_networkState_mutex.lock();
					g_networkState = NS_Waiting_for_others;
					g_networkState_mutex.unlock();

				}
				else
				{
					g_networkState = NS_GamePlayer_Choosing;
				}
				
			}
		}
		else if (g_networkState == NS_GamePlayer_ReadytoFight)
		{
			if (!isServer) {
				std::cout << "Choose Target (type their name)" << std::endl;
				string Target;
				cin.ignore();
				getline(cin, Target);
				

				RakNet::BitStream bs;
				bs.Write((RakNet::MessageID)ID_GAMELOOP_FIGHT);
				RakNet::RakString Action(Target.c_str());
				bs.Write(Action);
				g_networkState = NS_Waiting_for_others;
				//returns 0 when something is wrong
				assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
				//g_networkState = NS_Pending;

			}
		}
		else if (g_networkState == NS_Pending)
		{
			static bool doOnce = false;
			if(!doOnce)
				std::cout << "pending..." << std::endl;

			doOnce = true;
		
			
		}
		else if (g_networkState == NS_Waiting_for_others)
		{
			static bool doOnce = false;
			if (!doOnce)
				std::cout << "waiting for other players..." << std::endl;

			doOnce = true;


		}
		
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}	
}

bool HandleLowLevelPackets(RakNet::Packet* packet)
{
	bool isHandled = true;
		// We got a packet, get the identifier with our handy function
		unsigned char packetIdentifier = GetPacketIdentifier(packet);

		// Check if this is a network message packet
		switch (packetIdentifier)
		{
		case ID_DISCONNECTION_NOTIFICATION:
			// Connection lost normally
			printf("ID_DISCONNECTION_NOTIFICATION\n");
			break;
		case ID_ALREADY_CONNECTED:
			// Connection lost normally
			printf("ID_ALREADY_CONNECTED with guid %" PRINTF_64_BIT_MODIFIER "u\n", packet->guid);
			break;
		case ID_INCOMPATIBLE_PROTOCOL_VERSION:
			printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
			break;
		case ID_REMOTE_DISCONNECTION_NOTIFICATION: // Server telling the clients of another client disconnecting gracefully.  You can manually broadcast this in a peer to peer enviroment if you want.
			printf("ID_REMOTE_DISCONNECTION_NOTIFICATION\n");
			break;
		case ID_REMOTE_CONNECTION_LOST: // Server telling the clients of another client disconnecting forcefully.  You can manually broadcast this in a peer to peer enviroment if you want.
			printf("ID_REMOTE_CONNECTION_LOST\n");
			break;
		case ID_NEW_INCOMING_CONNECTION:
			//client connecting to server
			OnIncomingConnection(packet);
			printf("ID_NEW_INCOMING_CONNECTION\n");
			break;
		case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
			OnIncomingConnection(packet);
			printf("ID_REMOTE_NEW_INCOMING_CONNECTION\n");
			break;
		case ID_CONNECTION_BANNED: // Banned from this server
			printf("We are banned from this server.\n");
			break;
		case ID_CONNECTION_ATTEMPT_FAILED:
			printf("Connection attempt failed\n");
			break;
		case ID_NO_FREE_INCOMING_CONNECTIONS:
			// Sorry, the server is full.  I don't do anything here but
			// A real app should tell the user
			printf("ID_NO_FREE_INCOMING_CONNECTIONS\n");
			break;

		case ID_INVALID_PASSWORD:
			printf("ID_INVALID_PASSWORD\n");
			break;

		case ID_CONNECTION_LOST:
			// Couldn't deliver a reliable packet - i.e. the other system was abnormally
			// terminated
			printf("ID_CONNECTION_LOST\n");
			OnLostConnection(packet);
			break;

		case ID_CONNECTION_REQUEST_ACCEPTED:
			// This tells the client they have connected
			printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n", packet->systemAddress.ToString(true), packet->guid.ToString());
			printf("My external address is %s\n", g_rakPeerInterface->GetExternalID(packet->systemAddress).ToString(true));
			OnConnectionAccepted(packet);
			break;
		case ID_CONNECTED_PING:
		case ID_UNCONNECTED_PING:
			printf("Ping from %s\n", packet->systemAddress.ToString(true));
			break;
		default:
			isHandled = false;
			break;
		}
		return isHandled;
}

void PacketHandler()
{
	while (isRunning)
	{
		for (RakNet::Packet* packet = g_rakPeerInterface->Receive(); packet != nullptr; g_rakPeerInterface->DeallocatePacket(packet), packet = g_rakPeerInterface->Receive())
		{
			if (!HandleLowLevelPackets(packet))
			{
				//our game specific packets
				unsigned char packetIdentifier = GetPacketIdentifier(packet);
				switch (packetIdentifier)
				{
				case ID_THEGAME_LOBBY_READY:
					OnLobbyReady(packet);
					break;
				case ID_PLAYER_READY:

						DisplayPlayerReady(packet);
					
					break;
				case ID_GAMECHAT:
					OnPlayerMessage(packet);
					break;
				case ID_GAMECHAT_MESSAGE:
					
							DisplayPlayerMessages(packet);
				
				
					break;
				case ID_THEGAME_START:
					
					OnPlayerReady(packet);
					checkPlayersReady(packet);
					
					break;
				case ID_GAMELOOP_SETUP:
					//cout << "gameloop" << endl;
					playerSetUp(packet);
					break;
				case ID_GAMELOOP_PREFIGHT:
				//	cout << "prefoght" << endl;
					setPlayerStates(packet);
					break;
				case ID_GAMELOOP_FIGHT:
					
					FIGHT(packet);
					//cout << "Fighting" << endl;
					OnPlayerMessage(packet);
					DisplayPlayerMessages(packet);
					
					break;
				case ID_UPDATE_PLAYER_STATES:
					
					if (!isServer)
					{
						
						changePlayerStates(packet);
					}
					break;
				case ID_ALLPLAYERS_READY:
					
					std::cout << "Lobby" << std::endl;
					g_networkState_mutex.lock();
					g_networkState = NS_GamePlayerReady;
					g_networkState_mutex.unlock();
					break;
				default:
					break;
				}
			}
		}
		
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}
}

int main()
{
	g_rakPeerInterface = RakNet::RakPeerInterface::GetInstance();

	std::thread inputHandler(InputHandler);
	std::thread packetHandler(PacketHandler);

	while (isRunning)
	{
		if (g_networkState == NS_PendingStart)
		{
			if (isServer)
			{
				RakNet::SocketDescriptor socketDescriptors[1];
				socketDescriptors[0].port = SERVER_PORT;
				socketDescriptors[0].socketFamily = AF_INET; // Test out IPV4

				bool isSuccess = g_rakPeerInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
			//	assert(isSuccess);
				//ensures we are server
				g_rakPeerInterface->SetMaximumIncomingConnections(MAX_CONNECTIONS);
				std::cout << "server started" << std::endl;
				g_networkState_mutex.lock();
				g_networkState = NS_Started;
				g_networkState_mutex.unlock();
			}
			//client
			else
			{
				RakNet::SocketDescriptor socketDescriptor(CLIENT_PORT, 0);
				socketDescriptor.socketFamily = AF_INET;

				while (RakNet::IRNS2_Berkley::IsPortInUse(socketDescriptor.port, socketDescriptor.hostAddress, socketDescriptor.socketFamily, SOCK_DGRAM) == true)
					socketDescriptor.port++;

				RakNet::StartupResult result = g_rakPeerInterface->Startup(8, &socketDescriptor, 1);
				assert(result == RakNet::RAKNET_STARTED);
				
				g_networkState_mutex.lock();
				g_networkState = NS_Started;
				g_networkState_mutex.unlock();

				g_rakPeerInterface->SetOccasionalPing(true);
				//"127.0.0.1" = local host = your machines address
				RakNet::ConnectionAttemptResult car = g_rakPeerInterface->Connect("127.0.0.1", SERVER_PORT, nullptr, 0);
				RakAssert(car == RakNet::CONNECTION_ATTEMPT_STARTED);
				std::cout << "client attempted connection..." << std::endl;
				
			}
		}
		
	}

	//std::cout << "press q and then return to exit" << std::endl;
	//std::cin >> userInput;

	inputHandler.join();
	packetHandler.join();
	return 0;
}