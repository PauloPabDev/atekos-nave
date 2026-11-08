#include "FirmwareUpdate.h"
#include "Display.h"
#include "ManufacturerKey.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <cstring>
namespace {
  String toHex(const uint8_t* b,size_t n){
    char buf[3];String out;out.reserve(n*2);
    for(size_t i=0;i<n;i++){snprintf(buf,sizeof(buf),"%02x",b[i]);out+=buf;}
    return out;
  }
  bool verifySignature(const uint8_t hash[32],const String& signature){
    if(strlen(UPDATE_SIGNING_PUBLIC_KEY_PEM)<80 || signature.length()>384)return false;
    uint8_t sig[256]={0};size_t n=0;
    if(mbedtls_base64_decode(sig,sizeof(sig),&n,(const unsigned char*)signature.c_str(),signature.length())!=0)return false;
    mbedtls_pk_context pk;mbedtls_pk_init(&pk);
    int code=mbedtls_pk_parse_public_key(&pk,(const unsigned char*)UPDATE_SIGNING_PUBLIC_KEY_PEM,strlen(UPDATE_SIGNING_PUBLIC_KEY_PEM)+1);
    if(code==0)code=mbedtls_pk_verify(&pk,MBEDTLS_MD_SHA256,hash,32,sig,n);
    mbedtls_pk_free(&pk);
    return code==0;
  }
}
bool FirmwareUpdate::install(const String& url,const String& expectedSha256Hex,
                             const String& signature,const String& ca, Display& display,String& error){
  if(strlen(UPDATE_SIGNING_PUBLIC_KEY_PEM)<80){error="signing_key_missing";return false;}
  if(!url.startsWith("https://") || url.indexOf('@')>=0 || url.indexOf('\n')>=0 || url.indexOf('\r')>=0){error="invalid_https_url";return false;}
  if(expectedSha256Hex.length()!=64 || signature.isEmpty()){error="invalid_manifest";return false;}
  WiFiClientSecure client;
  client.setTimeout(6000);
  if(ca.isEmpty())client.useBuiltinCACertBundle();else client.setCACert(ca.c_str());
  HTTPClient http;http.setTimeout(6000);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if(!http.begin(client,url)){error="https_begin_failed";return false;}
  int status=http.GET();
  if(status!=200){error="download_http_"+String(status);http.end();return false;}
  const int n=http.getSize();
  if(n<1024 || n>0x300000){error="firmware_size_invalid";http.end();return false;}
  if(!Update.begin(n)){error="ota_partition_error";http.end();return false;}
  display.status("Actualizando firmware","Verificando firma...");
  mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);mbedtls_sha256_starts(&ctx,0);
  NetworkClient *stream=http.getStreamPtr();
  uint8_t buf[1024];int total=0;uint32_t last=millis();bool failed=false;
  while(total<n && millis()-last<10000){
    int avail=stream->available();
    if(avail<=0){delay(3);continue;}
    int want=min((int)sizeof(buf),min(avail,n-total));
    int got=stream->readBytes(buf,want);
    if(got<=0)continue;
    last=millis();
    mbedtls_sha256_update(&ctx,buf,got);
    if(Update.write(buf,got)!=(size_t)got){failed=true;break;}
    total+=got;
  }
  uint8_t hash[32]={0};mbedtls_sha256_finish(&ctx,hash);mbedtls_sha256_free(&ctx);
  if(failed || total!=n){Update.abort();http.end();error="download_interrupted";return false;}
  if(!toHex(hash,32).equalsIgnoreCase(expectedSha256Hex) || !verifySignature(hash,signature)){
    Update.abort();http.end();error="invalid_firmware_signature_or_hash";return false;
  }
  if(!Update.end()){Update.abort();http.end();error="ota_finish_failed";return false;}
  http.end();
  display.status("Firmware validado","Reiniciando...");delay(300);
  ESP.restart();
  return true;
}
