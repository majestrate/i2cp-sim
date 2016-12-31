# i2cp sim

i2p network simulator


## dependancies

* boost 1.60
* libssl


## building

    make


## running

run a loopback i2cp server with a 75% chance of dropping a message with 500ms latency

    ./i2psim 127.0.0.1 7564 0.75 500
