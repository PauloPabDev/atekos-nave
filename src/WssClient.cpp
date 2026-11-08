#include "WssClient.h"
#include <WiFi.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <esp_system.h>
#include <string.h>
namespace {
  const char* WS_GUID="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  String base64(const uint8_t* data,size_t len){
    unsigned char output[64]={0};size_t used=0;
    if(len>32 || mbedtls_base64_encode(output,sizeof(output),&used,data,len)!=0)return "";
    return String((const char*)output).substring(0,used);
  }
  bool validHeaderValue(const String &v){return v.indexOf('\n')<0 && v.indexOf('\r')<0;}
}
void WssClient::configure(const String& url,const String& token,const String& customCAPem){
  disconnect(); url_=url;token_=token;ca_=customCAPem;
  retryAt_=0;retryDelay_=2000;
}
bool WssClient::parseUrl(){
  if(!url_.startsWith("wss://") || !validHeaderValue(url_) || !validHeaderValue(token_))return false;
  const String rest=url_.substring(6);
  const int slash=rest.indexOf('/');
  String authority=slash<0?rest:rest.substring(0,slash);
  path_=slash<0?"/":rest.substring(slash);
  if(authority.isEmpty() || authority.indexOf('@')>=0 || authority.indexOf('#')>=0 || authority.indexOf(' ')>=0)return false;
  if(authority.indexOf('[')>=0 || authority.indexOf(']')>=0)return false; // IPv6 intentionally unsupported in v1
  int colon=authority.indexOf(':');port_=443;
  if(colon>=0){
    String p=authority.substring(colon+1);
    long parsed=p.toInt();if(parsed<=0 || parsed>65535)return false;
    port_=(uint16_t)parsed;
    authority=authority.substring(0,colon);
  }
  host_=authority;
  return host_.length()>0 && host_.length()<200 && path_.length()<768 && token_.length()>0;
}
bool WssClient::connectNow(){
  if(!parseUrl())return false;
  client_.stop();
  client_.setTimeout(2500);
  if(ca_.length()) client_.setCACert(ca_.c_str());
  else client_.useBuiltinCACertBundle(); // Arduino ESP32 >= 3.x, public CA store
  if(!client_.connect(host_.c_str(),port_))return false;
  uint8_t nonce[16];esp_fill_random(nonce,sizeof(nonce));
  const String key=base64(nonce,sizeof(nonce));
  if(key.isEmpty())return false;
  String expectedSource=key+WS_GUID;
  uint8_t digest[20]={0};
  if(mbedtls_sha1((const unsigned char*)expectedSource.c_str(),expectedSource.length(),digest)!=0)return false;
  const String expected=base64(digest,sizeof(digest));
  String hostLine=host_;
  if(port_!=443)hostLine+=":"+String(port_);
  client_.print("GET "+path_+" HTTP/1.1\r\n");
  client_.print("Host: "+hostLine+"\r\n");
  client_.print("Connection: Upgrade\r\nUpgrade: websocket\r\n");
  client_.print("Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: "+key+"\r\n");
  client_.print("Authorization: Bearer "+token_+"\r\n\r\n");
  String status=client_.readStringUntil('\n');status.trim();
  if(!status.startsWith("HTTP/1.1 101") && !status.startsWith("HTTP/1.0 101"))return false;
  bool gotAccept=false,gotUpgrade=false,gotConnection=false;
  size_t headerBytes=status.length();
  for(int n=0;n<32;n++){
    String line=client_.readStringUntil('\n');headerBytes+=line.length();line.trim();
    if(headerBytes>4096)return false;
    if(line.isEmpty())break;
    int sep=line.indexOf(':');if(sep<0)continue;
    String keyHeader=line.substring(0,sep);keyHeader.toLowerCase();
    String value=line.substring(sep+1);value.trim();
    if(keyHeader=="sec-websocket-accept")gotAccept=(value==expected);
    if(keyHeader=="upgrade"){value.toLowerCase();gotUpgrade=(value=="websocket");}
    if(keyHeader=="connection"){value.toLowerCase();gotConnection=(value.indexOf("upgrade")>=0);}
  }
  if(!gotAccept || !gotUpgrade || !gotConnection)return false;
  connected_=true;rxLen_=0;lastPong_=millis();lastPing_=millis();retryDelay_=2000;
  if(onState_)onState_(true);
  return true;
}
void WssClient::markOffline(){
  const bool was=connected_;connected_=false;client_.stop();rxLen_=0;
  retryAt_=millis()+retryDelay_+(esp_random()%750);
  retryDelay_=min(retryDelay_*2,60000UL);
  if(was && onState_)onState_(false);
}
void WssClient::disconnect(){connected_=false;client_.stop();rxLen_=0;}
bool WssClient::sendFrame(uint8_t opcode,const uint8_t* bytes,size_t len){
  if(!connected_ || len>MAX_FRAME)return false;
  uint8_t hdr[8];size_t hs=2;
  hdr[0]=0x80|(opcode&0x0F);
  if(len<126){hdr[1]=0x80|(uint8_t)len;}
  else {hdr[1]=0x80|126;hdr[2]=len>>8;hdr[3]=len&255;hs=4;}
  uint8_t key[4];esp_fill_random(key,sizeof(key));
  if(client_.write(hdr,hs)!=hs || client_.write(key,4)!=4){markOffline();return false;}
  // All client->server WS frames MUST be masked per RFC6455.
  for(size_t off=0;off<len;){
    uint8_t masked[256];const size_t chunk=min(sizeof(masked),len-off);
    for(size_t i=0;i<chunk;i++)masked[i]=bytes[off+i]^key[(off+i)%4];
    if(client_.write(masked,chunk)!=chunk){markOffline();return false;}
    off+=chunk;
  }
  return true;
}
bool WssClient::send(const String &json){return sendFrame(0x1,(const uint8_t*)json.c_str(),json.length());}
void WssClient::handleFrame(uint8_t op,const uint8_t* bytes,size_t len){
  switch(op){
    case 0x1: { String s; s.reserve(len+1);for(size_t i=0;i<len;i++)s+=(char)bytes[i]; if(onText_)onText_(s);break; }
    case 0x8: markOffline();break;
    case 0x9: sendFrame(0xA,bytes,len);break;
    case 0xA: lastPong_=millis();break;
    default:break;
  }
}
void WssClient::readIncoming(){
  // bounded work per loop: no socket readBytes(), no long frame read timeout.
  size_t consumed=0;
  while(client_.available()>0 && consumed<768 && rxLen_<sizeof(buffer_)){
    const int b=client_.read();if(b<0)break;
    buffer_[rxLen_++]=uint8_t(b);consumed++;
  }
  for(int frames=0;frames<3;frames++){
    if(rxLen_<2)break;
    uint8_t op=buffer_[0]&0x0F;
    if((buffer_[0]&0x80)==0 || (buffer_[0]&0x70)!=0 || (buffer_[1]&0x80)!=0){markOffline();return;}
    size_t header=2, length=(buffer_[1]&0x7F);
    if(length==126){if(rxLen_<4)break;length=(size_t(buffer_[2])<<8)|buffer_[3];header=4;}
    if(length==127 || length>MAX_FRAME || (op>=0x8 && length>125)){markOffline();return;}
    if(rxLen_<header+length)break;
    handleFrame(op,buffer_+header,length);
    if(!connected_)return;
    const size_t total=header+length;
    memmove(buffer_,buffer_+total,rxLen_-total);rxLen_-=total;
  }
  if(rxLen_>=sizeof(buffer_))markOffline();
}
void WssClient::loop(){
  if(WiFi.status()!=WL_CONNECTED){if(connected_)markOffline();return;}
  if(!connected_){
    if((int32_t)(millis()-retryAt_)<0)return;
    if(!connectNow())markOffline();
    return;
  }
  if(!client_.connected()){markOffline();return;}
  readIncoming();
  if(!connected_)return;
  if(millis()-lastPing_>20000){
    const uint8_t ping[]={'m','d'};sendFrame(0x9,ping,sizeof(ping));lastPing_=millis();
  }
  if(millis()-lastPong_>70000)markOffline();
}
