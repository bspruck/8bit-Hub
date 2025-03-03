
// System libraries
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>

// Debugging
//#define __DEBUG_JOY__
//#define __DEBUG_JPG__
//#define __DEBUG_MOUSE__
//#define __DEBUG_CMD__
//#define __DEBUG_COM__
//#define __DEBUG_IO__
//#define __DEBUG_PCK__
//#define __DEBUG_TCP__
//#define __DEBUG_UDP__
//#define __DEBUG_URL__
//#define __DEBUG_WEB__

// Firmware Version
char espVersion[5] = "?";
char espUpdate[5] = "?";
char megaVersion[5] = "v0.6";
char megaUpdate[5] = "?";
char urlEsp[]  = "http://8bit-unity.com/Hub-ESP8266.bin";
char urlMega[] = "http://8bit-unity.com/Hub-Mega2560.bin";
char urlVer[]  = "http://8bit-unity.com/Hub-Version.txt";

// HUB Commands
#define HUB_SYS_ERROR     0
#define HUB_SYS_RESET     1
#define HUB_SYS_NOTIF     2
#define HUB_SYS_SCAN      3
#define HUB_SYS_CONNECT   4
#define HUB_SYS_IP        5
#define HUB_SYS_MOUSE     6
#define HUB_SYS_VERSION   7
#define HUB_SYS_UPDATE    8
#define HUB_SYS_STATE     9  // COM 
#define HUB_SYS_RESEND    9  // ESP
#define HUB_DIR_LS       10  // Todo: Implement for root directory /microSD
#define HUB_DIR_MK       11
#define HUB_DIR_RM       12
#define HUB_DIR_CD       13
#define HUB_FILE_OPEN    21
#define HUB_FILE_SEEK    22
#define HUB_FILE_READ    23
#define HUB_FILE_WRITE   24
#define HUB_FILE_CLOSE   25
#define HUB_UDP_OPEN     30
#define HUB_UDP_RECV     31
#define HUB_UDP_SEND     32
#define HUB_UDP_CLOSE    33
#define HUB_UDP_SLOT     34
#define HUB_TCP_OPEN     40
#define HUB_TCP_RECV     41
#define HUB_TCP_SEND     42
#define HUB_TCP_CLOSE    43
#define HUB_TCP_SLOT     44
#define HUB_WEB_OPEN     50
#define HUB_WEB_RECV     51
#define HUB_WEB_HEADER   52
#define HUB_WEB_BODY     53
#define HUB_WEB_SEND     54
#define HUB_WEB_CLOSE    55
#define HUB_URL_GET      60
#define HUB_URL_READ     61

// String definitions
#ifdef __DEBUG_CMD__
  const char* cmdString[] = 
  {"SYS_ERROR","SYS_RESET","SYS_NOTIF", "SYS_SCAN",  "SYS_CONNECT","SYS_IP",   "SYS_MOUSE","SYS_VERSION","SYS_UPDATE","",
   "DIR_LS",   "DIR_MK",   "DIR_RM",    "DIR_CD",    "",           "",         "",         "",           "",          "",
   "FILE_OPEN","FILE_SEEK","FILE_READ", "FILE_WRITE","FILE_CLOSE", "",         "",         "",           "",          "",
   "UDP_OPEN", "UDP_RECV", "UDP_SEND",  "UDP_CLOSE", "UDP_SLOT",   "",         "",         "",           "",          "",
   "TCP_OPEN", "TCP_RECV", "TCP_SEND",  "TCP_CLOSE", "TCP_SLOT",   "",         "",         "",           "",          "",
   "WEB_OPEN", "WEB_RECV", "WEB_HEADER","WEB_BODY",  "WEB_SEND",   "WEB_CLOSE","",         "",           "",          "",
   "URL_GET",  "URL_READ", "",          "",          "",           "",         "",         "",           "",          ""};
#endif
const char* blank = "                    ";
const char* fail  = "Update failed!      ";

// Useful macros
#define MIN(a,b) (a>b ? b : a)
#define MAX(a,b) (a>b ? a : b)

// HUB Modes
#define MODE_NONE  0
#define MODE_APPLE 1
#define MODE_ATARI 2
#define MODE_BBC   3
#define MODE_C64   4
#define MODE_NES   5
#define MODE_ORIC  6
#define MODE_LYNX  7
#define HUB_MODES  8
const char* modeString[HUB_MODES] = {"Please setup", "Apple //", "Atari 8bit", "BBC Micro", "C64/C128", "NES", "Oric", "Lynx"};
byte hubMode = MODE_NONE;

// COMM Params
#define FILES    8     // Number of file handles
#define SLOTS    8     // Number of connection handles
#define PACKET   256   // Max. packet length (bytes)
#define TIMEOUT  1000  // Packet timout (milliseconds)

// HUB Files
File hubFile[FILES];

// IP Params
char IP[17] = "Not connected...";

// Buffers for data exchange with ESP8266
char serBuffer[PACKET];
unsigned char serLen;

////////////////////////////////
//     DEBUGGING functions    //
////////////////////////////////

/*void udpDebug() {
    // Setup request    
    unsigned char cmd[] = {1};

    while (true) {
        writeCMD(HUB_UDP_SLOT);
        writeChar(0);
        writeCMD(HUB_UDP_OPEN);
        writeChar(199); writeChar(47);
        writeChar(196); writeChar(106);
        writeInt(5000); writeInt(5000);
        writeCMD(HUB_UDP_SEND);
        writeBuffer(cmd, 1);
        
        // Wait for answer
        uint32_t timeout = millis()+3000;
        while (millis() < timeout) {
            if (Serial3.find("CMD") && readChar() == HUB_UDP_RECV) {
                readChar();
                if (readBuffer())
                    Serial.print("Received: ");
                    Serial.println(serLen);
                break;
            }
        }
        if (millis() >= timeout) {
              Serial.println("Timeout");
              lcd.print("Timeout");
        }
        writeCMD(HUB_UDP_CLOSE);
        delay(500);
    }
}*/

////////////////////////////////
//      PACKET functions      //
////////////////////////////////

// Define packet structure
typedef struct packet {
    unsigned char ID;
    unsigned char cmd;
    unsigned char slot;  
    unsigned char len;
    unsigned char* data;
    uint32_t timeout;
    struct packet* next;
} packet_t;

packet_t* packetHead = NULL;
unsigned char packetID = 0;

void pushPacket(unsigned char cmd, signed char slot) {
    // Create new packet
    packet_t* packet = malloc(sizeof(packet_t));
    packet->next = NULL;

    // Assign ID & Timeout
    if (++packetID>254) { packetID = 1; }
    packet->ID = packetID;
    packet->timeout = millis() + TIMEOUT;

    // Copy data to packet
    packet->len = serLen;
    packet->cmd = cmd;
    packet->slot = slot;
    packet->data = (unsigned char*)malloc(serLen);
    memcpy(packet->data, serBuffer, serLen);
    
    // Append packet at tail of linked list
    if (!packetHead) {
        packetHead = packet;
    } else {
        packet_t *packetTail = packetHead;
        while (packetTail->next != NULL) {
            packetTail = packetTail->next;
        }
        packetTail->next = packet;
    }
#ifdef __DEBUG_PCK__
    Serial.print("PUSH: "); Serial.println((byte)packet->ID);
#endif
}

packet_t *getPacket(unsigned char cmd, signed char slot) {
    // Find packet with matching cmd/slot
    packet_t *packet = packetHead;
    while (packet) {
        if (packet->cmd == cmd && packet->slot == slot)
            return packet;
        packet = packet->next;
    }
    return NULL;
}

void popPacket(unsigned char ID) {
    // Pop packet with matching ID
    packet_t *prev = NULL, *next = NULL, *packet = packetHead;
    while (packet) {
        next = packet->next;
        if (packet->ID == ID) {
            if (prev)
                prev->next = next;
            else
                packetHead = next;
        #ifdef __DEBUG_PCK__
            Serial.print("POP: "); Serial.println((byte)packet->ID);    
        #endif
            free(packet->data);
            free(packet);
            return;
        }
        prev = packet;
        packet = next;
    }
}

void packetTimeout() {
  // Remove packets that have exceeded timeout
  while (packetHead && millis() > packetHead->timeout) {
      popPacket(packetHead->ID);
  }
}

//////////////////////////
//    List functions    //
//////////////////////////

// Define list structure
typedef struct list {
    char* data;
    struct list* next;
} list_t;

list_t* listHead;

void pushList(char* data) {
    // Create new packet
    list_t* elt = malloc(sizeof(list_t));
    elt->data = data;
    elt->next = NULL;

    // Append packet at tail of linked list
    if (!listHead) {
        listHead = elt;
    } else {
        list_t *tail = listHead;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = elt;
    }     
}

unsigned char* getList(unsigned char index) {
    byte i=0;
    list_t* elt = listHead;
    while (elt && i<index) {
        elt = elt->next;
        i++;
    }
    if (elt) {
        return elt->data;
    } else {
        return NULL;
    }
}

unsigned char lenList() {
    byte i=0;
    list_t* elt = listHead;
    while (elt) {
        elt = elt->next;
        i++;
    }
    return i;    
}

void clearList() {
    list_t* elt;
    while (listHead) {
        elt = listHead;
        listHead = elt->next;
        free(elt);
    }
}

////////////////////////////////
//    PERIPHERAL functions    //
////////////////////////////////

// Joystick states
#define JOY_UP    1
#define JOY_DOWN  2
#define JOY_LEFT  4
#define JOY_RIGHT 8
#define JOY_FIRE1 16
#define JOY_FIRE2 32

// Joystick modes
#define JOY_STD  0
#define JOY_PASE 1

// Joystick params
unsigned char joyMode = JOY_STD;
unsigned char joyState[4] = { 255, 255, 255, 255 };
unsigned char joyPins[4][8] = { { 23, 25, 27, 29, 22, 24, 28, 26 },   // U, D, L, R, F1, F2, Alt F2, GND
                                { 31, 33, 35, 37, 30, 32, 36, 34 },
                                { 39, 41, 43, 45, 38, 40, 44, 42 },
                                {  0,  0,  0,  0,  0,  0,  0,  0 } }; // Joy #4 unassigned in current version
unsigned char joyStnd[6] = {   1,  2,  4,  8, 16, 32 };
unsigned char joyPase[6] = {  16,  8,  1,  2,  4,  0 };

// Mouse params
unsigned char mouseState[2] = { 255, 255 };
unsigned char mouseSpeed = 1;
unsigned char mouseFPS = 1;

void setupJOY() {
    // Set I/O pins
    for (byte i=0; i<3; i++) {
        for (byte j=0; j<7; j++) {
            pinMode(joyPins[i][j], INPUT_PULLUP);  // Inputs
        }
        pinMode(joyPins[i][7], OUTPUT);            // Ground
        digitalWrite(joyPins[i][7], LOW);
    }  

    // Be careful with Atari 7800 joypads (Pin 9 causes short-circuit)
    for (byte i=0; i<3; i++) { 
        if (!digitalRead(joyPins[i][6])) joyPins[i][6] = joyPins[i][5];
    }
}

