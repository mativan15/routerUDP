#include "protocol.h"
#include <unistd.h>
#include <strings.h>
#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <thread>

using namespace std;

map<string, sockaddr_in> clientsToAddr;
map<string, string> addrToClient;
mutex clientsMutex;

static void printClientsLocked() {
  cout << "Complete list of clients:" << endl;
  for (auto &client : clientsToAddr) {
    cout << "    [router] -> client [" << client.first << "] ("
         << addrKey(client.second) << ")" << endl;
  }
}

void sendOK(int SocketFD, const sockaddr_in &clientAddr) {
  sendPayload(SocketFD, clientAddr, "K");
}

void sendError(int SocketFD, const sockaddr_in &clientAddr, const string &error) {
  string payload = "E" + intForBuffer(error.size(), 5) + error;
  sendPayload(SocketFD, clientAddr, payload);
}

static bool requireLogin(int SocketFD, const sockaddr_in &clientAddr, string &nickname) {
  string key = addrKey(clientAddr);

  {
    lock_guard<mutex> lock(clientsMutex);
    auto it = addrToClient.find(key);
    if (it != addrToClient.end()) {
      nickname = it->second;
      return true;
    }
  }

  sendError(SocketFD, clientAddr, "You must login first");
  return false;
}

void readLogin(int SocketFD, const string &payload, const sockaddr_in &clientAddr) {
  size_t pos = 1;
  string nickname = parseStringWithLen(payload, pos, 3);
  string key = addrKey(clientAddr);
  string error;

  if (nickname.empty()) {
    sendError(SocketFD, clientAddr, "Nickname cannot be empty");
    return;
  }

  {
    lock_guard<mutex> lock(clientsMutex);
    if (clientsToAddr.count(nickname)) {
      error = "Client [" + nickname + "] already logged in, try another nickname";
    } else if (addrToClient.count(key)) {
      error = "Client [" + addrToClient[key] + "] already logged in this address, try another connection";
    } else {
      clientsToAddr[nickname] = clientAddr;
      addrToClient[key] = nickname;
      cout << "[server] Registered client nickname [" << nickname << "] -> " << key << endl;
      printClientsLocked();
    }
  }

  if (!error.empty()) {
    sendError(SocketFD, clientAddr, error);
    return;
  }
  sendOK(SocketFD, clientAddr);
}

void readUnicast(int SocketFD, const string &payload, const sockaddr_in &clientAddr) {
  size_t pos = 1;
  parseStringWithLen(payload, pos, 3);
  string recipient = parseStringWithLen(payload, pos, 3);
  string message = parseStringWithLen(payload, pos, 7);

  string sender;
  if (!requireLogin(SocketFD, clientAddr, sender)) return;

  if (message.size() > 99999) {
    sendError(SocketFD, clientAddr, "Message too long");
    return;
  }

  sockaddr_in recipientAddr;
  bool found = false;
  {
    lock_guard<mutex> lock(clientsMutex);
    auto it = clientsToAddr.find(recipient);
    if (it != clientsToAddr.end()) {
      recipientAddr = it->second;
      found = true;
    }
  }

  if (!found) {
    sendError(SocketFD, clientAddr, "Client [" + recipient + "] not found");
    return;
  }

  string out = "u" + intForBuffer(sender.size(), 3) + sender
      + intForBuffer(message.size(), 5) + message;
  sendPayload(SocketFD, recipientAddr, out);
}

void readBroadcast(int SocketFD, const string &payload, const sockaddr_in &clientAddr) {
  size_t pos = 1;
  parseStringWithLen(payload, pos, 3);
  string message = parseStringWithLen(payload, pos, 7);

  string sender;
  if (!requireLogin(SocketFD, clientAddr, sender)) return;

  if (message.size() > 99999) {
    sendError(SocketFD, clientAddr, "Message too long");
    return;
  }

  vector<sockaddr_in> recipients;
  {
    lock_guard<mutex> lock(clientsMutex);
    for (auto &client : clientsToAddr) {
      if (client.first != sender) {
        recipients.push_back(client.second);
      }
    }
  }

  string out = "b" + intForBuffer(sender.size(), 3) + sender
      + intForBuffer(message.size(), 5) + message;
  for (const sockaddr_in &recipientAddr : recipients) {
    sendPayload(SocketFD, recipientAddr, out);
  }
}

