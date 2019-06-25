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
  M5.Lcd.println(mnemonic);
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
    Serial.println("mnemonic exists");
    // load mnemonic from address 10
    mnemonic = EEPROM.readString(10);
  }else{
    Serial.println("generating new mnemonic...");
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
  M5.Lcd.setTextSize(1);
  M5.Lcd.qrcode(account.xpub(), 60, 20, 200);

  M5.Lcd.setCursor(100, 5);
  M5.Lcd.println("Master public key:");

//  M5.Lcd.setCursor(0, M5.Lcd.height()-30);
//  M5.Lcd.println(account.xpub());

  return account.xpub();
}

void setup(){
  // initialize the M5Stack object
  M5.begin();

  // Serial
  Serial.begin(115200);
  Serial.println("\n\nReady to print stuff!");

  SerialBT.begin("M5wallet"); //Bluetooth device name
  Serial.println("The device started, now you can pair it with bluetooth!");

  EEPROM.begin(300); // should be enough for any mnemonic and other data
  loadMnemonic();

  Serial.print("Master public key: ");
  Serial.println(account.xpub());
  Serial.print("First bitcoin address: ");
  Serial.println(account.derive("m/0/0").address());

  showXpub();
}

String showAddress(){
  String addr = account.child(change).child(ind).address();
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.qrcode(String("bitcoin:")+addr, 60, 10, 200);

  M5.Lcd.setCursor(100, 5);
  if(change){
    M5.Lcd.println("Change address:");
  }else{
    M5.Lcd.println("Receiving address:");
  }

  M5.Lcd.setCursor(30, 210);
  M5.Lcd.setTextSize(1);
  M5.Lcd.print(addr);

  String path = String("m/84'/1'/0'/")+int(change)+"/"+ind;
  M5.Lcd.setCursor(110, 220);
  M5.Lcd.print(path);
  return addr;
}

void doCommand(){
  // show xpub
  if(command.startsWith("xpub")){
    String xpub = showXpub();
    Serial.println(xpub);
    SerialBT.println(xpub);
    return;
  }
  // show receiving address
  if(command.startsWith("addr ")){
    change = false;
    ind = command.substring(5).toInt();
    String addr = showAddress();
    Serial.println(addr);
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
    M5.Lcd.println("\n Sign transaction:\n\n");
    for(int i=0; i<tx.tx.outputsNumber; i++){ 
      M5.Lcd.print(" ");
      M5.Lcd.println(tx.tx.txOuts[i].address(&Testnet));
      M5.Lcd.print(" -> ");
      M5.Lcd.print(tx.tx.txOuts[i].btcAmount()*1000);
      M5.Lcd.println(" mBTC\n");

      Serial.print(tx.tx.txOuts[i].address(&Testnet));
      Serial.print(" -> ");
      Serial.print(tx.tx.txOuts[i].btcAmount()*1000);
      Serial.println(" mBTC");
    }
    M5.Lcd.print(" Fee: ");
    M5.Lcd.print((uint32_t)tx.fee());
    M5.Lcd.println(" satoshi");
    Serial.print("Fee: ");
    Serial.print((uint32_t)tx.fee());
    Serial.println(" satoshi");

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
  Serial.print("Unknown command: ");
  Serial.println(command);
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
