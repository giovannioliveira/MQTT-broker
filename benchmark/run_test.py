#!/usr/bin/env python
import threading
from random import random
import time
import os

n_clients = 2
host= '192.168.0.112'

def spawn_sub():
    os.system('mosquitto_sub -h '+host+' -t foobar')


def spawn_pub():
    while(True):
        os.system('mosquitto_pub -h '+host+' -t foobar -m loremipsum')
        time.sleep(random())



def main():
    for i in range(n_clients):
        if i < n_clients/2:
            threading.Thread(target=spawn_sub).start()
        else:
            threading.Thread(target=spawn_pub).start()


if __name__ == "__main__":
    main()