void readFileTransfer(int SocketFD, const string &payload, const sockaddr_in &clientAddr) {
  size_t pos = 1;
  string recipient = parseStringWithLen(payload, pos, 3);
  string fileName = parseStringWithLen(payload, pos, 5);
  string fileContent = parseStringWithLen(payload, pos, 20);

  string sender;
  if (!requireLogin(SocketFD, clientAddr, sender)) return;

  sockaddr_in recipientAddr;
  bool found = false;
  {
    lock_guard<mutex> lock(clientsMutex);
    auto it = clientsToAddr.find(recipient);
    if (it != clientsToAddr.end()) {
      recipientAddr = it->second;
      found = true;
    }
  }

  if (!found) {
    sendError(SocketFD, clientAddr, "Client [" + recipient + "] not found");
    return;
  }

  string out = "f" + intForBuffer(sender.size(), 3) + sender
      + intForBuffer(fileName.size(), 5) + fileName
      + intForBuffer(fileContent.size(), 20) + fileContent;
  sendPayload(SocketFD, recipientAddr, out);
}

void readLogout(int SocketFD, const sockaddr_in &clientAddr) {
  string key = addrKey(clientAddr);
  string nickname;
  bool loggedOut = false;

  {
    lock_guard<mutex> lock(clientsMutex);
    auto itAddr = addrToClient.find(key);
    if (itAddr != addrToClient.end()) {
      nickname = itAddr->second;
      addrToClient.erase(itAddr);
      clientsToAddr.erase(nickname);
      loggedOut = true;
    }

    if (loggedOut) {
      cout << "[server] Client [" << nickname << "] logged out" << endl;
      printClientsLocked();
    }
  }

  if (loggedOut) {
    sendOK(SocketFD, clientAddr);
  } else {
    sendError(SocketFD, clientAddr, "Client not found");
  }
}

void readList(int SocketFD, const sockaddr_in &clientAddr) {
  string nickname;
  if (!requireLogin(SocketFD, clientAddr, nickname)) return;

  string listStr = "Complete list of clients:\n";
  {
    lock_guard<mutex> lock(clientsMutex);
    for (auto &client : clientsToAddr) {
      listStr += "    client [" + client.first + "] (" + addrKey(client.second) + ")\n";
    }
  }

  string payload = "t" + intForBuffer(listStr.size(), 7) + listStr;
  sendPayload(SocketFD, clientAddr, payload);
}

void processPayload(int SocketFD, string payload, sockaddr_in clientAddr) {
  if (payload.empty()) return;

  char clientOpt = payload[0];
  switch (clientOpt){
    case 'L':
      readLogin(SocketFD, payload, clientAddr);
      break;

    case 'U':
      readUnicast(SocketFD, payload, clientAddr);
      break;

    case 'B':
      readBroadcast(SocketFD, payload, clientAddr);
      break;

    case 'F':
      readFileTransfer(SocketFD, payload, clientAddr);
      break;

    case 'O':
      readLogout(SocketFD, clientAddr);
      break;

    case 'T':
      readList(SocketFD, clientAddr);
      break;

    default:
      cout << "Invalid client response: " << string(1, clientOpt) << endl;
      sendError(SocketFD, clientAddr, "Invalid client response");
      break;
  }
}

int main() {
  struct sockaddr_in stSockAddr;
  int SocketFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (-1 == SocketFD) {
    perror("can not create socket");
    exit(EXIT_FAILURE);
  }

  memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
  stSockAddr.sin_family = AF_INET;
  stSockAddr.sin_port = htons(PORT);
  stSockAddr.sin_addr.s_addr = INADDR_ANY;

  if (-1 == ::bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
    perror("error bind failed");
    close(SocketFD);
    exit(EXIT_FAILURE);
  }

  cout << "[server] UDP server started on port " << PORT << endl;
  cout << "[server] Waiting for datagrams..." << endl;

  for (;;) {
    sockaddr_in clientAddr;
    string payload = recvPayloadFrom(SocketFD, clientAddr);
    if (payload.empty()) continue;

    thread worker(processPayload, SocketFD, payload, clientAddr);
    worker.detach();
  }

  close(SocketFD);
  return 0;
}
