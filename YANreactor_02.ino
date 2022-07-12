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
float dm = 0;                   //derivace dm/dt [g/h]
float window[10] = {};          //okno pro sliding average
float sl_avg = 0;               //sliding average
float dm_min = 0;              //minimum sliding average; zacina na 10, aby pokryl pripadne pocatecni kolisani
unsigned long cas_predch = 0;   //cas predchoziho mereni hmotnosti (kvuli derivaci)


// cerpani N feedu
byte PWMpin = 9;
long Kp = 3000;
float error = 0;
  
long output = 0;
long max_output = 10000;
float dead_band = 0.05;
float pump_coef = 0.00020354;   //koeficient pro prepocet output >> Vfeed
unsigned long total_feed = 0;

unsigned long test_cas = 10;   //cas v ms pro testovani

void setup()
{
   Serial.begin(9600);
   //while (!Serial) {;}
   //Serial.println("serial link ..... OK");
   
 //HX711
 Serial.print("setting AD converter");
 // nastavení pinů jako výstup a vstup
 pinMode(pSCK, OUTPUT);
 pinMode(pDT, INPUT);
 // probuzení modulu z power-down módu
 digitalWrite (pSCK, LOW);
 // spuštění prvního měření pro nastavení měřícího kanálu
 mereni_hmotnosti();
 Serial.println(" ..... OK");


 //SD karta a log soubor
 Serial.print("setting SD memory");
 while (!SD.begin(10)) {;}
 Serial.println(" ..... OK");

 Serial.print("creating log file");
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
 logfile.println("cas_h; hmotnost_akt; dm; sl_avg; dm_min; error; output; total_feed");
 logfile.close();
 Serial.println(" ..... OK");

 //davkovani feedu
 digitalWrite(7, HIGH);   //set direction of pump rotation
 digitalWrite(8, LOW);    //set direction of pump rotation
 pinMode(PWMpin, OUTPUT);
 analogWrite(PWMpin, 0);

 Serial.println(" ################################################### ");

 //header na seriak:
 Serial.println("cas_h; hmotnost_akt; dm; sl_avg; dm_min; error; output, total_feed");
}


//#####################################################################
void loop()
{
  //hmotnost_akt = mereni_hmotnosti() - total_feed;  
  // nahrada rovnici pro ucely testovani
  float cas_h = test_cas / 3600000.0;
  hmotnost_akt = -0.00001859*pow(cas_h, 4) + 0.002987*pow(cas_h, 3) - 0.1515*pow(cas_h, 2) + 1.4765*cas_h + 1877;
  Serial.print(cas_h); Serial.print(";"); 
  Serial.print(hmotnost_akt); Serial.print(";"); 

  dm = (hmotnost_akt - hmotnost_predch) / float((test_cas - cas_predch) / 3600000.0);
  hmotnost_predch = hmotnost_akt;
  Serial.print(dm, 5); Serial.print(";");
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

  Serial.print(sl_avg); Serial.print(";");
  
  
  //aktualizuje minimum
  if(sl_avg<dm_min) dm_min = sl_avg;
  
  Serial.print(dm_min); Serial.print(";");

  //vypocte error - odchylku aktualni derivace od max. poklesu
  //ale jen v pripade, ze uz je dm_min < 0 (tzn. ze uz skutecne doslo k ubytku hmotnosti
  if(dm_min < 0){
    error = sl_avg - dm_min;
  }
  Serial.print(error); Serial.print(";");

  // vypocte proporcne k error dobu spusteni cerpadla
  if(error > dead_band && test_cas > 7200000){   //deadband + odriznout prvni 2 h  
    output = Kp * error;
    if(output > max_output) output = max_output;    //omezeni max output
    
    //spusti cerpadlo a pocka vypocteny cas + pripocte nacerpane mnozstvi k total_feed
    analogWrite(PWMpin, 127);   //motor cerpadla je zde napevno na polovicni vykon
    delay(output);
    analogWrite(PWMpin, 0);
    total_feed += output * pump_coef;   // prepocte output [ms] na mnozstvi feedu a pricte k sume feedu [g]
  }
  else output = 0;

  Serial.print(output); Serial.print(";");
  Serial.println(total_feed);  
  
  cas_predch = test_cas;


 // vypis mereni, davkovani feedu atd. na SD
 logfile = SD.open(filename, FILE_WRITE);
 logfile.print(test_cas/1000); logfile.print(";"); 
 logfile.print(hmotnost_akt, 5); logfile.print(";");
 logfile.print(dm, 5); logfile.print(";");
 logfile.print(sl_avg, 5); logfile.print(";");
 logfile.print(dm_min); logfile.print(";");
 logfile.print(error); logfile.print(";");
 logfile.print(output); logfile.print(";");
 logfile.print(total_feed); logfile.print("\n");
 logfile.close();


 //delay 10 minut
 //delay(600000);
 test_cas += 600000;
 delay(100);
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
