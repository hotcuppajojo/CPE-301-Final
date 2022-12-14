#include <Wire.h>
#include <Stepper.h>
#include "uRTCLib.h"
#include <LiquidCrystal.h>
#include <DHT.h>
#include <DHT_U.h>
#include "Arduino.h"

#define TBE 0x20
#define RDA 0x80
#define THSENSPIN 7
#define DHTTYPE DHT11
#define WRITE_HIGH_PD(pin_num) *port_d |= (0x01 << pin_num);
#define WRITE_LOW_PD(pin_num) *port_d &= ~(0x01 << pin_num);
#define WRITE_HIGH_PA(pin_num) *port_a |= (0x01 << pin_num);
#define WRITE_LOW_PA(pin_num) *port_a &= ~(0x01 << pin_num);
#define WRITE_HIGH_PK(pin_num) *port_k |= (0x01 << pin_num);
#define WRITE_LOW_PK(pin_num) *port_k &= ~(0x01 << pin_num);

bool timer = false;
bool isIdle = false;
bool enabled = false;
bool test = false;
bool consoleMessage = false;
const float stepNum = 50;
unsigned long int delayStart = 0;
unsigned long int tempHigh = 100;
unsigned long int waterLow = 100;
volatile unsigned long int isr_delay = 0;
volatile unsigned long int startStop_count = 0;
volatile unsigned char *port_d = (unsigned char *)0x2B;
volatile unsigned char *ddr_d = (unsigned char *)0x2A;
volatile unsigned char *pin_d = (unsigned char *)0x29;
volatile unsigned char *my_ADMUX = (unsigned char *)0x7C;
volatile unsigned char *my_ADCSRB = (unsigned char *)0x7B;
volatile unsigned char *my_ADCSRA = (unsigned char *)0x7A;
volatile unsigned int *my_ADC_DATA = (unsigned int *)0x78;
volatile unsigned char *port_k = (unsigned char *)0x108;
volatile unsigned char *ddr_k = (unsigned char *)0x107;
volatile unsigned char *pin_k = (unsigned char *)0x106;
volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
volatile unsigned int *myUBRR0 = (unsigned int *)0x00C4;
volatile unsigned char *myUDR0 = (unsigned char *)0x00C6;
volatile unsigned char *port_b = (unsigned char *)0x25;
volatile unsigned char *ddr_b = (unsigned char *)0x24;
volatile unsigned char *pin_b = (unsigned char *)0x23;
volatile unsigned char *port_a = (unsigned char *)0x22;
volatile unsigned char *ddr_a = (unsigned char *)0x21;
volatile unsigned char *pin_a = (unsigned char *)0x20;

uRTCLib rtc(0x68);
Stepper VENTMOTOR(stepNum, 2, 4, 3, 5);
LiquidCrystal display(8, 9, 10, 11, 12, 13);
DHT THSENS(THSENSPIN, DHTTYPE);

void setup() {
  *ddr_a |= 0b00101010;

  U0init(9600);
  THSENS.begin();
  display.begin(16, 2);
  display.clear();
  VENTMOTOR.setSpeed(500);
  adc_init();
  URTCLIB_WIRE.begin();
  rtc.set(0, 40, 11, 3, 7, 12, 22);
}

void loop() {
  bool startStop = *pin_d & 0b00000100;
  attachInterrupt(digitalPinToInterrupt(19), myISR, FALLING);
  if(startStop & !enabled){
    enabled = true;
    delayStart = millis() - 60000;
    Serial.println("State has changed...");
    Serial.println(startStop_count);
  } else if (startStop){
    consoleMessage = false;
    enabled = false;
    Serial.println("State has changed...");
    Serial.println(startStop_count);
  }

  bool button_1 = *pin_d & 0b00001000;
  bool button_2 = *pin_d & 0b00010000;
  if (!consoleMessage){
    if (button_1) {
      VENTMOTOR.step(50);
      Serial.println("Vent raising");
      printTime();
    } else if (button_2) {
      VENTMOTOR.step(-50);
      Serial.println("Vent lowering");
      printTime();      
    }
  }

  if (!consoleMessage && ((millis() - delayStart) >= 60000) && !enabled) {
    display.clear();
    delayStart = millis();
  } else if (!consoleMessage && ((millis() - delayStart) >= 60000) && enabled) {
    display.clear();
    delayStart = millis();
    float humidity = THSENS.readHumidity();
    float temp_celsius = THSENS.readTemperature();
    float temp_farenheit = THSENS.readTemperature(true);
    display.print("Temp   : ");
    display.print(temp_farenheit);
    if (temp_farenheit > tempHigh) {
      isIdle = false;
    } else {
      isIdle = true;
    }
    display.print("\337F");
    display.setCursor(0, 1);
    display.print("Humid %: ");
    display.print(humidity);
  }

  unsigned int adc_reading = adc_read(0);
  if (enabled && !isIdle && adc_reading < waterLow){
    consoleMessage = true;
  }
  if ((adc_reading <= waterLow) && enabled){
    isIdle = false;
    consoleMessage = true;
    display.clear();
    display.setCursor(0, 0);
    display.print("Please refill");
    display.setCursor(0, 1);
    display.print("water");
  }

  if (enabled && !consoleMessage && !isIdle) {
    if (!timer){
      Serial.print("Fan enabled at time...");
      printTime();
      timer = true;
    }
    test = true;
    *port_a |= 0b00001010;
  } else if (enabled && isIdle) {
    *port_a &= 0b11110101;
    if (timer){
      Serial.print("Fan enabled, error at time...");
      printTime();
      timer = false;
    }
    test = true;
  } else {
    *port_a &= 0b11110101;
    timer = false;
    if (test){
      Serial.print("Cooler turned off");
      printTime();
      test = false;
    }
  }

  if (!enabled){
    display.clear();
  }

  statusLEDs();
  delay(1000);
}

