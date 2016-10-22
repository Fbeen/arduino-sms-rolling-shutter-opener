/* 
 * Arduino Rolluik opener via SMS bericht
 *
 * Author: Frank Beentjes frankbeen@gmail.com
 *
 * 15 maart 2015
 *
 * benodigde onderdelen:
 * - arduino UNO
 * - GPRS Shield V2.0 (of compatible)
 * - 2 channel Relay Shield With XBee/BTBee interface SHD_RL01 (of compatible)
 * - GSM kaart waarvan de pincode unlocked is
 * - 9 - 12 volt adapter
 * 
 */
 
#include <SoftwareSerial.h>

// digitale pinnen
#define UART_RX 0       // UART wordt gebruikt voor de seriële communicatie met de pc
#define UART_TX 1
#define PUSHBUTTON 2    // drukknop voor rolluik naar beneden
#define RELAY_UP  4
#define RELAY_DOWN  5
#define SOFT_RX 7       // Softwarematige seriële poort voor communicatie met GSM shield
#define SOFT_TX 8
#define POWER  9        // PIN 9 wordt gebruikt om de GSM shield uit de slaapstand te halen
#define STATUS_LED 13   // oranje LED op het arduino UNO board

// status van de relais
#define STOP 0
#define UP 1
#define DOWN 2

// sturing in seconden voor het rolluik op of neer
#define COMMAND_TIME  180 

// whitelist van telefoonnummers 
#define TELNRS 3
String telnrs[TELNRS] = { "625057635", "622450687", "622498620" };

// mogelijke 'op' commmando's
#define OPENEN 4
String openen[OPENEN] = { "open", "omhoog", "up", "op" };

// mogelijke 'neer' commando's
#define SLUITEN 5
String sluiten[SLUITEN] = { "dicht", "beneden", "down", "close", "neer" };

// globale variabelen
int statusRelay = STOP;      // variabele om de status van de relais te onthouden
int countseconds = 0;        // variabele om seconden te tellen 
String buffer;               // buffer om berichten in op te slaan die van de gsm-shield komen
int count=0;                 // teller om het aantal karakters bij te houden in de buffer 
boolean busy = false;        // busy indicator zodat er geen commando's worden verstuurd naar de gsm-shield als we nog een antwoord aan het verwerken zijn

SoftwareSerial GPRS(SOFT_RX, SOFT_TX); // instantieer een softwarematige seriële poort voor de communicatie met het GSM shield

// 'op' commando
void up() {
    Serial.println("Rolluik Open");
    digitalWrite(STATUS_LED,LOW);
    digitalWrite(RELAY_DOWN,LOW);
    delay(500);
    digitalWrite(STATUS_LED,HIGH);
    digitalWrite(RELAY_UP,HIGH);
    countseconds = 0;
    statusRelay = UP;
} 

// 'neer' commando
void down() {
    Serial.println("Rolluik Dicht");
    digitalWrite(STATUS_LED,LOW);
    digitalWrite(RELAY_UP,LOW);
    delay(500);
    digitalWrite(STATUS_LED,HIGH);
    digitalWrite(RELAY_DOWN,HIGH);
    countseconds = 0;
    statusRelay = DOWN;
} 

// 'stop' commando
void stop() {
    digitalWrite(STATUS_LED,LOW);
    digitalWrite(RELAY_UP,LOW);
    digitalWrite(RELAY_DOWN,LOW);
    countseconds = 0;
    statusRelay = STOP;
} 

// functie die kijkt of er één van de telefoonnummers uit de whitelist voorkomt in de opgegeven tekst
boolean haveTelnr(String text)
{
  for(int i = 0 ; i< TELNRS; i++)
  {
    if(text.indexOf(telnrs[i]) != -1)
      return true;
  }
  
  return false;
}

// functie die kijkt of er één van de 'op' commando's uit de whitelist voorkomt in de opgegeven tekst
boolean haveOpen(String text)
{
  text.toLowerCase();
  
  for(int i = 0 ; i < OPENEN; i++)
  {
    if(text.indexOf(openen[i]) != -1)
      return true;
  }
  
  return false;
}