void readJOY() {
    // Read digital pins
    switch (joyMode) {
    case JOY_STD:    
        for (byte i=0; i<3; i++) { 
            joyState[i] |= 63;
            for (byte j=0; j<6; j++) {
                if (!digitalRead(joyPins[i][j])) { joyState[i] &= ~joyStnd[j]; }  
            } 
            if (!digitalRead(joyPins[i][6])) { joyState[i] &= ~joyStnd[5]; } 
        }  
        break;
    case JOY_PASE:
        for (byte i=0; i<2; i++) { 
            joyState[i] = 32;
            for (byte j=0; j<6; j++) {
                if (digitalRead(joyPins[i][j])) { joyState[i] += joyPase[j]; }  
            }
            if (!digitalRead(joyPins[i][6])) { joyState[i] &= ~joyPase[5]; } 
        }
        break;
    }
#ifdef __DEBUG_JOY__
    Serial.print("Joystick: "); 
    for (byte i=0; i<4; i++) { Serial.println(joyState[i]); Serial.print(","); }
#endif    
}

void readMouse() {
    unsigned char s = readChar();
    signed char x = readChar();
    signed char y = readChar();
    signed char w = readChar();

    // Adjust inputs
    switch (mouseSpeed) {
    case 0:
        x /= 6; y /= -4;
        break;
    case 1:
        x /= 4; y /= -3;
        break;
    default:
        x /= 2; y /= -2;
      break;        
    }
    if (w == -128) w = 0;

    // Register inputs
    joyState[0] |= 192; joyState[1] |= 192; joyState[2] |= 192;
    if (s&1) { joyState[0] &= ~64;  }  // L Button
    if (s&2) { joyState[0] &= ~128; }  // R Button
    if (s&4) { joyState[1] &= ~64;  }  // M Button
    if (w<0) { joyState[2] &= ~64;  }  // W Up
    if (w>0) { joyState[2] &= ~128; }  // W Down
    if ((x!=0 || y!=0) && (mouseState[0]==255 || mouseState[1] ==255)) {
      mouseState[0] = 80; mouseState[1] = 100;
    }
    if (x<0) { mouseState[0] -= MIN(mouseState[0], -x);    }
    if (x>0) { mouseState[0] += MIN(159-mouseState[0], x); }
    if (y<0) { mouseState[1] -= MIN(mouseState[1], -y);    }
    if (y>0) { mouseState[1] += MIN(199-mouseState[1], y); }
#ifdef __DEBUG_MOUSE__   
    Serial.print("M:"); Serial.print((s&1)>0); // L but
    Serial.print(",");  Serial.print((s&4)>2); // M but
    Serial.print(",");  Serial.print((s&2)>1); // R but
    Serial.print(",");  Serial.print(mouseState[0]);
    Serial.print(",");  Serial.print(mouseState[1]);
    Serial.print(",");  Serial.println(w);
#endif    
}

////////////////////////////////
//        SD functions        //
////////////////////////////////

unsigned char sdInserted = 0;

void printDir(File dir, int numTabs) {
    while (true) {
        File entry =  dir.openNextFile();
        if (!entry) {
          // no more files
          break;
        }
        for (uint8_t i=0; i<numTabs; i++) {
          Serial.print('\t');
        }
        Serial.print(entry.name());
        if (entry.isDirectory()) {
          Serial.println("/");
          printDir(entry, numTabs+1);
        } else {
          // files have sizes, directories do not
          Serial.print("\t\t");
          Serial.println(entry.size(), DEC);
        }
        entry.close();
    }
}

void setupSD() {  
    // Try to init SD card
    Serial.println("Initializing SD card...");
    if (!SD.begin(53)) {
        Serial.println("Notif: No Micro-SD card inserted");
        sdInserted = 0;
        return;
    }
    
    // Open root folder
    File root = SD.open("/");
    sdInserted = 1;

    // Debugging
    //decodeJPG("banner.jpg");    
    //printDir(root, 1); 
}

////////////////////////////////
//        LCD functions       //
////////////////////////////////

#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27,2,1,0,4,5,6,7);

void setupLCD() {
    // Setup LCD light
    lcd.begin(20,4);
    lcd.setBacklightPin(3,POSITIVE);
    lcd.setBacklight(HIGH);
    displayHeader();
}

void displayHeader() {
    // Show header
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("    < 8bit-Hub >");
}

void displayMode() {
    lcd.setCursor(0,1);
    lcd.print("Mode:               ");
    lcd.setCursor(5,1);
    lcd.print(modeString[hubMode]);
    Serial.print("Hub Mode: ");
    Serial.println(modeString[hubMode]);    
}

void displaySD() {
    lcd.setCursor(0,2);
    lcd.print("SD:");
    if (sdInserted) {
        lcd.print("Inserted");
    } else {
        lcd.print("Not inserted");
    }
}

void displayIP() {
    lcd.setCursor(0,3);
    lcd.print("IP:                 ");
    lcd.setCursor(3,3);
    lcd.print(IP);
}

//////////////////////
//    COM Packets   //
//////////////////////

#define COM_ERR_OK      0
#define COM_ERR_NODATA  1
#define COM_ERR_HEADER  2 
#define COM_ERR_TRUNCAT 3
#define COM_ERR_CORRUPT 4
#define COM_ERR_RESENT  5

// Packet information
volatile unsigned char checksum;
volatile unsigned char comOutID = 0, comOutLen, comOutBuffer[PACKET], *comOutData, comOutInd = 0;
volatile unsigned char comInHeader, comInCMD, comInID, comInBuffer[PACKET], comInLen = 0;
volatile byte comCode = COM_ERR_NODATA;

// Variables used by Atari/C64/NES/Oric interrupts
volatile unsigned char hasHeader, hasCMD, hasLen, rcvLen; 
volatile unsigned char inByte = 0, outByte = 0;
volatile unsigned char interruptMode = 1;
volatile unsigned char interruptOffset = 0;
volatile unsigned long interruptTimer = 0;

// Variables used for Debugging I/O
#ifdef __DEBUG_IO__
  volatile unsigned int inRec = 0, outRec = 0, inErr = 0, inSta = 0, outSta = 0;
  unsigned long timerIO = millis();
#endif
    
void setupCOM() {
    // Setup COM Connection
    switch (hubMode) {
    case MODE_ATARI:
        pinMode(3, INPUT_PULLUP); // STROBE (PC)
        pinMode(4, INPUT_PULLUP); // R/W DATA BIT
        pinMode(5, INPUT_PULLUP); // R/W STATE
        pinMode(6, OUTPUT);       // READY (HUB)
        PORTH |= _BV(PH3);        // Pin 6 HIGH == READY
        attachInterrupt(digitalPinToInterrupt(3), atariInterrupt, FALLING);   
        break;
    case MODE_C64:
        pinMode(18, INPUT_PULLUP); // STROBE (PC)
        pinMode(17, INPUT_PULLUP); // R/W STATE
        pinMode(16, OUTPUT);       // READY (HUB)
        pinMode(6,  INPUT_PULLUP); // R/W DATA BIT 1
        pinMode(5,  INPUT_PULLUP); // R/W DATA BIT 2
        pinMode(4,  INPUT_PULLUP); // R/W DATA BIT 3
        pinMode(3,  INPUT_PULLUP); // R/W DATA BIT 4
        PORTH &= ~_BV(PH1);        // Pin 16 LOW == READY   
        attachInterrupt(digitalPinToInterrupt(18), c64Interrupt, FALLING);    
        break;   
    case MODE_LYNX:
        // Setup Serial 2 on pins 16/17
        Serial2.begin(62500, SERIAL_8N2);   // Lynx comm (Bauds 62500, 41666, 9600)
        while (!Serial2) { }
        Serial2.setTimeout(30);
        Serial2.flush();
        Serial2.readString();
        break;
    case MODE_NES:
        pinMode(18, INPUT_PULLUP); // STROBE (PC)
        pinMode(16, INPUT_PULLUP); // R/W STATE
        pinMode(17, INPUT_PULLUP); // READY (PC)
        pinMode(6,  OUTPUT);       // READY (HUB)
        pinMode(7,  INPUT_PULLUP); // DATA IN 1
        pinMode(4,  OUTPUT);       // DATA OUT 1
        pinMode(5,  OUTPUT);       // DATA OUT 2
        PORTH &= ~_BV(PH3);        // Pin 6 HIGH (inverted) == READY
        attachInterrupt(digitalPinToInterrupt(18), nesInterrupt, RISING);           
        break;   
    case MODE_ORIC:
        pinMode(3,  INPUT_PULLUP); // STROBE (PC)
        pinMode(17, INPUT_PULLUP); // R/W STATE
        pinMode(16, INPUT_PULLUP); // READY (PC)
        pinMode(2,  OUTPUT);       // READY (HUB)
        pinMode(4,  INPUT_PULLUP); // R/W DATA BIT 1
        pinMode(5,  INPUT_PULLUP); // R/W DATA BIT 2
        pinMode(6,  INPUT_PULLUP); // R/W DATA BIT 3
        pinMode(7,  INPUT_PULLUP); // R/W DATA BIT 4
        PORTE |=  _BV(PE4);        // Pin 2 HIGH           
        attachInterrupt(digitalPinToInterrupt(3), oricInterrupt, FALLING);
        break;  
    }
}

void preparePacket() {
    // Prepare data
    comOutInd = comOutLen = 0; 
    comOutBuffer[comOutLen++] = outByte = 170;
    if (comInCMD == HUB_SYS_STATE) {
        // Package hub state (joy/mouse)
        comOutBuffer[comOutLen++] = checksum = comOutID;
        comOutBuffer[comOutLen++] = 6;
        for (unsigned char i=0; i<4; i++) { comOutBuffer[comOutLen++] = joyState[i]; checksum += joyState[i]; }
        for (unsigned char i=0; i<2; i++) { comOutBuffer[comOutLen++] = mouseState[i]; checksum += mouseState[i]; }
    } else {
        // Do we have packet ready to send?
        signed char slot = -1;
        if (comInCMD == HUB_UDP_RECV) slot = 0;
        if (comInCMD == HUB_TCP_RECV) slot = 0;
        packet_t *packet = getPacket(comInCMD, slot);            

        // Retrieve data (if any)
        if (packet) {
            comOutBuffer[comOutLen++] = checksum = comOutID = packet->ID;
            comOutBuffer[comOutLen++] = packet->len;
            for (unsigned char i=0; i<packet->len; i++) { comOutBuffer[comOutLen++] = packet->data[i]; checksum += packet->data[i]; }
        } else {
            comOutBuffer[comOutLen++] = checksum = comOutID;
            comOutBuffer[comOutLen++] = 0;
        }
    }
    comOutBuffer[comOutLen++] = checksum;
}

