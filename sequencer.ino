#include "synth.h"
#include <LiquidCrystal.h>
#include "QuickStats.h"

LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

int sensorValue = 0;
int bit1 = 8;
int bit2 = 9;
int bit3 = 10;

int value = 0;
byte mode = 0;
byte osc = 0;

int aPitch[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int aSet[4] = {0, 0, 0, 0};

int mySensVals[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int aReading[8];
int aState[8] = {1, 1, 1, 1, 1, 1, 1, 1};
int aPrevious[8] = {1, 1, 1, 1, 1, 1, 1, 1};
const int quant = 10;
float med[quant];
String aOsc[6] = {"SIN", "TRI", "SQR", "SAW", "RMP", "WAV"};
long interval = 50;
unsigned long previousMillis = 0;
int i = 0;

synth edgar;
QuickStats stats;


void setup() {
  edgar.begin(CHA);
  pinMode(bit1, OUTPUT);
  pinMode(bit2, OUTPUT);
  pinMode(bit3, OUTPUT);
  pinMode(A0, INPUT);
  pinMode(A1, INPUT_PULLUP);
  pinMode(A2, INPUT);
  pinMode(A3, INPUT_PULLUP);
  lcd.clear();
  lcd.begin(16, 2);
  lcd.print("BitSynth");
  delay(500);
  lcd.clear();
}

void loop()
{
  //A block, 4 pots
  for (int count = 0; count < 4 ; count++)
  {
    digitalWrite(bit1, bitRead(count, 0));
    digitalWrite(bit2, bitRead(count, 1));
    digitalWrite(bit3, bitRead(count, 2));
    for (int x = 0; x < quant; x++)
    {
      med[x] = analogRead(A0);
    }
    mySensVals[count] = stats.median(med, quant);
    if (count == 0)
    {
      aSet[count] = map(mySensVals[count], 0, 1023, 5, 0);
    }
    else if (count == 1)
    {
      aSet[count] = map(mySensVals[count], 0, 1023, 10, 350);
    }
    else if (count == 2)
    {
      aSet[count] = map(mySensVals[count], 0, 1023, 127, 0);
    }
    else if (count == 3)
    {
      aSet[count] = map(mySensVals[count], 0, 1023, 999, 1);
    }
  }
  //B block, 8 buttons
  for (int count = 0; count < 8 ; count++)
  {
    digitalWrite(bit1, bitRead(count, 0));
    digitalWrite(bit2, bitRead(count, 1));
    digitalWrite(bit3, bitRead(count, 2));
    aReading[count] = digitalRead(A1);
    if (aReading[count] == LOW && aPrevious[count] == HIGH)
    {
      if (aState[count] == LOW)
      {
        aState[count] = HIGH;
      }
      else
      {
        aState[count] = LOW;
      }
    }
    aPrevious[count] = aReading[count];
  }
  //C block, 8 pots
  for (int count = 0; count < 8 ; count++)
  {
    digitalWrite(bit1, bitRead(count, 0));
    digitalWrite(bit2, bitRead(count, 1));
    digitalWrite(bit3, bitRead(count, 2));
    for (int x = 0; x < quant; x++)
    {
      med[x] = analogRead(A2);
    }
    mySensVals[count] = stats.median(med, quant);
    aPitch[count] = map(mySensVals[count], 0, 1023, 84, 12);
  }
  //D block, 1 buttons
  digitalWrite(bit1, bitRead(0, 0));
  digitalWrite(bit2, bitRead(0, 1));
  digitalWrite(bit3, bitRead(0, 2));
  int rst = digitalRead(A3);
  if (rst == 0)
  {
    for (int y = 0; y < 8; y++)
    {
      aState[y] = HIGH;
    }
    lcd.setCursor(0, 1);
    lcd.print("RST");
  }
  else
  {
    lcd.setCursor(0, 1);
    lcd.print("   ");
  }
  switch (aSet[0])
  {
    case 0:
      {
        edgar.setupVoice(0, SINE, 0, ENVELOPE0, 0, 64, 500);
        break;
      }
    case 1:
      {
        edgar.setupVoice(0, TRIANGLE, 0, ENVELOPE0, 0, 64, 500);
        break;
      }
    case 2:
      {
        edgar.setupVoice(0, SQUARE, 0, ENVELOPE0, 0, 64, 500);
        break;
      }
    case 3:
      {
        edgar.setupVoice(0, SAW, 0, ENVELOPE0, 0, 64, 500);
        break;
      }
    case 4:
      {
        edgar.setupVoice(0, RAMP, 0, ENVELOPE0, 0, 64, 500);
        break;
      }
    case 5:
      {
        edgar.setupVoice(0, NOISE, 0, ENVELOPE0, 0, 64, 500);
        break;
      }
  }
  unsigned long currentMillis = millis();
  while (currentMillis - previousMillis >= aSet[1] && i < 8)
  {
    previousMillis = currentMillis;
    if (aState[i] == 1)
    {
      //edgar.setMod(0, aSet[3]);
      edgar.setLength(0, aSet[2]);
      edgar.setPitch(0, aPitch[i]);
      edgar.setFilter(0, aSet[3]);
      lcd.setCursor(13, 0);
      lcd.print(aOsc[aSet[0]]); //Osc
      lcd.setCursor(3, 1);
      lcd.print("             ");
      lcd.setCursor(4, 1);
      lcd.print(aSet[1]); //Speed
      lcd.setCursor(9, 1);
      lcd.print(aSet[2]); //Length
      lcd.setCursor(13, 1);
      lcd.print(aSet[3]); //Mod
      lcd.setCursor(0, 0);
      lcd.print("        ");
      lcd.setCursor(i, 0);
      lcd.write(0xFF);
      lcd.setCursor(0, 0);
      lcd.print(i);
      edgar.trigger(0);
      delay(aSet[1]);
    }
    else
    {
      delay(aSet[1]);
    }
    i++;
  }
  if (i == 8)
  {
    i = 0;
  }
}
