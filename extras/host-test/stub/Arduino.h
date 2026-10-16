#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <string>
#include <deque>
#define OUTPUT 1
#define HIGH 1
#define LOW 0
inline void pinMode(int,int){} inline void digitalWrite(int,int){}
extern unsigned long g_us;
inline void delayMicroseconds(unsigned us){ g_us+=us; }
inline void delay(unsigned ms){ g_us+=ms*1000UL; }
inline unsigned long millis(){ return g_us/1000UL; }
struct String{ std::string s; String(){} String(const std::string& c):s(c){} void trim(){ while(!s.empty()&&isspace((unsigned char)s.back()))s.pop_back(); size_t i=0; while(i<s.size()&&isspace((unsigned char)s[i]))i++; s=s.substr(i);} size_t length()const{return s.size();} void toCharArray(char* b,unsigned n){strncpy(b,s.c_str(),n); b[n-1]=0;} };
struct Stream{ virtual ~Stream(){} virtual int available()=0; virtual String readStringUntil(char)=0;
  void println(const char* s=""){printf("%s\n",s);} void printf(const char* f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);} };
struct SerialC: Stream{ std::deque<std::string> q; void begin(int){} void setTimeout(int){} int available(){return (int)q.size();} String readStringUntil(char){ String r(q.front()); q.pop_front(); return r;} };
extern SerialC Serial;