boolean checkPacket() {
    // Compute checksum
    checksum = comInCMD;
    for (byte i=0; i<comInLen; i++)
        checksum += comInBuffer[i]; 

    // Verify checksum
    if (comInBuffer[comInLen] != checksum) { 
        #ifdef __DEBUG_IO__
            inErr++;
        #endif                
        comCode = COM_ERR_CORRUPT;
        return false;
    } else {
        comCode = COM_ERR_OK;
        return true;
    }
}

// Function used by Atari/C64/NES/Oric interrupts
void processByte() {
    // Check header
    if (!hasHeader) {
        if (inByte == 85 || inByte == 170) {
            comInHeader = inByte;
            hasHeader = 1; 
        } else {
        #ifdef __DEBUG_IO__
            inErr++;
        #endif          
        }       
        return;
    }
    
    // Check CMD
    if (!hasCMD) {
        comInCMD = inByte;
        hasCMD = 1;
        return;
    }

    // If this a recv request?
    if (comInHeader == 85) {    
        // Process byte
        comInID = inByte;
      
        // Process packets
        popPacket(comInID);
        preparePacket(); 
    } else {
        // Check for length
        if (!hasLen) {
            comInLen = inByte;
            hasLen = 1;
            rcvLen = 0;
            return;
        }
      
        // Add data to buffer
        comInBuffer[rcvLen++] = inByte;
      
        // Check if packet was fully received (including extra byte for checksum)
        if (rcvLen < comInLen+1) return;

        // Check packet and reset state
        comOutInd = 0; comOutLen = 1;
        if (checkPacket()) {
            if (comInLen)
                comProcessCMD();
            outByte = 85;
        } else {
            outByte = 0;            
        }       
    }
    
    // Reset incoming state
    hasHeader = 0;
    hasCMD = 0;
    hasLen = 0;
}

/////////////////////////////////
//     ATARI Communication     //
/////////////////////////////////

void atariRead() {
    // Setup pin for input
    DDRG &= ~_BV(PG5); PORTG |= _BV(PG5);  
    delayMicroseconds(8);
      
    // Read 1 bit from computer
    inByte |= ((PING & _BV(PG5))>0) << (7-interruptOffset++); 
}

void atariWrite() {
    // Setup pin for output
    DDRG |= _BV(PG5); 
    
    // Write 1 bit to computer      
    if (outByte & (0b00000001 << interruptOffset++)) { PORTG |= _BV(PG5); } else { PORTG &= ~_BV(PG5); }
}

void atariInterrupt() {
    // Lock ready pin
   PORTH &= ~_BV(PH3);  // Pin 6 LOW

    // Check R/W pin
    if (!(PINE & _BV(PE3))) {
        atariRead();
        interruptMode = 1;
    } else {
        atariWrite();
        interruptMode = 0;
    }

    // If byte complete?
    if (interruptOffset < 8) {
        // Update timer
        interruptTimer = millis();
    } else {
        // Reset offset 
        interruptOffset = 0;
      
        // Process data
        if (interruptMode) {
            processByte();
            inByte = 0;
        #ifdef __DEBUG_IO__  
            inRec++;         
        #endif          
        } else {
            outByte = comOutBuffer[++comOutInd];
        #ifdef __DEBUG_IO__  
            outRec++;         
        #endif          
        }
 
        // Unlock ready pin
        PORTH |= _BV(PH3);  // Pin 4 HIGH == READY
    }
}

////////////////////////////////
//      C64 Communication     //
////////////////////////////////

void c64Read() {
    // Setup pins for input
    DDRH &= ~_BV(PH3); PORTH |= _BV(PH3);  // pin 6
    DDRE &= ~_BV(PE3); PORTE |= _BV(PE3);  // pin 5
    DDRG &= ~_BV(PG5); PORTG |= _BV(PG5);  // pin 4
    DDRE &= ~_BV(PE5); PORTE |= _BV(PE5);  // pin 3
    
    // Read 4 bits from computer      
    inByte |= ((PINH & _BV(PH3))>0) << (interruptOffset++); 
    inByte |= ((PINE & _BV(PE3))>0) << (interruptOffset++); 
    inByte |= ((PING & _BV(PG5))>0) << (interruptOffset++); 
    inByte |= ((PINE & _BV(PE5))>0) << (interruptOffset++);
}

void c64Write() {
    // Setup pins for output
    DDRH |= _BV(PH3);   // pin 6
    DDRE |= _BV(PE3);   // pin 5
    DDRG |= _BV(PG5);   // pin 4
    DDRE |= _BV(PE5);   // pin 3
    
    // Write 4 bits to computer      
    if (outByte & (1 << interruptOffset++)) { PORTH |= _BV(PH3); } else { PORTH &= ~_BV(PH3); }
    if (outByte & (1 << interruptOffset++)) { PORTE |= _BV(PE3); } else { PORTE &= ~_BV(PE3); }
    if (outByte & (1 << interruptOffset++)) { PORTG |= _BV(PG5); } else { PORTG &= ~_BV(PG5); }
    if (outByte & (1 << interruptOffset++)) { PORTE |= _BV(PE5); } else { PORTE &= ~_BV(PE5); }
}

void c64Interrupt() {
    // Lock ready pin
    PORTH |= _BV(PH1);  // HIGH   

    // Check R/W pin
    if (!(PINH & _BV(PH0))) {
        c64Read();
        interruptMode = 1;
    } else {
        c64Write();
        interruptMode = 0;
    }

    // If byte complete?
    if (interruptOffset < 8) {
      // Reset time
      interruptTimer = millis();
    } else {      
      // Reset offset 
      interruptOffset = 0;
      
      // Process data
      if (interruptMode) {
          processByte();
          inByte = 0;
      #ifdef __DEBUG_IO__  
          inRec++;
      #endif          
      } else {
          outByte = comOutBuffer[++comOutInd];
      #ifdef __DEBUG_IO__  
          outRec++;
      #endif          
      } 

      // Unlock ready pin
      PORTH &= ~_BV(PH1);  // LOW   
    }
}

////////////////////////////////
//     LYNX Communication     //
////////////////////////////////

unsigned char lynxBitPeriod = 16;   // Bauds: 62500=16 / 41666=24 / 9600=104
unsigned long lynxTimer;

void lynxOutputMode(void) {
    // Switch pin to output
    PORTD |= B00000100;
    DDRD |= B00000100;
    lynxTimer = micros();  
    while (micros()-lynxTimer < lynxBitPeriod);
}

void lynxInputMode(void) {
    // Switch pin to input
    DDRD &= B11111011;  
}

void lynxWrite(char value) {
    unsigned char i, parity = 0, mask = 1;

    // Start Bit
    PORTD &= B11111011;
    lynxTimer += lynxBitPeriod; while (micros()-lynxTimer < lynxBitPeriod) ;

    // Value Bits
    for (i=0; i<8; i++) {
        if (value & mask) { 
            PORTD |= B00000100;
            parity++;
        } else { 
            PORTD &= B11111011;
        }
          mask = mask << 1;
          lynxTimer += lynxBitPeriod; while (micros()-lynxTimer < lynxBitPeriod) ;
    }
      
    // Parity Bit
    if (parity % 2) {
        PORTD &= B11111011;
    } else {
        PORTD |= B00000100;
    }
    lynxTimer += lynxBitPeriod; while (micros()-lynxTimer < lynxBitPeriod) ; 

    // Stop Bit
    PORTD |= B00000100;
    for (i=0; i<6; i++) {   // Bauds: 41666,62500 = i<6 / 9600 = i<1
        lynxTimer += lynxBitPeriod; while (micros()-lynxTimer < lynxBitPeriod) ;
    }
}

void lynxProcessCOM() {  
    // Have we got data?
    if (!Serial2.available()) { comCode = COM_ERR_NODATA; return; }

    // Get Header
    if (!Serial2.readBytes((unsigned char*)&comInHeader, 1)) { comCode = COM_ERR_HEADER; return; }
  #ifdef __DEBUG_IO__  
    inRec++;
  #endif

    // Check Header
    if (comInHeader != 170 && comInHeader != 85) { comCode = COM_ERR_HEADER; return; }
    
    // Get Command
    if (!Serial2.readBytes((unsigned char*)&comInCMD, 1)) { comCode = COM_ERR_TRUNCAT; return; }
  #ifdef __DEBUG_IO__  
    inRec++;
  #endif

    // Send or Receive?
    if (comInHeader == 85) {
        // Get RecvID
        if (!Serial2.readBytes((unsigned char*)&comInID, 1)) { comCode = COM_ERR_TRUNCAT; return; }
      #ifdef __DEBUG_IO__  
        inRec++;
      #endif

        // Process packets
        popPacket(comInID);
        preparePacket();
                
        // Send DATA
        //delayMicroseconds(6*lynxBitPeriod);   // Bauds: 62500=6* / 41666=8* / 9600=2*
        lynxOutputMode();
        for (unsigned char i=0; i<comOutLen; i++) {
            lynxWrite(comOutBuffer[i]);
          #ifdef __DEBUG_IO__  
            outRec++;
          #endif            
        }          

    } else {
        // Get Length
        if (!Serial2.readBytes((unsigned char*)&comInLen, 1)) { comCode = COM_ERR_TRUNCAT; return; }
      #ifdef __DEBUG_IO__  
        inRec++;
      #endif
    
        // Get Buffer+Checksum
        if (!Serial2.readBytes((unsigned char*)comInBuffer, comInLen+1)) { comCode = COM_ERR_TRUNCAT; return; }
      #ifdef __DEBUG_IO__  
        inRec += comInLen+1;
      #endif

        // Check Data Integrity
        if (checkPacket()) {        
            // Send ACKNOW and process CMD
            lynxOutputMode();
            lynxWrite(85);
            comProcessCMD();
        } else {
            // Send NG
            lynxOutputMode();
            lynxWrite(0);  
        }
      #ifdef __DEBUG_IO__  
        outRec++;
      #endif            
    }

    // Return pin to input and clear data sent to self
    lynxInputMode();
    while (Serial2.available())
        Serial2.read(); 
}

/////////////////////////////////
//      NES Communication      //
/////////////////////////////////

void nesRead() {
    // Read 1 bit from computer      
    inByte |= ((PINH & _BV(PH4))>0) << (interruptOffset++);  // Pin 7 
}

void nesWrite() {
    // Write 2 bits to computer      
    if (outByte & (1 << interruptOffset++)) { PORTG &= ~_BV(PG5); } else { PORTG |= _BV(PG5); }
    if (outByte & (1 << interruptOffset++)) { PORTE &= ~_BV(PE3); } else { PORTE |= _BV(PE3); } 
}

void nesInterrupt() { 
    // Check R/W pin
    if (!(PINH & _BV(PH0))) return;
    
    // Lock ready pin
    PORTH |= _BV(PH3);  // Pin 6 LOW (inverted)       
    
    // Check R/W pin
    if (!(PINH & _BV(PH1))) {
        nesRead();
        interruptMode = 1;
    } else {
        nesWrite();
        interruptMode = 0;
    }
    
    // If byte complete?
    if (interruptOffset < 8) {
        // Update timer
        interruptTimer = millis();
    } else {
        // Reset offset 
        interruptOffset = 0;
      
        // Process data
        if (interruptMode) {
            processByte();
            inByte = 0;
        #ifdef __DEBUG_IO__  
            inRec++;         
        #endif          
        } else {
            outByte = comOutBuffer[++comOutInd];
        #ifdef __DEBUG_IO__  
            outRec++;         
        #endif          
        }
                
        // Unlock ready pin
        delayMicroseconds(10);
        PORTH &= ~_BV(PH3); // Pin 6 HIGH (inverted)          
    }
}

