# Dandelion
A secure video chat application

## Mac setup

brew install yasm

1. git clone http://git.videolan.org/git/x264.git x264
2. ./configure --prefix=/usr/local --enable-shared
3. make
4. sudo make install

1. git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
2. ./configure  --prefix=/usr/local --enable-gpl --enable-nonfree --enable-libfreetype --enable-libx264
3. make
4. sudo make install

1. https://www.libsdl.org/download-2.0.php
2. ./configure
3. make
4. sudo make install (defaults: /usr/local/lib + /usr/local/include/SDL2 + sdl2-config)

```
ffmpeg -s 640x480 -r 30 -f avfoundation -i "0:0" -c:v libx264 -tune zerolatency -pix_fmt yuv420p -c:a aac -b:a 128k -f mpegts udp://127.0.0.1:1234
```
