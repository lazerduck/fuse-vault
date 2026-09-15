#define _POSIX_C_SOURCE 200809L
#include "file_block_device.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool range(const fv_host_file_block_context_t *c,uint64_t first,uint32_t count){return count>0u&&first<c->blocks&&(uint64_t)count<=c->blocks-first;}
static fv_block_result_t transfer(fv_block_device_t*d,uint64_t first,uint32_t count,uint8_t *data,bool writing){
    if (!d || !data) return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    fv_host_file_block_context_t*c=d->context;if(!range(c,first,count))return FV_BLOCK_ERROR_OUT_OF_RANGE;
    int fd=open(c->path,writing?O_WRONLY|O_CLOEXEC:O_RDONLY|O_CLOEXEC);if(fd<0)return FV_BLOCK_ERROR_NOT_READY;size_t length=(size_t)count*FV_BLOCK_SIZE,done=0;off_t offset=(off_t)(first*FV_BLOCK_SIZE);
    while(done<length){ssize_t n=writing?pwrite(fd,data+done,length-done,offset+(off_t)done):pread(fd,data+done,length-done,offset+(off_t)done);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);return FV_BLOCK_ERROR_IO;}done+=(size_t)n;} if(close(fd)!=0)return FV_BLOCK_ERROR_IO;return FV_BLOCK_OK;
}
static fv_block_result_t read_blocks(fv_block_device_t*d,uint64_t f,uint32_t n,uint8_t*out){return transfer(d,f,n,out,false);}
static fv_block_result_t write_blocks(fv_block_device_t*d,uint64_t f,uint32_t n,const uint8_t*in){return transfer(d,f,n,(uint8_t*)in,true);}
static fv_block_result_t sync_blocks(fv_block_device_t*d){fv_host_file_block_context_t*c=d->context;int fd=open(c->path,O_RDONLY|O_CLOEXEC);if(fd<0)return FV_BLOCK_ERROR_NOT_READY;bool ok=fsync(fd)==0&&close(fd)==0;return ok?FV_BLOCK_OK:FV_BLOCK_ERROR_IO;}
static uint64_t count_blocks(const fv_block_device_t*d){const fv_host_file_block_context_t*c=d->context;return c->blocks;}
static bool present(const fv_block_device_t*d){const fv_host_file_block_context_t*c=d->context;struct stat s;return stat(c->path,&s)==0&&S_ISREG(s.st_mode)&&(uint64_t)s.st_size==c->blocks*FV_BLOCK_SIZE;}
static const fv_block_device_ops_t OPS={.read=read_blocks,.write=write_blocks,.sync=sync_blocks,.block_count=count_blocks,.is_present=present};
bool fv_host_file_block_device_init(fv_block_device_t*d,fv_host_file_block_context_t*c,const char*path,uint64_t blocks){if(!d||!c||!path||blocks==0u||strlen(path)>=sizeof(c->path)||blocks>INT64_MAX/FV_BLOCK_SIZE)return false;memcpy(c->path,path,strlen(path)+1u);c->blocks=blocks;int fd=open(path,O_RDWR|O_CREAT|O_CLOEXEC,S_IRUSR|S_IWUSR);if(fd<0)return false;struct stat s;bool ok=fstat(fd,&s)==0;if(ok&&s.st_size==0)ok=ftruncate(fd,(off_t)(blocks*FV_BLOCK_SIZE))==0;else if(ok)ok=(uint64_t)s.st_size==blocks*FV_BLOCK_SIZE;if(close(fd)!=0)ok=false;if(!ok)return false;d->ops=&OPS;d->context=c;return true;}