////////////////////////////////
//     ORIC Communication     //
////////////////////////////////

void oricRead() {
    // Setup pins for input
    DDRG &= ~_BV(PG5);   // Pin 4
    DDRE &= ~_BV(PE3);   // Pin 5
    DDRH &= ~_BV(PH3);   // Pin 6
    DDRH &= ~_BV(PH4);   // Pin 7

    // Read 4 bits from computer      
    inByte |= ((PING & _BV(PG5))>0) << (interruptOffset++); 
    inByte |= ((PINE & _BV(PE3))>0) << (interruptOffset++); 
    inByte |= ((PINH & _BV(PH3))>0) << (interruptOffset++); 
    inByte |= ((PINH & _BV(PH4))>0) << (interruptOffset++);    
}

void oricWrite() {
    // Setup pins for output
    DDRG |= _BV(PG5);   // Pin 4
    DDRE |= _BV(PE3);   // Pin 5
    DDRH |= _BV(PH3);   // Pin 6
    DDRH |= _BV(PH4);   // Pin 7

    // Write 4 bits to computer      
    if (outByte & (1 << interruptOffset++)) { PORTG |= _BV(PG5); } else { PORTG &= ~_BV(PG5); }
    if (outByte & (1 << interruptOffset++)) { PORTE |= _BV(PE3); } else { PORTE &= ~_BV(PE3); }
    if (outByte & (1 << interruptOffset++)) { PORTH |= _BV(PH3); } else { PORTH &= ~_BV(PH3); }
    if (outByte & (1 << interruptOffset++)) { PORTH |= _BV(PH4); } else { PORTH &= ~_BV(PH4); }
}

void oricInterrupt() {
    // Check ready pin (PC)
    if (!(PINH & _BV(PH1))) {
        // Setup pins for input
        DDRG &= ~_BV(PG5);   // Pin 4
        DDRE &= ~_BV(PE3);   // Pin 5
        DDRH &= ~_BV(PH3);   // Pin 6
        DDRH &= ~_BV(PH4);   // Pin 7
        return;   
    }

    // Check R/W pin
    if (!(PINH & _BV(PH0))) {
        oricRead();
        interruptMode = 1;
    } else {
        oricWrite();
        interruptMode = 0;
    }

    // Cycle ACKNOW pin (HUB)
    PORTE &= ~_BV(PE4);      // Pin 2 LOW
    delayMicroseconds(10);   
    PORTE |=  _BV(PE4);      // Pin 2 HIGH     

    // If byte complete?
    if (interruptOffset < 8) {
        // Update timer
        interruptTimer = millis();  
    } else {              
        // Reset offset/timer 
        interruptOffset = 0;
      
        // Process data
        if (interruptMode) {
            processByte();
            inByte = 0;
        #ifdef __DEBUG_IO__  
            inRec++;         
        #endif          
        } else {
            outByte = comOutBuffer[++comOutInd];
        #ifdef __DEBUG_IO__  
            outRec++;         
        #endif          
        }
    }      
}

/////////////////////////////
//    SERIAL Connection    //
/////////////////////////////

void setupSERIAL() {
    // Setup PC/Wifi Connection
    Serial.begin(115200);               // PC conn.
    Serial3.begin(115200);              // ESP8266 conn.
    while (!Serial) { }
    while (!Serial3) { }
    Serial.setTimeout(10);
    Serial3.setTimeout(10);
    Serial.flush();
    Serial3.flush();
    Serial.readString();
    Serial3.readString();

    // Display Reboot Message
    Serial.println("-System Reboot-");    
}

void writeCMD(unsigned char cmd) {
    Serial3.print("CMD");
    Serial3.write(cmd);
}

void writeChar(unsigned char var) {
    Serial3.write(var);
}

void writeInt(unsigned int var) {
    Serial3.write((char*)&var, 2);
}

void writeBuffer(char* buffer, unsigned char len) {
    Serial3.write(len);
    Serial3.write(buffer, len);
}

unsigned char readChar() {
    // Get char from serial  
    uint32_t timeout = millis()+10;    
    while (!Serial3.available()) {
        if (millis() > timeout)
          return 0;
    }    
    return Serial3.read();
}

void readInt(unsigned int *var) {
    // Get one int from serial
    Serial3.readBytes((char*)var, 2);  
}

unsigned char readBuffer() {
    // Read buffer of known length
    uint32_t timeout;
    unsigned char i = 0;
    unsigned char len = readChar();
    while (i<len) {
        timeout = millis()+10;    
        while (!Serial3.available()) {
            if (millis() > timeout) {
            #ifdef __DEBUG_CMD__
                Serial.print("ESP: Timeout (");
                Serial.print(i);
                Serial.print("/");
                Serial.print(len);
                Serial.println(")");
            #endif             
                writeCMD(HUB_SYS_RESEND);
                serLen = 0;
                return 0;
            }
        }    
        serBuffer[i++] = Serial3.read();
    }
    serBuffer[len] = 0;
    serLen = len;
    return len;
}

void readIP() {
    if (readBuffer()) {
        strcpy(IP, serBuffer);
        Serial.print("IP: ");
        Serial.println(serBuffer);
    }  
}

void readNotif() {
    if (readBuffer()) {
        Serial.print("Notif: ");
        Serial.println(serBuffer);
    }
}

void readError() {
    if (readBuffer()) {
        Serial.print("Error: ");
        Serial.println(serBuffer);
    }  
}

////////////////////////////
//  UDP/TCP/WEB functions //
////////////////////////////

void readUdp() {
    // Store data into packet
    char slot = readChar(); // slot
    if (readBuffer()) {
        pushPacket(HUB_UDP_RECV, slot);  
    #ifdef __DEBUG_UDP__
        Serial.print("UDP RECV: ");
        Serial.write(serBuffer, serLen); 
        Serial.print("\n");
    #endif     
    }
}

void readTcp() {
    // Store data into packet
    char slot = readChar(); // slot
    if (readBuffer()) {
        pushPacket(HUB_TCP_RECV, slot);  
    #ifdef __DEBUG_TCP__
        Serial.print("TCP RECV: ");
        Serial.write(serBuffer, serLen); 
        Serial.print("\n");
    #endif     
    } 
}

void readWeb() {
    // Store data into packet
    if (readBuffer()) {
        pushPacket(HUB_WEB_RECV, -1);
    #ifdef __DEBUG_WEB__
        Serial.print("WEB RECV: ");
        Serial.write(serBuffer, serLen); 
        Serial.print("\n");
    #endif     
    }   
}

///////////////////////////
//     URL functions     //
///////////////////////////

#define URL_NULL   0
#define URL_PACKET 1
#define URL_UPDATE 2

unsigned long urlSize;
unsigned char urlMode = URL_PACKET;

void getHttp() {
    // Store data into packet
    if (readBuffer()) {
        unsigned char len[4];
        memcpy(len, serBuffer, 4);
        urlSize = (unsigned long)len[0]+256L*(unsigned long)len[1]+65536L*(unsigned long)len[2]+16777216L*(unsigned long)len[3];
        if (urlMode == URL_PACKET) {
            // Send back file size
            memcpy(serBuffer, (char*)&urlSize, 4); serLen = 4;
            pushPacket(HUB_URL_GET, -1);
        }
    #ifdef __DEBUG_URL__
        Serial.print("URL GET: ");
        Serial.print(urlSize); Serial.println(" bytes");
    #endif     
    }
}

void readHttp() {
    // Store data to Packet/OTA update
    if (readBuffer()) {
        switch (urlMode) {
        case URL_PACKET:
            pushPacket(HUB_URL_READ, -1);  
            break;
        case URL_UPDATE:
            for (unsigned char i=0; i<serLen; i++)
                InternalStorage.write(serBuffer[i]);
            break;
        }
    #ifdef __DEBUG_URL__
        Serial.print("URL READ: ");
        Serial.print(serLen); Serial.println(" bytes");         
    #endif     
    }
}

////////////////////////////////
//     Command Processing     //
////////////////////////////////

char lastEspCMD;
char lastComCMD;

void espProcessCMD() {
    lastEspCMD = readChar();
    switch (lastEspCMD) {
    case HUB_SYS_IP:
        readIP();
        displayIP();
        break;

    case HUB_SYS_SCAN:
        readBuffer();
        break;
        
    case HUB_SYS_NOTIF:
        readNotif();
        break;
        
    case HUB_SYS_ERROR:
        readError();
        break;
        
    case HUB_SYS_MOUSE:
        readMouse();
        break;

    case HUB_UDP_RECV:
        readUdp();
        break;

    case HUB_TCP_RECV:
        readTcp();
        break;            

    case HUB_WEB_RECV:
        readWeb();
        break;  

    case HUB_URL_GET:
        getHttp();
        break;                                    

    case HUB_URL_READ:
        readHttp();
        break;                                    
    }  
#ifdef __DEBUG_CMD__
    if (lastEspCMD != HUB_SYS_MOUSE) {
        Serial.print("ESP: "); 
        Serial.print(cmdString[lastEspCMD]); Serial.print(" (");
        Serial.print(serLen, DEC); Serial.println(")");
    }
#endif
}

