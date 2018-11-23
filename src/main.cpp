#include <Arduino.h>
#include <EtherCard.h>
#include <avr/wdt.h>

uint8_t Ethernet::buffer[700];
uint8_t outputBuffer[100];
const uint8_t macaddr[] = {0x47, 0xF8, 0xEA, 0x3A, 0xFB, 0x7C};
const uint8_t ip[] = {192, 168, 89, 101};
const uint8_t netmask[] = {255, 255, 255, 0};
const uint16_t udpPort = 10089;
const uint8_t serverIp[] = {192, 168, 89, 10};

bool bootSequence_YELLOW = false;
bool systemActive_GREEN = false;
bool shutdownSequence_RED = false;

const uint8_t InternalStatus_Idle = 0;
const uint8_t InternalStatus_SendStart = 1;
const uint8_t InternalStatus_SendStop = 2;
uint8_t internalStatus = InternalStatus_Idle;

const uint8_t RemoteStatus_StartingOrOn = 1;
const uint8_t RemoteStatus_StoppingOrOff = 2;
uint8_t remoteStatus = RemoteStatus_StoppingOrOff;

unsigned long lastSendMillis = 0;
const unsigned long SendIntervalMilliseconds = 1000;

// Start/Stop message: [1 : Type(1B)][1 : Version(1B)][0/1 : Command(1B)][0..3 : HWConfig(1B)]
char startStopMessage[] = {0x01, 0x01, 0x00, 0x00};

void onUdpPacketReceived(uint16_t, uint8_t *, uint16_t, const char *, uint16_t);

void setup()
{
  Serial.begin(9600);
  wdt_enable(WDTO_8S);
  pinMode(PIN2, OUTPUT); // bootSequence_YELLOW
  pinMode(PIN3, OUTPUT); // systemActive_GREEN
  pinMode(PIN4, OUTPUT); // shutdownSequence_RED
  pinMode(PIN5, INPUT);  // START button
  pinMode(PIN6, INPUT);  // STOP button
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
  digitalWrite(PIN2, bootSequence_YELLOW);
  digitalWrite(PIN3, systemActive_GREEN);
  digitalWrite(PIN4, shutdownSequence_RED);
  if (digitalRead(PIN5) && remoteStatus == RemoteStatus_StoppingOrOff)
  {
    internalStatus = InternalStatus_SendStart;
  }
  else if (digitalRead(PIN6) && remoteStatus == RemoteStatus_StartingOrOn)
  {
    internalStatus = InternalStatus_SendStop;
  }
  if (internalStatus == InternalStatus_SendStart || internalStatus == InternalStatus_SendStop)
  {
    unsigned long now = millis();
    if ((now - lastSendMillis) > SendIntervalMilliseconds)
    {
      startStopMessage[2] = internalStatus == InternalStatus_SendStart ? 1 : 0;
      ether.sendUdp(startStopMessage, sizeof startStopMessage, udpPort, serverIp, udpPort);
      lastSendMillis = now;
    }
  }
}

void onStatusMessageReceived(const char *data, uint16_t len, uint8_t version)
{
  if (version == 1 && len == 3)
  {
    uint8_t ledStatus = (uint8_t)data[2];
    bootSequence_YELLOW = ledStatus & 0x01;
    systemActive_GREEN = ledStatus & 0x02;
    shutdownSequence_RED = ledStatus & 0x04;
    if (bootSequence_YELLOW || (systemActive_GREEN && !shutdownSequence_RED))
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