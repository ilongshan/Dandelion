# Dandelion
A secure video chat application

## Mac setup

### Requirements:

```
brew install yasm
```

### libx264
```
1. git clone http://git.videolan.org/git/x264.git x264
2. ./configure --prefix=/usr/local --enable-shared
3. make
4. sudo make install
```

### ffmpeg
```
1. git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
2. ./configure  --prefix=/usr/local --enable-gpl --enable-nonfree --enable-libfreetype --enable-libx264
3. make
4. sudo make install
```

### SDL2.0
```
1. https://www.libsdl.org/download-2.0.php
2. ./configure
3. make
4. sudo make install (defaults: /usr/local/lib + /usr/local/include/SDL2 + sdl2-config)
```

## Building

```
make clean
make
```

## Testing

You can easily test this application by streaming data from your device to a UDP endpoint.

UDP
```
ffmpeg -s 640x480 -r 30 -f avfoundation -i "0:0" -c:v libx264 -tune zerolatency -pix_fmt yuv420p -f mpegts udp://127.0.0.1:1234
```

TCP
```
ffmpeg -s 640x480 -r 30 -f avfoundation -i "0:0" -c:v libx264 -tune zerolatency -pix_fmt yuv420p -c:a aac -b:a 128k -f mpegts pipe:1 | nc -l -k 51234
```
