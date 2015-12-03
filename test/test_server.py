#!/usr/bin/env python
#-*- coding:gbk -*-
##
# @file test_server.py
# @brief
# @author hulei
# @version 1.0
# @date 2012-05-08

import socket

s = socket.socket()
s.bind(("127.0.0.1", 9025))
s.listen(0)
while 1:
    sc, addr = s.accept()
    print "client connected"
    while 1:
        try:
            ret = sc.recv(1024)
            sc.send(ret)
        except:
            print "error"
            break
        if ret == "":
            print "error2"
            break
