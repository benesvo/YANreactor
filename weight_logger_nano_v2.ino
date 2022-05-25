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
}

void loop()
{
 // výpis měření a jeho výsledku
 logfile = SD.open(filename, FILE_WRITE);
 logfile.print(millis()/1000); logfile.print(";"); 
 logfile.println(mereni_hmotnosti(), 5);
 logfile.close();

 Serial.println(mereni_hmotnosti(), 5);

 //delay 10 minut
 delay(600000);
}


//#####################################################################
// vytvoření funkce pro měření z nastaveného kanálu
float mereni_hmotnosti()
{
 float prum_hmotnost = 0L;
  
 for(int i = 0; i < 100; i++){
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
