#include "Display.h"
#include "Board.h"
#include "Dynamic.h"
#include "Images.h"
#include <algorithm>
#include <mbedtls/base64.h>
#include <stdlib.h>

bool Display::begin(const String& deviceId){
  deviceId_=deviceId;
  pinMode(board::SD_CS, OUTPUT); digitalWrite(board::SD_CS, HIGH);
  pinMode(board::LCD_BL, OUTPUT); digitalWrite(board::LCD_BL, LOW);
  bus_ = new Arduino_HWSPI(board::LCD_DC, board::LCD_CS, board::LCD_SCK, board::LCD_MOSI);
  // IPS=true turns on color inversion, which this panel needs. begin() is true even with
  // wrong pins (SPI has no reply), so only looking at the screen confirms it.
  gfx_ = new Arduino_ST7789(bus_, board::LCD_RST, 0, true, board::WIDTH, board::HEIGHT, 34,0,34,0);
  if(!gfx_->begin(20000000)) return false;
  setBrightness(board::BACKLIGHT_PERCENT);
  status("MetaDisplay C6", "Iniciando...");
  return true;
}
void Display::setBrightness(uint8_t percent){
  percent = constrain(percent,0,50); // cap intentionally conservative
  analogWrite(board::LCD_BL, (uint8_t)(percent * 255 / 100));
}
uint16_t Display::color(const char* value, uint16_t fallback){
  if(!value || strlen(value)!=7 || value[0]!='#') return fallback;
  for(int i=1;i<7;i++) if(!isxdigit(value[i])) return fallback;
  uint32_t v=strtoul(value+1,nullptr,16);
  return gfx_->color565((v>>16)&255,(v>>8)&255,v&255);
}
// System screens stay in portrait whatever the template rotation.
void Display::status(const String& line1, const String& line2){
  if(!gfx_) return;
  gfx_->setRotation(0);
  gfx_->fillScreen(0x0809); gfx_->setTextColor(0xffff); gfx_->setTextSize(2);
  gfx_->setCursor(8,42); gfx_->print("MetaDisplay");
  gfx_->setTextSize(1); gfx_->setCursor(8,102); gfx_->print(line1);
  gfx_->setCursor(8,121); gfx_->print(line2);
  drawDeviceId(250);
}
// Label plus the ID in size 2. Size 2 is 12 px per char (15 chars = 180 px > 172 px),
// so chars go 11 px apart to keep the ID big and on one line.
void Display::drawDeviceId(int y){
  gfx_->setTextSize(1); gfx_->setTextColor(0xC618); gfx_->setCursor(8,y); gfx_->print("ID para tu servidor:");
  gfx_->setTextSize(2); gfx_->setTextColor(0xFFE0);
  const int x0=max(0,(board::WIDTH-(int)deviceId_.length()*11)/2);
  for(size_t i=0;i<deviceId_.length();i++){gfx_->setCursor(x0+i*11,y+14); gfx_->print(deviceId_[i]);}
}
void Display::portalInfo(const String& ssid,const String& pass,const String& ip,bool clientJoined){
  if(!gfx_) return;
  gfx_->setRotation(0);
  gfx_->fillScreen(0x0809);
  // Text above size 1 is centered; the built-in font is 6px wide per size step.
  auto line=[&](int y,uint8_t size,uint16_t c,const String& s){
    gfx_->setTextSize(size); gfx_->setTextColor(c);
    gfx_->setCursor(size>1?max(0,(board::WIDTH-(int)s.length()*6*size)/2):8,y); gfx_->print(s);
  };
  // 192.168.4.1 is the ESP32 soft-AP address when no static IP is set.
  const String ipNote=ip=="192.168.4.1"?"   (IP predeterminada)":"";
  line(14,2,0xffff,"MetaDisplay");
  drawDeviceId(262);
  if(clientJoined){
    line(38,1,0xC618,"Modo configuracion");
    line(84,2,0x07E0,"Alguien se");
    line(104,2,0x07E0,"conecto");
    line(150,1,0xffff,"Abre en el navegador:");
    line(164,2,0x07FF,ip);
    line(186,1,0xC618,ipNote);
    line(220,1,0xffff,"Si la pagina no se abre");
    line(232,1,0xffff,"sola, escribe esa IP.");
    return;
  }
  line(38,1,0xC618,"Sin red: modo configuracion");
  line(62,1,0xffff,"1. Conecta tu movil a");
  line(74,1,0xffff,"   la red WiFi:");
  line(88,1,0x07FF,"   "+ssid);
  line(112,1,0xffff,"2. Clave de la red:");
  line(126,3,0xFFE0,pass);
  line(162,1,0xffff,"3. Abre en el navegador:");
  line(176,2,0x07FF,ip);
  line(198,1,0xC618,ipNote);
  line(222,1,0xffff,"4. Elige tu WiFi, pon el");
  line(234,1,0xffff,"   servidor y el token,");
  line(246,1,0xffff,"   y pulsa Save.");
  line(304,1,0xC618,"Esperando conexion...");
}
namespace {
  // Numeric property clamped to the contract's range, since binds can bring anything; def when absent.
  int num(JsonObjectConst e,const char* k,int def,int lo,int hi){
    JsonVariantConst v=e[k];
    return v.is<double>()?(int)constrain(v.as<double>(),(double)lo,(double)hi):def;
  }
  // At most `max` points, even if a `when.set` brought more than the template validated.
  int readPoints(JsonArrayConst list,int16_t (*pt)[2],int max){
    int n=0;
    for(JsonArrayConst p:list){if(n==max)break; pt[n][0]=p[0]|0; pt[n][1]=p[1]|0; n++;}
    return n;
  }
}
// A quad `s` px wide centered on the segment; odd and even widths cover exactly s rows when axis-aligned.
void Display::thickLine(int x0,int y0,int x1,int y1,int s,uint16_t c){
  const float dx=x1-x0, dy=y1-y0, len=sqrtf(dx*dx+dy*dy);
  if(len<0.5f){gfx_->fillCircle(x0,y0,s/2,c);return;}
  const float nx=-dy/len*(s-1)/2.f, ny=dx/len*(s-1)/2.f;
  const int ax=lroundf(x0+nx), ay=lroundf(y0+ny), bx=lroundf(x0-nx), by=lroundf(y0-ny);
  const int cx=lroundf(x1-nx), cy=lroundf(y1-ny), ex=lroundf(x1+nx), ey=lroundf(y1+ny);
  gfx_->fillTriangle(ax,ay,bx,by,cx,cy,c);
  gfx_->fillTriangle(ax,ay,cx,cy,ex,ey,c);
}
// Round joints (a circle per vertex) so wide polylines have no gaps; close to SVG, not identical.
void Display::polyline(const int16_t (*pt)[2],int n,bool closed,int s,uint16_t c){
  const int segments=closed && n>2?n:n-1;
  for(int i=0;i<segments;i++){
    const int j=(i+1)%n;
    if(s==1)gfx_->drawLine(pt[i][0],pt[i][1],pt[j][0],pt[j][1],c);
    else thickLine(pt[i][0],pt[i][1],pt[j][0],pt[j][1],s,c);
  }
  if(s>1)for(int i=0;i<n;i++)gfx_->fillCircle(pt[i][0],pt[i][1],s/2,c);
}
// Even-odd fill by rows: edge crossings at mid-row, sorted, filled in pairs.
void Display::fillPolygon(const int16_t (*pt)[2],int n,uint16_t c){
  int y0=gfx_->height(), y1=-1;
  for(int i=0;i<n;i++){y0=min<int>(y0,pt[i][1]);y1=max<int>(y1,pt[i][1]);}
  y0=max(y0,0); y1=min<int>(y1,gfx_->height()-1);
  int16_t xs[64];
  for(int y=y0;y<=y1;y++){
    int k=0; const float yc=y+0.5f;
    for(int i=0,j=n-1;i<n;j=i++){
      const float ya=pt[i][1], yb=pt[j][1];
      if((ya<=yc)!=(yb<=yc))xs[k++]=lroundf(pt[i][0]+(yc-ya)*(pt[j][0]-pt[i][0])/(yb-ya));
    }
    std::sort(xs,xs+k);
    for(int i=0;i+1<k;i+=2)gfx_->drawFastHLine(xs[i],y,xs[i+1]-xs[i]+1,c);
  }
}
// Streams the base64 from /template.raw a row at a time. 512 chars = 384 bytes, even and a multiple of 3,
// so no pixel or RLE run is split between chunks. Never paints more than w*h pixels.
void Display::drawImage(File& raw,int x,int y,int w,int h,bool rle,const char* ref){
  uint32_t off,len;
  if(!raw || !imageRef(ref,raw.size(),off,len) || w>320)return;
  static uint16_t row[320]; static uint8_t b64[512], bytes[384];
  const uint32_t total=uint32_t(w)*h;
  uint32_t p=0; int col=0;
  auto put=[&](uint16_t c,uint32_t n){
    while(n-- && p<total){
      row[col++]=c; p++;
      if(col==w){gfx_->draw16bitRGBBitmap(x,y+(p-1)/w,row,w,1);col=0;}
    }
  };
  raw.seek(off);
  for(uint32_t done=0;done<len && p<total;delay(1)){
    const size_t k=raw.read(b64,min<uint32_t>(sizeof b64,len-done));
    size_t n=0;
    if(!k || mbedtls_base64_decode(bytes,sizeof bytes,&n,b64,k))return;
    done+=k;
    if(!rle)for(size_t i=0;i+1<n;i+=2)put((bytes[i]<<8)|bytes[i+1],1);
    else for(size_t i=0;i+2<n;i+=3)put((bytes[i+1]<<8)|bytes[i+2],bytes[i]+1);
  }
}
// One element after the dynamic layer. The 1.0 types keep the 1.2.0 drawing and defaults.
void Display::drawElement(JsonObjectConst e,File& raw){
  const String kind=e["type"] | "";
  const int x=num(e,"x",0,0,319), y=num(e,"y",0,0,319), w=num(e,"w",0,1,320), h=num(e,"h",0,1,320);
  const int s=num(e,"stroke",1,1,32);
  const uint16_t c=color(e["color"] | "#FFFFFF");
  const uint16_t fill=color(e["fillColor"].as<const char*>(),c); // new shapes: fillColor, else color
  const bool filled=e["filled"] | false;
  if(kind=="text"){
    gfx_->setTextSize(num(e,"size",1,1,4)); gfx_->setTextColor(c); gfx_->setTextWrap(false);
    gfx_->setCursor(x,y); gfx_->print(dyn::text(e["value"]).c_str());
  } else if(kind=="rect" || kind=="button"){
    const int radius=num(e,"radius",0,0,10);
    if(e["filled"] | (kind=="button")) gfx_->fillRoundRect(x,y,w,h,radius,color(e["fillColor"] | "#151E36", c));
    else for(int i=0;i<s && w-2*i>0 && h-2*i>0;i++){ // stroke goes inside the box
      if(radius)gfx_->drawRoundRect(x+i,y+i,w-2*i,h-2*i,radius,c); else gfx_->drawRect(x+i,y+i,w-2*i,h-2*i,c);
    }
    if(kind=="button"){
      gfx_->setTextSize(num(e,"size",1,1,3)); gfx_->setTextColor(c);
      gfx_->setCursor(x+6,y+(h/2)-5); gfx_->print(dyn::text(e["value"]).c_str());
    }
  } else if(kind=="line"){
    const int x2=num(e,"x2",x,0,319), y2=num(e,"y2",y,0,319);
    if(s==1)gfx_->drawLine(x,y,x2,y2,c); else thickLine(x,y,x2,y2,s,c);
  } else if(kind=="progress"){
    if(w>=4 && h>=4){
      gfx_->drawRect(x,y,w,h,c);
      gfx_->fillRect(x+2,y+2,(int)((w-4)*constrain(e["value"].as<float>(),0.f,100.f)/100.f),h-4,color(e["fillColor"]|"#9F78FA",c));
    }
  } else if(kind=="circle" || kind=="arc"){
    const int r=num(e,"r",1,1,320), inner=max(r-s/2,0), outer=r+(s-1)/2;
    if(kind=="circle"){
      if(filled)gfx_->fillCircle(x,y,r,fill);
      else if(s==1)gfx_->drawCircle(x,y,r,c);
      else gfx_->fillArc(x,y,outer,inner,0,360,c);
      return;
    }
    // Degrees, 0 at 3 o'clock and clockwise, as Arduino_GFX's fillArc; a full turn or more is a circle.
    const int start=num(e,"start",0,-360,360), end=num(e,"end",0,-360,360);
    const int sweep=abs(end-start)>=360?360:((end-start)%360+360)%360;
    if(sweep)gfx_->fillArc(x,y,outer,inner,start,start+sweep,c);
  } else if(kind=="ellipse"){
    const int rx=num(e,"rx",1,1,320), ry=num(e,"ry",1,1,320);
    if(filled)gfx_->fillEllipse(x,y,rx,ry,fill);
    else if(s==1)gfx_->drawEllipse(x,y,rx,ry,c);
    else{
      int16_t pt[48][2];
      for(int i=0;i<48;i++){const float t=i*2*PI/48; pt[i][0]=lroundf(x+rx*cosf(t)); pt[i][1]=lroundf(y+ry*sinf(t));}
      polyline(pt,48,true,s,c);
    }
  } else if(kind=="polygon"){
    int16_t pt[64][2];
    const int n=readPoints(e["points"],pt,64);
    const bool closed=e["closed"] | true;
    if(n<2)return;
    if(filled && closed)fillPolygon(pt,n,fill); else polyline(pt,n,closed,s,c);
  } else if(kind=="curve"){
    int16_t p[4][2], pt[25][2];
    const int n=readPoints(e["points"],p,4);
    if(n<3)return;
    for(int i=0;i<=24;i++){ // quadratic or cubic Bezier sampled in 24 segments
      const float t=i/24.f, u=1-t;
      for(int a=0;a<2;a++){
        const float v=n==3?u*u*p[0][a]+2*u*t*p[1][a]+t*t*p[2][a]
                           :u*u*u*p[0][a]+3*u*u*t*p[1][a]+3*u*t*t*p[2][a]+t*t*t*p[3][a];
        pt[i][a]=lroundf(v);
      }
    }
    polyline(pt,25,false,s,c);
  } else if(kind=="image"){
    const String enc=e["encoding"] | "";
    if(enc=="rgb565" || enc=="rgb565-rle")drawImage(raw,x,y,w,h,enc=="rgb565-rle",e["data"]);
  }
}
void Display::render(JsonObjectConst t,JsonObjectConst values,bool online,int selectedAction,int frame,File& raw){
  if(!gfx_) return;
  gfx_->setRotation((t["rotation"] | 0)/90);
  gfx_->fillScreen(color(t["background"] | "#090D18",0x0000));
  const dyn::Scope scope{values,t["params"]};
  JsonDocument el; // one element at a time, with its dynamic layer resolved
  auto draw=[&](JsonArrayConst elements){
    for(JsonObjectConst e:elements){el.clear(); if(dyn::resolveElement(e,scope,el))drawElement(el.as<JsonObjectConst>(),raw);}
  };
  draw(t["elements"]);
  if(frame>=0) draw(t["animation"]["frames"][frame]["elements"]);
  // Outline marks the action the button runs when the template has more than one.
  if(selectedAction>=0){
    JsonObjectConst a=t["actions"][selectedAction];
    if(!a.isNull()) gfx_->drawRoundRect((a["x"]|0)-2,(a["y"]|0)-2,(a["w"]|0)+4,(a["h"]|0)+4,4,0xFFE0);
  }
  if(highlightActive_) drawHighlight();
  gfx_->fillCircle(gfx_->width()-7,7,3,online?0x07E0:0xF800);
  // A re-render would otherwise erase a toast that is still showing.
  if(feedbackActive_) drawFeedback();
}
void Display::highlight(int x,int y,int w,int h){
  if(!gfx_) return;
  highlightUntil_=millis()+300; highlightActive_=true;
  highlight_[0]=x; highlight_[1]=y; highlight_[2]=w; highlight_[3]=h;
  drawHighlight();
}
void Display::drawHighlight(){
  for(int i=1;i<=2;i++) gfx_->drawRect(highlight_[0]-i,highlight_[1]-i,highlight_[2]+2*i,highlight_[3]+2*i,0xFFE0);
}
void Display::feedback(const String& message,uint16_t col,uint32_t durationMs){
  if(!gfx_) return;
  feedbackUntil_=millis()+durationMs; feedbackActive_=true;
  feedbackText_=message.substring(0,24); feedbackColor_=col;
  drawFeedback();
}
void Display::drawFeedback(){
  const int w=gfx_->width(), h=gfx_->height(); // the template's canvas, rotated or not
  gfx_->fillRoundRect(5,h-40,w-10,35,5,0x2104);
  gfx_->setTextColor(feedbackColor_);gfx_->setTextSize(1);gfx_->setCursor(11,h-27);
  gfx_->print(feedbackText_);
}
bool Display::serviceFeedback(){
  bool ended=false;
  if(feedbackActive_ && (int32_t)(millis()-feedbackUntil_)>=0){feedbackActive_=false;ended=true;}
  if(highlightActive_ && (int32_t)(millis()-highlightUntil_)>=0){highlightActive_=false;ended=true;}
  return ended;
}