void statusLEDs() {
 
  if (consoleMessage) {
    WRITE_HIGH_PK(6);
  } else {
    WRITE_LOW_PK(6);
  }
 
  if (isIdle & !consoleMessage & enabled) {
    WRITE_HIGH_PK(7);
  } else {
    WRITE_LOW_PK(7);
  }

  if (!enabled) {
    WRITE_HIGH_PK(4);
  } else {
    WRITE_LOW_PK(4);
  }

  if (!isIdle && enabled && !consoleMessage){
    WRITE_HIGH_PK(5);
  } else {
    WRITE_LOW_PK(5);
  }
}


// External Functions
void myISR() {
  if ((millis() - isr_delay) >= 1000) {
    isr_delay = millis();
    startStop_count++;
  }
}

void printTime(){
  rtc.refresh();
  Serial.print(rtc.year());
  Serial.print('/');
  Serial.print(rtc.month());
  Serial.print('/');
  Serial.print(rtc.day());
  Serial.print(' ');
  Serial.print(rtc.hour());
  Serial.print(':');
  Serial.print(rtc.minute());
  Serial.print(':');
  Serial.print(rtc.second());
}

void adc_init() {
  *my_ADCSRA |= 0b10000000;
  *my_ADCSRA &= 0b11011111;
  *my_ADCSRA &= 0b11110111;
  *my_ADCSRA &= 0b11111000;
  *my_ADCSRB &= 0b11110111;
  *my_ADCSRB &= 0b11111000;
  *my_ADMUX &= 0b01111111;
  *my_ADMUX |= 0b01000000;
  *my_ADMUX &= 0b11011111;
  *my_ADMUX &= 0b11100000;
}
unsigned int adc_read(unsigned char adc_channel_num) {
  *my_ADMUX &= 0b11100000;
  *my_ADCSRB &= 0b11110111;
  if (adc_channel_num > 7) {
    adc_channel_num -= 8;
    *my_ADCSRB |= 0b00001000;
  }
  *my_ADMUX += adc_channel_num;
  *my_ADCSRA |= 0x40;
  while ((*my_ADCSRA & 0x40) != 0);
  return *my_ADC_DATA;
}

void print_int(unsigned int out_num) {
  unsigned char print_flag = 0;
  if (out_num >= 1000) {
    U0putchar(out_num / 1000 + '0');
    print_flag = 1;
    out_num = out_num % 1000;
  }
  if (out_num >= 100 || print_flag) {
    U0putchar(out_num / 100 + '0');
    print_flag = 1;
    out_num = out_num % 100;
  }
  if (out_num >= 10 || print_flag) {
    U0putchar(out_num / 10 + '0');
    print_flag = 1;
    out_num = out_num % 10;
  }
  U0putchar(out_num + '0');
  U0putchar('\n');
}

void U0init(int U0baud) {
  unsigned long FCPU = 16000000;
  unsigned int tbaud;
  tbaud = (FCPU / 16 / U0baud - 1);
  *myUCSR0A = 0x20;
  *myUCSR0B = 0x18;
  *myUCSR0C = 0x06;
  *myUBRR0 = tbaud;
  Serial.begin(9600);
}
unsigned char U0kbhit() {
  return *myUCSR0A & RDA;
}
unsigned char U0getchar() {
  return *myUDR0;
}
void U0putchar(unsigned char U0pdata) {
  while ((*myUCSR0A & TBE) == 0);
  *myUDR0 = U0pdata;
}