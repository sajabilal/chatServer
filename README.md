Event-Driven Chat Server
- The function of the chat server is to forward each incoming message’s overall client connections (i.e., to all
  clients) except for the client connection over which the message was received. this behavior in an entirely event-
  driven manner (no threads).
- The messages is written to all recipients in capital letters, regardless of how they are sent from the sender.
- “select” is used to check which socket descriptor is ready for reading or writing.
- When checking the read set,the main socket is distinguished, which gets new connections, from the other sockets that negotiate data with the clients.
- When the main socket is ready for reading, accept is called; when any other socket is ready for reading read or recv is called.
- A queue is maintained for each connection. This queue holds all msgs that must be written on that connection.
- The socket is set to be non-blocking.
