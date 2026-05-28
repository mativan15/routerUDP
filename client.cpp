#include "protocol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <strings.h>
#include <thread>
#include <string>
#include <atomic>
#include <chrono>
#include <vector>
#include <map>
#include <limits>
using namespace std;


int SocketFD;
sockaddr_in serverAddr;
atomic<bool> running(false);
char localOpt;
string localNickname;



void readUnicast(string payload){
  size_t pos = 1;
  string sender = parseStringWithLen(payload, pos, 3);
  string message = parseStringWithLen(payload, pos, 5);
  cout << "\n [Unicast] From [" << sender << "] says: " << message << endl;
}

void readBroadcast(string payload){
  size_t pos = 1;
  string sender = parseStringWithLen(payload, pos, 3);
  string message = parseStringWithLen(payload, pos, 5);
  cout << "\n [Broadcast] From [" << sender << "] says: " << message << endl;
}

void readList(string payload){
  size_t pos = 1;
  string list = parseStringWithLen(payload, pos, 7);
  cout << "\n [Client List]\n" << list << endl;
}

static string baseName(string fileName) {
  size_t slash = fileName.find_last_of("/\\");
  if (slash != string::npos) fileName = fileName.substr(slash + 1);
  return fileName;
}

void writeCompletedFile(string outPath, string fileContent) {
  outPath = baseName(outPath);
  FILE *out = fopen(outPath.c_str(), "wb");
  if (!out) {
    perror("fopen (incoming file)");
    return;
  }

  if (!fileContent.empty() && fwrite(fileContent.data(), 1, fileContent.size(), out) != fileContent.size()) {
    perror("fwrite");
  }
  fclose(out);
  cout << "Received file [" << outPath << "] (" << fileContent.size() << " bytes)" << endl;
}

void readFileFromPeer(string payload){
  size_t pos = 1;
  string sender = parseStringWithLen(payload, pos, 3);
  string fileName = parseStringWithLen(payload, pos, 5);
  string fileContent = parseStringWithLen(payload, pos, 20);
  cout << "\n [File Transfer] From [" << sender << "], filename [" << fileName << "]" << endl;
  writeCompletedFile(fileName, fileContent);
}

void readError(const string &payload){
  size_t pos = 1;
  string message = parseStringWithLen(payload, pos, 5);
  cout << "Server error: [" << message << "]" << endl;
}

void threadRead() {
  while (running.load()) {
    string payload = recvPayload(SocketFD);
    if (payload.empty()) {
      running.store(false);
      break;
    }

    char serverOpt = payload[0];
    switch (serverOpt){
      case 'K':
        if (localOpt == 'l') { cout << "Logged in successfully" << endl; running.store(true);}
        else if (localOpt == 'O') { cout << "Logged out successfully" << endl; running.store(false);}
        else { cout << "OK" << endl;}
        break;

      case 'E':
        if (localOpt == 'l') { cout << "Error while logging in\n"; running.store(false);}
        else if (localOpt == 'O') { cout << "Error while logging out\n";}
        readError(payload);
        break;

      case 'u':
        readUnicast(payload);
        break;

      case 'b':
        readBroadcast(payload);
        break;

      case 't':
        readList(payload);
        break;

      case 'f':
        readFileFromPeer(payload);
        break;

      default:
        cout << "Invalid server response: " << string(1, serverOpt) << endl;
        break;
    }
  }
}

void Login(){
  string nickname;
  cout << "LOGIN\nType your nickname: " << flush;
  getline(cin, nickname);
  if (nickname.size() > 999) {
    cout << "Nickname too long" << endl;
    return;
  }
  localNickname = nickname;
  string payload = "L" + intForBuffer(nickname.size(), 3) + nickname;
  if (!sendPayload(SocketFD, serverAddr, payload)) {
    running.store(false);
  }
}

