import numpy as np
import socket
import sys
import struct
import random
import concurrent.futures

class Client:
    def __init__(self, host, port, mode):
        self.host = host
        self.port = port
        self.mode = mode
        self.message = self.setMessage()
        self.socket = self.setSocket()
    
    def setMessage(self):
        if self.mode == "tcp":
            result = []
            for _ in range(3):  
                sublist = [random.random() * 100 for _ in range(200)] 
                result.append(sublist)
            return np.array(result, dtype=float)

    def setSocket(self):
        if self.mode == "tcp":
            return socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def closeSocket(self):
        (self.socket).close()
    
    def sendMessageTCP(self):
        byte_data = b''
        for sublist in self.message:
            byte_data += b''.join(struct.pack('f', f) for f in sublist)

        (self.socket).connect((self.host, self.port))
        (self.socket).sendall(byte_data)
        self.closeSocket()


def send_data_to_server(address, port, mode):
    client = Client(address, port, mode)
    client.setMessage()

    if client.mode == "tcp":
        client.sendMessageTCP()

def main():
    address, mode, port = "127.0.0.1", sys.argv[1], int(sys.argv[2])

    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures = []
        for _ in range(3):
            futures.append(executor.submit(send_data_to_server, address, port, mode))
        concurrent.futures.wait(futures)

if __name__ == "__main__":
    main()