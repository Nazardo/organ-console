#include <Arduino.h>
#include <EtherCard.h>
#include <avr/wdt.h>

uint8_t Ethernet::buffer[700];
uint8_t outputBuffer[100];
const uint8_t macaddr[] = {0x47, 0xF8, 0xEA, 0x3A, 0xFB, 0x7C};
const uint8_t ip[] = {192, 168, 89, 101};
const uint8_t netmask[] = {255, 255, 255, 0};
const uint16_t udpPort = 9000;
const uint8_t serverIp[] = {192, 168, 89, 5};

const uint8_t InternalStatus_Idle = 0;
const uint8_t InternalStatus_SendStart = 1;
const uint8_t InternalStatus_SendStop = 2;
uint8_t internalStatus = InternalStatus_Idle;

const uint8_t RemoteStatus_Unknown = 0;
const uint8_t RemoteStatus_StartingOrOn = 1;
const uint8_t RemoteStatus_StoppingOrOff = 2;
uint8_t remoteStatus = RemoteStatus_Unknown;


unsigned long lastResetMillis = 0;
const unsigned long resetDebounceMilliseconds = 3000;

unsigned long lastSendMillis = 0;
const unsigned long SendIntervalMilliseconds = 1000;

unsigned long blinkMillis = 0;
const unsigned long BlinkIntervalMilliseconds = 500;
bool blinkMultiplier = false;

// Start/Stop message: [1 : Type(1B)][1 : Version(1B)][0/1 : Command(1B)][0..3 : HWConfig(1B)]
char startStopMessage[] = {0x01, 0x01, 0x00, 0x00};
// Reset message: [2 : Type(1B)][1 : Version(1B)]
char resetMessage[] = {0x02, 0x01};

void onUdpPacketReceived(uint16_t, uint8_t *, uint16_t, const char *, uint16_t);

const uint8_t LedRedPin = 2;
const uint8_t LedYellowPin = 3;
const uint8_t LedGreenPin = 4;
const uint8_t ButtonStartPin = 5;
const uint8_t ButtonStopPin = 6;
const uint8_t ButtonResetPin = 7;

bool ledRedActive = false;
bool ledRedBlinking = false;
bool ledYellowActive = false;
bool ledYellowBlinking = true;
bool ledGreenActive = false;
bool ledGreenBlinking = false;

void setup()
{
  Serial.begin(9600);
  wdt_enable(WDTO_8S);
  pinMode(LedRedPin, OUTPUT);
  pinMode(LedYellowPin, OUTPUT);
  pinMode(LedGreenPin, OUTPUT);
  pinMode(ButtonStartPin, INPUT_PULLUP);
  pinMode(ButtonStopPin, INPUT_PULLUP);
  pinMode(ButtonResetPin, INPUT_PULLUP);
  ether.begin(sizeof Ethernet::buffer, macaddr, SS);
  ether.staticSetup(ip, 0, 0, netmask);
  ether.copyIp(ether.hisip, serverIp);
  ether.udpServerListenOnPort(onUdpPacketReceived, udpPort);
  wdt_reset();
  delay(5000);
}

void loop()
{
  wdt_reset();
  uint16_t recv = ether.packetReceive();
  ether.packetLoop(recv);
  digitalWrite(LedRedPin, ledRedActive || (ledRedBlinking && blinkMultiplier));
  digitalWrite(LedYellowPin, ledYellowActive || (ledYellowBlinking && blinkMultiplier));
  digitalWrite(LedGreenPin, ledGreenActive || (ledGreenBlinking && blinkMultiplier));
  if (digitalRead(ButtonStartPin) == 0 && remoteStatus == RemoteStatus_StoppingOrOff)
  {
    internalStatus = InternalStatus_SendStart;
  }
  else if (digitalRead(ButtonStopPin) == 0 && remoteStatus == RemoteStatus_StartingOrOn)
  {
    internalStatus = InternalStatus_SendStop;
  }
  unsigned long now = millis();
  if (now - blinkMillis > BlinkIntervalMilliseconds)
  {
    blinkMultiplier = !blinkMultiplier;
    blinkMillis = now;
  }
  if ((internalStatus == InternalStatus_SendStart || internalStatus == InternalStatus_SendStop)
      && now - lastSendMillis > SendIntervalMilliseconds)
  {
    uint8_t command = 0;
    if (internalStatus == InternalStatus_SendStart)
    {
      Serial.println("UDP > Send START");
      command = 1;
    }
    else
    {
      Serial.println("UDP > Send STOP");
      command = 0;
    }
    startStopMessage[2] = command;
    ether.sendUdp(startStopMessage, sizeof startStopMessage, udpPort, serverIp, udpPort);
    lastSendMillis = now;
  } else if (digitalRead(ButtonResetPin) == 0 && now - lastResetMillis > resetDebounceMilliseconds) {
    ether.sendUdp(resetMessage, sizeof resetMessage, udpPort, serverIp, udpPort);
    lastResetMillis = now;
  }
}

void onStatusMessageReceived(const char *data, uint16_t len, uint8_t version)
{
  if (version == 1 && len == 3)
  {
    uint8_t stateMachineStatus = (uint8_t)data[2];
    ledYellowBlinking = false; // Stop blinking since message has arrived
    ledRedActive = stateMachineStatus == 0 || stateMachineStatus == 6;
    ledRedBlinking = stateMachineStatus == 5;
    ledGreenActive = stateMachineStatus == 2 || stateMachineStatus == 3;
    ledGreenBlinking = stateMachineStatus == 1;
    ledYellowActive = !(stateMachineStatus == 0 || stateMachineStatus == 3);

    if (stateMachineStatus > 0 && stateMachineStatus < 4)
    {
      remoteStatus = RemoteStatus_StartingOrOn;
      if (internalStatus == InternalStatus_SendStart)
      {
        internalStatus = InternalStatus_Idle;
      }
    }
    else
    {
      remoteStatus = RemoteStatus_StoppingOrOff;
      if (internalStatus == InternalStatus_SendStop)
      {
        internalStatus = InternalStatus_Idle;
      }
    }
  }
}

const uint8_t MessateType_Status = 0;
const uint8_t MessageType_StartStop = 1;

void onUdpPacketReceived(uint16_t dest_port, uint8_t src_ip[IP_LEN], uint16_t src_port, const char *data, uint16_t len)
{
  if (len < 2)
  {
    return;
  }
  uint8_t version = (uint8_t)data[1];
  switch (data[0])
  {
  case 0x00:
    onStatusMessageReceived(data, len, version);
    break;
  }
}