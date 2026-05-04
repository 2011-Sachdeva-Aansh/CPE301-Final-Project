/*
 * Smart Energy-Saving Room Lighting System
 * CPE 301 Final Project
 * Author: Aansh Sachdeva
 * Board:  Arduino Mega 2560
 *
 * Pin Map:
 *  TRIG→9(PH6)  ECHO→10(PB4)  LDR→A0
 *  LCD: RS→7  EN→6  D4-D7→3,4,5,8
 *  RTC: SDA→20  SCL→21
 *  LEDs: OFF→22(PA0)  IDLE→23(PA1)  ACTIVE→24(PA2)  ERROR→25(PA3)  ROOM→13(PB7)
 *  Buttons: ON→2(PE4,INT4)  OFF→30(PC7)  RST→12(PB6)
 */

#include <LiquidCrystal.h>
#include <Wire.h>
#include <RTClib.h>

LiquidCrystal lcd(7, 6, 3, 4, 5, 8);
RTC_DS1307 rtc;

//Thresholds
#define DISTANCE_THRESHOLD  200
#define LIGHT_THRESHOLD     450
#define LDR_FAULT_LOW       5
#define LDR_FAULT_HIGH      1023
#define FAULT_CONFIRM_MS    3000
#define DEBOUNCE_MS         50

typedef enum { STATE_OFF, STATE_IDLE, STATE_ACTIVE, STATE_ERROR } SystemState;

volatile bool onButtonPressed = false;
SystemState   currentState    = STATE_OFF;
unsigned long faultStartTime  = 0;
bool          faultTimerActive = false;
unsigned long lastDisplayTime  = 0;

unsigned long lastOffDebounce = 0;
unsigned long lastRstDebounce = 0;
bool          lastOffState    = false;
bool          lastRstState    = false;

//GPIO macros
#define SET_HIGH(port, bit)   ((port) |=  (1 << (bit)))
#define SET_LOW(port, bit)    ((port) &= ~(1 << (bit)))
#define READ_PIN(pinreg, bit) (((pinreg) >> (bit)) & 0x01)

//ISR – ON button (PE4 / Pin 2)
void onButtonISR(){
  onButtonPressed = true;
}

//UART
void uart_init(){
  UBRR0H = 0; UBRR0L = 103;
  UCSR0B = (1 << TXEN0);
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}
void uart_putchar(char c){
  while(!(UCSR0A & (1 << UDRE0)));
  UDR0 = c;
}
void uart_print(const char* s){ while (*s) uart_putchar(*s++); }
void uart_println(const char* s){ uart_print(s); uart_putchar('\r'); uart_putchar('\n'); }
void uart_print_ulong(unsigned long val){
  if(val == 0){ uart_putchar('0'); return; }
  char buf[12]; int i = 0;
  while(val > 0){ buf[i++] = '0' + (val % 10); val /= 10; }
  for(int j = i - 1; j >= 0; j--) uart_putchar(buf[j]);
}
void uart_print_int(int val){
  if(val < 0){ uart_putchar('-'); val = -val; }
  uart_print_ulong((unsigned long)val);
}
void uart_print_float(float val, int decimals){
  if(val < 0) { uart_putchar('-'); val = -val; }
  uart_print_ulong((unsigned long)val);
  uart_putchar('.');
  for(int i = 0; i < decimals; i++) {
    val -= (unsigned long)val; val *= 10;
    uart_putchar('0' + (int)val);
  }
}
void uart_print_pad2(int val){
  if(val < 10) uart_putchar('0');
  uart_print_int(val);
}

