// MJPEG check: veyamux-mjpeg-test <file.jpg>... decodes each JPEG to NV12 (graphics card if it can, else CPU), writes
// <file>.nv12, and feeds a truncated JPEG (FFmpeg shows what arrived; it must not fail the capture).
#include "CaptureSource.h"
#include <linux/videodev2.h>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace veyamux;
int main(int argc,char**argv){
  for(int i=1;i<argc;++i){
    std::ifstream f(argv[i],std::ios::binary); std::vector<uint8_t> j((std::istreambuf_iterator<char>(f)),{});
    FrameConverter c; std::string e;
    if(!c.init(V4L2_PIX_FMT_MJPEG,640,360,0,e)){printf("init %s\n",e.c_str());return 1;}
    std::vector<uint8_t> nv; int w,h,st; bool full=false, uns=false;
    bool ok=c.decodeNv12(j.data(),j.size(),nv,w,h,st,full,&uns,e);
    printf("%s ok=%d %dx%d stride %d full=%d path=%s ms=%.2f\n",argv[i],ok,w,h,st,full,c.decodePath().c_str(),c.decodeMs());
    std::string out=std::string(argv[i])+".nv12"; std::ofstream(out,std::ios::binary).write((char*)nv.data(),nv.size());
    // corrupt
    std::vector<uint8_t> bad(j.begin(), j.begin()+j.size()/3);
    ok=c.decodeNv12(bad.data(),bad.size(),nv,w,h,st,full,&uns,e);
    printf("  truncated: ok=%d corrupt=%llu\n",ok,(unsigned long long)c.corruptFrames());
  }
}
