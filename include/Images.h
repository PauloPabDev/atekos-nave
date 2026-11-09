#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// Template images: their base64 stays in /template.raw (the message as received) and the structure keeps
// "@offset:len", its position there. Callers provide delay() (Arduino, or a stub in the host tests).

// Offset and length of an image's base64; false if `ref` is malformed or points past rawSize.
inline bool imageRef(const char* ref,uint32_t rawSize,uint32_t& off,uint32_t& len){
  unsigned o=0,l=0; char extra;
  if(!ref || sscanf(ref,"@%u:%u%c",&o,&l,&extra)!=2 || l%4 || l>rawSize || o>rawSize-l)return false;
  off=o;len=l;return true;
}

// Copies src to dst replacing the base64 of each image ("data" of an object inside an "elements" array, in
// template.elements or animation.frames[i].elements) with "@offset:len" in src. Any other "data" is left alone.
template<class In,class Out> bool stripImages(In& src,Out& dst){
  struct Level { bool array, elements; } st[32];
  int depth=0;
  char key[12]="", last[12]="";
  size_t keyLen=0, pos=0, start=0;
  bool inStr=false, esc=false, isKey=false, expectKey=false, skip=false;
  uint8_t buf[512];
  for(size_t n;(n=src.read(buf,sizeof buf))>0;delay(1)){
    for(size_t i=0;i<n;i++,pos++){
      const char c=buf[i];
      if(skip){ // inside an image's base64: not copied
        if(c=='"'){
          char ref[24];
          const int len=snprintf(ref,sizeof ref,"@%u:%u\"",unsigned(start),unsigned(pos-start));
          dst.write((const uint8_t*)ref,len); skip=false;
        }else if(c=='\\')return false;
        continue;
      }
      if(inStr){
        dst.write(uint8_t(c));
        if(esc)esc=false;
        else if(c=='\\')esc=true;
        else if(c=='"'){inStr=false;if(isKey){key[keyLen]=0;strcpy(last,key);}}
        else if(isKey && keyLen<sizeof key-1)key[keyLen++]=c;
        continue;
      }
      if(c=='"'){
        dst.write(uint8_t(c));
        isKey=depth>0 && !st[depth-1].array && expectKey;
        if(!isKey && !strcmp(last,"data") && depth>=2 && !st[depth-1].array && st[depth-2].elements){
          skip=true; start=pos+1;
        }else{inStr=true;keyLen=0;}
        continue;
      }
      if(c=='{' || c=='['){
        if(depth==32)return false;
        st[depth++]={c=='[', c=='[' && !strcmp(last,"elements")};
        expectKey=c=='{'; last[0]=0;
      }else if(c=='}' || c==']'){if(depth)depth--;expectKey=false;}
      else if(c==',')expectKey=depth>0 && !st[depth-1].array;
      else if(c==':')expectKey=false;
      dst.write(uint8_t(c));
    }
  }
  return !skip && !inStr && depth==0;
}