// functie die kijkt of er één van de 'neer' commando's uit de whitelist voorkomt in de opgegeven tekst
boolean haveClose(String text)
{
  text.toLowerCase();

  for(int i = 0 ; i < SLUITEN; i++)
  {
    if(text.indexOf(sluiten[i]) != -1)
      return true;
  }
  
  return false;
}

// functie die het berichtnummer leest uit de tekst die door de GSM shield wordt teruggegeven. het berichtnummer gebruiken we om het bericht te wissen.
int getMessageNumber(String text)
{
  int i = text.indexOf("CMGL:");

  if(i == -1)
    return 0;
  
  i += 6;
  text = text.substring(i);
  
  i = text.indexOf(",");
  
  return text.substring(0, i).toInt();
}

// setup functie wordt eenmalig aangeroepen als de arduino opstart
void setup()
{
  //set pins as input or outputs
  pinMode(2, INPUT);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(RELAY_UP, OUTPUT);
  pinMode(RELAY_DOWN, OUTPUT);
  pinMode(POWER, OUTPUT);

  // power on voor de GSM shield
  digitalWrite(POWER,HIGH);
  delay(2000);
  digitalWrite(POWER,LOW);
  
  // begin seriële comunicatie
  GPRS.begin(19200);               // the GPRS baud rate   
  Serial.begin(19200);             // the Serial port of Arduino baud rate.
  delay(1000);
  
  // vertel de GSM shield dat we in de textmode willen werken
  GPRS.println("AT+CMGF=1");
  delay(1000);

  // hieronder wordt TIMER1 ingesteld op één interupt per seconde en wordt de timer gestart
  cli();//stop interrupts

  //set timer1 interrupt at 1Hz
  TCCR1A = 0;// set entire TCCR1A register to 0
  TCCR1B = 0;// same for TCCR1B
  TCNT1  = 0;//initialize counter value to 0
  // set compare match register for 1hz increments
  OCR1A = 15624;// = (16*10^6) / (1*1024) - 1 (must be <65536)
  // turn on CTC mode
  TCCR1B |= (1 << WGM12);
  // Set CS12 and CS10 bits for 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);  
  // enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);

  sei();//allow interrupts

  Serial.println("Ready");
}

// interrupt routine. Wordt éénmaal per seconde aangeroepen
ISR(TIMER1_COMPA_vect)
{
  // indien we aan het afwachten zijn vragen we iedere seconde of er een sms bericht is
  if(!busy)
    GPRS.println("AT+CMGL=\"ALL\"");
  
  // indien één van de relais ingeschakeld is tellen we de seconden om zo na het verlopen van de COMMAND_TIME het stop commando te kunnen geven
  if(statusRelay != STOP)
  {
    countseconds++;
  
    if(countseconds == COMMAND_TIME) {
       stop();
    }
  }
}

void loop()
{
  char chr;      // slaat één karakter op per keer tijdens het lezen van de seriële GSM-shield poort
  int smsNr;     // variabele om het nummer van het SMS-bericht op te slaan
  int pushTime;  // teller voor de drukknop
  
  if (GPRS.available())              // if date is comming from softwareserial port ==> data is comming from gprs shield
  {
    busy = true;
    
    // tekst ontvangen van de GSM-shield
    while(GPRS.available())     
    {
      chr = GPRS.read();    
      buffer.concat(chr);
      if(count == 64)break;
    }
    
    // kijken of het sms bericht van een geldige telefoon afkomt
    if(haveTelnr(buffer))
    {
      Serial.println(buffer);
      
      if(haveOpen(buffer))
      {
        up();
      }
      else if(haveClose(buffer))
      {
        down();
      }
    }
    
    // indien de tekst een sms-bericht bevat dan willen we dit bericht nu verwijderen 
    smsNr = getMessageNumber(buffer);
    if(smsNr > 0)
      GPRS.println(String("AT+CMGD=") + smsNr);
    
    // clear buffer
    buffer = "";
    count = 0;
 
    busy = false; 
  }
  
  // als de drukknop ingedrukt wordt
  if(digitalRead(PUSHBUTTON))
  {
    pushTime++;
    
    // drukknop moet minimaal 100x als ingedrukt gelezen worden
    if(pushTime > 100)
    {
      down();
      pushTime = 0;
    }
  }

}