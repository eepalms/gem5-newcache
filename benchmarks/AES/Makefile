# x86
CC = gcc
# ALPHA
#CC = alphaev67-unknown-linux-gnu-gcc
# use static link for SE mode
LINK_TYPE = -static
# use default dynamic link for FS mode
#LINK_TYPE = 

all: aes
aes: enc_file.c aes_core.c aes.h aes_cbc.c aes_locl.h Makefile
	$(CC) $(LINK_TYPE) -o aes -lm enc_file.c aes_cbc.c aes_core.c

clean:
	rm -rf aes output*

