#include "Config.h"
#include <WiFi.h>
#include <esp_system.h>
namespace {
  String randomPassword(){
    // 8 lowercase letters (WPA2 minimum) so it is easy to type on a phone; no l/o.
    const char alphabet[]="abcdefghijkmnpqrstuvwxyz";
    String v; for(int i=0;i<8;i++) v+=alphabet[esp_random()%(sizeof(alphabet)-1)]; return v;
  }
  bool validUrl(const String &url){
    return url.length()>=12 && url.length()<=240 && url.startsWith("wss://") &&
           url.indexOf('@')<0 && url.indexOf('#')<0 && url.indexOf(' ')<0 &&
           url.indexOf('\n')<0 && url.indexOf('\r')<0;
  }
  String makePem(String body){
    body.trim(); if(body.isEmpty()) return "";
    // A base64 DER encoded CA is pasteable into WiFiManager's ordinary input.
    // Do not treat an arbitrary certificate as a valid CA until the TLS verifier checks it.
    if(body.length()>2900)return "";
    for(size_t i=0;i<body.length();i++){
      char c=body[i];
      if(!isalnum((unsigned char)c) && c!='+' && c!='/' && c!='=')return "";
    }
    String pem="-----BEGIN CERTIFICATE-----\n";
    for(size_t i=0;i<body.length();i+=64){pem+=body.substring(i,i+64);pem+='\n';}
    pem+="-----END CERTIFICATE-----\n";
    return pem;
  }
}
bool Config::save(const String& url,const String& token,const String& caBody){
  if(!validUrl(url) || token.isEmpty() || token.length()>380 || token.indexOf('\r')>=0 || token.indexOf('\n')>=0)return false;
  const String pem=makePem(caBody);
  if(!caBody.isEmpty() && pem.isEmpty() && caBody!="PUBLIC")return false;
  prefs_.putString("url",url);
  prefs_.putString("token",token);
  prefs_.putString("ca",caBody=="PUBLIC"?"":pem);
  prefs_.remove("ca-next");prefs_.remove("ca-prev");prefs_.putBool("ca-trial",false);
  settings_.url=url;settings_.token=token;settings_.caPem=caBody=="PUBLIC"?"":pem;
  return true;
}
bool Config::begin(Display& display){
  prefs_.begin("metadisplay",false);
  settings_.url=prefs_.getString("url","");
  settings_.token=prefs_.getString("token","");
  settings_.caPem=prefs_.getString("ca","");
  // During planned private-CA rotation accept the current and the staged CA.
  // Commit later removes the old CA after the server switches certificates.
  if(!prefs_.getBool("ca-trial",false)){
    String staged=prefs_.getString("ca-next","");
    if(!staged.isEmpty() && !settings_.caPem.isEmpty())settings_.caPem+="\n"+staged;
  }
  settings_.portalPass=prefs_.getString("ap-pass","");
  if(settings_.portalPass.length()!=8){settings_.portalPass=randomPassword();prefs_.putString("ap-pass",settings_.portalPass);}
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  if(settings_.url.isEmpty() || settings_.token.isEmpty()) return portal(display);
  WiFi.begin();
  uint32_t started=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-started<18000)delay(100);
  if(WiFi.status()==WL_CONNECTED)return true;
  return portal(display);
}
bool Config::portal(Display& display){
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(12);
  wm.setBreakAfterConfig(true);
  wm.setCaptivePortalEnable(true);
  wm.setConfigPortalBlocking(false);
  const char* ssid="MetaDisplay-Setup";
  char url[248]={0},token[390]={0},caBody[3008]={0};
  settings_.url.toCharArray(url,sizeof(url));
  settings_.token.toCharArray(token,sizeof(token));
  // Private CA: optional base64 DER only (no PEM headers/newlines).
  WiFiManagerParameter urlP("url","Servidor WebSocket (wss://)",url,sizeof(url));
  WiFiManagerParameter tokenP("token","Bearer token (sin prefijo)",token,sizeof(token));
  WiFiManagerParameter caP("ca","CA privada en base64 DER (opcional)",caBody,sizeof(caBody));
  wm.addParameter(&urlP);wm.addParameter(&tokenP);wm.addParameter(&caP);
  wm.startConfigPortal(ssid,settings_.portalPass.c_str());
  // AP password is random per unit and shown physically on its LCD; the screen
  // switches when a phone joins or leaves. Portal stops on save or timeout.
  int clients=-1;
  while(wm.getConfigPortalActive()){
    if(wm.process())break;
    int now=WiFi.softAPgetStationNum();
    if(now!=clients){clients=now;Serial.printf("AP clients: %d\n",clients);display.portalInfo(ssid,settings_.portalPass,WiFi.softAPIP().toString(),clients>0);}
    delay(10);
  }
  if(WiFi.status()!=WL_CONNECTED) return false;
  String newUrl(urlP.getValue()),newToken(tokenP.getValue()),newCa(caP.getValue());
  if(newCa.isEmpty() && !settings_.caPem.isEmpty()){
    // Preserve private CA if user just reconfigures WiFi.
    prefs_.putString("url",newUrl);prefs_.putString("token",newToken);
    if(!validUrl(newUrl) || newToken.isEmpty())return false;
    settings_.url=newUrl;settings_.token=newToken;
    return true;
  }
  if(!save(newUrl,newToken,newCa)){
    display.status("Configuracion invalida","Reintenta en portal");
    return false;
  }
  return true;
}
void Config::clearNetworkAndReboot(){
  WiFi.disconnect(true,true); delay(200); ESP.restart();
}

bool Config::stagePrivateCA(const String &base64Der){
  if(settings_.caPem.isEmpty())return false; // public CA bundle is firmware-controlled
  String next=makePem(base64Der);
  if(next.isEmpty() || next.length()>3900)return false;
  if(prefs_.putString("ca-next",next)==0)return false;
  settings_.caPem=prefs_.getString("ca","")+"\n"+next;
  return true;
}
bool Config::activateStagedCA(){
  const String next=prefs_.getString("ca-next","");
  if(next.isEmpty() || settings_.caPem.isEmpty())return false;
  prefs_.putString("ca-prev",prefs_.getString("ca",""));
  prefs_.putString("ca",next);
  prefs_.putBool("ca-trial",true);
  prefs_.remove("ca-next");
  ESP.restart();
  return true;
}
void Config::confirmTrial(){
  if(prefs_.getBool("ca-trial",false)){
    prefs_.putBool("ca-trial",false);
    prefs_.remove("ca-prev");
  }
}
void Config::rollbackTrialIfNeeded(uint32_t startedAt){
  if(!prefs_.getBool("ca-trial",false))return;
  if(millis()-startedAt<90000)return;
  const String previous=prefs_.getString("ca-prev","");
  if(previous.length())prefs_.putString("ca",previous);
  prefs_.putBool("ca-trial",false);
  prefs_.remove("ca-prev");
  ESP.restart();
}
