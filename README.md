# mqttAgent

This is simple daemon app that listens all mqtt messages and saves those to mysql database.

## Requirements

To build mqttAgent you have to have following tools

* CMake
* GCC/CLang

1. Install Paho (requires libssl-dev)
(paho requires libssl)
sudo apt-get install libssl-dev

MQTT C Client:
git clone https://github.com/eclipse/paho.mqtt.c.git
cd paho.mqtt.c
make
sudo make install

will be installed to:
/usr/local/lib/libpaho-mqtt3c.so
/usr/local/lib/libpaho-mqtt3cs.so
/usr/local/lib/libpaho-mqtt3a.so
/usr/local/lib/libpaho-mqtt3as.so
/usr/local/include

2. install libmysqlclient-dev
sudo apt-get install libmysqlclient-dev

3. install curl
sudo apt-get install libcurl4-openssl-dev

## Build

To build example of daemon you have to type following commands:

    cd mqttAgent
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr ../
    make
    sudo make install

## Usage

You can test running mqttAgent from command line:

    ./bin/mqttAgent

But running the app in this way is not running running daemon. Let
have a look at command line parameters and arguments

    Usage: ./bin/mqttAgent [OPTIONS]

     Options:
      -h --help                 Print this help
      -c --conf_file filename   Read configuration from the file
      -t --test_conf filename   Test configuration file
      -l --log_file  filename   Write logs to the file
      -d --daemon               Daemonize this application
      -p --pid_file  filename   PID file used by daemonized app

When you will run `./bin/mqttAgent` with parameter `--daemon` or `-d`, then
it will become real UNIX daemon. But this is not the way, how UNIX daemons
are started nowdays. Some init scripts or service files are used for
this purpose.

When you use Linux distribution using systemd, then you can try start daemon using

    systemctl start mqttAgent
    systemctl status mqttAgent
    systemctl reload mqttAgent
    systemctl stop mqttAgent

to autostart
    systemctl enable mqttAgent
    systemctl disable mqttAgent