void comProcessCMD() {
    unsigned char tmp;
    unsigned int offset;
    unsigned long length;   
    switch (comInCMD) {  
    case HUB_SYS_RESET:
        // Reset packets and files                
        packetID = 0;
        while (packetHead) popPacket(packetHead->ID);
        for (byte i=0; i<FILES; i++) {
            if (hubFile[i]) hubFile[i].close();
        }
        Serial.println("-System Reset-");
        break;

    case HUB_SYS_IP:
        // Send back IP address
        strcpy(serBuffer, IP);
        serLen = strlen(IP);
        pushPacket(HUB_SYS_IP, -1);
        break;

    case HUB_DIR_LS: // Todo: Implement for root directory /microSD
        {
            unsigned char skip = comInBuffer[0];
            unsigned char count = 0;
            length = 1;

            // TODO: we may want to start a different dir if CD is implemented
            File dir = SD.open("/");
            while (true) {
                File entry =  dir.openNextFile();
                if (!entry) {
                    // no more files
                    break;
                }
                if (!entry.isDirectory()){
                    // skip any dir; TODO: change when CD is implemented
                    if( skip!=0){
                        // skip entries as requested
                        skip--;
                    }else{
                        unsigned int slen=strlen(entry.name());
                        if( length+slen+3>255){
                            // printf("Buffer length exceeded!");
                            break;
                        }
                        memcpy(&serBuffer[length], entry.name(), slen);
                        length += slen;
                        serBuffer[length++] = 0;
                        serBuffer[length++] = (unsigned char)(entry.size() & 0xff);
                        serBuffer[length++] = (unsigned char)((entry.size() >> 8)&0xff);
                        count++;
                    }
                }

                entry.close();
            }
            dir.close();
            serBuffer[0] = count;
            serLen = length;
        }

        pushPacket(HUB_DIR_LS, -1);
        break;

    case HUB_FILE_OPEN:
        // Check if file was previously opened
        if (hubFile[comInBuffer[0]])
            hubFile[comInBuffer[0]].close();
        
        // Open file (modes are 0:read, 1:write, 2:append)
        switch (comInBuffer[1]) {
        case 0:                
            hubFile[comInBuffer[0]] = SD.open(&comInBuffer[2], FILE_READ); 
            break;
        case 1:
            if (SD.exists(&comInBuffer[2])) { SD.remove(&comInBuffer[2]); }
            hubFile[comInBuffer[0]] = SD.open(&comInBuffer[2], FILE_WRITE); 
            break;
        case 2:                
            hubFile[comInBuffer[0]] = SD.open(&comInBuffer[2], FILE_WRITE);
            break;
        }

        // Send back file size
        length = hubFile[comInBuffer[0]].size();
        memcpy(serBuffer, (char*)&length, 4); serLen = 4;
        pushPacket(HUB_FILE_OPEN, -1);
        break;

    case HUB_FILE_SEEK:
        // Seek file position (offset from beginning)
        offset = (comInBuffer[2] * 256) + comInBuffer[1];
        if (hubFile[comInBuffer[0]])
            hubFile[comInBuffer[0]].seek(offset);
        break;

    case HUB_FILE_READ:
        if (hubFile[comInBuffer[0]]) {
            // Read chunk from file
            serLen = 0;
            while (hubFile[comInBuffer[0]].available() && tmp<comInBuffer[1])
                serBuffer[serLen++] = hubFile[comInBuffer[0]].read();
            pushPacket(HUB_FILE_READ, -1);
        }
        break;

    case HUB_FILE_WRITE:
        if (hubFile[comInBuffer[0]]) 
            hubFile[comInBuffer[0]].write((unsigned char*)&comInBuffer[1], comInLen-3);
        break;

    case HUB_FILE_CLOSE:
        if (hubFile[comInBuffer[0]]) 
            hubFile[comInBuffer[0]].close();
        break;                          
      
    case HUB_UDP_OPEN:
        // Forward CMD to ESP
        writeCMD(HUB_UDP_OPEN);
        writeChar(comInBuffer[0]); writeChar(comInBuffer[1]);
        writeChar(comInBuffer[2]); writeChar(comInBuffer[3]);
        writeInt(comInBuffer[4]+256*comInBuffer[5]); 
        writeInt(comInBuffer[6]+256*comInBuffer[7]);
        break;

    case HUB_UDP_SEND:
        // Forward CMD to ESP
        writeCMD(HUB_UDP_SEND);
        writeBuffer(comInBuffer, comInLen);
        break;

    case HUB_UDP_CLOSE:
        // Forward CMD to ESP
        writeCMD(HUB_UDP_CLOSE);
        break;
        
    case HUB_TCP_OPEN:
        // Forward CMD to ESP
        writeCMD(HUB_TCP_OPEN);
        writeChar(comInBuffer[0]); writeChar(comInBuffer[1]);
        writeChar(comInBuffer[2]); writeChar(comInBuffer[3]);
        writeInt(comInBuffer[4]+256*comInBuffer[5]); 
        break;

    case HUB_TCP_SEND:
        // Forward CMD to ESP
        writeCMD(HUB_TCP_SEND);
        writeBuffer(comInBuffer, comInLen);
        break;

    case HUB_TCP_CLOSE:
        // Forward CMD to ESP
        writeCMD(HUB_TCP_CLOSE);
        break;

    case HUB_WEB_OPEN:
        // Forward CMD to ESP
        writeCMD(HUB_WEB_OPEN);
        writeInt(comInBuffer[0]+256*comInBuffer[1]); // Port
        writeInt(comInBuffer[2]+256*comInBuffer[3]); // TimeOut
        break; 

    case HUB_WEB_HEADER:
        // Forward CMD to ESP
        writeCMD(HUB_WEB_HEADER);
        writeBuffer(comInBuffer, comInLen);                 
        break;

    case HUB_WEB_BODY:
        // Forward CMD to ESP
        writeCMD(HUB_WEB_BODY);
        writeBuffer(comInBuffer, comInLen);        
        break;

    case HUB_WEB_SEND:
        // Forward CMD to ESP
        writeCMD(HUB_WEB_SEND);
        break;
        
    case HUB_WEB_CLOSE:
        // Forward CMD to ESP
        writeCMD(HUB_WEB_CLOSE);
        break;  

    case HUB_URL_GET:
        // Forward CMD to ESP
        writeCMD(HUB_URL_GET);
        writeBuffer(comInBuffer, comInLen);
        break;                                    

    case HUB_URL_READ:
        // Forward CMD to ESP
        writeCMD(HUB_URL_READ);
        writeChar(comInBuffer[0]);
        break;
    }
#ifdef __DEBUG_CMD__
    Serial.print("COM: "); 
    Serial.print(cmdString[comInCMD]); Serial.print(" (");
    Serial.print(comInLen, DEC); Serial.println(")");
#endif
}

////////////////////////////////
//       WIFI functions       //
////////////////////////////////

char ssid[32];
char pswd[64];

void wifiVersion() {
    // Get ESP version
    writeCMD(HUB_SYS_VERSION);    
    lastEspCMD = 0;     
    uint32_t timeout = millis()+3000;
    while (lastEspCMD != HUB_SYS_VERSION) {
        if (Serial3.find("CMD")) 
            espProcessCMD();
        if (millis() > timeout) {
            Serial.println("Error: ESP not responding");      
            return;
        }
    }

    // Check version was received correctly
    readBuffer();
    memcpy(espVersion, serBuffer, 4);
    if (strncmp(espVersion, "v", 1)) {
        Serial.println("Error: cannot determine current ESP version."); 
        memcpy(espVersion, "?", 1);
    }
}

unsigned char wifiScan() {
    // Scan and wait for Answer
    writeCMD(HUB_SYS_SCAN);    
    uint32_t timeout = millis()+9000;
    lastEspCMD = 0;
    while (lastEspCMD != HUB_SYS_SCAN) {
        if (Serial3.find("CMD"))
            espProcessCMD();
        if (millis() > timeout) {
            Serial.println("Error: ESP not responding");      
            return 0;
        }
    }
    Serial.println(serBuffer);
    return 1;       
}

unsigned char askUpdate(char *core, char *version, char *update) {
    // Ask user if they want to update
    lcd.setCursor(0,1);
    lcd.print("Update available!");               
    lcd.setCursor(0,2);
    lcd.print(core); lcd.print(": "); 
    lcd.print(version); lcd.print(" -> "); lcd.print(update);
    lcd.setCursor(0,3);
    lcd.print("Press FIRE to update");       

    // Give 5s to decide...
    unsigned char doUpdate = 0;
    uint32_t timer = millis(); 
    while (millis()-timer < 5000) {
        readJOY();
        if (!(joyState[0] & joyStnd[4]) || !(joyState[0] & joyStnd[5])) {
            doUpdate = 1;
            break;
        }
    }

    // Clear LCD and return result
    lcd.setCursor(0,1); lcd.print(blank);
    lcd.setCursor(0,2); lcd.print(blank);
    lcd.setCursor(0,3); lcd.print(blank);
    return doUpdate;
}

void checkUpdate() {
    // Check if we received IP...
    uint32_t timeout = millis()+9000;    
    while (lastEspCMD != HUB_SYS_IP) {
        if (Serial3.find("CMD"))
            lastEspCMD = readChar();
        if (millis() > timeout) {
            Serial.println("Error: ESP not responding");      
            return;
        }
    }
    readIP();
    if (!strcmp(serBuffer, "Not connected..."))
        return;
    
    // Check latest version number
    writeCMD(HUB_URL_GET);
    writeBuffer(urlVer, strlen(urlVer)); 
    writeCMD(HUB_URL_READ);
    writeChar(20);

    // Wait to receive data
    urlMode = URL_NULL;   
    timeout = millis()+9000;
    while (lastEspCMD != HUB_URL_READ) {
        if (Serial3.find("CMD")) 
            espProcessCMD();
        if (millis() > timeout) {
            Serial.println("Error: ESP not responding");      
            return;
        }            
    }
    memcpy(espUpdate, &serBuffer[4], 4); espUpdate[4] = 0;
    memcpy(megaUpdate, &serBuffer[16], 4); megaUpdate[4] = 0;
    urlMode = URL_PACKET;   

    // Check version data was received correctly
    if (strncmp(espVersion, "v", 1)) {
        Serial.println("Error: cannot determine current ESP version.");  
        return;
    }
    if (strncmp(espUpdate, "v", 1)) {
        Serial.println("Error: cannot determine latest ESP version.");
        return;
    }    
    if (strncmp(megaUpdate, "v", 1)) {
        Serial.println("Error: cannot determine latest MEGA version.");
        return;
    }
    
    // Display info
    Serial.print("ESP Firmware: "); Serial.print(espVersion); Serial.print(" -> "); Serial.println(espUpdate); 
    Serial.print("MEGA Firmware: "); Serial.print(megaVersion); Serial.print(" -> "); Serial.println(megaUpdate); 
    if (!strncmp(espVersion, espUpdate, 4) && !strncmp(megaVersion, megaUpdate, 4)) {
        lcd.setCursor(0,1); lcd.print("System up-to-date!");
        delay(1000); return;
    }
    
    // Apply ESP update?
    if (strncmp(espVersion, espUpdate, 4)) {
        // Ask user if they want to update
        if (askUpdate("Wifi", espVersion, espUpdate)) {
            // Process with update
            Serial.println("Updating ESP...");
            lcd.setCursor(0,1); lcd.print("Updating Wifi...    ");
            writeCMD(HUB_SYS_UPDATE);
            writeBuffer(urlEsp, strlen(urlEsp));
    
            // Wait for ESP to Reboot   
            timeout = millis() + 60000;     
            while (lastEspCMD != HUB_SYS_VERSION) {
                delay(1000);
                writeCMD(HUB_SYS_VERSION);
                if (Serial3.find("CMD")) 
                    espProcessCMD();
                if (millis() > timeout) {
                    lcd.setCursor(0,1); lcd.print(fail);
                    Serial.println("Error: ESP not responding");
                    delay(1000); return;
                }                
            }
            readBuffer();
    
            // Check new ESP version
            if (!strncmp(serBuffer, espUpdate, 4)) {
                memcpy(espVersion, serBuffer, 4);
                lcd.setCursor(0,1); lcd.print("Wifi updated!       ");
                Serial.println("ESP update complete");              
            } else {
                lcd.setCursor(0,1); lcd.print("Error: Ver. mismatch");
                Serial.println("ESP update failed");                      
            }
    
            // Reset Wifi, and clear screen
            setupESP();
            while (lastEspCMD != HUB_SYS_IP)
                if (Serial3.find("CMD")) espProcessCMD(); 
            lcd.setCursor(0,1); lcd.print(blank);               
            lcd.setCursor(0,2); lcd.print(blank);               
            lcd.setCursor(0,3); lcd.print(blank);               
        }
    }
    
    // Apply MEGA update?
    if (strncmp(megaVersion, megaUpdate, 4)) {
        // Ask user if they want to update
        if (askUpdate("Core", megaVersion, megaUpdate)) {        
            // Fetch update file on ESP
            lcd.setCursor(0,1); lcd.print("Updating Core...    ");
            Serial.println("Downloading update...");
            writeCMD(HUB_URL_GET);
            writeBuffer(urlMega, strlen(urlMega));
    
            // Wait to receive file size
            lastEspCMD = 0;            
            timeout = millis() + 6000;     
            while (lastEspCMD != HUB_URL_GET) {
                if (Serial3.find("CMD")) espProcessCMD();
                if (millis() > timeout) {
                    lcd.setCursor(0,1); lcd.print(fail);
                    Serial.println("Error: ESP not responding");      
                    delay(1000); return;
                }                
            }
    
            // Check file has size
            if (!urlSize) {
                lcd.setCursor(0,1); lcd.print(fail);
                Serial.println("Error: cannot download update.");
                delay(1000); return;
            }
    
            // Check there is enough storage for update
            if (!InternalStorage.open(urlSize)) {
                lcd.setCursor(0,1); lcd.print(fail);
                Serial.println("Error: not enough space to store the update.");
                delay(1000); return;
            }
    
            // Get update file from ESP
            urlMode = URL_UPDATE;
            unsigned long recvSize = 0;
            while (1) {
                // Request next url packet
                writeCMD(HUB_URL_READ);
                writeChar(64);
        
                // Wait to receive packet
                lastEspCMD = 0;
                timeout = millis() + 6000;     
                while (lastEspCMD != HUB_URL_READ) {
                    if (Serial3.find("CMD")) espProcessCMD();
                    if (millis() > timeout) {
                        lcd.setCursor(0,1); lcd.print(fail);
                        Serial.println("Error: ESP not responding");      
                        delay(1000); return;
                    }                       
                }

                // Check if packet was empty
                if (serLen)
                    recvSize += serLen;
                else
                    break;
            }
            urlMode = URL_PACKET;
            
            // Close the internal storage
            InternalStorage.close();
    
            // Check size of received data
            if (recvSize == urlSize) {
                Serial.print("Received: ");
                Serial.print(recvSize);
                Serial.print("/");
                Serial.print(urlSize);
                Serial.println(" bytes");
            } else {
                lcd.setCursor(0,1); lcd.print(fail);
                Serial.println("Error: could not download entire file.");
                delay(2000);
                return;
            }
    
            lcd.setCursor(0,1); lcd.print("Please reboot Hub!  ");
            Serial.println("Update done...");
            Serial.flush();
            InternalStorage.apply();
        }
    }
}

