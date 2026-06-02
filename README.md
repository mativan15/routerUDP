# UDP Router Chat

This project implements a small UDP-based chat router in C++. A central server
keeps track of logged-in clients by nickname and forwards messages or files
between them. The client is an interactive terminal program that can log in,
send unicast messages, broadcast messages, request the current client list,
send files, and log out.

## Files

- `server.cpp` - UDP server/router. It accepts datagrams from clients, registers
  nicknames, tracks client addresses, and forwards messages or files.
- `client.cpp` - Interactive UDP client. It logs into the server and exposes a
  terminal menu for chat operations.
- `protocol.h` - Shared protocol helpers, datagram fragmentation/reassembly,
  fixed-width length encoding, and UDP send/receive helpers.
- `Makefile` - Build and run targets for the server and client.

## Requirements

- A C++17 compiler
- POSIX sockets support
- `make`

This code is written for Unix-like systems such as Linux or macOS.

## Build

```sh
make
```

This creates two executables:

- `server`
- `client`

To remove build outputs:

```sh
make clean
```

## Run

Start the server in one terminal:

```sh
make run-server
```

Start one or more clients in separate terminals:

```sh
make run-client
```

The server listens on UDP port `45000`. The client currently connects to
`127.0.0.1:45000`, so the server and clients are expected to run on the same
machine unless the client source is changed.

## Client Usage

When a client starts, it asks for a nickname and sends a login request to the
server. After a successful login, the menu shows these options:

```text
U -> Unicast Message
B -> Broadcast Message
L -> List
F -> File
O -> Logout
```

Options:

- `U` sends a message to one specific logged-in nickname.
- `B` sends a message to all other logged-in clients.
- `L` asks the server for the current list of clients.
- `F` sends a local file to one specific logged-in nickname.
- `O` logs out and ends the session.

Received files are written in the client's current working directory using only
the base file name. Any directory path from the sender is stripped before saving.

## High-Level Design

The server is a router, not a direct peer-to-peer transport. Every client sends
requests to the server, and the server forwards data to the target clients.

The server stores two maps:

- `clientsToAddr`: nickname to UDP address
- `addrToClient`: UDP address to nickname

These maps are protected by a mutex because the server creates a detached worker
thread for each received payload. Each worker decodes one client request and
performs the requested action.

The client uses a background receive thread. The main thread reads menu choices
and sends requests, while the background thread waits for server responses and
incoming messages.

## Datagram Protocol

The protocol sends fixed-size UDP datagrams of `500` bytes.

Each datagram has:

```text
2 bytes   flag
4 bytes   sequence number
494 bytes payload chunk
```

The flag describes fragmentation state:

- `01` - first fragment
- `00` - middle fragment
- `11` - final fragment, or a complete single-datagram payload

The sequence number is a zero-padded 4-digit decimal value. Payloads larger than
494 bytes are split into multiple datagrams and reassembled by sequence number
when the final fragment arrives.

Application payloads use one command byte followed by fixed-width length fields
and data. Length fields are zero-padded decimal strings.

## Client-to-Server Messages

Login:

```text
L + nickname_len(3) + nickname
```

Unicast:

```text
U + sender_len(3) + sender + recipient_len(3) + recipient + message_len(7) + message
```

Broadcast:

```text
B + sender_len(3) + sender + message_len(7) + message
```

File transfer:

```text
F + recipient_len(3) + recipient + file_name_len(5) + file_name + file_content_len(20) + file_content
```

List clients:

```text
T
```

Logout:

```text
O
```

## Server-to-Client Messages

OK response:

```text
K
```

Error response:

```text
E + error_len(5) + error_message
```

Forwarded unicast:

```text
u + sender_len(3) + sender + message_len(5) + message
```

Forwarded broadcast:

```text
b + sender_len(3) + sender + message_len(5) + message
```

Client list:

```text
t + list_len(7) + list_text
```

Forwarded file:

```text
f + sender_len(3) + sender + file_name_len(5) + file_name + file_content_len(20) + file_content
```

## Important Limits and Notes

- Nicknames and recipient names are limited to `999` bytes.
- Chat messages are limited to `99,999` bytes.
- File names are limited by the 5-digit file name length field.
- File contents use a 20-digit length field, but practical size is limited by
  memory and UDP reliability.
- UDP does not guarantee delivery, ordering, or duplicate protection. The code
  reorders fragments by sequence number after all fragments arrive, but it does
  not retransmit missing packets.
- The receiver does not validate that all fragment sequence numbers are present.
- The server identifies clients by source IP and UDP port.
- The client has the server address hard-coded as `127.0.0.1`.

## Example Session

Terminal 1:

```sh
make run-server
```

Terminal 2:

```sh
make run-client
```

Log in as `alice`.

Terminal 3:

```sh
make run-client
```

Log in as `bob`. From Alice's client, choose `U`, enter `bob`, and type a
message. The server receives Alice's request and forwards it to Bob's UDP
address.
