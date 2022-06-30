// HX711 AD 24-bit převodník s 2 kanály
// piny pro připojení SCK a DT z modulu
int pSCK = 2;
int pDT = 3;

//SD karta
/*  SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 10 (for MKRZero SD: SDCARD_SS_PIN)
*/
#include <SPI.h>
#include <SD.h>
File logfile;
String filename = "data_01.log";

// mereni hmotnosti
float hmotnost_akt = 0;         //aktualni hmotnost
float hmotnost_predch = 0;      //predchozi hmotnost (kvuli derivaci)
float dm = 0;                   //derivace dm/dt
float window[10] = {};          //okno pro sliding average
float sl_avg = 0;               //sliding average
float dm_min = 0;               //minimum sliding average
unsigned long cas_predch = 0;   //cas predchoziho mereni hmotnosti (kvuli derivaci)


// cerpani N feedu
byte PWMpin = 9;
long Kp = 300;
float error = 0;
  
long output = 0;
float dead_band = 0.05;
long pump_coef = 100;   //koeficient pro prepocet output >> Vfeed
unsigned long total_feed = 0;



void setup()
{
   Serial.begin(9600);
   while (!Serial) {;}
   
 //HX711  
 // nastavení pinů jako výstup a vstup
 pinMode(pSCK, OUTPUT);
 pinMode(pDT, INPUT);
 // probuzení modulu z power-down módu
 digitalWrite (pSCK, LOW);
 // spuštění prvního měření pro nastavení měřícího kanálu
 mereni_hmotnosti();

 //SD karta a log soubor
 while (!SD.begin(10)) {;}
 
 //vytvori *.log soubor s unikatnim nazvem
 byte poradi = 1;
 while(SD.exists(filename)){
   poradi++;
   filename = "data_";
   if(poradi < 10)    filename.concat("0");
   filename.concat(poradi);
   filename.concat(".log");
 }
 //vytvori log soubor
 logfile = SD.open(filename, FILE_WRITE);
 logfile.println("cas[s];hmotnost[g]");
 logfile.close();

 //davkovani feedu
 pinMode(PWMpin, OUTPUT);
 analogWrite(PWMpin, 0);
}


//#####################################################################
void loop()
{
  hmotnost_akt = mereni_hmotnosti() - total_feed;  
  
  dm = (hmotnost_akt - hmotnost_predch) / (millis() - cas_predch);

  //prida aktualni dm do sliding average okna
  for(byte i=1; i<10; i++){
    window[i] = window[i - 1];
  }
  window[0] = dm;

  //spocte sliding average
  sl_avg = 0;
  for(byte i=0; i<10; i++){
    sl_avg += window[i];
  }
  sl_avg = sl_avg / 10;

  //aktualizuje minimum
  if(sl_avg<dm_min) dm_min = sl_avg;


  //vypocte mnozstvi nadavkovaneho feedu (resp. dobu, po kterou bude spustene cerpadlo)
  long dt = millis() - cas_predch;

  error = sl_avg - dm_min;
  
  if(error > dead_band){   //deadband
    output = Kp * error;
    total_feed += output / pump_coef;   // prepocte output na mnozstvi feedu a pricte k sume feedu
    
    //spusti cerpadlo a pocka vypocteny cas
    analogWrite(PWMpin, 124);
    delay(output);
    analogWrite(PWMpin, 0);
  }
  else output = 0;
  
  cas_predch = millis();


 // výpis měření a davkovani feedu
 logfile = SD.open(filename, FILE_WRITE);
 logfile.print(millis()/1000); logfile.print(";"); 
 logfile.print(hmotnost_akt, 5); logfile.print(";");
 logfile.print(sl_avg); logfile.print(";");
 logfile.print(output); logfile.print(";");
 logfile.println(total_feed);
 logfile.close();

 Serial.println(hmotnost_akt, 5);

   //delay 10 minut
 delay(600000);
}


//#####################################################################
// vytvoření funkce pro měření z nastaveného kanálu
float mereni_hmotnosti()
{
 float prum_hmotnost = 0L;
  
 for(byte i = 0; i < 100; i++){
 byte mericiMod = 1;
 byte index;
 long vysledekMereni = 0L;
 // načtení 24-bit dat z modulu
 while(digitalRead (pDT));
 for (index = 0; index < 24; index++)
 {
 digitalWrite (pSCK, HIGH);
 vysledekMereni = (vysledekMereni << 1) | digitalRead (pDT);
 digitalWrite (pSCK, LOW);
 }
 // nastavení měřícího módu
 for (index = 0; index < mericiMod; index++)
 {
 digitalWrite (pSCK, HIGH);
 digitalWrite (pSCK, LOW);
 }
 // konverze z 24-bit dvojdoplňkového čísla
 // na 32-bit znaménkové číslo
 if (vysledekMereni >= 0x800000 )
 vysledekMereni = vysledekMereni | 0xFF000000;

  prum_hmotnost = prum_hmotnost + vysledekMereni;
  }
  // vypocet prumeru a prevod na uV
  prum_hmotnost = prum_hmotnost / 100 / 256;

  // prepocet na g - jen dvojbodova kalibrace 0 / 50 g
  prum_hmotnost = -0.3734 * prum_hmotnost - 142.5397;
  
 return(prum_hmotnost);
}