void setupESP() {  
    // Connect Wifi
    writeCMD(HUB_SYS_CONNECT);
    writeBuffer(ssid, strlen(ssid));
    writeBuffer(pswd, strlen(pswd));
    writeChar(10);   // Packet refresh period (ms)  
}

//////////////////////////
//     TEST FUNCTIONS   //
//////////////////////////

const char* joyCode = "UDLRAB";

void wifiTest() {
    // Run Wifi Test
    setupESP();
    lcd.setCursor(0,0);
    lcd.print("Wifi:");
    lastEspCMD = 0;     
    while (lastEspCMD != HUB_SYS_IP)
        if (Serial3.find("CMD"))
            lastEspCMD = readChar();
    readBuffer();
    lcd.print(serBuffer);
}

void urlTest() {
    unsigned char url[] = "http://8bit-unity.com/test.txt";
    lcd.setCursor(0,3); 
    lcd.print("URL:");    
    Serial.print("URL: ");

    // Setup request
    writeCMD(HUB_URL_GET);
    writeBuffer(url, strlen(url)); 
    writeCMD(HUB_URL_READ);
    writeChar(16);

    // Wait for answer
    uint32_t timeout = millis()+3000;
    while (millis() < timeout) {
        if (Serial3.find("CMD") && readChar() == HUB_URL_READ) {
            if (readBuffer()) {
                Serial.println(serBuffer);
                lcd.print(serBuffer);
            }
            break;
        }
    }
    if (millis() >= timeout) {
          Serial.println("Timeout");
          lcd.print("Timeout");
    }
}

void tcpTest() {
    lcd.setCursor(0,1); 
    lcd.print("TCP:");
    Serial.print("TCP: ");
    
    // Setup request    
    writeCMD(HUB_TCP_OPEN);
    writeChar(199); writeChar(47);
    writeChar(196); writeChar(106);
    writeInt(1234);
    writeCMD(HUB_TCP_SEND);
    writeBuffer("Packet received", 16);
    
    // Wait for answer
    uint32_t timeout = millis()+3000;
    while (millis() < timeout) {
        if (Serial3.find("CMD") && readChar() == HUB_TCP_RECV) {
            readChar();
            if (readBuffer()) {
                Serial.println(serBuffer);
                lcd.print(serBuffer);
            }
            break;
        }
    }
    if (millis() >= timeout) {
          Serial.println("Timeout");
          lcd.print("Timeout");
    }
    writeCMD(HUB_TCP_CLOSE);
}

void udpTest() {
    lcd.setCursor(0,2); 
    lcd.print("UDP:");
    Serial.print("UDP: "); 

    // Setup request    
    writeCMD(HUB_UDP_SLOT);
    writeChar(0);
    writeCMD(HUB_UDP_OPEN);
    writeChar(199); writeChar(47);
    writeChar(196); writeChar(106);
    writeInt(1234); writeInt(4321);
    writeCMD(HUB_UDP_SEND);
    writeBuffer("Packet received", 16);
    
    // Wait for answer
    uint32_t timeout = millis()+3000;
    while (millis() < timeout) {
        if (Serial3.find("CMD") && readChar() == HUB_UDP_RECV) {
            readChar();
            if (readBuffer()) { 
                Serial.println(serBuffer);
                lcd.print(serBuffer);
            }
            break;
        }
    }
    if (millis() >= timeout) {
          Serial.println("Timeout");
          lcd.print("Timeout");
    }
    writeCMD(HUB_UDP_CLOSE);
}

// HTML content types
const char ctImg[] = "Content-Type: image/jpg\r\nCache-Control: max-age=999999, public";
const char ctTxt[] = "Content-Type: text/html";

// Hint: max string length is 255, but better to keep below 192 bytes!
const char htmlHead[] = "<html><center>Welcome to 8bit-Unity Web Server<br><br><img src=\"logo.jpg\" width=\"48\"><br><br><a href=\"support\">Supported</a> platforms | <a href=\"future\">Future</a> platforms";
const char htmlSup1[] = "<br><br><table style=\"border:1px solid black;text-align:center\"><tr><td>Apple //</td><td>Atari XL/XE</td><td>Commodore 64</td><td>Oric</td><td>Lynx</td></tr><tr><td><img src=\"apple.jpg\" width=\"64\">";
const char htmlSup2[] = "</td><td><img src=\"atari.jpg\" width=\"64\"></td><td><img src=\"c64.jpg\" width=\"64\"></td><td><img src=\"atmos.jpg\" width=\"64\"></td><td><img src=\"lynx.jpg\" width=\"64\"></td></tr></table>";
const char htmlFut1[] = "<br><br><table style=\"border:1px solid black;text-align:center\"><tr><td>BBC</td><td>NES</td><td>MSX</td><td>CPC</td><td>...</td></tr></table>";
const char htmlFoot[] = "<br></center></html>";

void webTest() {
    lcd.setCursor(0,2); 
    lcd.print("WEB:");
    Serial.println("WEB:");

    // Setup WEB Server
    writeCMD(HUB_WEB_OPEN);
    writeInt(80);
    writeInt(3000);

    // Wait for WEB Request
    while (1) {
        if (Serial3.find("CMD") && readChar() == HUB_WEB_RECV) {
            if (readBuffer()) {
                writeCMD(HUB_WEB_HEADER); writeBuffer((char*)ctTxt, strlen(ctTxt));
                if (!strncmp(serBuffer, "GET / ", 6)) {      
                    writeCMD(HUB_WEB_BODY); writeBuffer((char*)htmlHead, strlen(htmlHead));
                    writeCMD(HUB_WEB_BODY); writeBuffer((char*)htmlSup1, strlen(htmlSup1));
                    writeCMD(HUB_WEB_BODY); writeBuffer((char*)htmlSup2, strlen(htmlSup2));
                    writeCMD(HUB_WEB_BODY); writeBuffer((char*)htmlFoot, strlen(htmlFoot));
                }
                writeCMD(HUB_WEB_SEND); 
                
                Serial.write(serBuffer, serLen);
                Serial.print('\n');
            }
        }
    }
}

void runTests() {  
    // Network Tests
    lcd.clear();
    wifiTest();
    tcpTest();
    udpTest();
    urlTest();
    delay(2000);

    // Start Mouse (specify refresh period in ms)
    writeCMD(HUB_SYS_MOUSE);
    switch (mouseFPS) {
    case 1:
      writeChar(200);  // (5 FPS)  
      break; 
    case 2:
      writeChar(100);  // (10 FPS)   
      break;
    default:
      writeChar(50);   // (20 FPS)   
      break;
    }
    
    // Run Joy Test
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Joy1: , , , , ,");
    lcd.setCursor(0, 1); lcd.print("Joy2: , , , , ,");
    lcd.setCursor(0, 2); lcd.print("Joy3: , , , , ,");
    lcd.setCursor(0, 3); lcd.print("Mous:   ,   , , , ,");
    uint32_t timeout = millis()+60000;
    while (millis()<timeout) {
        // Check Joysticks
        readJOY();
        for (byte i=0; i<3; i++) {
           for (byte j=0; j<6; j++) {
              if (!(joyState[i] & joyStnd[j])) { 
                  lcd.setCursor(5+2*j, i);  lcd.print(joyCode[j]);
              }         
           }
        }
        // Check Mouse
        if (Serial3.find("CMD")) {
            char cmd = readChar();        
            if (cmd == HUB_SYS_MOUSE) {
                readMouse();
                lcd.setCursor(5,3);  lcd.print("   ");
                lcd.setCursor(5,3);  lcd.print(mouseState[0]);
                lcd.setCursor(9,3);  lcd.print("   ");
                lcd.setCursor(9,3);  lcd.print(mouseState[1]);
                lcd.setCursor(13,3); if (!(joyState[0] & 64))  lcd.print("L");
                lcd.setCursor(15,3); if (!(joyState[1] & 64))  lcd.print("M");
                lcd.setCursor(17,3); if (!(joyState[0] & 128)) lcd.print("R");
                lcd.setCursor(19,3); if (!(joyState[2] & 64))  lcd.print("U");
                lcd.setCursor(19,3); if (!(joyState[2] & 128)) lcd.print("D");
            }
        }
    }
}

