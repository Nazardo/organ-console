#include <Arduino.h>
#include <EtherCard.h>
#include <avr/wdt.h>

uint8_t Ethernet::buffer[700];
uint8_t outputBuffer[100];
const uint8_t MacAddress[] = {0x47, 0xF8, 0xEA, 0x3A, 0xFB, 0x7C};
const uint8_t LocalIp[] = {192, 168, 89, 101};
const uint8_t netmask[] = {255, 255, 255, 0};
const uint16_t UdpPort = 9000;
const uint8_t ServerIp[] = {192, 168, 89, 5};
const uint8_t ConsoleId = 0x01;

const uint8_t LedRedPin = 2;
const uint8_t LedYellowPin = 3;
const uint8_t LedGreenPin = 4;
const uint8_t ButtonGreenPin = 5;
const uint8_t ButtonRedPin = 6;
const uint8_t ButtonBlackPin = 7;
const uint8_t NumberOfButtons = 3;

const uint8_t ButtonStatus_Idle = 0;
const uint8_t ButtonStatus_Pressing = 1;
const uint8_t ButtonStatus_Releasing = 2;

uint8_t buttonPins[] = { ButtonGreenPin, ButtonRedPin, ButtonBlackPin };
uint8_t buttonStatuses[] = { ButtonStatus_Idle, ButtonStatus_Idle, ButtonStatus_Idle };
unsigned long lastButtonUpdateTimes[] = { 0, 0, 0 };

const unsigned long ButtonDebounceInterval = 1000;
const unsigned long ButtonLongPressInterval = 5000;

unsigned long lastConsoleMessageSent = 0;
const unsigned long KeepAliveSendInterval = 3000;

unsigned long blinkMillis = 0;
const unsigned long BlinkInterval = 500;
bool blinkMultiplier = false;

// Console message
// sent every KeepAliveSendInterval milliseconds or each time a button is pressed.
// [00000001] -> 1
// [XXXXXXXX] -> X: console id
// [PL0000XX] -> P: pressed?, L: short/long press, X: button number (0-NumberOfButtons)
char consoleMessage[] = { 0x01, ConsoleId, 0x00 };

void onUdpPacketReceived(uint16_t, uint8_t *, uint16_t, const char *, uint16_t);

bool ledRedActive = false;
bool ledRedBlinking = false;
bool ledYellowActive = false;
bool ledYellowBlinking = false;
bool ledGreenActive = false;
bool ledGreenBlinking = false;

void setup()
{
  Serial.begin(9600);
  wdt_enable(WDTO_8S);
  pinMode(LedRedPin, OUTPUT);
  pinMode(LedYellowPin, OUTPUT);
  pinMode(LedGreenPin, OUTPUT);
  pinMode(ButtonGreenPin, INPUT_PULLUP);
  pinMode(ButtonRedPin, INPUT_PULLUP);
  pinMode(ButtonBlackPin, INPUT_PULLUP);
  ether.begin(sizeof Ethernet::buffer, MacAddress, SS);
  ether.staticSetup(LocalIp, 0, 0, netmask);
  ether.copyIp(ether.hisip, ServerIp);
  ether.udpServerListenOnPort(onUdpPacketReceived, UdpPort);
  wdt_reset();
  delay(5000);
}

void loop()
{
  wdt_reset();
  uint16_t recv = ether.packetReceive();
  ether.packetLoop(recv);
  unsigned long now = millis();
  if (now - blinkMillis > BlinkInterval)
  {
    blinkMultiplier = !blinkMultiplier;
    blinkMillis = now;
    digitalWrite(LedRedPin, ledRedActive || (ledRedBlinking && blinkMultiplier));
    digitalWrite(LedYellowPin, ledYellowActive || (ledYellowBlinking && blinkMultiplier));
    digitalWrite(LedGreenPin, ledGreenActive || (ledGreenBlinking && blinkMultiplier));
  }
  uint8_t buttonPressedByte = 0x00;
  // Debounce routine for input buttons.
  // For each button, its status and the time at which it entered such status are stored.
  // Transitions last for at least ButtonDebounceMilliseconds milliseconds.
  for (uint8_t btnId = 0; btnId < NumberOfButtons; ++btnId) {
    bool debounceElapsed = (now - lastButtonUpdateTimes[btnId]) > ButtonDebounceInterval;
    if (debounceElapsed) {
      uint8_t buttonStatus = buttonStatuses[btnId];
      if (digitalRead(buttonPins[btnId]) == 0) {
        if (buttonStatus == ButtonStatus_Idle) {
          buttonStatuses[btnId] = ButtonStatus_Pressing;
          lastButtonUpdateTimes[btnId] = now;
        }
      } else if (buttonStatus == ButtonStatus_Pressing) {
        bool isLongPress = (now - lastButtonUpdateTimes[btnId]) > ButtonLongPressInterval;
        buttonPressedByte = 0x80 | btnId;
        if (isLongPress) {
          buttonPressedByte |= 0x40;
        }
        buttonStatuses[btnId] = ButtonStatus_Releasing;
        lastButtonUpdateTimes[btnId] = now;
        break;
      } else if (buttonStatus == ButtonStatus_Releasing){
        buttonStatuses[btnId] = ButtonStatus_Idle;
        lastButtonUpdateTimes[btnId] = now;
      }
    }
  }

  if (buttonPressedByte || (now - lastConsoleMessageSent) > KeepAliveSendInterval) {
    consoleMessage[2] = buttonPressedByte;
    ether.sendUdp(consoleMessage, sizeof consoleMessage, UdpPort, ServerIp, UdpPort);
    lastConsoleMessageSent = now;
  }
}

void parseLedValues(uint8_t ledByte)
{
  ledGreenActive = ledByte & 0x01;
  ledGreenBlinking = ledByte & 0x02;
  ledYellowActive = ledByte & 0x04;
  ledYellowBlinking = ledByte & 0x08;
  ledRedActive = ledByte & 0x10;
  ledRedBlinking = ledByte & 0x20;
}

const uint8_t MessateType_Status = 0;
const uint8_t MessageType_StartStop = 1;

void onUdpPacketReceived(uint16_t dest_port, uint8_t src_ip[IP_LEN], uint16_t src_port, const char *data, uint16_t len)
{
  if (len < 2)
  {
    return;
  }
  uint8_t msgType = (uint8_t)data[0];
  switch (msgType)
  {
  case 0x00:
    parseLedValues(data[1]);
    break;
  }
}