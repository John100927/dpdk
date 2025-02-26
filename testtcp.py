import socket
s =socket.socket()

host = '140.120.15.187'
port = 8080

s.connect((host,port))
send_data='love&peace'
s.send(send_data.encode())

recvData = s.recv(1024).decode()
print('recv',recvData)
s.close