// USB adapter for Wii extension controllers,
// e.g. Classic Controller or NES Classic Mini Controller
//
// technical details about the connector and protocol from:
// http://wiibrew.org/wiki/Wiimote/Extension_Controllers

// pinout for my extension cable:
// (the connector end, with notched side up)
//
// bottom left:   green  (3.3v)
// bottom middle: white  (device detect)
// bottom right:  red    (sda)
// upper left:    black  (scl)
// upper right:   yellow (gnd)

#include <Wire.h>
#include <HID-Project.h>

// uncomment to enable debug mode, prints infos to serial
//#define DEBUG 1

#ifdef DEBUG
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) (void)0
#define DEBUG_PRINTLN(...) (void)0
#endif

// TODO: use device detect
const int DEVICE_DETECT_PIN = 7;
// how often should we poll the controller (16ms = 60Hz)
const unsigned long POLL_INTERVAL_MS = 16;
// controllers use 400kHz I2C clock
const unsigned long I2C_CLOCK_SPEED = 400000;
// address of the controller
const byte I2C_ADDRESS = 0x52;

const byte INIT_DATA_1[] = { 0xf0, 0x55 };
const byte INIT_DATA_2[] = { 0xfb, 0x00 };

const byte ID_ADDRESS = 0xfa;
const byte ID_LEN = 6;

const byte DATA_ADDRESS = 0x00;
const byte DATA_LEN = 6;

// all relevant controllers seem to use these IDs,
// e.g. classic controller (pro), hori battle pad,
// nes classic mini controller

const byte VALID_ID_1[] = { 0x00, 0x00, 0xA4, 0x20, 0x01, 0x01 };
const byte VALID_ID_2[] = { 0x01, 0x00, 0xA4, 0x20, 0x01, 0x01 };

// map from bitset of dpad buttons to Gamepad API dpad value.
// assuming the bits are down, right, up, left.
// for invalid combinations, choose a meaningful fallback.
const int8_t DPAD_MAPPINGS[] =
{
  GAMEPAD_DPAD_CENTERED,   // 0000 = none
  GAMEPAD_DPAD_DOWN,       // 0001 = down
  GAMEPAD_DPAD_RIGHT,      // 0010 = right
  GAMEPAD_DPAD_DOWN_RIGHT, // 0011 = down+right
  GAMEPAD_DPAD_UP,         // 0100 = up
  GAMEPAD_DPAD_DOWN,       // 0101 = down+up = invalid
  GAMEPAD_DPAD_UP_RIGHT,   // 0110 = right+up
  GAMEPAD_DPAD_RIGHT,      // 0111 = right+up+down = invalid
  GAMEPAD_DPAD_LEFT,       // 1000 = left
  GAMEPAD_DPAD_DOWN_LEFT,  // 1001 = down+left
  GAMEPAD_DPAD_RIGHT,      // 1010 = right+left = invalid
  GAMEPAD_DPAD_DOWN,       // 1011 = right+left+down = invalid
  GAMEPAD_DPAD_UP_LEFT,    // 1100 = up+left
  GAMEPAD_DPAD_LEFT,       // 1101 = down+up+left = invalid
  GAMEPAD_DPAD_UP,         // 1110 = right+up+left = invalid
  GAMEPAD_DPAD_CENTERED    // 1111 = all buttons = invalid
};

// map from Wii controller buttons to Gamepad API values
enum ButtonMappings : uint8_t
{
  BUTTON_A = 1,
  BUTTON_B = 2,
  BUTTON_X = 3,
  BUTTON_Y = 4,
  BUTTON_PLUS = 5,
  BUTTON_HOME = 6,
  BUTTON_MINUS = 7,
  BUTTON_LEFT_TRIGGER = 8,
  BUTTON_RIGHT_TRIGGER = 9,
  BUTTON_ZL = 10,
  BUTTON_ZR = 11
};

// controller data for a Classic Controller according to wiki
struct ControllerData
{
  byte left_stick_x : 6;
  byte right_stick_x1 : 2;

  byte left_stick_y : 6;
  byte right_stick_x2 : 2;

  byte right_stick_y : 5;
  byte left_trigger1 : 2;
  byte right_stick_x3 : 1;

  byte right_trigger : 5;
  byte left_trigger2 : 3;

  byte dummy : 1; // bit is always set?
  byte button_right_trigger : 1;
  byte button_plus : 1;
  byte button_home : 1;
  byte button_minus : 1;
  byte button_left_trigger : 1;
  byte button_down : 1;
  byte button_right : 1;

  byte button_up : 1;
  byte button_left : 1;
  byte button_zr : 1;
  byte button_x : 1;
  byte button_a : 1;
  byte button_y : 1;
  byte button_b : 1;
  byte button_zl : 1;

  // right stick x axis is stored in non-contiguous bits
  byte getRightStickX() const
  {
    return (right_stick_x1 << 3) |
           (right_stick_x2 << 1) |
           right_stick_x3;
  }

  // left trigger axis is stored in non-contiguous bits
  byte getLeftTrigger() const
  {
    return (left_trigger1 << 3) | left_trigger2;
  }

