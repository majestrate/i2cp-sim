# i2cp sim

i2p network simulator


## dependencies

* boost 1.60
* libssl


## building

    make


## running

run a loopback i2cp server with a 75% chance of dropping a message with 500ms latency and 10ms jitter

    ./i2psim 127.0.1.1 7564 0.75 500 10

run i2p tunnel script, will set up 1 client tunnel and 1 server tunnel pointing to 127.0.0.1:80

    I2P=$HOME/i2p ./i2ptunnel i2ptunnel-commands.txt

you will get a prompt

run `close all` and then `quit` to exit
