/*
 * A simple bitcoin hardware wallet with bluetooth connectivity on M5Stack ESP32 board
 * 
 */
#include <M5Stack.h>

// bluetooth serial, copy-paste from the example
#include "BluetoothSerial.h"
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif
BluetoothSerial SerialBT;

// send back every character received over bluetooth
#define USE_ECHO true

// check if buttons are used to confirm transaction
bool is_sign_request = false;

// persistent memory
#include "EEPROM.h"

// bitcoin stuff
#include "Bitcoin.h"
#include "Electrum.h"

// our root master key, derived from mnemonic and empty password in loadMnemonic()
HDPrivateKey hd;
// segwit master private key for testnet, 
// derived from hd as m/84'/1'/0' in loadMnemonic()
// (see bip32 and bip84)
HDPrivateKey account;

// address index
uint32_t ind = 0;
// change or not change address (internal / external)
bool change = false;

// holds the transaction to be signed
ElectrumTx tx;

// holds the command received over bluetooth
String command = "";

// shows mnemonic on the screen and waits for the user to press the button
void showMnemonic(String mnemonic){
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.println("\nWrite down your mnemonic:\n\n");
  
  M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
  M5.Lcd.println(mnemonic);
  M5.Lcd.setFreeFont(&FreeSans9pt7b);
  
  M5.Lcd.println("\n\nand press any button to continue\n");
  bool ok = false;
  while(!ok){
    M5.update();
    if(M5.BtnA.wasReleased() || M5.BtnB.wasReleased() || M5.BtnC.wasReleased()){
      ok = true;
    }
  }
}

// loads mnemonic from persistent memory or generates one
void loadMnemonic(){
  // first we check if the mnemonic is already in the eeprom
  // if it is there, the first bytes should be a string "mnemonic"
  String magic = EEPROM.readString(0);
  String mnemonic;
  if(magic == "mnemonic"){ // do we already have a mnemonic?
    // load mnemonic from address 10
    mnemonic = EEPROM.readString(10);
  }else{
    // random number generation
    // truly random only if WiFi or Bluetooth is enabled
    // not super efficient as we use only 1 byte from every 32-bit random number
    uint8_t rnd[16] = { 0 };
    for(int i=0; i<sizeof(rnd); i++){
      rnd[i] = esp_random();
    }
    mnemonic = generateMnemonic(rnd, sizeof(rnd));
    EEPROM.writeString(0, "mnemonic");
    EEPROM.writeString(10, mnemonic);
    EEPROM.commit();
    showMnemonic(mnemonic);
  }
  // derive root master key and account (native segwit, testnet)
  hd.fromMnemonic(mnemonic, "");
  account = hd.derive("m/84'/1'/0'/");
}

String showXpub(){
  // Lcd display
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setTextColor(BLACK);

  M5.Lcd.qrcode(account.xpub(), 60, 20, 200);

  M5.Lcd.setCursor(M5.Lcd.width()/2, 10);
  M5.Lcd.drawString("Master public key:", M5.Lcd.width()/2, 10);

  return account.xpub();
}

void setup(){
  // initialize the M5Stack object
  M5.begin();
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setFreeFont(&FreeSans9pt7b);

  SerialBT.begin("M5wallet"); //Bluetooth device name

  EEPROM.begin(300); // should be enough for any mnemonic and other data
  loadMnemonic();
  
  showXpub();
}

String showAddress(){
  String addr = account.child(change).child(ind).address();
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.qrcode(String("bitcoin:")+addr, 80, 25, 170);

  M5.Lcd.setFreeFont(&FreeSans9pt7b);
  if(change){
    M5.Lcd.drawString(String("Change address ")+ind+":", M5.Lcd.width()/2, 10);
  }else{
    M5.Lcd.drawString(String("Receiving address ")+ind+":", M5.Lcd.width()/2, 10);
  }

  M5.Lcd.drawString(addr.substring(0,20), M5.Lcd.width()/2, M5.Lcd.height()-32);
  M5.Lcd.drawString(addr.substring(20), M5.Lcd.width()/2, M5.Lcd.height()-15);
  return addr;
}

