#include <stdio.h>
#include <stdint.h>
static uint8_t img[1048576];
int main(int c,char **v){
  FILE *f=fopen(v[1],"rb"); if(!f) return 1;
  size_t n=fread(img,1,8704,f); fclose(f); if(n<16) return 2;
  img[510]=0x55; img[511]=0xAA;
  f=fopen(v[2],"rb"); if(!f) return 3;
  fread(img+8704,1,sizeof(img)-8704,f); fclose(f);
  f=fopen(v[3],"wb"); fwrite(img,1,sizeof(img),f); fclose(f); return 0;
}