//ADC
void adc_init(){
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}
int adc_read(uint8_t channel){
  ADMUX  = (1 << REFS0) | (channel & 0x07);
  ADCSRB = 0;
  ADCSRA |= (1 << ADSC);
  while(ADCSRA & (1 << ADSC));
  return ADC;
}
void setup() {
  // put your setup code here, to run once:
  DDRA |= 0x0F;  PORTA &= ~0x0F;
  DDRB |= (1 << 7); PORTB &= ~(1 << 7);
  DDRH |= (1 << 6); PORTH &= ~(1 << 6);
  DDRB  &= ~(1 << 4);
  DDRE  &= ~(1 << 4); PORTE |= (1 << 4);
  DDRC &= ~(1 << 7); PORTC |= (1 << 7); 
  DDRB  &= ~(1 << 6); PORTB |= (1 << 6);

  uart_init();
  adc_init();
  lcd.begin(16, 2);
  lcd.clear();

  Wire.begin();
  if(!rtc.begin()){
    uart_println("[WARN] RTC not found - using millis()");
  }else{
    DateTime now = rtc.now();
    if(now.year() < 2024){
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      uart_println("[INFO] RTC time set");
    }
  }

  attachInterrupt(digitalPinToInterrupt(2), onButtonISR, FALLING);
  enterState(STATE_OFF);
  logEvent("System initialized");
}

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long now = millis();
  if(onButtonPressed){
    onButtonPressed = false;
    if(currentState == STATE_OFF){
      enterState(STATE_IDLE);
      lastDisplayTime = now;
    }
  }
  //OFF button PC7 Pin 30
  bool offRead = !READ_PIN(PINC, 7);
  if(offRead != lastOffState){ lastOffDebounce = now; lastOffState = offRead; }
  if((now - lastOffDebounce) > DEBOUNCE_MS && offRead){
    if(currentState != STATE_OFF) enterState(STATE_OFF);
  lastOffState = false;
}

  //RST button PB6 Pin 12
  bool rstRead = !READ_PIN(PINB, 6);
  if(rstRead != lastRstState){ lastRstDebounce = now; lastRstState = rstRead; }
  if((now - lastRstDebounce) > DEBOUNCE_MS && rstRead){
    if(currentState == STATE_ERROR){
      faultTimerActive = false;
      enterState(STATE_IDLE);
      lastDisplayTime = now;
    }
    lastRstState = false;
  }

  if(currentState == STATE_IDLE || currentState == STATE_ACTIVE){
    if((now - lastDisplayTime) >= 10000UL || lastDisplayTime == 0){
      lastDisplayTime = now;

      float distance = readDistanceCM();
      int   lightVal = adc_read(0);
      bool  occupied = (distance > 0 && distance < DISTANCE_THRESHOLD);
      bool  isDark   = (lightVal < LIGHT_THRESHOLD);
      bool  isFault  = (lightVal <= LDR_FAULT_LOW || lightVal >= LDR_FAULT_HIGH);

      updateLCDReadings(distance, lightVal);

      uart_print("["); printTimestamp(); uart_print("] ");
      uart_print("Dist:"); uart_print_float(distance, 1);
      uart_print("cm LDR:"); uart_print_int(lightVal);
      uart_print(occupied ? " OCC:YES" : " OCC:NO ");
      uart_print(isDark   ? " DARK:YES" : " DARK:NO");
      uart_println("");

      if(isFault){
        if(!faultTimerActive){
          faultTimerActive = true;
          faultStartTime   = now;
          logEvent("Sensor fault detected");
        }else if((now - faultStartTime) >= FAULT_CONFIRM_MS){
          logEvent("Fault confirmed - entering ERROR");
          enterState(STATE_ERROR);
          faultTimerActive = false;
          return;
        }
      }else{
        if(faultTimerActive){
          faultTimerActive = false;
          logEvent("Fault cleared");
        }
      }

      if(currentState == STATE_IDLE && occupied && isDark){
        enterState(STATE_ACTIVE);
      }else if(currentState == STATE_ACTIVE && (!occupied || !isDark)){
        enterState(STATE_IDLE);
      }
    }
  } 
}

//State Machine
void enterState(SystemState newState){
  currentState = newState;
  PORTA &= ~0x0F;          //clear all state LEDs
  SET_LOW(PORTB, 7);       //room LED off

  switch(newState){
    case STATE_OFF:
      SET_HIGH(PORTA, 0);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("  Smart Lighting");
      lcd.setCursor(0, 1); lcd.print("   System  OFF  ");
      logEvent("STATE: OFF");
      break;
    case STATE_IDLE:
      SET_HIGH(PORTA, 1);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("State: IDLE     ");
      lcd.setCursor(0, 1); lcd.print("Monitoring...   ");
      lastDisplayTime = 0;
      logEvent("STATE: IDLE");
      break;
    case STATE_ACTIVE:
      SET_HIGH(PORTA, 2);
      SET_HIGH(PORTB, 7);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("State: ACTIVE   ");
      lcd.setCursor(0, 1); lcd.print("Room Light: ON  ");
      lastDisplayTime = 0;
      logEvent("STATE: ACTIVE");
      break;
    case STATE_ERROR:
      SET_HIGH(PORTA, 3);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("!!SENSOR ERROR!!");
      lcd.setCursor(0, 1); lcd.print("Press RESET btn ");
      logEvent("STATE: ERROR");
      break;
  }
}

//HC-SR04 – micros() based timing, no delay()
float readDistanceCM(){
  unsigned long t;
  SET_LOW(PORTH, 6);
  t = micros(); while (micros() - t < 2);
  SET_HIGH(PORTH, 6);
  t = micros(); while (micros() - t < 10);
  SET_LOW(PORTH, 6);
  unsigned long count = 0;
  while(!READ_PIN(PINB, 4)){
    if (++count > 480000UL) return -1.0;
  }

  unsigned long start = micros();
  while(READ_PIN(PINB, 4)){
    if ((micros() - start) > 30000UL) return -1.0;
  }
  return ((micros() - start) * 0.0343f) / 2.0f;
}

//LCD update
void updateLCDReadings(float dist, int ldr){
  lcd.setCursor(0, 1);
  if(dist < 0){
    lcd.print("D:---  L:");
  }else{
    lcd.print("D:");
    if(dist < 10) lcd.print("  ");
    else if(dist < 100) lcd.print(" ");
    lcd.print((int)dist); lcd.print("cm ");
    lcd.print("L:");
  }
  if(ldr < 10) lcd.print("   ");
  else if(ldr < 100) lcd.print("  ");
  else if(ldr < 1000)lcd.print(" ");
  lcd.print(ldr);
  lcd.print("  ");
}

//Logging
void logEvent(const char* message){
  uart_print("["); printTimestamp(); uart_print("] ");
  uart_println(message);
}

void printTimestamp(){
  if(!rtc.begin()){
    unsigned long s = millis() / 1000;
    uart_print_pad2((int)(s / 3600));        uart_putchar(':');
    uart_print_pad2((int)((s % 3600) / 60)); uart_putchar(':');
    uart_print_pad2((int)(s % 60));
  }else{
    DateTime now = rtc.now();
    uart_print_pad2(now.year() % 100); uart_putchar('/');
    uart_print_pad2(now.month());      uart_putchar('/');
    uart_print_pad2(now.day());        uart_putchar(' ');
    uart_print_pad2(now.hour());       uart_putchar(':');
    uart_print_pad2(now.minute());     uart_putchar(':');
    uart_print_pad2(now.second());
  }
}