void doCommand(){
  // show xpub
  if(command.startsWith("xpub")){
    String xpub = showXpub();
    SerialBT.println(xpub);
    return;
  }
  // show receiving address
  if(command.startsWith("addr ")){
    change = false;
    ind = command.substring(5).toInt();
    String addr = showAddress();
    SerialBT.println(addr);
    return;
  }
  // sign electrum transaction
  if(command.startsWith("sign ")){
    is_sign_request = true;
    String rawtx = command.substring(5);
    tx.parse(rawtx.c_str(), rawtx.length());
    M5.Lcd.fillScreen(WHITE);
    M5.Lcd.setCursor(0,0);
    M5.Lcd.setTextSize(1);
    // display transaction on screen
    M5.Lcd.println("\nSign transaction:\n");
    for(int i=0; i<tx.tx.outputsNumber; i++){ 
      M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
      M5.Lcd.print(String("Out ")+i+": ");
      M5.Lcd.setFreeFont(&FreeSans9pt7b);
      M5.Lcd.print(tx.tx.txOuts[i].address(&Testnet));
      M5.Lcd.print(" -> ");
      M5.Lcd.print(tx.tx.txOuts[i].btcAmount()*1000);
      M5.Lcd.println(" mBTC");
    }
    M5.Lcd.setFreeFont(&FreeSansBold9pt7b);
    M5.Lcd.print("Fee: ");
    M5.Lcd.setFreeFont(&FreeSans9pt7b);
    M5.Lcd.print((uint32_t)tx.fee());
    M5.Lcd.println(" satoshi");

    M5.Lcd.setCursor(50, M5.Lcd.height()-20);
    M5.Lcd.print("Cancel");
    M5.Lcd.setCursor(M5.Lcd.width()-100, M5.Lcd.height()-20);
    M5.Lcd.print("Confirm");
    return;
  }
  if(command.startsWith("wipe")){
    EEPROM.writeString(0, "blah"); // erasing the magic word
    for(int i=0; i<200; i++){
      EEPROM.writeUChar(10+i, 'q'); // wiping the mnemonic
    }
    EEPROM.writeUChar(210, '\0');
    EEPROM.commit();
    ESP.restart();
    return;
  }
  SerialBT.print("Unknown command: ");
  SerialBT.println(command);
}

// the loop routine runs over and over again forever
void loop(){
  M5.update();

  // buttons
  if(!is_sign_request){ // not signing request - navigate across addresses
    if (M5.BtnA.wasReleased() && ind > 0){
      ind--;
      showAddress();
    }else if(M5.BtnB.wasReleased()){
      change = !change;
      showAddress();
    }else if(M5.BtnC.wasReleased()){
      ind++;
      showAddress();
    }else if(M5.BtnB.pressedFor(3000)){ // turn off after 3 sec hold
      M5.powerOFF();
    }
  }else{ // signing request - confirm / cancel
    if(M5.BtnA.wasReleased()){
      Serial.println("Cancel");
      SerialBT.println("Cancel");
      is_sign_request = false;
      showAddress();
    }else if(M5.BtnC.wasReleased()){
      tx.sign(account);
      Serial.println(tx);
      SerialBT.println(tx);
      is_sign_request = false;
      showAddress();
    }
  }
  
  // reading commands from bluetooth
  while(SerialBT.available()) {
    char c = SerialBT.read();
    if(!isAscii(c)){ // electrum adds non-ascii junk sometimes
      return;
    }
    Serial.print(c);
    if(USE_ECHO){ // print back the character in echo mode
      SerialBT.print(c);
    }
    if(c == '\n'){ // new line - time to execute the command
      doCommand();
      command = "";
    }else{
      command += String(c);
    }
  }
}
