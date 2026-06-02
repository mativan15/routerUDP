#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

using namespace std;

const int PORT = 45000;
constexpr int DATAGRAM_SIZE = 500;
constexpr int HEADER_SIZE = 7;
constexpr int PAYLOAD_SIZE = DATAGRAM_SIZE-HEADER_SIZE;

struct Datagram {
  string flag;
  int sequence;
  string payload;
  sockaddr_in from;
};

struct Fragment {
  int sequence;
  string payload;
};

static string intForBuffer(long long value, int width) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%0*lld", width, value);
  return string(buffer, width);
}

static char checksumMod7(const string &flag, const string &sequence, const string &payload) {
  int sum = 0;
  for (unsigned char c : flag) sum += c;
  for (unsigned char c : sequence) sum += c;
  for (unsigned char c : payload) sum += c;
  return (char)('0' + (sum % 7));
}

static string addrKey(const sockaddr_in &addr) {
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
  return string(ip) + ":" + to_string(ntohs(addr.sin_port));
}


static string parseStringWithLen(const string &data, size_t &pos, int lenght) {
  if (pos + (size_t)lenght > data.size()) {
    return "";
  }
  string field = data.substr(pos, (size_t)lenght);
  int value = atoi(field.c_str());
  pos += (size_t)lenght;

  if (pos + (size_t)value > data.size()) {
    value = (int)(data.size() - pos);
  }
  string result = data.substr(pos, (size_t)value);
  pos += (size_t)value;
  return result;
}

static bool sendDatagram(int SocketFD, const sockaddr_in &to, const string &flag, int sequence, const string &payload) {
  if (flag.size() != 2 || payload.size() != PAYLOAD_SIZE) {
    return false;
  }

  char datagram[DATAGRAM_SIZE];
  memset(datagram, 0, sizeof(datagram));

  memcpy(datagram, flag.data(), 2);
  string seq = intForBuffer(sequence, 4);
  memcpy(datagram + 2, seq.data(), 4);
  datagram[6] = checksumMod7(flag, seq, payload);
  memcpy(datagram + HEADER_SIZE, payload.data(), PAYLOAD_SIZE);

  ssize_t sent = sendto(SocketFD, datagram, DATAGRAM_SIZE, 0,
                        (const struct sockaddr *)&to, sizeof(to));
  if (sent != DATAGRAM_SIZE) {
    perror("sendto");
    return false;
  }
  cout << "Sent datagram -> " << string(datagram,DATAGRAM_SIZE) << endl; 
  return true;
}

static bool sendPayload(int SocketFD, const sockaddr_in &to, const string &payload) {
  if (payload.size() <= PAYLOAD_SIZE) {
    string chunk = payload;
    chunk.append(PAYLOAD_SIZE - chunk.size(), '#');
    return sendDatagram(SocketFD, to, "11", 0, chunk);
  }

  int sequence = 0;
  for (size_t pos = 0; pos < payload.size(); pos += PAYLOAD_SIZE) {
    string flag = "00";
    if (pos == 0) flag = "01";

    string chunk = payload.substr(pos, PAYLOAD_SIZE);
    bool isLast = pos + PAYLOAD_SIZE >= payload.size();
    if (isLast) {
      flag = "11";
      if (chunk.size() < PAYLOAD_SIZE) {
        chunk.append(PAYLOAD_SIZE - chunk.size(), '@');
      }
    }

    if (!sendDatagram(SocketFD, to, flag, sequence, chunk)) {
      return false;
    }
    sequence++;
  }

  return true;
}

static bool recvDatagram(int SocketFD, Datagram &packet) {
  for (;;) {
    char buffer[DATAGRAM_SIZE];
    sockaddr_in from;
    socklen_t fromLen = sizeof(from);

    ssize_t received = recvfrom(SocketFD, buffer, DATAGRAM_SIZE, 0, (struct sockaddr *)&from, &fromLen);
    if (received <= 0) {
      perror("recvfrom");
      return false;
    }
    if (received != DATAGRAM_SIZE) {
      cerr << "Invalid datagram size: " << received << endl;
      continue;
    }

    packet.flag = string(buffer, 2);
    string seq = string(buffer + 2, 4);
    packet.sequence = atoi(seq.c_str());
    packet.payload = string(buffer + HEADER_SIZE, PAYLOAD_SIZE);
    packet.from = from;
    char checksum = buffer[6];
    char expected = checksumMod7(packet.flag, seq, packet.payload);
    if (checksum != expected) {
      cerr << "Invalid datagram checksum: got " << checksum << ", expected " << expected << endl;
      continue;
    }
    cout << "Received datagram <- " << packet.payload << endl;
    return true;
  }
}

static string recvPayloadFrom(int SocketFD, sockaddr_in &from) {
  static map<string, vector<Fragment>> fragmentsBySender;

  for (;;) {
    Datagram packet;
    if (!recvDatagram(SocketFD, packet)) {
      return "";
    }

    from = packet.from;
    string key = addrKey(packet.from);

    if (packet.flag == "11" && packet.sequence == 0) {
      return packet.payload;
    }

    vector<Fragment> &fragments = fragmentsBySender[key];
    if (packet.flag == "01") {
      fragments.clear();
      fragments.push_back({packet.sequence, packet.payload});
    } else if (packet.flag == "00") {
      fragments.push_back({packet.sequence, packet.payload});
    } else if (packet.flag == "11") {
      fragments.push_back({packet.sequence, packet.payload});
      sort(fragments.begin(), fragments.end(),
           [](const Fragment &a, const Fragment &b) {
             return a.sequence < b.sequence;
           });

      bool complete = true;
      for (size_t i = 0; i < fragments.size(); ++i) {
        if (fragments[i].sequence != (int)i) {
          complete = false;
          break;
        }
      }
      if (!complete) {
        cerr << "Missing datagram fragment before final datagram" << endl;
        fragments.clear();
        continue;
      }

      string fullPayload;
      for (const Fragment &fragment : fragments) {
        fullPayload += fragment.payload;
      }
      fragments.clear();
      return fullPayload;
    } else {
      cerr << "Invalid flag: " << packet.flag << endl;
    }
  }
}

static inline string recvPayload(int SocketFD) {
  sockaddr_in from;
  return recvPayloadFrom(SocketFD, from);
}

#endif
