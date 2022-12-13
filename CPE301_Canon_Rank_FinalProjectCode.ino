#include <LiquidCrystal.h>//lcd library
#include <DHT.h>//DHT temp sensor library
#include <RTClib.h>//clock library
#include <SoftwareSerial.h>
#include <Stepper.h>//stepper motor library

RTC_DS3231 rtc;
//stepper motor initialization
#define STEPS 64
Stepper stepper(STEPS, 34,36,38,40);
int potVal = 200;
//int isr_count =0;//used for debugging
int is_h20;//water level sensor 
int cur_second;
//interrupt button initialization
const int buttonPinISR = 2;
const byte resetButtonPinISR = 18;
volatile bool toggleStop = true;
volatile bool resetToggle = true;



volatile unsigned char * portH = (unsigned char *) 0x102;//led port/dataregister/pin
volatile unsigned char * ddrH = (unsigned char *) 0x101;
volatile unsigned char * pinH = (unsigned char *) 0x100;

volatile unsigned char * portB = (unsigned char *) 0x25;//interrupt button
volatile unsigned char * ddrB = (unsigned char *) 0x24;
volatile unsigned char * pinB = (unsigned char *) 0x23;

volatile unsigned char * portG = (unsigned char *) 0x34;//fan
volatile unsigned char * ddrG = (unsigned char *) 0x33;
volatile unsigned char * pinG = (unsigned char *) 0x32;

volatile unsigned char *myTCCR1A = (unsigned char *) 0x80;//timer setup
volatile unsigned char *myTCCR1B = (unsigned char *) 0x81;
volatile unsigned char *myTCCR1C = (unsigned char *) 0x82;
volatile unsigned char *myTIMSK1 = (unsigned char *) 0x6F;
volatile unsigned int  *myTCNT1  = (unsigned  int *) 0x84;
volatile unsigned char *myTIFR1 =  (unsigned char *) 0x36;
//LCD initialization
const int rs = A3, en = A5, d4 = A9, d5 = A10, d6 = A11, d7 = A12; //lcd pins
LiquidCrystal lcd(rs, en, d4, d5, d6, d7); 
//clock setup
DateTime now;
//temp/humidity sensor initialization
#define DHTPIN 3 // Defining the data output pin to Arduino
#define DHTTYPE DHT11 // Specify the sensor type(DHT11 or DHT22)
DHT dht(DHTPIN, DHTTYPE);//temp sensor 
float humidity = 0.0;//humidity variable
float tempF = 0.0;//temp variable
int thresholdTemp = 75;//threshold temperature
//clock measuring, couldn't figure out how to encorporate minutes, best I could do was seconds with given time
int prevSecond = 0;//previous second

//state machine changing variable
volatile int evapState = 1;    // 0=error, 1= idle, 2=idle, 3=running
int prevEvapState = 1;//sets it to disabled

