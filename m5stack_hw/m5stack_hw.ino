#include <M5Stack.h>

// bluetooth serial
#include "BluetoothSerial.h"
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif
BluetoothSerial SerialBT;

#define USE_ECHO true

bool is_sign_request = false;

// persistent memory
#include "EEPROM.h"

// bitcoin
#include "Bitcoin.h"
#include "Electrum.h"

// our root master key from mnemonic and password
HDPrivateKey hd;
// segwit master private key for testnet (see bip32 and bip84)
HDPrivateKey account;

// address index
uint32_t ind = 0;
// change or not change
bool change = false;

ElectrumTx tx;

String command = "";

void loadMnemonic(){
  // first we check if the mnemonic is already in the eeprom
  // if it is there, the first bytes should be a string "mnemonic"
  String magic = EEPROM.readString(0);
  String mnemonic;
  if(magic == "mnemonic"){
    Serial.println("mnemonic exists");
    // load mnemonic from address 10
    mnemonic = EEPROM.readString(10);
  }else{
    Serial.println("generating new mnemonic...");
    // random number generation
    // truly random only if WiFi or Bluetooth is enabled
    uint8_t rnd[16] = { 0 };
    for(int i=0; i<sizeof(rnd); i++){
      rnd[i] = esp_random();
    }
    mnemonic = generateMnemonic(rnd, sizeof(rnd));
    EEPROM.writeString(0, "mnemonic");
    EEPROM.writeString(10, mnemonic);
    EEPROM.commit();
  }
  
  Serial.println(mnemonic);
  hd.fromMnemonic(mnemonic, "");
  account = hd.derive("m/84'/1'/0'/");
}

String showXpub(){
  // Lcd display
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.qrcode(account.xpub());
  return account.xpub();
}

void setup(){
  // initialize the M5Stack object
  M5.begin();

  // Serial
  Serial.begin(115200);
  Serial.println("\n\nReady to print stuff!");

  SerialBT.begin("ESP32test2"); //Bluetooth device name
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
  M5.Lcd.fillScreen(WHITE);
  String addr = account.child(change).child(ind).address();
  M5.Lcd.qrcode(String("bitcoin:")+addr, 60, 10, 200);
  M5.Lcd.setCursor(30, 210);
  M5.Lcd.print(addr);
  String path = String("m/84'/1'/0'/")+int(change)+"/"+ind;
  M5.Lcd.setCursor(110, 220);
  M5.Lcd.print(path);
  return addr;
}

void doCommand(){
  if(command.startsWith("xpub")){
    String xpub = showXpub();
    Serial.println(xpub);
    SerialBT.println(xpub);
    return;
  }
  if(command.startsWith("addr ")){
    change = false;
    ind = command.substring(5).toInt();
    String addr = showAddress();
    Serial.println(addr);
    SerialBT.println(addr);
    return;
  }
  if(command.startsWith("sign ")){
    is_sign_request = true;
    String rawtx = command.substring(5);
    tx.parse(rawtx.c_str(), rawtx.length());
    M5.Lcd.fillScreen(WHITE);
    M5.Lcd.setCursor(0,0);
    for(int i=0; i<tx.tx.outputsNumber; i++){
      M5.Lcd.print(tx.tx.txOuts[i].address(&Testnet));
      M5.Lcd.print(" -> ");
      M5.Lcd.print(tx.tx.txOuts[i].btcAmount()*1000);
      M5.Lcd.println(" mBTC");

      Serial.print(tx.tx.txOuts[i].address(&Testnet));
      Serial.print(" -> ");
      Serial.print(tx.tx.txOuts[i].btcAmount()*1000);
      Serial.println(" mBTC");
    }
    M5.Lcd.print("Fee: ");
    M5.Lcd.println(float(tx.fee())/100000);
    Serial.print("Fee: ");
    Serial.println(float(tx.fee())/100000);
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
  if(!is_sign_request){
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
  }else{
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
  
  // commands
  while(SerialBT.available()) {
    char c = SerialBT.read();
    if(!isAscii(c)){
      return;
    }
    Serial.print(c);
    if(USE_ECHO){
      SerialBT.print(c);
    }
    if(c == '\n'){
      doCommand();
      command = "";
    }else{
      command += String(c);
    }
  }
}
