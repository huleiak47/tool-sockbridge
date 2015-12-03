#!/usr/bin/env python
#-*- coding:gbk -*-
##
# @file test_client.py
# @brief 
# @author hulei
# @version 1.0
# @date 2012-05-08

import socket

s = socket.socket()
s.connect(("127.0.0.1", 9026))
s.send("this is a test for the sockbridge\r\n\tccaafsdfero@!$!@3418~~``++-")
s.recv(1024)
s.send("\x00\xa4\x04\x00\x05\x01\x02\x03\x04\x05\xff")
s.recv(1024)
s.close()
