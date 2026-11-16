#include "AssetManifest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    uint8_t v[]="123456789";
    assert(AssetManifest_CRC32(v,9)==0xCBF43926U);
    uint32_t h[]={ASSET_MAGIC,9,0xCBF43926U,1};
    assert(AssetManifest_Valid(h,v,9,h[2]));
    v[3]^=1;assert(!AssetManifest_Valid(h,v,9,h[2]));v[3]^=1;
    h[0]=0;assert(!AssetManifest_Valid(h,v,9,h[2]));h[0]=ASSET_MAGIC;
    assert(!AssetManifest_Valid(h,v,8,h[2]));
    assert(!AssetManifest_Valid(h,v,9,h[2]^1));
    h[3]=2;assert(!AssetManifest_Valid(h,v,9,h[2]));
    if(argc==3) {
        FILE *f=fopen(argv[1],"rb");assert(f);assert(fseek(f,0,SEEK_END)==0);
        long n=ftell(f);assert(n>256);rewind(f);
        uint8_t *data=malloc((size_t)n);assert(data);assert(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);
        f=fopen(argv[2],"rb");assert(f);uint32_t crc;assert(fread(&crc,1,4,f)==4);fclose(f);
        assert(AssetManifest_Valid((uint32_t*)data,data+256,(uint32_t)n-256,crc));
        data[n-1]^=1;assert(!AssetManifest_Valid((uint32_t*)data,data+256,(uint32_t)n-256,crc));free(data);
    }
    puts("PASS: actual asset blob/internal CRC pair, standard CRC32, corruption and stale/length/version rejection");
}
