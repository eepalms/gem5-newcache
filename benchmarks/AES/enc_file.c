#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aes.h"
#define BUFSIZE	(1024*32)
#define MAX_BLOCK_SIZE 128

int main(int argc, char **argv)
{
    unsigned char *buf, *buf2;
    unsigned int flen;
    FILE    *fin = 0, *fout = 0;
    int enc;
    static const unsigned char key16[16]=
                {0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
                 0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,0x12};
    unsigned char iv[MAX_BLOCK_SIZE/8];
    AES_KEY aes_ks1;

    if(argc != 4 || (argv[3][0] != 'D' && argv[3][0] != 'E'))
    {
        printf("usage: aes in_filename out_filename [D/E]\n");
        return -1;
    }

    if (argv[3][0]=='E')
       enc = AES_ENCRYPT;
    else
       enc = AES_DECRYPT;


    if(!(fin = fopen(argv[1], "r")))   /* try to open the input file */
    {
        printf("The input file: %s could not be opened\n", argv[1]);
        return -2;
    }

    buf = (unsigned char *)malloc(BUFSIZE);
    buf2 = (unsigned char *)malloc(BUFSIZE);

    flen = fread(buf, sizeof(unsigned char), BUFSIZE, fin);


    if(!(fout = fopen(argv[2], "w")))  /* try to open the output file */
    {
        printf("The output file: %s could not be opened\n", argv[2]);
        return -3;
    }


    AES_set_encrypt_key(key16,128,&aes_ks1);
    memset(iv,0,sizeof(iv));

    AES_cbc_encrypt(buf,buf2,flen,&aes_ks1,iv,enc);

    fwrite(buf2, sizeof(unsigned char), flen, fout);

    fclose(fin);
    fclose(fout);
    free(buf);
    free(buf2);
    return 0;
}