//////////////////////////
//    GUI Functions     //
//////////////////////////

boolean upperCase = false;
byte keyRow = 1, keyCol = 1;
char inputCol, inputBuf[32]; 
char* lower[3] = {" 1234567890-=[]\\    ", " abcdefghijklm;'  S ",  " nopqrstuvwxyz,./ R "};
char* upper[3] = {" !@#$%^&*()_+{}|    ",  " ABCDEFGHIJKLM:\"  S ", " NOPQRSTUVWXYZ<>? R "};

void printKeyboard() {
    for (byte i=0; i<3; i++) {
      lcd.setCursor(0,i+1); 
      if (upperCase) lcd.print(upper[i]); 
      else           lcd.print(lower[i]); 
    }
}

void refreshInput() {
    unsigned char offset;
    lcd.setCursor(inputCol, 0);
    if (strlen(inputBuf) > (20-inputCol)) {
        offset = strlen(inputBuf) - (20-inputCol); 
        lcd.print(&inputBuf[offset]);
    } else {
        lcd.print(inputBuf);
        offset = inputCol+strlen(inputBuf);
        while (offset++ < 20)
            lcd.print(" ");
    }
}

void keyboardInput(char* param) {
    // Special Characters
    lower[0][15] = 164;
    lower[0][18] = 127;
    upper[0][18] = 127;

    // Reset input buffer
    for (byte i=0; i<32; i++) inputBuf[i] = 0;

    // Display Parameter and Current Input
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(param);
    inputCol = strlen(param);
    printKeyboard();

    // Wait for release
    while ((joyState[0] & 63) != 63) readJOY();      
    
    // Get Input from User
    char* letter;
    while (true) {
        // Check Joystick
        readJOY();
        if (!(joyState[0] & joyStnd[0])) { keyRow--; if (keyRow<1)  keyRow = 3; }
        if (!(joyState[0] & joyStnd[1])) { keyRow++; if (keyRow>3)  keyRow = 1; }
        if (!(joyState[0] & joyStnd[2])) { keyCol--; if (keyCol<1)  keyCol = 18; }
        if (!(joyState[0] & joyStnd[3])) { keyCol++; if (keyCol>18) keyCol = 1; }
        if (!(joyState[0] & joyStnd[4])) {
            if (keyCol == 18) {
                switch (keyRow) {
                case 1:
                    if (strlen(inputBuf)) {
                        inputBuf[strlen(inputBuf)-1] = 0; // Delete char
                        refreshInput();
                    }
                    break;      
                case 2:
                    // Switch upper/lower
                    upperCase = !upperCase;
                    printKeyboard();
                    break;                    
                case 3:
                    // Exit keyboard
                    return;
                }               
            } else { 
                // Print character
                if (strlen(inputBuf)<31) {
                    if (upperCase) { 
                        letter = upper[keyRow-1][keyCol];
                    } else {
                        letter = lower[keyRow-1][keyCol];
                    }
                    inputBuf[strlen(inputBuf)] = letter;
                    inputBuf[strlen(inputBuf)+1] = 0;
                    refreshInput();
                }
            }
        }
        if (!(joyState[0] & joyStnd[5])) {
            if (strlen(inputBuf)) {
                inputBuf[strlen(inputBuf)-1] = 0; // Delete char
                refreshInput();
            }
        }
        
        // Update Cursor
        lcd.setCursor(keyCol, keyRow);
        lcd.noCursor();
        delay(50);
        lcd.cursor();
        delay(150);        
    }
}

byte listPage = 0;
signed char listRow = 0;

unsigned char listSelection() {
    unsigned char s;
    while(true) {
        // Reset screen
        lcd.clear();
    
        // Select page
        list_t* elt = listHead;
        byte i=0; 
        while (i<listPage) {
            elt = elt->next;
            i++;
        }
    
        // Show elements on current page
        for (i=0; i<4; i++) {
            if (elt) {
                lcd.setCursor(1,i);
                s = 0;
                while (s<strlen(elt->data) && s<19)
                    lcd.print(elt->data[s++]);
                elt = elt->next;
            }
        }
    
        // Joystick control
        while (true) {
            // Blink cursor
            lcd.setCursor(0,listRow);
            lcd.print(" ");
            
            // Check Joystick
            readJOY();
            if (!(joyState[0] & joyStnd[0])) { 
                if (listRow>0) {
                    listRow--; 
                } else {
                    if (listPage>0) {
                        listPage -= 4;
                        listRow = 3;
                        break;
                    }
                }
            }
            if (!(joyState[0] & joyStnd[1])) {
                if (listPage+listRow < lenList()-1) {
                    if (listRow<3) {
                        listRow++; 
                    } else {
                        listPage += 4;
                        listRow = 0;
                        break;
                    }
                }
            }
            if (!(joyState[0] & joyStnd[4]) || !(joyState[0] & joyStnd[5])) {
                unsigned char index = listPage+listRow;
                listPage = 0;
                listRow = 0;
                return index;
            }
            
            // Blink cursor
            lcd.setCursor(0,listRow);
            lcd.print(">");
            delay(200);        
        }
    }
}

////////////////////////////////
//      CONFIG functions      //
////////////////////////////////

void readConfig() {
    // Read config from eeprom
    byte i;
    hubMode    = 255-EEPROM.read(0); if (hubMode>=HUB_MODES) hubMode = 0;
    mouseSpeed = EEPROM.read(1); if (mouseSpeed>=3) mouseSpeed = 1;
    mouseFPS   = EEPROM.read(2); if (mouseFPS>=3) mouseFPS = 1;
    for (i=0; i<32; i++) ssid[i] = 255-EEPROM.read(32+i);
    for (i=0; i<64; i++) pswd[i] = 255-EEPROM.read(64+i);

    // Override with some defaults
    if (!strlen(ssid) && !strlen(pswd)) {
        strcpy(ssid, "8bit-Unity");
        strcpy(pswd, "0123456789"); 
    }
}

void writeConfig() {
    // Read config from eeprom
    byte i;
    EEPROM.write(0, 255-hubMode);
    EEPROM.write(1, mouseSpeed);
    EEPROM.write(2, mouseFPS);
    for (i=0; i<32; i++) EEPROM.write(32+i, 255-ssid[i]);
    for (i=0; i<64; i++) EEPROM.write(64+i, 255-pswd[i]);
}

byte menu = 1, row = 1;
boolean changes = false;  // Let's be kind on eeprom...
const char* menuHeader[4] = {"   < Hub Config >   ", "  < Wifi Config >   ", "  < Mouse Config >  ", "    < Firmware >    "};
const char* menuParam[4][3] = { {"Mode:", "Test:", "Exit:"}, {"Ntwk:", "SSID:", "Pass:"}, {"Spd.:", "Rfr.:", ""}, {"Wifi:", "Core:", "Updt:"} };
const char *labelSpeed[3] = {"Slow", "Med.", "Fast"};
const char *labelFPS[3] = {"5 FPS", "10 FPS", "20 FPS"};

void configMenu() {
    char s=0;
    lcd.setCursor(0,2);
    lcd.print("Entering Config...");
  
    while (true) {
        // Wait for release
        while ((joyState[0] & 63) != 63) readJOY();  
        
        // Show menu
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print(menuHeader[menu-1]);
        for (byte param=3; param>0; param--) {
            // Show Param
            lcd.setCursor(0,param);
            lcd.print(menuParam[menu-1][param-1]);
            
            // Show Value
            switch (menu) {
            case 1:
                switch(param) {
                case 1:
                    lcd.print(modeString[hubMode]);
                    break;    
                case 2:
                    lcd.print("Run now");
                    break;    
                case 3:
                    lcd.print("Save & reboot");
                    break;    
                } break;
            case 2:
                switch(param) {
                case 1:
                    lcd.print("Scan now");
                    break;
                case 2:
                    s = 0;
                    while (s<strlen(ssid) && s<15)
                        lcd.print(ssid[s++]);
                    break;    
                case 3:
                    s = 0;
                    while (s<strlen(pswd) && s<15)
                        lcd.print(pswd[s++]);
                    break;    
                } break;            
            case 3:
                switch(param) {
                case 1:
                    lcd.print(labelSpeed[mouseSpeed]);
                    break;
                case 2:
                    lcd.print(labelFPS[mouseFPS]);
                    break;    
                case 3:
                    break;    
                } break;                 
            case 4:
                switch(param) {
                case 1:
                    lcd.print(espVersion);
                    break;
                case 2:
                    lcd.print(megaVersion);
                    break;    
                case 3:
                    lcd.print("Check now");
                    break;
                } break;            
            }
        }
        lcd.setCursor(5,row);
        lcd.cursor();

        // Wait for input
        while ((joyState[0] & 63) == 63) readJOY(); 
        lcd.noCursor();

        // Check Input
        if (!(joyState[0] & joyStnd[0])) { row--; if (row<1) row = 3; }
        if (!(joyState[0] & joyStnd[1])) { row++; if (row>3) row = 1; }
        if (!(joyState[0] & joyStnd[2])) { menu--; if (menu<1)  menu = 4; }
        if (!(joyState[0] & joyStnd[3])) { menu++; if (menu>4)  menu = 1; }
        if (!(joyState[0] & joyStnd[4]) || !(joyState[0] & joyStnd[5])) {   
            switch (menu) {
            case 1:
                switch(row) {
                case 1:
                    // Change Mode
                    delay(200);
                    for (byte i=1; i<HUB_MODES; i++)
                        pushList(modeString[i]);
                    hubMode = listSelection()+1;
                    clearList();
                    changes = true;             
                    break;    
                case 2:
                    // Run Tests
                    runTests();
                    break;    
                case 3:
                    // Save Config and Boot
                    if (changes) writeConfig();
                    displayHeader();
                    return;
                } break;
            case 2:     
                switch(row) {
                case 1:
                    // Scan Networks
                    if (wifiScan()) {
                        // Create SSID list
                        s=0; while (serBuffer[s] != 0) {
                            while (serBuffer[s] != '\n' && serBuffer[s] != '\r' && serBuffer[s] != 0) s++;
                            serBuffer[s++] = 0;
                            if (serBuffer[s]) {
                                pushList(&serBuffer[s]);
                            }
                        }

                        // Select SSID
                        s = listSelection();
                        strcpy(ssid, getList(s));
                        clearList();
                        
                        // Set PSWD
                        keyboardInput(menuParam[menu-1][2]);
                        strcpy(pswd, inputBuf);                    
                        changes = true;
                    } else {
                        // Cannot fetch...
                        lcd.setCursor(5,1); 
                        lcd.print("No SSID found");
                        delay(1000);
                    }
                    break;
                case 2:
                    // Set SSID
                    keyboardInput(menuParam[menu-1][1]);
                    strcpy(ssid, inputBuf);
                    changes = true;
                    break;    
                case 3:
                    // Set PSWD
                    keyboardInput(menuParam[menu-1][2]);
                    strcpy(pswd, inputBuf);
                    changes = true;
                    break;    
                } break;            
            case 3:
                switch(row) {
                case 1:
                    // Change Mouse Speed
                    delay(200);
                    for (byte i=0; i<3; i++)
                        pushList(labelSpeed[i]);
                    mouseSpeed = listSelection();
                    clearList();
                    changes = true;
                    break;    
                case 2:
                    // Change Mouse Refresh
                    delay(200);
                    for (byte i=0; i<3; i++)
                        pushList(labelFPS[i]);
                    mouseFPS = listSelection();
                    clearList();
                    changes = true;
                    break;       
                } break;
            case 4:         
                switch(row) {
                case 3:
                    // Try to Update
                    lcd.setCursor(0,1); lcd.print("Please wait...");
                    lcd.setCursor(0,2); lcd.print(blank);
                    lcd.setCursor(0,3); lcd.print(blank);
                    setupESP();
                    checkUpdate();
                    break;
                } break;            
            }
        }
    }
}