void UnicastMsg(){
  string recipient;
  string message;
  cout << "[Type nickname of recipient] > " << flush;
  getline(cin, recipient);
  cout << "[Type message for " << recipient << "] > " << flush;
  getline(cin, message);
  if (recipient.size() > 999 || message.size() > 99999) {
    cout << "Message or recipient too long" << endl;
    return;
  }

  string payload = "U"  + intForBuffer(localNickname.size(), 3) + localNickname
      + intForBuffer(recipient.size(), 3) + recipient +  intForBuffer((int)message.size(), 7) + message;
  if (!sendPayload(SocketFD, serverAddr, payload)) {
    running.store(false);
  }
}

void BroadcastMsg(){
  string message;
  cout << "[Type broadcast message] > " << flush;
  getline(cin, message);
  if (message.size() > 99999) {
    cout << "Message too long" << endl;
    return;
  }

  string payload = "B" + intForBuffer(localNickname.size(), 3) + localNickname +
      intForBuffer((int)message.size(), 7) + message;
  if (!sendPayload(SocketFD, serverAddr, payload)) {
    running.store(false);
  }
}

void ReqList(){
  string payload = "T";
  if (!sendPayload(SocketFD, serverAddr, payload)) {
    running.store(false);
  }
}

void SendFile(){
  string recipient, fileName;
  cout << "[Type nickname of recipient] > " << flush;
  getline(cin, recipient);
  cout << "Type name of file to send: " << flush;
  getline(cin, fileName);

  if (recipient.size() > 999 || fileName.size() > 99999) {
    cout << "Recipient or file name too long" << endl;
    return;
  }

  FILE *f = fopen(fileName.c_str(), "rb");
  if (!f) {
    perror("fopen");
    cout << "Error while opening file" << endl;
    return;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    perror("fseek");
    cout << "Error while seeking file" << endl;
    fclose(f);
    return;
  }
  long long fileSize = ftell(f);
  if (fileSize < 0) {
    cout << "Error: File size invalid" << endl;
    cout << "File size: " << fileSize << endl;
    fclose(f);
    return;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    perror("fseek");
    fclose(f);
    return;
  }

  vector<char> body((size_t)fileSize);
  if (fileSize > 0 && fread(body.data(), 1, (size_t)fileSize, f) != (size_t)fileSize) {
    perror("fread");
    cout << "Error while reading file" << endl;
    fclose(f);
    return;
  }
  fclose(f);

  string fileContent;
  if (!body.empty()) {
    fileContent.assign(body.data(), body.size());
  }
  string payload = "F" + intForBuffer(recipient.size(), 3) + recipient +
      intForBuffer(fileName.size(), 5) + fileName +
      intForBuffer(fileSize, 20) + fileContent;
  if (!sendPayload(SocketFD, serverAddr, payload)) {
    running.store(false);
  }
  cout << "File datagrams sent successfully" << endl;
}

void Logout(){
  string payload = "O";
  if (!sendPayload(SocketFD, serverAddr, payload)) {
    running.store(false);
  }
}

int main() {
  int Res;
  SocketFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (-1 == SocketFD) {
    perror("cannot create socket");
    exit(EXIT_FAILURE);
  }

  memset(&serverAddr, 0, sizeof(struct sockaddr_in));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(PORT);
  Res = inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

  if (Res <= 0) {
    perror("error: invalid address family");
    close(SocketFD);
    exit(EXIT_FAILURE);
  }

  running.store(true);
  localOpt = 'l';
  Login();
  thread(threadRead).detach();
  this_thread::sleep_for(chrono::milliseconds(500));

  while (running.load()) {
    cout << "\nMenu:\nU -> Unicast Message\nB -> Broadcast Message\nL -> List\nF -> File\nO -> Logout\n";
    cin >> localOpt;
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    switch (localOpt){
      case 'U':
        UnicastMsg();
        break;
      case 'B':
        BroadcastMsg();
        break;
      case 'L':
        ReqList();
        break;
      case 'F':
        SendFile();
        break;
      case 'O':
        Logout();
        this_thread::sleep_for(chrono::milliseconds(300));
        running.store(false);
        break;
      default:
        cout << "Invalid option, try again" << endl;
        break;
    }
  }
  cout << "END OF SESSION" << endl;
  close(SocketFD);
  return 0;
}
