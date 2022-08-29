// YANreactor
// firmware ridici jednotky reaktoru pro mereni spotreby YAN u kvasinek
// Sleduje ubytek hmotnosti (CO2) mereny tenzometrem. Pokud zacne rychlost ubytku hmotnosti klesat, snazi se ji udrzet davkovanim roztoku YAN.
//


// perioda mereni, vypoctu a davkovani
unsigned long perioda = 600000; //[ms]


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
String filename;


// LCD
#include <LiquidCrystal.h>
#include <TimeLib.h>
LiquidCrystal lcd(14,15,16,17,18,19);            //rs, en, d4, d5, d6, d7


// mereni hmotnosti
float hmotnost_akt = 0;         //aktualni hmotnost
float hmotnost_predch = 0;      //predchozi hmotnost (kvuli derivaci)
float dm = 0;                   //derivace dm/dt [g/h]
float window[10] = {};          //okno pro sliding average
float sl_avg = 0;               //sliding average
float dm_min = 0;               //minimum sliding average
unsigned long cas_predch = 0 - perioda;       //cas predchoziho mereni hmotnosti (kvuli derivaci) + kvuli cyklu mereni (0-perioda, aby to zmerilo jednou na zacatku)


// cerpani N feedu
byte PWMpin = 9;
long Kp = 3000;
float error = 0;
  
long output = 0;                //doba cerpaciho pulsu[ms]
long max_output = 10000;
float dead_band = 0.05;         //deadband pro cerpani feedu
float pump_coef = 0.0002031;   //koeficient pro prepocet output >> Vfeed ([ms] >> [ml])
float total_feed = 0;   //celkovy objem nacerpaneho feedu


//#####################################################################
//                                SETUP
//#####################################################################

void setup()
{
   //LCD inicializace + welcome message
 lcd.begin(16,4);
  
  lcd.clear();
  //lcd.setCursor(6,0);  lcd.print("EPS");
  //lcd.setCursor(2,1);  lcd.print("biotechnology");
  lcd.setCursor(4,0);  lcd.print("HERZLICH");
  lcd.setCursor(3,1);  lcd.print("WILLKOMMEN");
  lcd.setCursor(2,2);  lcd.print("YAN-reaktor");
  lcd.setCursor(0,3);  lcd.print("2022");
  lcd.setCursor(11,3); lcd.print("v2.1b");
  delay(3000);
  lcd.clear();

  
   //Serial.begin(9600);
   //while (!Serial) {;}
   //Serial.println("serial link ..... OK");
   
 //HX711
 //Serial.print("setting AD converter");
 lcd.setCursor(0,0); lcd.print("set ADC");
 // nastavení pinů jako výstup a vstup
 pinMode(pSCK, OUTPUT);
 pinMode(pDT, INPUT);
 // probuzení modulu z power-down módu
 digitalWrite (pSCK, LOW);
 // spuštění prvního měření pro nastavení měřícího kanálu
 mereni_hmotnosti();
 //Serial.println(" ..... OK");
 lcd.print(" ... OK");


 //SD karta a log soubor
 //Serial.print("setting SD memory");
 lcd.setCursor(0,1); lcd.print("set SD");
 while (!SD.begin(10)) {;}
 //Serial.println(" ..... OK");
 lcd.print(" .... OK");

 //Serial.print("creating log file");
 lcd.setCursor(0,2); lcd.print("mk log");
 create_log_file();   //vytvori *.log soubor s unikatnim nazvem
 //Serial.println(" ..... OK");
 lcd.print(" .... OK");
 
  
 //davkovani feedu
 digitalWrite(7, HIGH);   //set direction of pump rotation
 digitalWrite(8, LOW);    //set direction of pump rotation
 pinMode(PWMpin, OUTPUT);
 analogWrite(PWMpin, 0);

 //Serial.println(" ################################################### ");
 //header na seriak:
 //Serial.println("cas_h; hmotnost_akt; dm; sl_avg; dm_min; error; output, total_feed");

  delay(3000);
  lcd.clear();
  lcd.setCursor(0,1);
  lcd.print("spousteni mereni");
  lcd.setCursor(0,2);
  for(byte i=0; i<15; i++){
    lcd.print(char(255));
    delay(500);
  }
  lcd.clear();

}