  // return a Dpad enum value for the Gamepad API
  int8_t getDpadValue() const
  {
    byte down_bit = (button_down == 0);
    byte right_bit = (button_right == 0) << 1;
    byte up_bit = (button_up == 0) << 2;
    byte left_bit = (button_left == 0) << 3;

    byte dpad_index = (down_bit | right_bit | up_bit | left_bit);
    return DPAD_MAPPINGS[dpad_index];
  }
};

int i2cWrite(const byte *data, byte len)
{
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(data, len);
  int err = Wire.endTransmission();
  return err;
}

int i2cWrite(byte data)
{
  byte arr[] = { data };
  return i2cWrite(arr, 1);
}

int i2cRead(byte address, byte len, byte *result)
{
  int err = i2cWrite(address);
  if (err != 0)
    return err;

  delay(1); // necessary to avoid blocking?

  Wire.requestFrom(I2C_ADDRESS, len);
  while (Wire.available() < len)
    delay(1);

  Wire.readBytes(result, len);
  return 0;
}

void initController() {
  DEBUG_PRINTLN("init...");

  // initialize controller in unencrypted mode
  // (see "new way" initialization on the wiki page)
  int err1 = i2cWrite(INIT_DATA_1, sizeof(INIT_DATA_1));
  int err2 = i2cWrite(INIT_DATA_2, sizeof(INIT_DATA_2));

  DEBUG_PRINTLN("init done...");
  DEBUG_PRINT("err1=");
  DEBUG_PRINTLN(err1);
  DEBUG_PRINT("err2=");
  DEBUG_PRINTLN(err2);
}

void getControllerId()
{
  DEBUG_PRINTLN("getControllerId...");

  byte id[ID_LEN];
  int err = i2cRead(ID_ADDRESS, ID_LEN, id);

  DEBUG_PRINT("err=");
  DEBUG_PRINTLN(err);

  DEBUG_PRINT("controller id=");
  for (byte i = 0; i < ID_LEN; ++i)
  {
    DEBUG_PRINT(' ');

    byte b = id[i];
    if (b < 0x10)
      DEBUG_PRINT('0');
    DEBUG_PRINT(id[i], HEX);
  }

  DEBUG_PRINTLN();
}

int getControllerData(ControllerData &data)
{
  int err = i2cRead(DATA_ADDRESS, DATA_LEN, (byte*) &data);
  return err;
}

void updateGamepad(ControllerData &data)
{
  // the Gamepad API remembers button states and expects us
  // to press/release buttons (without making the states available),
  // but the Wii controller gives us absolute data,
  // so always "release" all buttons and then fill them again.
  Gamepad.releaseAll();

  Gamepad.dPad1(data.getDpadValue());

  if (data.button_right_trigger == 0) Gamepad.press(BUTTON_RIGHT_TRIGGER);
  if (data.button_plus == 0)          Gamepad.press(BUTTON_PLUS);
  if (data.button_home == 0)          Gamepad.press(BUTTON_HOME);
  if (data.button_minus == 0)         Gamepad.press(BUTTON_MINUS);
  if (data.button_left_trigger == 0)  Gamepad.press(BUTTON_LEFT_TRIGGER);
  if (data.button_zr == 0)            Gamepad.press(BUTTON_ZR);
  if (data.button_x == 0)             Gamepad.press(BUTTON_X);
  if (data.button_a == 0)             Gamepad.press(BUTTON_A);
  if (data.button_y == 0)             Gamepad.press(BUTTON_Y);
  if (data.button_b == 0)             Gamepad.press(BUTTON_B);
  if (data.button_zl == 0)            Gamepad.press(BUTTON_ZL);

  // analog axes for left/right stick and triggers
  // (have to be scaled to Gamepad API's range)
  Gamepad.xAxis((int16_t(data.left_stick_x) - 32) << 10);
  Gamepad.yAxis((int16_t(data.left_stick_y) - 32) << 10);

  Gamepad.rxAxis((int16_t(data.getRightStickX()) - 16) << 11);
  Gamepad.ryAxis((int16_t(data.right_stick_y) - 16) << 11);

  Gamepad.zAxis(int8_t(data.getLeftTrigger()) << 2);
  Gamepad.rzAxis(int8_t(data.right_trigger) << 2);
}

void setup()
{
  pinMode(DEVICE_DETECT_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

#ifdef DEBUG
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(9600);
  while (!Serial)
    delay(1); // wait for serial monitor

  DEBUG_PRINTLN("debug mode...");
  digitalWrite(LED_BUILTIN, LOW);
#endif

  Wire.setClock(I2C_CLOCK_SPEED);
  Wire.begin();

  Gamepad.begin();
  Gamepad.releaseAll();

  initController();
  getControllerId();
}

void loop()
{
  unsigned long start = millis();

  // read wii controller state
  ControllerData data;
  int err = getControllerData(data);

  if (err == 0)
  {
    // translate wii controller to usb gamepad
    updateGamepad(data);
  }
  else
  {
    // TODO: reset device?
    DEBUG_PRINT("getControllerData err=");
    DEBUG_PRINTLN(err);
  }

  // send usb gamepad updates
  Gamepad.write();

  unsigned long duration = millis() - start;
  bool shouldSleep = (duration < POLL_INTERVAL_MS);

  // set led if we don't manage the intended interval
  digitalWrite(LED_BUILTIN, (shouldSleep ? LOW : HIGH));

  if (shouldSleep)
    delay(POLL_INTERVAL_MS - duration);
}