//int errorcount = 0; used for debugging
void setup() {

  Serial.begin(9600);
  stepper.setSpeed(200);//stepper motor speed

    // SETUP RTC MODULE
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1);
  }

  DateTime now = rtc.now();
  prevSecond = now.second();  

  *ddrH |= 0b01111000;//set all leds to outputs
  *ddrG |= 0b00100000;//set fan pin to output
  //timer setup
  *myTCCR1A = 0x00;
  *myTCCR1B = 0x00;
  *myTCCR1C = 0x00;
  pinMode(A15,INPUT);  // Initialize H20 Detector
 // ACSR = B01011010; // comparator interrupt enabled and tripped on falling edge.
  //lcd initialization
  pinMode(A14,OUTPUT);
  pinMode(A13,OUTPUT);
  pinMode(A4,OUTPUT);
  pinMode(A0,OUTPUT);
  pinMode(A1,OUTPUT);
  digitalWrite(A14,LOW); 
  digitalWrite(A13,HIGH); 
  digitalWrite(A4,LOW); 
  digitalWrite(A0,LOW);
  digitalWrite(A1,HIGH);
  lcd.begin(16, 2);
  //temp sensor initialization
  dht.begin();
  //interrupt buttons
  pinMode(buttonPinISR, INPUT_PULLUP);
  pinMode(resetButtonPinISR, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(buttonPinISR), buttonISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(resetButtonPinISR), resetISR, FALLING);
}
void loop() {
  now = rtc.now();
  cur_second = now.second();
  potVal = map(analogRead(A2),0,1024,0,500);
  //run vent control in every state except disabled
  if ((potVal>300) && (evapState!= 1)) {
    stepper.step(1);//clockwise (open)
    Serial.print("open ");
    Serial.print(now.second(),DEC);
    Serial.print("\n");
    updateRTC();
  }
  else if ((potVal <=100) && (evapState !=1 )) {
    updateRTC();
    stepper.step(-1);
    Serial.print("closing");
    Serial.print("\n");
 } else {

    //int freq = 100;//delay frequency
    //read humidity and temp from DHT sensor
    //my_delay(10);
    //delay(10);
    humidity = dht.readHumidity(true);
    //my_delay(10);
    //delay(10);
    tempF = dht.readTemperature(true);
    //This is was a way to figure out if the temp sensor was connected properly. Sensor would be connected properly and then get trapped in this without reading anything; not sure why
    //  if (isnan(humidity)){
    //    Serial.println("Failed to read the DHT sensor. Check connections");
    //  }
// if((cur_second % 10) == 0) {
//     Serial.print(is_h20,HEX);
//     Serial.print("  no movement ");
//     Serial.print(cur_second,DEC);
//     Serial.print("\n");
//  } 
      // Determine current state based on toggleStop, temperature, and water level
      if (evapState > 1){
       is_h20 = analogRead(A15);
       if (is_h20 < 1) {
          evapState = 0;
       }
      } 
       if (evapState != 0) {
        if (toggleStop == true) {
          evapState = 1;  // DISABLED
        } else {
          if (tempF <= thresholdTemp) {
            evapState = 2;  // IDLE
          }
          if (tempF > thresholdTemp) {
            evapState = 3;  // RUNNING
          }
        }
    }
    if ((cur_second != prevSecond) || (evapState != prevEvapState)) {
      //debugging code        
      // Serial.print((int)tempF,HEX);
      // Serial.print(" / ");
      // Serial.print(humidity);
      // Serial.print("\n)");
      // Serial.print(Pval); //for debugging
      // Serial.print("/ ");
      // Serial.print(potVal);
      // Serial.print("\n");

 //State machine switch statement
        switch(evapState) {
          case 1: // Disabled State
              DISABLED();
              rtc.begin(); // vent pot occasionally mucks up RTC (for some reason)
            break;
          case 2: // Idle State
              //if ((evapState != prevEvapState) || (now.second() == 30)){      
                IDLE_(humidity, tempF);
                //rtc.begin();
              //}
            break;
          case 3: // Running State
              //if ((evapState != prevEvapState) || (now.second() == 30)){
                RUNNING(humidity, tempF);
              //}
                //rtc.begin();
            break;
          case 0:
              evapState = ERROR_();
              //rtc.begin();
              break;
          default: // Error State
              evapState = ERROR_();
              break;
        } // end switch
      
      // This takes care of the needed to check toggle and/or running changing, so that code wouldn't be needed
      if (evapState != prevEvapState) {
          updateRTC();
          prevEvapState = evapState;
        }
        prevSecond = now.second();
      }
  }

}
void DISABLED(){ //disabled state conditions (project requirements) 
  *portH |= 0b01111000;
  *portH &= 0b01011000;
  *portG |= 0b00100000;
  *portG &= 0b00100000;
  lcd.clear();
  lcd.print("Disabled");
  Serial.print("Disabled\n");
}
void RUNNING(float humidity, float tempF){//running state conditions (project requirements)
  *portH |= 0b01111000;
  *portH &= 0b01110000;
  *portG |= 0b00100000;
  *portG &= 0b00000000;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(String("Humid: ") + String(humidity) + String("%"));
  lcd.setCursor(0, 1);
  lcd.print(String("Temp: ") + String(tempF) + String("F"));
  Serial.print("Running\n");
  }
void IDLE_(float humidity, float tempF){//idle state conditions (project requirements)
  *portH |= 0b01111000;//turn fan off
  *portH &= 0b00111000;
  *portG |= 0b00100000;
  *portG &= 0b00100000;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(String("Humid: ") + String(humidity) + String("%"));
  lcd.setCursor(0, 1);
  lcd.print(String("Temp: ") + String(tempF) + String("F"));
  Serial.print("Idle\n");
 }
int ERROR_(){//error state conditions (project requirements)
  int newState = 0;
  *portH |= 0b01111000;
  *portH &= 0b01101000;
  *portG |= 0b00100000;
  *portG &= 0b00100000;
  lcd.clear();
  lcd.setCursor(3,0);
  lcd.print("Water Level");
  lcd.setCursor(5, 1);
  lcd.print("Too Low");
  Serial.print("Error\n");
  if(resetToggle == false){//interrupt conditions
    resetToggle = true;
    newState = 1;
  }
  return newState;
}
void resetISR(){//error reset interrupt
  if(digitalRead(resetButtonPinISR) == LOW){
    resetToggle = !resetToggle;        
  }
}
void updateRTC(){ //clock function to get the time during state change and vent opening or closing
  DateTime now = rtc.now();
  //used for debugging
  // Serial.print(evapState,DEC);
  // Serial.print("=State Changed ");
  Serial.print(now.hour(), DEC);
  Serial.print(": ");
  Serial.print(now.minute(), DEC);
  Serial.print(" #");
  Serial.print(toggleStop); 
  Serial.print("\n");
  // Serial.print(" ISR:"); 
  // Serial.print(isr_count, DEC);
}
void buttonISR(){//state change interrupt
   if (digitalRead(buttonPinISR) == LOW){
     toggleStop = !toggleStop;
     isr_count++;
   }
}

void my_delay(unsigned int freq)//delay function
{
  // calc period
  double period = 1.0/double(freq);
  // 50% duty cycle
  double half_period = period/ 2.0f;
  // clock period def
  double clk_period = 0.0000000625;
  // calc ticks
  unsigned int ticks = period / clk_period;
  // stop the timer
  *myTCCR1B &= 0xF8;
  // set the counts
  *myTCNT1 = (unsigned int) (65536 - ticks);
  // start the timer
  * myTCCR1B |= 0b00000001;
  // wait for overflow
  while((*myTIFR1 & 0x01)==0); // 0b 0000 0000
  // stop the timer
  *myTCCR1B &= 0xF8;   // 0b 0000 0000
  // reset TOV           
  *myTIFR1 |= 0x01;
}
