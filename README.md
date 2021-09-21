CONTENTS OF THIS FILE
---------------------
* Introduction
* Technical Characteristcs
* Requirements
* Running
* Building from Source
* Run benchmark tests
* Maintainers


INTRODUCTION
------------
A tiny implementation of a multithreaded MQTT broker with support for QoS=0 (at most once delivery).


TECHNICAL CHARACTERISTICS
-------------------------
- Programming Language:
  C (std. C99);
- Tested Compiler/Platform:
  GNU GCC 9.3.0 / Linux x64
- Build automation tool:
  Cmake 3.16.3
- Concurrency Technology:
  Multi-threading, Single-Process;
- Data store:
  In-memory (volatile);
- Service Port:
  Configurable (executable arg.);
- Maximum clients:
  In-source (define MAXCLIENTS).


REQUIREMENTS
------------
- Run pre-built executable:
  Linux x64 environment
- Compile source:
  Cmake 3.16.3+
- Run benchmark tests:
  Python 3
  Mosquitto_clients Debian package (mosquitto_pub and mosquitto_sub)


RUNNING
-------
From root directory and using a Linux x64 environment, run:
$ chmod +x ./build/MQTT_broker # gives permission to executable

$ ./build/MQTT_broker 1883 # runs broken in port 1883


BUILDING FROM SOURCE
--------------------
$ cmake . -B ./build -DCMAKE_BUILD_TYPE="Release" && make -C ./build # runs Cmake and build project with Make

$ ./build/MQTT_broker 1883 # runs broken in port 1883


RUN BENCHMARK TESTS
-------------------
Open and edit script to configure IP and number of clients for test.
From root directory, run:
$ chmod +x ./benchmark/run_tests.py # gives execution permission to script
$ ./benchmark/run_tests.py  # runs broken in port 1883


MAINTAINERS
-----------
Giovanni Oliveira <giovanni@ime.usp.br>
