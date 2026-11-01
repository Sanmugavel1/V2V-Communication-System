#include <SPI.h>
#include <RF24.h>
#include <Wire.h>

// NRF24L01 pins
#define CE_PIN  9
#define CSN_PIN 10

RF24 radio(CE_PIN, CSN_PIN);

// Communication address
const byte address[6] = "CAR01";

// MPU6500 I2C settings
#define MPU_ADDR       0x68
#define WHO_AM_I_REG   0x75
#define PWR_MGMT_1     0x6B
#define ACCEL_CONFIG   0x1C
#define GYRO_CONFIG    0x1B
#define ACCEL_XOUT_H   0x3B

// Both boards MUST use this exact structure
struct DataPacket {
  uint8_t vehicleID;
  uint16_t counter;
  float speed;

  uint8_t mpuPresent;

  int16_t ax_mg;
  int16_t ay_mg;
  int16_t az_mg;

  int16_t gx_dps10;
  int16_t gy_dps10;
  int16_t gz_dps10;
};

DataPacket data;
DataPacket receivedData;

bool mpuOK = false;
bool radioOK = false;

// --------------------------------
// MPU REGISTER WRITE
// --------------------------------
bool writeMPURegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);

  return Wire.endTransmission() == 0;
}

// --------------------------------
// MPU REGISTER READ
// --------------------------------
bool readMPURegisters(uint8_t reg, uint8_t *buffer,
                      uint8_t length) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  uint8_t received = Wire.requestFrom(
    (uint8_t)MPU_ADDR, length, (uint8_t)true
  );

  if (received != length) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (uint8_t i = 0; i < length; i++) {
    buffer[i] = Wire.read();
  }

  return true;
}

// --------------------------------
// CHECK AND INITIALIZE MPU6500
// --------------------------------
bool initMPU6500() {
  uint8_t whoAmI = 0;

  if (!readMPURegisters(WHO_AM_I_REG, &whoAmI, 1)) {
    Serial.println("MPU6500 I2C communication failed!");
    return false;
  }

  Serial.print("MPU WHO_AM_I = 0x");
  Serial.println(whoAmI, HEX);

  // Require the requested MPU6500 ID
  if (whoAmI != 0x70) {
    Serial.println("MPU6500 not identified: expected 0x70");
    return false;
  }

  // Wake up the sensor and select the internal clock
  if (!writeMPURegister(PWR_MGMT_1, 0x01)) {
    Serial.println("MPU power configuration failed!");
    return false;
  }

  delay(100);

  // Accelerometer: +/- 2g
  if (!writeMPURegister(ACCEL_CONFIG, 0x00)) {
    return false;
  }

  // Gyroscope: +/- 250 degrees/second
  if (!writeMPURegister(GYRO_CONFIG, 0x00)) {
    return false;
  }

  Serial.println("MPU6500 detected and configured!");
  return true;
}

// --------------------------------
// READ ACCELEROMETER + GYROSCOPE
// --------------------------------
bool readMPU6500() {
  uint8_t raw[14];

  if (!readMPURegisters(ACCEL_XOUT_H, raw, 14)) {
    return false;
  }

  int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
  int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
  int16_t az = (int16_t)((raw[4] << 8) | raw[5]);
  
  // raw[6] and raw[7] are temperature bytes

  int16_t gx = (int16_t)((raw[8]  << 8) | raw[9]);
  int16_t gy = (int16_t)((raw[10] << 8) | raw[11]);
  int16_t gz = (int16_t)((raw[12] << 8) | raw[13]);

  // Convert accelerometer readings to milli-g
  data.ax_mg = (int32_t)ax * 1000L / 16384;
  data.ay_mg = (int32_t)ay * 1000L / 16384;
  data.az_mg = (int32_t)az * 1000L / 16384;

  // Convert gyroscope readings to 0.1 degrees/second
  data.gx_dps10 = (int32_t)gx * 10L / 131;
  data.gy_dps10 = (int32_t)gy * 10L / 131;
  data.gz_dps10 = (int32_t)gz * 10L / 131;

  return true;
}

// --------------------------------
// SETUP
// --------------------------------
void setup() {
  Serial.begin(9600);
  Wire.begin();

  Serial.println("MEGA STARTING...");

  // MPU failure must not stop radio communication
  mpuOK = initMPU6500();

  if (!mpuOK) {
    Serial.println("MPU STATUS: NOT AVAILABLE");
  }

  if (!radio.begin()) {
    Serial.println("NRF24L01 not detected!");
    while (1) {}
  }

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(100);
  radio.setRetries(5, 15);

  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);

  radio.stopListening();

  data.vehicleID = 1;
  data.counter = 0;
  data.speed = 25.5;

  data.mpuPresent = 0;

  Serial.println("MEGA TWO-WAY COMMUNICATION READY");
}

// --------------------------------
// MAIN LOOP
// --------------------------------
void loop() {
  data.counter++;

  // Preserve your original example speed
  data.speed = 25.5 + (data.counter % 10);

  // Update MPU data if the sensor is available
  if (mpuOK) {
    if (!readMPU6500()) {
      mpuOK = false;
      Serial.println("MPU READ FAILED; marking unavailable");
    }
  }

  data.mpuPresent = mpuOK ? 1 : 0;

  if (!mpuOK) {
    data.ax_mg = 0;
    data.ay_mg = 0;
    data.az_mg = 0;

    data.gx_dps10 = 0;
    data.gy_dps10 = 0;
    data.gz_dps10 = 0;
  }

  // Send Mega data to Nano
  radio.stopListening();

  bool success = radio.write(&data, sizeof(data));

  if (success) {
    Serial.print("MEGA SENT | ID: ");
    Serial.print(data.vehicleID);

    Serial.print(" | Counter: ");
    Serial.print(data.counter);

    Serial.print(" | Speed: ");
    Serial.print(data.speed);

    Serial.print(" | MPU: ");
    Serial.println(data.mpuPresent ? "OK" : "NOT AVAILABLE");
  } else {
    Serial.println("MEGA TRANSMISSION FAILED!");
  }

  // Wait for Nano's response
  radio.startListening();

  unsigned long startTime = millis();
  bool responseReceived = false;

  while (millis() - startTime < 250) {
    if (radio.available()) {
      radio.read(&receivedData, sizeof(receivedData));
      responseReceived = true;
      break;
    }
  }

  if (responseReceived) {
    Serial.println("--- NANO DATA RECEIVED ---");

    Serial.print("Vehicle ID: ");
    Serial.println(receivedData.vehicleID);

    Serial.print("Counter: ");
    Serial.println(receivedData.counter);

    Serial.print("Speed: ");
    Serial.println(receivedData.speed);

    Serial.print("Nano MPU: ");
    Serial.println(
      receivedData.mpuPresent ? "OK" : "NOT AVAILABLE"
    );

    if (receivedData.mpuPresent) {
      Serial.print("Accel mg: ");
      Serial.print(receivedData.ax_mg);
      Serial.print(", ");
      Serial.print(receivedData.ay_mg);
      Serial.print(", ");
      Serial.println(receivedData.az_mg);

      Serial.print("Gyro 0.1 dps: ");
      Serial.print(receivedData.gx_dps10);
      Serial.print(", ");
      Serial.print(receivedData.gy_dps10);
      Serial.print(", ");
      Serial.println(receivedData.gz_dps10);
    }

    Serial.println("--------------------------");
  } else {
    Serial.println("No response received from Nano!");
  }

  delay(1000);
}