////////////////////////////
//      MEGA Routines     //
////////////////////////////

void setup() {
    // Setup comm.
    setupSERIAL();
    setupLCD();
    setupJOY();
    
    // Read config from EEPROM
    readConfig();
    wifiVersion();
    
    // Chance to enter config (press fire)
    lcd.setCursor(0,1);
    lcd.print("Press FIRE for Conf.");       
    uint32_t timer = millis(); 
    while (millis()-timer < 3000) {
        readJOY();
        if (!(joyState[0] & joyStnd[4]) || !(joyState[0] & joyStnd[5])) {
            configMenu();
        }
    }

    // Setup peripherals
    lcd.setCursor(0,1);
    lcd.print("Booting...          ");
    setupESP();
    setupSD();

    // Check for Updates
    checkUpdate();

    // Start Mouse
    writeCMD(HUB_SYS_MOUSE);
    writeChar(100);  // Refresh period (ms)      
    
    // Display status info on LCD
    displayMode();
    displaySD();
    displayIP();

    // Initiate COM
    setupCOM();    
}

#ifdef __DEBUG_COM__
  int comCnt, comErr[6];
#endif

void loop() { 
    // Check packets time-out
    packetTimeout();

    // Check Joysticks states
    readJOY(); 
      
    // Process commands from ESP8266
    if (Serial3.find("CMD")) 
        espProcessCMD();

    // Process COM I/O
    if (hubMode == MODE_LYNX) {
        // Process Lynx Communication (asynchronously)
        lynxProcessCOM();      
    } else    
    if (hubMode == MODE_ATARI || hubMode == MODE_C64 || hubMode == MODE_NES || hubMode == MODE_ORIC) {
        // Check if data burst stalled (allow extra time for VBI/DLI interrupts)
        if (interruptOffset && (millis()-interruptTimer) > 5) {
            interruptOffset = 0; 
            hasHeader = 0;
            inByte = 0;
            if (hubMode == MODE_ATARI)
                PORTH |= _BV(PH3);   // Pin 4 HIGH == READY
            if (hubMode == MODE_C64)
                PORTH &= ~_BV(PH1);  // Pin 16 LOW   
            if (hubMode == MODE_NES)    
                PORTH &= ~_BV(PH3);  // Pin 6 HIGH (inverted) 
        #ifdef __DEBUG_IO__  
            if (interruptMode)              
                inSta++;
            else
                outSta++;
        #endif    
        } 
    }

    // Display transfer rate
#ifdef __DEBUG_IO__
    if (millis()-timerIO > 5000) {
        timerIO = millis();
        Serial.print(" Rate: in=");
        Serial.print(inRec/5); Serial.print("b/s, out=");
        Serial.print(outRec/5); Serial.print("b/s, err(in)=");
        Serial.print(inErr); Serial.print(", stall(in)=");
        Serial.print(inSta); Serial.print(", stall(out)=");
        Serial.println(outSta); 
        inRec = 0; inErr = 0; inSta = 0; 
        outRec = 0; outSta = 0;
    } 
#endif
    
    // Display COM Stats
#ifdef __DEBUG_COM__
    if (comCode != COM_ERR_NODATA) {
        comErr[comCode] += 1;
        Serial.print(" (RX)");
        Serial.print(" ID=");  Serial.print(comInID);
        Serial.print(" Len="); Serial.print(comInLen);
        Serial.print(" (TX)");
        Serial.print(" ID=");  Serial.print(comOutID);
        Serial.print(" Len="); Serial.print(comOutLen);
        Serial.print(" (ERR)");        
        Serial.print(" Head="); Serial.print(comErr[COM_ERR_HEADER]);
        Serial.print(" Trnc="); Serial.print(comErr[COM_ERR_TRUNCAT]);
        Serial.print(" Crpt="); Serial.print(comErr[COM_ERR_CORRUPT]);
        Serial.print(" Rsnt="); Serial.println(comErr[COM_ERR_RESENT]);
    }
#endif
}

////////////////////////////////
//        JPG functions       //
////////////////////////////////
/*

#include <picojpeg.h>

const unsigned char lynxPaletteR[] = {165,  66,  66,  49,  49,  33,  33, 115, 231, 247, 247, 214, 132, 247, 132, 0};
const unsigned char lynxPaletteG[] = { 16,  49, 132, 198, 198, 132,  82,  82, 115, 231, 148,  49,  33, 247, 132, 0};
const unsigned char lynxPaletteB[] = {198, 181, 198, 198,  82,  33,  82,  33,  82,   0, 165,  66,  66, 247, 132, 0};

File jpgFile;
unsigned int jpgSize, jpgOfs;

uint8_t callbackJPG(uint8_t* pBuf, uint8_t buf_size, uint8_t *pBytes_actually_read, void *pCallback_data) {
    unsigned int n = MIN(jpgSize - jpgOfs, buf_size);
    *pBytes_actually_read = (uint8_t)(n);
    jpgFile.read(pBuf,n);
    jpgOfs += n;
    return 0;    
}

void decodeJPG(unsigned char* filename) {
    // Open file from SD card
    uint32_t timer = millis();     

    // Open jpeg file from SD card
    jpgFile = SD.open(filename, FILE_READ);
    jpgSize = jpgFile.size();
    jpgOfs = 0;

    // Initialize decoder
    pjpeg_image_info_t jpgInfo;
    unsigned char status = pjpeg_decode_init(&jpgInfo, callbackJPG, NULL, 0);
    if (status) {
      Serial.print("pjpeg_decode_init() failed with status ");
      Serial.println(status);
      if (status == PJPG_UNSUPPORTED_MODE) {
        Serial.println("Progressive JPEG files are not supported.");
      }
      return;
    }
    
    // Open cache file to store decoded image
    if (SD.exists("cache")) { SD.remove("cache"); }
    File cacheFile = SD.open("cache", FILE_WRITE);
    
    // Scanning variables
    unsigned int xElt, yElt;
    unsigned char xElt8, yElt8;
    unsigned char xMin, xMax, yMin, yMax;
    unsigned int palDist, palMini;
    unsigned char palIndex;
    unsigned char bmp[9][82];

    // Scan through block rows
    for (unsigned int yMCU=0; yMCU<jpgInfo.m_height; yMCU+=jpgInfo.m_MCUHeight) {

        // Computer's Y coordinate range
        yMin = (102 * (yMCU                    )) / jpgInfo.m_height;
        yMax = (102 * (yMCU+jpgInfo.m_MCUHeight)) / jpgInfo.m_height;
        yMax = MIN(yMax, 102);

        // Reset bmp array
        for (unsigned int y=yMin; y<yMax; y++) {
            bmp[y-yMin][0] = 0x52;
            memset(&bmp[y-yMin][1], 0, 81);
        }

        // Scan through block cols
        for (unsigned int xMCU=0; xMCU<jpgInfo.m_width; xMCU+=jpgInfo.m_MCUWidth) {

            // Computer's X coordinate range
            xMin = (160 * (xMCU                   )) / jpgInfo.m_width;
            xMax = (160 * (xMCU+jpgInfo.m_MCUWidth)) / jpgInfo.m_width;
            xMax = MIN(xMax, 160);

            // Decode this block
            pjpeg_decode_mcu();
    
            // Scan Y coordinate range
            for (unsigned int y=yMin; y<yMax; y++) {
                yElt = (y * jpgInfo.m_height)/102 - yMCU;  yElt8 = yElt/8;
                //Serial.print((byte)y); Serial.print(": ");
    
                // Scan X coordinate range
                for (unsigned int x=xMin; x<xMax; x++) {
                    xElt = (x * jpgInfo.m_width)/160 - xMCU;  xElt8 = xElt/8;
                    //Serial.print((byte)x); Serial.print(", ");
    
                    // Compute offset of element
                    unsigned int block_ofs = (xElt8 * 8U) + (yElt8 * 16U);
                    unsigned int elt_oft = 8*(yElt%8) + (xElt%8);
                    const uint8_t *pSrcR = jpgInfo.m_pMCUBufR + block_ofs + elt_oft;
                    const uint8_t *pSrcG = jpgInfo.m_pMCUBufG + block_ofs + elt_oft;
                    const uint8_t *pSrcB = jpgInfo.m_pMCUBufB + block_ofs + elt_oft;
    
                    // Find matching palette color
                    palMini = 3*256;
                    for (byte i=0; i<16; i++) {
                        palDist = abs(lynxPaletteR[i] - *pSrcR) + abs(lynxPaletteG[i] - *pSrcG) + abs(lynxPaletteB[i] - *pSrcB);
                        if (palDist<palMini) {
                            palIndex = i; 
                            palMini = palDist;
                        }
                    }

                    // Save to bmp buffer
                    bmp[y-yMin][x/2+1] |= palIndex << (4*((x+1)%2));
                    //Serial.print(palIndex); Serial.print(",");                
                }
                //Serial.println();
            }
            //Serial.println();
        }

        // Write to char file
        cacheFile.write(&bmp[0][0], 82*(yMax-yMin));
    }

    // Close files
    cacheFile.write((byte)0x00);
    cacheFile.close();  
    jpgFile.close();
#ifdef __DEBUG_JPG__ 
    Serial.println(F("JPEG image info"));
    Serial.print("Width      : "); Serial.println(jpgInfo.m_width);
    Serial.print("Height     : "); Serial.println(jpgInfo.m_height);
    Serial.print("Scan type  : "); Serial.println(jpgInfo.m_scanType);
    Serial.print("MCU/row    : "); Serial.println(jpgInfo.m_MCUSPerRow);
    Serial.print("MCU/col    : "); Serial.println(jpgInfo.m_MCUSPerCol);
    Serial.print("MCU/width  : "); Serial.println(jpgInfo.m_MCUWidth);
    Serial.print("MCU/height : "); Serial.println(jpgInfo.m_MCUHeight);
    Serial.print("Render time: "); Serial.print(millis()-timer); Serial.println(" ms");
#endif  
}
*/