//#####################################################################
//                               LOOP
//#####################################################################

void loop()
{
  lcd.setCursor(0,0);  lcd.print("             ");    // 13 blank characters as a place for clock
  lcd.setCursor(0,0);  vypis_cas();

    
  delay(1000);

  
//##### sekvence mereni a vypoctu jednou za periodu:
  if(millis() > cas_predch + perioda){
    
    hmotnost_akt = mereni_hmotnosti();

    
    //spocte derivaci dm/dt
    dm = (hmotnost_akt - hmotnost_predch) / float((millis() - cas_predch) / 3600000.0);   //[g/h]
  
    
    //prida aktualni dm do sliding average okna
    for(byte i=1; i<10; i++){window[i] = window[i - 1];}
    window[0] = dm;
    //spocte sliding average
    sl_avg = 0;
    for(byte i=0; i<10; i++){sl_avg += window[i];}
    sl_avg = sl_avg / 10;
    if(millis() < 6000000) sl_avg = 0;    //dokud se sliding average okno nezaplni (coz je cca @90 min), tak je sliding average=0 (jinak to pocita ruzny haluze)

        
    //aktualizuje minimum
    if(sl_avg<dm_min) dm_min = sl_avg;
  
  
    //vypocte error - odchylku aktualni derivace od max. poklesu
    //ale jen v pripade, ze uz je dm_min < 0 (tzn. ze uz skutecne doslo k ubytku hmotnosti
    if(dm_min < 0){error = sl_avg - dm_min;}
    //Serial.print(error); Serial.print(";");
  
  
    // vypocte proporcne k error dobu spusteni cerpadla
    if(error > dead_band && millis() > 7200000){   //deadband + odriznout prvni 2 h  
      output = Kp * error;
      if(output > max_output) output = max_output;    //omezeni max output
      //spusti cerpadlo a pocka vypocteny cas + pripocte nacerpane mnozstvi k total_feed
      analogWrite(PWMpin, 127);   //motor cerpadla je zde napevno na polovicni vykon (127)
      delay(output);
      analogWrite(PWMpin, 0);
      total_feed += output * pump_coef;   // prepocte output [ms] na mnozstvi feedu a pricte k sume feedu [g]

      hmotnost_predch = mereni_hmotnosti();   // zvazit po nadavkovani
    }
    else output = 0;
  
    
    // vypis mereni, davkovani feedu atd. na SD
    logfile = SD.open(filename, FILE_WRITE);
    logfile.print(float(millis() / 3600000.0), 5); logfile.print(";"); 
    logfile.print(hmotnost_akt, 5); logfile.print(";");
    logfile.print(dm, 5); logfile.print(";");
    logfile.print(sl_avg, 5); logfile.print(";");
    logfile.print(dm_min); logfile.print(";");
    logfile.print(error); logfile.print(";");
    logfile.print(output); logfile.print(";");
    logfile.print(total_feed); logfile.print("\n");
    logfile.close();


    // vypis na LCD
    lcd.clear();  
    lcd.setCursor(0,1);  lcd.print("m: "); lcd.print(hmotnost_akt); lcd.print(" g");
    lcd.setCursor(0,2);  lcd.print("imp(-1): "); lcd.print(output); lcd.print(" ms");
    lcd.setCursor(0,3);  lcd.print("Vsum: "); lcd.print(total_feed); lcd.print(" ml");


    cas_predch = millis();    //aktualizuje cas posledniho cyklu mereni/vypoctu/davkovani
  }
}


//#####################################################################
//                      FUNCTION DEFINITIONS
//#####################################################################


//#####################################################################
//vytvori *.log soubor s unikatnim nazvem
void create_log_file()
{
 filename = "data_01.log";
 
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

//#####################################################################
// TIME ROUTINE
void vypis_cas()
 {  
  // write time on LCD
  lcd.print(day()-1); lcd.print("d ");
  lcd.print(hour()), lcd.print(":");
  if(minute() < 10) lcd.print("0");
  lcd.print(minute()); lcd.print(":");
  if(second() < 10) lcd.print("0");
  lcd.print(second());
}
//#####################################################################
