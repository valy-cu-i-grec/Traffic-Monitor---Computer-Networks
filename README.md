===== Traffic Assistant System (C-Socket Application)
This is a Client-Server system developed in C, designed for real-time traffic monitoring and driver assistance. The application uses TCP sockets, multi-threading, and I/O multiplexing to handle multiple clients simultaneously.

===== Key Features

Authentication: Secure user access through sign-up and login commands.
Automatic Speed Monitoring: The client features a dedicated thread that sends speed updates every 60 seconds.
Speed Limit Alerts: The server checks the driver's speed against the specific limit of the current street and sends warnings if the limit is exceeded.
Incident Reporting: Users can report traffic events such as police, accidents, or jams. The server then broadcasts these alerts to all other connected drivers.
Subscription-Based Information: Once subscribed, users can query real-time data:
Weather: Local conditions (temperature, road state) for the current street.
Gas Prices: Fuel prices (Standard/Premium) from various stations.
Sports Events: Information on matches or competitions that might block specific streets.

===== Technical Stack
Language: C.
Networking: TCP Sockets.
Concurrency: pthreads for managing multiple client sessions and background speed updates.
I/O Multiplexing: select() to handle simultaneous input from the keyboard and the network socket.
Data Integrity: Mutexes (pthread_mutex_t) to ensure thread-safe access to shared resources and files.

===== Installation and Usage
1. Compilation
Use the provided Makefile to generate the executables:

  'make'

2. Start the Server
By default, the server listens on port 2909:

  './server'

3. Start the Client
Connect to the server using its IP address and the designated port:

  './client <server_ip> 2909'

===== File Structure

Server.c: Central logic, database management, and alert broadcasting.
Client.c: User interface and automatic speed update thread.
Makefile: Compilation script.
streets.txt: Database of street names and speed limits.
users.txt: Registered user credentials.
gas.txt, weather.txt, sports.txt: Data sources for the subscription service.
